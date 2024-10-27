/* Copyright (C) 2013 by John Cronin <jncronin@tysos.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Provides an interface to the EMMC controller and commands for interacting
 * with an sd card */

/* References:
 *
 * PLSS 	- SD Group Physical Layer Simplified Specification ver 3.00
 * HCSS		- SD Group Host Controller Simplified Specification ver 3.00
 *
 * Broadcom BCM2835 Peripherals Guide
 */

/*
 * This file and associated emmc source and header files are derived from
 * John Cronin's original sources and modified to run on CheviotOS.
 */

//#define NDEBUG
//#define EMMC_DEBUG
#define LOG_LEVEL_INFO

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/debug.h>
#include <sys/syscalls.h>
#include <machine/cheviot_hal.h>
#include "timer.h"
#include "util.h"
#include "sdcard.h"
#include "mmio.h"
#include "globals.h"
#include "emmc_internal.h"


/* @brief   Internal handling of issuing SDIO command
 *
 */
void sd_issue_command_int(struct emmc_block_dev *dev, uint32_t cmd_reg,
                                 uint32_t argument, useconds_t timeout) {
  dev->last_cmd_reg = cmd_reg;
  dev->last_cmd_success = 0;

  // This is as per HCSS 3.7.1.1/3.7.2.2
  // Check Command Inhibit
  while (mmio_read(emmc_base + EMMC_STATUS) & 0x1) {
    delay_microsecs(10);  // FIXME: busy wait
  }
  // Is the command with busy?
  if ((cmd_reg & SD_CMD_RSPNS_TYPE_MASK) == SD_CMD_RSPNS_TYPE_48B) {
    // With busy

    // Is is an abort command?
    if ((cmd_reg & SD_CMD_TYPE_MASK) != SD_CMD_TYPE_ABORT) {
      // Not an abort command

      // Wait for the data line to be free
      while (mmio_read(emmc_base + EMMC_STATUS) & 0x2) {
        delay_microsecs(10); // FIXME: busy wait
      }
    }
  }

  // Is this a DMA transfer?
  int is_sdma = 0;
  if ((cmd_reg & SD_CMD_ISDATA) && (dev->use_sdma)) {
    is_sdma = 1;
  }

  if (is_sdma) {
    // Set system address register (ARGUMENT2 in RPi)
    // We need to define a 4 kiB aligned buffer to use here
    // Then convert its virtual address to a bus address
    //mmio_write(emmc_base + EMMC_ARG2, SDMA_BUFFER_PA);
    mmio_write(emmc_base + EMMC_ARG2, (uint32_t)buf_phys);
  }

  // Set block size and block count
  // For now, block size = 512 bytes, block count = 1,
  //  host SDMA buffer boundary = 4 kiB
  if (dev->blocks_to_transfer > 0xffff) {
    log_warn("blocks_to_transfer too great (%i)", dev->blocks_to_transfer);
    dev->last_cmd_success = 0;
    return;
  }
  
  uint32_t blksizecnt = dev->block_size | (dev->blocks_to_transfer << 16);
  mmio_write(emmc_base + EMMC_BLKSIZECNT, blksizecnt);

  // Set argument 1 reg
  mmio_write(emmc_base + EMMC_ARG1, argument);

  if (is_sdma) {
    // Set Transfer mode register
    cmd_reg |= SD_CMD_DMA;
  }

  // Set command reg
  mmio_write(emmc_base + EMMC_CMDTM, cmd_reg);

  // Wait for command complete interrupt
  TIMEOUT_WAIT(mmio_read(emmc_base + EMMC_INTERRUPT) & 0x8001, timeout);
  uint32_t irpts = mmio_read(emmc_base + EMMC_INTERRUPT);
	
  // Clear command complete status
  mmio_write(emmc_base + EMMC_INTERRUPT, 0xffff0001);

  // Test for errors
  if ((irpts & 0xffff0001) != 0x1) {
    log_warn("error occured whilst waiting for command complete interrupt");
    dev->last_error = irpts & 0xffff0000;
    dev->last_interrupt = irpts;
    return;
  }
  
  // Get response data
  switch (cmd_reg & SD_CMD_RSPNS_TYPE_MASK) {
  case SD_CMD_RSPNS_TYPE_48:
  case SD_CMD_RSPNS_TYPE_48B:
    dev->last_r0 = mmio_read(emmc_base + EMMC_RESP0);
    break;

  case SD_CMD_RSPNS_TYPE_136:
    dev->last_r0 = mmio_read(emmc_base + EMMC_RESP0);
    dev->last_r1 = mmio_read(emmc_base + EMMC_RESP1);
    dev->last_r2 = mmio_read(emmc_base + EMMC_RESP2);
    dev->last_r3 = mmio_read(emmc_base + EMMC_RESP3);
    break;
  }

  // If with data, wait for the appropriate interrupt
  if ((cmd_reg & SD_CMD_ISDATA) && (is_sdma == 0)) {
    uint32_t wr_irpt;
    int is_write = 0;
    if (cmd_reg & SD_CMD_DAT_DIR_CH)
      wr_irpt = (1 << 5); // read
    else {
      is_write = 1;
      wr_irpt = (1 << 4); // write
    }

    int cur_block = 0;
    uint32_t *cur_buf_addr = (uint32_t *)dev->buf;
    while (cur_block < dev->blocks_to_transfer) {
      if (dev->blocks_to_transfer > 1) {
        log_debug("multi block transfer, awaiting block %i ready", cur_block);
      }
      
      TIMEOUT_WAIT(mmio_read(emmc_base + EMMC_INTERRUPT) & (wr_irpt | 0x8000),
                   timeout);

      irpts = mmio_read(emmc_base + EMMC_INTERRUPT);
      mmio_write(emmc_base + EMMC_INTERRUPT, 0xffff0000 | wr_irpt);

      if ((irpts & (0xffff0000 | wr_irpt)) != wr_irpt) {
        log_error("error occured whilst waiting for data ready interrupt");
        dev->last_error = irpts & 0xffff0000;
        dev->last_interrupt = irpts;
        return;
      }

      // Transfer the block
      size_t cur_byte_no = 0;
      if (is_write) {
        while (cur_byte_no < dev->block_size) {
          uint32_t data = *cur_buf_addr;          
          mmio_write(emmc_base + EMMC_DATA, data);
          cur_byte_no += 4;
          cur_buf_addr++;
        }
      } else {
        while (cur_byte_no < dev->block_size) {
          uint32_t data = mmio_read(emmc_base + EMMC_DATA);
          *cur_buf_addr = data;
          cur_byte_no += 4;
          cur_buf_addr++;
        }
      }

      cur_block++;
    }
  }

  // Wait for transfer complete (set if read/write transfer or with busy)
  if ((((cmd_reg & SD_CMD_RSPNS_TYPE_MASK) == SD_CMD_RSPNS_TYPE_48B) ||
       (cmd_reg & SD_CMD_ISDATA)) &&
      (is_sdma == 0)) {
    // First check command inhibit (DAT) is not already 0
    if ((mmio_read(emmc_base + EMMC_STATUS) & 0x2) == 0)
      mmio_write(emmc_base + EMMC_INTERRUPT, 0xffff0002);
    else {
      TIMEOUT_WAIT(mmio_read(emmc_base + EMMC_INTERRUPT) & 0x8002, timeout);
      irpts = mmio_read(emmc_base + EMMC_INTERRUPT);
      mmio_write(emmc_base + EMMC_INTERRUPT, 0xffff0002);

      // Handle the case where both data timeout and transfer complete
      //  are set - transfer complete overrides data timeout: HCSS 2.2.17
      if (((irpts & 0xffff0002) != 0x2) && ((irpts & 0xffff0002) != 0x100002)) {
        log_error("error occured whilst waiting for transfer complete "
             "interrupt");

        dev->last_error = irpts & 0xffff0000;
        dev->last_interrupt = irpts;
        return;
      }
      mmio_write(emmc_base + EMMC_INTERRUPT, 0xffff0002);
    }
  } else if (is_sdma) {
    // For SDMA transfers, we have to wait for either transfer complete,
    //  DMA int or an error

    // First check command inhibit (DAT) is not already 0
    if ((mmio_read(emmc_base + EMMC_STATUS) & 0x2) == 0)
      mmio_write(emmc_base + EMMC_INTERRUPT, 0xffff000a);
    else {
      TIMEOUT_WAIT(mmio_read(emmc_base + EMMC_INTERRUPT) & 0x800a, timeout);
      irpts = mmio_read(emmc_base + EMMC_INTERRUPT);
      mmio_write(emmc_base + EMMC_INTERRUPT, 0xffff000a);

      // Detect errors
      if ((irpts & 0x8000) && ((irpts & 0x2) != 0x2)) {
        log_error("error occured whilst waiting for transfer complete "
             "interrupt");

        dev->last_error = irpts & 0xffff0000;
        dev->last_interrupt = irpts;
        return;
      }

      // Detect DMA interrupt without transfer complete
      // Currently not supported - all block sizes should fit in the
      //  buffer
      if ((irpts & 0x8) && ((irpts & 0x2) != 0x2)) {
        log_error("error: DMA interrupt occured without transfer complete");

        dev->last_error = irpts & 0xffff0000;
        dev->last_interrupt = irpts;
        return;
      }

      // Detect transfer complete
      if (irpts & 0x2) {
        // Transfer the data to the user buffer
#ifdef SDMA_SUPPORT
        hal_invalidate_dcache(buf, buf + BUF_SZ);
#endif
        //memcpy(dev->buf, (const void *)SDMA_BUFFER, dev->block_size);
      } else {
        // Unknown error
        if (irpts == 0) {
          log_error("timeout waiting for SDMA transfer to complete");
        } else {
          log_error("unknown SDMA transfer error");
        }
        
        if ((irpts == 0) && ((mmio_read(emmc_base + EMMC_STATUS) & 0x3) == 0x2)) {
          // The data transfer is ongoing, we should attempt to stop it
          log_warn("warning: aborting transfer");
          mmio_write(emmc_base + EMMC_CMDTM, sd_commands[STOP_TRANSMISSION]);
        }
        dev->last_error = irpts & 0xffff0000;
        dev->last_interrupt = irpts;
        return;
      }
    }
  }

  // Return success
  dev->last_cmd_success = 1;
}


/*
 *
 */
void sd_handle_card_interrupt(struct emmc_block_dev *dev) {
// Handle a card interrupt

  uint32_t status = mmio_read(emmc_base + EMMC_STATUS);

  // Get the card status
  if (dev->card_rca) {
    sd_issue_command_int(dev, sd_commands[SEND_STATUS], dev->card_rca << 16,
                         500000);
    if (FAIL(dev)) {
      log_error("unable to get card status");
    } else {
      log_debug("card status: %08x", dev->last_r0);
    }
  } else {
    log_warn("no card currently selected");
  }
}


/* @brief   Handle interrupts of SDIO interface
 *
 */
void sd_handle_interrupts(struct emmc_block_dev *dev) {
  uint32_t irpts = mmio_read(emmc_base + EMMC_INTERRUPT);
  uint32_t reset_mask = 0;

  if (irpts & SD_COMMAND_COMPLETE) {
    log_debug("spurious command complete interrupt");
    reset_mask |= SD_COMMAND_COMPLETE;
  }

  if (irpts & SD_TRANSFER_COMPLETE) {
    log_debug("spurious transfer complete interrupt");
    reset_mask |= SD_TRANSFER_COMPLETE;
  }

  if (irpts & SD_BLOCK_GAP_EVENT) {
    log_debug("spurious block gap event interrupt");
    reset_mask |= SD_BLOCK_GAP_EVENT;
  }

  if (irpts & SD_DMA_INTERRUPT) {
    log_debug("spurious DMA interrupt");
    reset_mask |= SD_DMA_INTERRUPT;
  }

  if (irpts & SD_BUFFER_WRITE_READY) {
    log_debug("spurious buffer write ready interrupt");
    reset_mask |= SD_BUFFER_WRITE_READY;
    sd_reset_dat();
  }

  if (irpts & SD_BUFFER_READ_READY) {
    log_debug("spurious buffer read ready interrupt");
    reset_mask |= SD_BUFFER_READ_READY;
    sd_reset_dat();
  }

  if (irpts & SD_CARD_INSERTION) {
    log_debug("card insertion detected");
    reset_mask |= SD_CARD_INSERTION;
  }

  if (irpts & SD_CARD_REMOVAL) {
    log_debug("card removal detected");
    reset_mask |= SD_CARD_REMOVAL;
    dev->card_removal = 1;
  }

  if (irpts & SD_CARD_INTERRUPT) {
    log_debug("card interrupt detected");
    sd_handle_card_interrupt(dev);
    reset_mask |= SD_CARD_INTERRUPT;
  }

  if (irpts & 0x8000) {
    log_warn("spurious error interrupt: %08x", irpts);
    reset_mask |= 0xffff0000;
  }

  mmio_write(emmc_base + EMMC_INTERRUPT, reset_mask);
}


/* @brief   Send SDIO command
 *
 */
void sd_issue_command(struct emmc_block_dev *dev, uint32_t command,
                             uint32_t argument, useconds_t timeout)
{
  // First, handle any pending interrupts
  sd_handle_interrupts(dev);

  // Stop the command issue if it was the card remove interrupt that was
  //  handled
  if (dev->card_removal) {
    dev->last_cmd_success = 0;
    return;
  }

  // Now run the appropriate commands by calling sd_issue_command_int()
  if (command & IS_APP_CMD) {
    command &= 0xff;

    if (sd_acommands[command] == SD_CMD_RESERVED(0)) {
      log_error("invalid command ACMD%i", command);
      dev->last_cmd_success = 0;
      return;
    }
    dev->last_cmd = APP_CMD;

    uint32_t rca = 0;
    if (dev->card_rca) {
      rca = dev->card_rca << 16;
		}
		
    sd_issue_command_int(dev, sd_commands[APP_CMD], rca, timeout);
    if (dev->last_cmd_success) {
      dev->last_cmd = command | IS_APP_CMD;
      sd_issue_command_int(dev, sd_acommands[command], argument, timeout);
    }
  } else {
    if (sd_commands[command] == SD_CMD_RESERVED(0)) {
      log_error("invalid command CMD%i", command);
      dev->last_cmd_success = 0;
      return;
    }

    dev->last_cmd = command;    
    sd_issue_command_int(dev, sd_commands[command], argument, timeout);
  }

#ifdef EMMC_DEBUG
  if (FAIL(dev)) {
    log_error("error issuing command: interrupts %08x: ", dev->last_interrupt);
    if (dev->last_error == 0) {
      log_error("TIMEOUT");
    } else {
      for (int i = 0; i < SD_ERR_RSVD; i++) {
        if (dev->last_error & (1 << (i + 16))) {
          log_error("error: %s", err_irpts[i]);
        }
      }
    }
  }
#endif
}


/*
 *
 */
int sd_ensure_data_mode(struct emmc_block_dev *edev) {
  if (edev->card_rca == 0) {
    // Try again to initialise the card
    int ret = sd_card_init((struct block_device **)&edev);
    if (ret != 0)
      return ret;
  }

  sd_issue_command(edev, SEND_STATUS, edev->card_rca << 16, 500000);
  if (FAIL(edev)) {
    log_error("ensure_data_mode() error sending CMD13");
    edev->card_rca = 0;
    return -1;
  }

  uint32_t status = edev->last_r0;
  uint32_t cur_state = (status >> 9) & 0xf;

  if (cur_state == 3) {
    // Currently in the stand-by state - select it
    sd_issue_command(edev, SELECT_CARD, edev->card_rca << 16, 500000);
    if (FAIL(edev)) {
      log_error("ensure_data_mode() no response from CMD17");
      edev->card_rca = 0;
      return -1;
    }
  } else if (cur_state == 5) {
    // In the data transfer state - cancel the transmission
    sd_issue_command(edev, STOP_TRANSMISSION, 0, 500000);
    if (FAIL(edev)) {
      log_error("ensure_data_mode() no response from CMD12");
      edev->card_rca = 0;
      return -1;
    }

    // Reset the data circuit
    sd_reset_dat();
  } else if (cur_state != 4) {
    // Not in the transfer state - re-initialise
    int ret = sd_card_init((struct block_device **)&edev);
    if (ret != 0)
      return ret;
  }

  // Check again that we're now in the correct mode
  if (cur_state != 4) {
    sd_issue_command(edev, SEND_STATUS, edev->card_rca << 16, 500000);
    if (FAIL(edev)) {
      log_error("ensure_data_mode() no response from CMD13");
      edev->card_rca = 0;
      return -1;
    }
    status = edev->last_r0;
    cur_state = (status >> 9) & 0xf;

    if (cur_state != 4) {
      log_error("unable to initialise SD card to data mode (state %i)", cur_state);
      edev->card_rca = 0;
      return -1;
    }
  }

  return 0;
}

#ifdef SDMA_SUPPORT
// We only support DMA transfers to buffers aligned on a 4 kiB boundary
static int sd_suitable_for_dma(void *buf) {
  if ((uintptr_t)buf & 0xfff)
    return 0;
  else
    return 1;
}
#endif

int sd_do_data_command(struct emmc_block_dev *edev, int is_write,
                              uint8_t *buf, size_t buf_size,
                              uint32_t block_no) {
  // PLSS table 4.20 - SDSC cards use byte addresses rather than block addresses
  if (!edev->card_supports_sdhc)
    block_no *= 512;

  // This is as per HCSS 3.7.2.1
  if (buf_size < edev->block_size) {
    log_error("do_data_command() called with buffer size (%i) less than "
         "block size (%i)",
         buf_size, edev->block_size);
    return -1;
  }

  edev->blocks_to_transfer = buf_size / edev->block_size;
  if (buf_size % edev->block_size) {
    log_error("do_data_command() called with buffer size (%i) not an "
         "exact multiple of block size (%i)",
         buf_size, edev->block_size);
    return -1;
  }
  edev->buf = buf;

  // Decide on the command to use
  int command;
  if (is_write) {
#ifdef SDMA_SUPPORT
    hal_flush_dcache(buf, buf + BUF_SZ);
#endif
    if (edev->blocks_to_transfer > 1)
      command = WRITE_MULTIPLE_BLOCK;
    else
      command = WRITE_BLOCK;
  } else {
#ifdef SDMA_SUPPORT
    hal_invalidate_dcache(buf, buf + BUF_SZ);
#endif
    
    if (edev->blocks_to_transfer > 1)
      command = READ_MULTIPLE_BLOCK;
    else
      command = READ_SINGLE_BLOCK;
  }

  int retry_count = 0;
  int max_retries = 3;
  while (retry_count < max_retries) {
#ifdef SDMA_SUPPORT
    // use SDMA for the first try only
    if ((retry_count == 0) && sd_suitable_for_dma(buf))
      edev->use_sdma = 1;
    else {
      log_info("retrying without SDMA");
      edev->use_sdma = 0;
    }
#else
    edev->use_sdma = 0;
#endif

    sd_issue_command(edev, command, block_no, 5000000);

    if (SUCCESS(edev))
      break;
    else {
      log_info("error sending CMD%i, ", command);
      log_info("error = %08x.  ", edev->last_error);
      retry_count++;
      if (retry_count < max_retries)
        log_info("Retrying...");
      else
        log_error("Giving up.");
    }
  }
  if (retry_count == max_retries) {
    edev->card_rca = 0;
    return -1;
  }

  return 0;
}



