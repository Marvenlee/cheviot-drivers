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
#define LOG_LEVEL_WARN

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

/* @brief   Initialize the SDIO interface
 *
 */
int sd_card_init(struct block_device **dev)
{
  // Check the sanity of the sd_commands and sd_acommands structures
  if (sd_commands_sz != (64 * sizeof(uint32_t))) {
    log_error("fatal error, sd_commands of incorrect size: %i"
         " expected %i",
         sd_commands_sz, 64 * sizeof(uint32_t));
    return -1;
  }
  if (sd_acommands_sz != (64 * sizeof(uint32_t))) {
    log_error("fatal error, sd_acommands of incorrect size: %i"
         " expected %i",
         sd_acommands_sz, 64 * sizeof(uint32_t));
    return -1;
  }

#if SDHCI_IMPLEMENTATION == SDHCI_IMPLEMENTATION_BCM_2708
// Power cycle the card to ensure its in its startup state

  if(bcm_2708_power_cycle() != 0) {
    log_error("BCM2708 controller did not power cycle successfully");
    return -1;
  }

  log_info("BCM2708 controller power-cycled");
#endif

  // Read the controller version
  uint32_t ver = mmio_read(emmc_base + EMMC_SLOTISR_VER);
  uint32_t vendor = ver >> 24;
  uint32_t sdversion = (ver >> 16) & 0xff;
  uint32_t slot_status = ver & 0xff;
  
  log_info("vendor %x, sdversion %x, slot_status %x", vendor, sdversion, slot_status);
  
  hci_ver = sdversion;

  if (hci_ver < 2) {
    log_error("only SDHCI versions >= 3.0 are supported");
    return -1;
  }

  // Reset the controller
  uint32_t control1 = mmio_read(emmc_base + EMMC_CONTROL1);
  control1 |= (1 << 24);
  // Disable clock
  control1 &= ~(1 << 2);
  control1 &= ~(1 << 0);
  mmio_write(emmc_base + EMMC_CONTROL1, control1);
  TIMEOUT_WAIT((mmio_read(emmc_base + EMMC_CONTROL1) & (0x7 << 24)) == 0,
               1000000);
  if ((mmio_read(emmc_base + EMMC_CONTROL1) & (0x7 << 24)) != 0) {
    log_error("controller did not reset properly");
    return -1;
  }

  log_debug("control0: %08x, control1: %08x, control2: %08x",
       mmio_read(emmc_base + EMMC_CONTROL0),
       mmio_read(emmc_base + EMMC_CONTROL1),
       mmio_read(emmc_base + EMMC_CONTROL2));

  // Read the capabilities registers
  capabilities_0 = mmio_read(emmc_base + EMMC_CAPABILITIES_0);
  capabilities_1 = mmio_read(emmc_base + EMMC_CAPABILITIES_1);

  log_debug("capabilities: %08x%08x", capabilities_1, capabilities_0);

	// Enable SD Bus Power VDD1 at 3.3V
  uint32_t control0 = mmio_read(emmc_base + EMMC_CONTROL0);
  control0 |= 0x0F << 8;
  mmio_write(emmc_base + EMMC_CONTROL0, control0);
  delay_microsecs(5000);


  // Check for a valid card
  TIMEOUT_WAIT(mmio_read(emmc_base + EMMC_STATUS) & (1 << 16), 500000);
  uint32_t status_reg = mmio_read(emmc_base + EMMC_STATUS);
  if ((status_reg & (1 << 16)) == 0) {
    log_warn("no card inserted");
    return -1;
  }
  
  // Clear control2
  mmio_write(emmc_base + EMMC_CONTROL2, 0);

  // Get the base clock rate
  uint32_t base_clock = sd_get_base_clock_hz();
  if (base_clock == 0) {
    base_clock = SD_RPI_BASE_CLOCK;
  }

  // Set clock rate to something slow
  control1 = mmio_read(emmc_base + EMMC_CONTROL1);
  control1 |= 1; // enable clock

  // Set to identification frequency (400 kHz)
  uint32_t f_id = sd_get_clock_divider(base_clock, SD_CLOCK_ID);
  if (f_id == SD_GET_CLOCK_DIVIDER_FAIL) {
    log_error("unable to get a valid clock divider for ID frequency");
    return -1;
  }
	
	control1 &= ~0xffe0;		// Clear old setting + clock generator select	
  //	control1 &= ~(0x3FF << 6);
	control1 |= f_id;

	// was not masked out and or'd with (7 << 16) in original driver
	control1 &= ~(0xF << 16);
	control1 |= (11 << 16);		// data timeout = TMCLK * 2^24

  mmio_write(emmc_base + EMMC_CONTROL1, control1);

  TIMEOUT_WAIT(mmio_read(emmc_base + EMMC_CONTROL1) & 0x2, 1000000);

  if ((mmio_read(emmc_base + EMMC_CONTROL1) & 0x2) == 0) {
    log_error("controller's clock did not stabilise within 1 second");
    return -1;
  }

  log_debug("control0: %08x, control1: %08x",
            mmio_read(emmc_base + EMMC_CONTROL0),
            mmio_read(emmc_base + EMMC_CONTROL1));

  // Enable the SD clock
  delay_microsecs(2000);
  control1 = mmio_read(emmc_base + EMMC_CONTROL1);
  control1 |= 4;
  mmio_write(emmc_base + EMMC_CONTROL1, control1);
  delay_microsecs(2000);

  // Mask off sending interrupts to the ARM
  //  mmio_write(emmc_base + EMMC_IRPT_EN, 0);
  // Reset interrupts
  mmio_write(emmc_base + EMMC_INTERRUPT, 0xffffffff);
  // Have all interrupts sent to the INTERRUPT register
  uint32_t irpt_mask = 0xffffffff & (~SD_CARD_INTERRUPT);

#ifdef SD_CARD_INTERRUPTS
  irpt_mask |= SD_CARD_INTERRUPT;    // FIXME
#endif

  mmio_write(emmc_base + EMMC_IRPT_MASK, irpt_mask);

  delay_microsecs(2000);

  // Prepare the device structure
  struct emmc_block_dev *ret;
  if (*dev == NULL)
    ret = (struct emmc_block_dev *)malloc(sizeof(struct emmc_block_dev));
  else
    ret = (struct emmc_block_dev *)*dev;

  memset(ret, 0, sizeof(struct emmc_block_dev));
  ret->bd.driver_name = driver_name;
  ret->bd.device_name = device_name;
  ret->bd.block_size = 512;
  ret->bd.read = sd_read;
#ifdef SD_WRITE_SUPPORT
  ret->bd.write = sd_write;
#endif
  ret->bd.supports_multiple_block_read = 1;
  ret->bd.supports_multiple_block_write = 1;
  ret->base_clock = base_clock;

	sd_issue_command(ret, GO_IDLE_STATE, 0, 1500000);

	if (FAIL(ret)) {
	  log_error("no CMD0 response");
	  return -1;
	}

// Send CMD8 to the card
// Voltage supplied = 0x1 = 2.7-3.6V (standard)
// Check pattern = 10101010b (as per PLSS 4.3.13) = 0xAA
#ifdef EMMC_DEBUG
  log_warn("note a timeout error on the following command (CMD8) is normal "
       "and expected if the SD card version is less than 2.0");
#endif

  sd_issue_command(ret, SEND_IF_COND, 0x1aa, 500000);
  int v2_later = 0;
  if (TIMEOUT(ret))
    v2_later = 0;
  else if (CMD_TIMEOUT(ret)) {
    if (sd_reset_cmd() == -1)
      return -1;
    mmio_write(emmc_base + EMMC_INTERRUPT, SD_ERR_MASK_CMD_TIMEOUT);
    v2_later = 0;
  } else if (FAIL(ret)) {
    log_error("failure sending CMD8 (%08x)", ret->last_interrupt);
    return -1;
  } else {
    if ((ret->last_r0 & 0xfff) != 0x1aa) {
      log_error("unusable card");
      log_error("CMD8 response %08x", ret->last_r0);
      return -1;
    } else
      v2_later = 1;
  }

// Here we are supposed to check the response to CMD5 (HCSS 3.6)
// It only returns if the card is a SDIO card
#ifdef EMMC_DEBUG
  log_debug("note that a timeout error on the following command (CMD5) is "
       "normal and expected if the card is not a SDIO card.");
#endif
  sd_issue_command(ret, IO_SET_OP_COND, 0, 10000);
  if (!TIMEOUT(ret)) {
    if (CMD_TIMEOUT(ret)) {
      if (sd_reset_cmd() == -1)
        return -1;
      mmio_write(emmc_base + EMMC_INTERRUPT, SD_ERR_MASK_CMD_TIMEOUT);
    } else {
      log_error("SDIO card detected - not currently supported");
      log_error("CMD5 returned %08x", ret->last_r0);
      return -1;
    }
  }

  // Call an inquiry ACMD41 (voltage window = 0) to get the OCR
  sd_issue_command(ret, ACMD(41), 0, 500000);
  if (FAIL(ret)) {
    log_error("inquiry ACMD41 failed");
    return -1;
  }

  // Call initialization ACMD41
  int card_is_busy = 1;
  while (card_is_busy) {
    uint32_t v2_flags = 0;
    if (v2_later) {
      // Set SDHC support
      v2_flags |= (1 << 30);

// Set 1.8v support
#ifdef SD_1_8V_SUPPORT
      if (!ret->failed_voltage_switch)
        v2_flags |= (1 << 24);
#endif

// Enable SDXC maximum performance
#ifdef SDXC_MAXIMUM_PERFORMANCE
      v2_flags |= (1 << 28);
#endif
    }

    sd_issue_command(ret, ACMD(41), 0x00ff8000 | v2_flags, 500000);
    if (FAIL(ret)) {
      log_error("error issuing ACMD41");
      return -1;
    }

    if ((ret->last_r0 >> 31) & 0x1) {
      // Initialization is complete
      ret->card_ocr = (ret->last_r0 >> 8) & 0xffff;
      ret->card_supports_sdhc = (ret->last_r0 >> 30) & 0x1;

#ifdef SD_1_8V_SUPPORT
      if (!ret->failed_voltage_switch)
        ret->card_supports_18v = (ret->last_r0 >> 24) & 0x1;
#endif

      card_is_busy = 0;
    } else {
      // Card is still busy
      delay_microsecs(500000);  // FIXME: reduce delay?
    }
  }

#ifdef EMMC_DEBUG
  log_info("card identified: OCR: %04x, 1.8v support: %i, SDHC support: %i",
       ret->card_ocr, ret->card_supports_18v, ret->card_supports_sdhc);
#endif

  // At this point, we know the card is definitely an SD card, so will
  // definitely support SDR12 mode which runs at 25 MHz
  sd_switch_clock_rate(base_clock, SD_CLOCK_NORMAL);

  // A small wait before the voltage switch
  delay_microsecs(20000);

  
#if 0  
  // Switch to 1.8V mode if possible
  if (ret->card_supports_18v) {
    log_info("switching to 1.8V mode");
    // As per HCSS 3.6.1

    // Send VOLTAGE_SWITCH
    sd_issue_command(ret, VOLTAGE_SWITCH, 0, 500000);
    if (FAIL(ret)) {
      log_warn("error issuing VOLTAGE_SWITCH");

      ret->failed_voltage_switch = 1;
      sd_power_off();
      
      log_info("calling sd_card_init again");
      return sd_card_init((struct block_device **)&ret);
    }

    // Disable SD clock
    control1 = mmio_read(emmc_base + EMMC_CONTROL1);
    control1 &= ~(1 << 2);
    mmio_write(emmc_base + EMMC_CONTROL1, control1);

    // Check DAT[3:0]
    status_reg = mmio_read(emmc_base + EMMC_STATUS);
    uint32_t dat30 = (status_reg >> 20) & 0xf;
    if (dat30 != 0) {
      log_info("DAT[3:0] did not settle to 0");

      ret->failed_voltage_switch = 1;
      sd_power_off();

      log_info("calling sd_card_init again");
      return sd_card_init((struct block_device **)&ret);
    }

#if 0
    // Set 1.8V signal enable to 1
    uint32_t control0 = mmio_read(emmc_base + EMMC_CONTROL0);
    control0 |= (1 << 8);
    mmio_write(emmc_base + EMMC_CONTROL0, control0);

    // Wait 5 ms
    // delay_microsecs(5000);

    // Check the 1.8V signal enable is set
    control0 = mmio_read(emmc_base + EMMC_CONTROL0);
    if (((control0 >> 8) & 0x1) == 0) {
      log_info("controller did not keep 1.8V signal enable high");
      ret->failed_voltage_switch = 1;
      sd_power_off();
      
      log_info("calling sd_card_init again");
      return sd_card_init((struct block_device **)&ret);
    }
#else

		// Enable SD Bus Power VDD1 at 3.3V
    uint32_t control0 = mmio_read(emmc_base + EMMC_CONTROL0);
    control0 |= 0x0F << 8;
    mmio_write(emmc_base + EMMC_CONTROL0, control0);
    delay_microsecs(5000);


#endif


    // Re-enable the SD clock
    control1 = mmio_read(emmc_base + EMMC_CONTROL1);
    control1 |= (1 << 2);
    mmio_write(emmc_base + EMMC_CONTROL1, control1);

    // Wait 1 ms
    // delay_microsecs(10000);

    // Check DAT[3:0]
    status_reg = mmio_read(emmc_base + EMMC_STATUS);
    dat30 = (status_reg >> 20) & 0xf;
    if (dat30 != 0xf) {
      log_info("DAT[3:0] did not settle to 1111b (%01x)", dat30);
      ret->failed_voltage_switch = 1;
      sd_power_off();

      log_info("calling sd_card_init again");
      return sd_card_init((struct block_device **)&ret);
    }

    log_info("voltage switch complete");
  }
#endif

  // Send CMD2 to get the cards CID
  sd_issue_command(ret, ALL_SEND_CID, 0, 500000);
  if (FAIL(ret)) {
    log_error("error sending ALL_SEND_CID");
    return -1;
  }
  uint32_t card_cid_0 = ret->last_r0;
  uint32_t card_cid_1 = ret->last_r1;
  uint32_t card_cid_2 = ret->last_r2;
  uint32_t card_cid_3 = ret->last_r3;

  log_info("card CID: %08x%08x%08x%08x", card_cid_3, card_cid_2, card_cid_1, card_cid_0);

  uint32_t *dev_id = (uint32_t *)malloc(4 * sizeof(uint32_t));
  dev_id[0] = card_cid_0;
  dev_id[1] = card_cid_1;
  dev_id[2] = card_cid_2;
  dev_id[3] = card_cid_3;
  ret->bd.device_id = (uint8_t *)dev_id;
  ret->bd.dev_id_len = 4 * sizeof(uint32_t);

// TODO:  Can we send CSD command to get device capacity/attributes
// How to determine HC capacity type cards?
// not sure what last params are for?

#if 0
  sd_issue_command(ret, SEND_CSD, 0, 500000);
  if (FAIL(ret)) {
    log_info("error sending SEND_CSD");
    return -1;
  }
  uint32_t card_csd_0 = ret->last_r0;
  uint32_t card_csd_1 = ret->last_r1;
  uint32_t card_csd_2 = ret->last_r2;
  uint32_t card_csd_3 = ret->last_r3;


  uint32_t *dev_csd = (uint32_t *)malloc(4 * sizeof(uint32_t));
  dev_csd[0] = card_csd_0;
  dev_csd[1] = card_csd_1;
  dev_csd[2] = card_csd_2;
  dev_csd[3] = card_csd_3;
  // ret->bd.device_id = (uint8_t *)dev_id;
  // ret->bd.dev_id_len = 4 * sizeof(uint32_t);
#endif

  // Send CMD3 to enter the data state
  sd_issue_command(ret, SEND_RELATIVE_ADDR, 0, 500000);
  if (FAIL(ret)) {
    log_error("error sending SEND_RELATIVE_ADDR");
    free(ret);
    return -1;
  }

  uint32_t cmd3_resp = ret->last_r0;

  ret->card_rca = (cmd3_resp >> 16) & 0xffff;
  uint32_t crc_error = (cmd3_resp >> 15) & 0x1;
  uint32_t illegal_cmd = (cmd3_resp >> 14) & 0x1;
  uint32_t error = (cmd3_resp >> 13) & 0x1;
  uint32_t status = (cmd3_resp >> 9) & 0xf;
  uint32_t ready = (cmd3_resp >> 8) & 0x1;

  if (crc_error) {
    log_error("CRC error");
    free(ret);
    free(dev_id);
    return -1;
  }

  if (illegal_cmd) {
    log_error("illegal command");
    free(ret);
    free(dev_id);
    return -1;
  }

  if (error) {
    log_error("generic error");
    free(ret);
    free(dev_id);
    return -1;
  }

  if (!ready) {
    log_error("not ready for data");
    free(ret);
    free(dev_id);
    return -1;
  }

  // Now select the card (toggles it to transfer state)
  sd_issue_command(ret, SELECT_CARD, ret->card_rca << 16, 500000);
  if (FAIL(ret)) {
    log_error("error sending CMD7");
    free(ret);
    return -1;
  }

  uint32_t cmd7_resp = ret->last_r0;
  status = (cmd7_resp >> 9) & 0xf;

  if ((status != 3) && (status != 4)) {
    log_error("invalid status (%i)", status);
    free(ret);
    free(dev_id);
    return -1;
  }

  // If not an SDHC card, ensure BLOCKLEN is 512 bytes
  if (!ret->card_supports_sdhc) {
    sd_issue_command(ret, SET_BLOCKLEN, 512, 500000);
    if (FAIL(ret)) {
      log_error("error sending SET_BLOCKLEN");
      free(ret);
      return -1;
    }
  }
  ret->block_size = 512;
  uint32_t controller_block_size = mmio_read(emmc_base + EMMC_BLKSIZECNT);
  controller_block_size &= (~0xfff);
  controller_block_size |= 0x200;
  mmio_write(emmc_base + EMMC_BLKSIZECNT, controller_block_size);

  // Get the cards SCR register
  ret->scr = (struct sd_scr *)malloc(sizeof(struct sd_scr));
  ret->buf = &ret->scr->scr[0];
  ret->block_size = 8;
  ret->blocks_to_transfer = 1;
  sd_issue_command(ret, SEND_SCR, 0, 500000);
  ret->block_size = 512;
  if (FAIL(ret)) {
    log_error("error sending SEND_SCR");
    free(ret->scr);
    free(ret);
    return -1;
  }

  // Determine card version
  // Note that the SCR is big-endian
  uint32_t scr0 = byte_swap(ret->scr->scr[0]);
  ret->scr->sd_version = SD_VER_UNKNOWN;
  uint32_t sd_spec = (scr0 >> (56 - 32)) & 0xf;
  uint32_t sd_spec3 = (scr0 >> (47 - 32)) & 0x1;
  uint32_t sd_spec4 = (scr0 >> (42 - 32)) & 0x1;
  ret->scr->sd_bus_widths = (scr0 >> (48 - 32)) & 0xf;
  if (sd_spec == 0)
    ret->scr->sd_version = SD_VER_1;
  else if (sd_spec == 1)
    ret->scr->sd_version = SD_VER_1_1;
  else if (sd_spec == 2) {
    if (sd_spec3 == 0)
      ret->scr->sd_version = SD_VER_2;
    else if (sd_spec3 == 1) {
      if (sd_spec4 == 0)
        ret->scr->sd_version = SD_VER_3;
      else if (sd_spec4 == 1)
        ret->scr->sd_version = SD_VER_4;
    }
  }

  log_debug("&scr: %08x", &ret->scr->scr[0]);
  log_debug("SCR[0]: %08x, SCR[1]: %08x", ret->scr->scr[0], ret->scr->scr[1]);
  log_debug("SCR: %08x%08x", byte_swap(ret->scr->scr[0]), byte_swap(ret->scr->scr[1]));
  log_debug("SCR: version %s, bus_widths %01x", sd_versions[ret->scr->sd_version], ret->scr->sd_bus_widths);

#ifdef SD_4BIT_DATA
  if (ret->scr->sd_bus_widths & 0x4) {
    // Set 4-bit transfer mode (ACMD6)
    // See HCSS 3.4 for the algorithm

    // Disable card interrupt in host
    uint32_t old_irpt_mask = mmio_read(emmc_base + EMMC_IRPT_MASK);
    uint32_t new_iprt_mask = old_irpt_mask & ~(1 << 8);
    mmio_write(emmc_base + EMMC_IRPT_MASK, new_iprt_mask);

    // Send ACMD6 to change the card's bit mode
    sd_issue_command(ret, SET_BUS_WIDTH, 0x2, 500000);
    if (FAIL(ret)) {
      log_warn("switch to 4-bit data mode failed");
    } else {
      // Change bit mode for Host
      uint32_t control0 = mmio_read(emmc_base + EMMC_CONTROL0);
      control0 |= 0x2;
      mmio_write(emmc_base + EMMC_CONTROL0, control0);

      // Re-enable card interrupt in host
      mmio_write(emmc_base + EMMC_IRPT_MASK, old_irpt_mask);
    }
  }
#endif

  log_info("found a valid version %s SD card", sd_versions[ret->scr->sd_version]);
  log_info("setup successful (status %i)", status);

  // Reset interrupt register
  mmio_write(emmc_base + EMMC_INTERRUPT, 0xffffffff);

  *dev = (struct block_device *)ret;

  return 0;
}

