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





/* @brief   Power off the SD card 
 *
 */
void sd_power_off() {
  uint32_t control0 = mmio_read(emmc_base + EMMC_CONTROL0);
  control0 &= ~(1 << 8); // Set SD Bus Power bit off in Power Control Register
  mmio_write(emmc_base + EMMC_CONTROL0, control0);
}


/* @brief   Get the base clock rate
 *
 */
uint32_t sd_get_base_clock_hz(void) {
  uint32_t base_clock;

#if BASE_CLOCK_SRC == BASE_CLOCK_RPI_DEFAULT
  base_clock = SD_RPI_BASE_CLOCK;  
#elif BASE_CLOCK_SRC == BASE_CLOCK_EMMC_CAPABILITIES
  capabilities_0 = mmio_read(emmc_base + EMMC_CAPABILITIES_0);
  base_clock = ((capabilities_0 >> 8) & 0xff) * 1000000;
#elif BASE_CLOCK_SRC == BASE_CLOCK_RPI_MAILBOX
	rpi_mbox_get_clock_rate(MBOX_CLOCK_ID_EMMC2, &base_clock);	
#else
  log_warn("get_base_clock_hz() is not implemented for this architecture.");
  return 0;
#endif

  return base_clock;
}


#if SDHCI_IMPLEMENTATION == SDHCI_IMPLEMENTATION_BCM_2708

/* @brief   Power off the SD Card
 *
 */
int bcm_2708_power_off()
{
  volatile uint32_t *mailbuffer = (uint32_t *)mailbuffer_virt_addr;

  // set up the buffer
  mailbuffer[0] = 8 * 4; // size of this message
  mailbuffer[1] = 0;     // this is a request

  // next comes the first tag
  mailbuffer[2] = 0x00028001; // set power state tag
  mailbuffer[3] = 0x8;        // value buffer size
  mailbuffer[4] = 0x8;        // is a request, value length = 8
  mailbuffer[5] = 0x0;        // device id and device id also returned here
  mailbuffer[6] = 0x2; 				// set power off, wait for stable and returns state
  mailbuffer[7] = 0;					// closing tag

  // send the message
  mbox_write(MBOX_PROP, mailbuffer_phys_addr);

  // read the response
  mbox_read(MBOX_PROP);

  if (mailbuffer[1] != MBOX_SUCCESS) {
    log_error("bcm_2708_power_off(): property mailbox did not return a valid "
         "response.");
    return -1;
  }

  if (mailbuffer[5] != 0x0) {
    log_error("property mailbox did not return a valid device id.");
    return -1;
  }

  if ((mailbuffer[6] & 0x3) != 0) {
    log_error("bcm_2708_power_off(): device did not power off successfully (%08x).", mailbuffer[6]);

    for (int t = 0; t < 8; t++) {
      log_debug("mb[%d] = %08x", t, mailbuffer[t]);
    }

    return -1;
  }

  return 0;
}


/* @brief   Power on the SD card
 *
 */
int bcm_2708_power_on() {
  volatile uint32_t *mailbuffer = (uint32_t *)mailbuffer_virt_addr;

  // set up the buffer
  mailbuffer[0] = 8 * 4; // size of this message
  mailbuffer[1] = 0;     // this is a request

  // next comes the first tag
  mailbuffer[2] = 0x00028001; // set power state tag
  mailbuffer[3] = 0x8;        // value buffer size
  mailbuffer[4] = 0x8;        // is a request, value length = 8
  mailbuffer[5] = 0x0;        // device id and device id also returned here
  mailbuffer[6] = 0x3; 				// set power on, wait for stable and returns state
  mailbuffer[7] = 0;					// closing tag

  // send the message
  mbox_write(MBOX_PROP, mailbuffer_phys_addr);

  // read the response
  mbox_read(MBOX_PROP);

  if (mailbuffer[1] != MBOX_SUCCESS) {
    log_error("bcm_2708_power_on(): property mailbox did not return a valid response.");
    return -1;
  }

  if (mailbuffer[5] != 0x0) {
    log_error("property mailbox did not return a valid device id.");
    return -1;
  }

  if ((mailbuffer[6] & 0x3) != 1) {
    log_error("bcm_2708_power_on(): device did not power on successfully (%08x).", mailbuffer[6]);
    return -1;
  }

  return 0;
}


/* @brief   Power cycle the SDIO interface
 *
 */
int bcm_2708_power_cycle() {
  if (bcm_2708_power_off() < 0)
    return -1;

  delay_microsecs(5000);

  return bcm_2708_power_on();
}

#endif // SDHCI_IMPLEMENTATION == SDHCI_IMPLEMENTATION_BCM_2708


/* @brief   Find Last Set bit in a uint32_t bit.
 * 
 * @param   x, 32 bit value to scan from last bit
 * @return  Value from 1 to 32 indicating the last bit that was set
 *          or 0 if no bits were set.
 */
int find_last_set(uint32_t x)
{
  int n = 32;
  
  if (x == 0) {
    return 32;
  }
  
  if ((x & 0xFFFF0000ul) == 0) {
    x << 16;
    n -= 16;
  }
  
  if ((x & 0xFF000000ul) == 0) {
    x << 8;
    n -= 8;
  }
  
  if ((x & 0xF0000000ul) == 0) {
    x << 4;
    n -= 4;
  }
  
  if ((x & 0xC0000000ul) == 0) {
    x << 2;
    n -= 2;
  }
  
  if ((x & 0x80000000ul) == 0) {
    n -= 1;
  }
  
  return n;
}


/* @brief   Calculate the clock divider field
 * 
 * @param   base_clock, the base_clock of the EMMC controller.
 * @param   freq, the desired frequency to run at
 *
 * Based on code in the Raspberry Pi forums that fixes issues with
 * clock divider calculation in the original sources.
 */
uint32_t sd_get_clock_divider(uint32_t base_clock, uint32_t freq) {
	uint32_t divisor;
	uint32_t closest = base_clock / freq;
	uint32_t shiftcount = find_last_set(closest - 1);		// Get the raw shiftcount
	if (shiftcount > 0) {
	  shiftcount--;					  // Note the offset of shift by 1 (look at the spec)
  }
	if (shiftcount > 7) {
	  shiftcount = 7;					// It's only 8 bits maximum on HOST_SPEC_V2
	}
	
	if (hci_ver >= 2) {
	  divisor = closest;	// Version 3 take closest
	} else {
	  divisor = (1 << shiftcount);				// Version 2 take power 2
	}
	
	if (divisor <= 2) {									// Too dangerous to go for divisor 1 unless you test
		divisor = 2;									// You can't take divisor below 2 on slow cards
		shiftcount = 0;									// Match shift to above just for debug notification
	}

	log_info("Divisor selected = %lu, pow 2 shift count = %lu\n", divisor, shiftcount);

	uint32_t hi = 0;
	if (hci_ver >= 2) {
	  hi = (divisor & 0x300) >> 2; // Only 10 bits on Hosts specs above 2
  }
  
  uint32_t lo = (divisor & 0x0ff);					// Low part always valid
  uint32_t cdiv = (lo << 8) + hi;						// Join and roll to position
	return cdiv;
}


/* @brief   Switch the clock rate whilst running
 *
 */
int sd_switch_clock_rate(uint32_t base_clock, uint32_t target_rate) {
  // Decide on an appropriate divider
  
  log_info("sd_switch_clock_rate(base:%u, targ:%u)", base_clock, target_rate);
  
  uint32_t divider = sd_get_clock_divider(base_clock, target_rate);
  if (divider == SD_GET_CLOCK_DIVIDER_FAIL) {
    log_error("couldn't get a valid divider for target rate %i Hz", target_rate);
    return -1;
  }

  log_info("clock divider: %u", divider);

  // Wait for the command inhibit (CMD and DAT) bits to clear
  while (mmio_read(emmc_base + EMMC_STATUS) & 0x3)
    delay_microsecs(1000);

  // Set the SD clock off
  uint32_t control1 = mmio_read(emmc_base + EMMC_CONTROL1);
  control1 &= ~(1 << 2);
  mmio_write(emmc_base + EMMC_CONTROL1, control1);
  delay_microsecs(2000);

  // Write the new divider
  control1 &= ~(0x3FF << 6);  // Clear old setting + clock generator select

  control1 |= divider;
  
  mmio_write(emmc_base + EMMC_CONTROL1, control1);
  delay_microsecs(2000);

  // Enable the SD clock
  control1 |= (1 << 2);
  mmio_write(emmc_base + EMMC_CONTROL1, control1);
  delay_microsecs(2000);

  log_info("set clock rate to %i Hz", target_rate);
  return 0;
}



/* @brief   Reset the CMD line
 *
 */
int sd_reset_cmd(void) {
  uint32_t control1 = mmio_read(emmc_base + EMMC_CONTROL1);
  control1 |= SD_RESET_CMD;
  mmio_write(emmc_base + EMMC_CONTROL1, control1);
  TIMEOUT_WAIT((mmio_read(emmc_base + EMMC_CONTROL1) & SD_RESET_CMD) == 0,
               1000000);
  if ((mmio_read(emmc_base + EMMC_CONTROL1) & SD_RESET_CMD) != 0) {
    log_error("CMD line did not reset properly");
    return -1;
  }
  return 0;
}


/* @brief   Reset the DAT line
 *
 */
int sd_reset_dat(void) {
  uint32_t control1 = mmio_read(emmc_base + EMMC_CONTROL1);
  control1 |= SD_RESET_DAT;
  mmio_write(emmc_base + EMMC_CONTROL1, control1);
  TIMEOUT_WAIT((mmio_read(emmc_base + EMMC_CONTROL1) & SD_RESET_DAT) == 0,
               1000000);
  if ((mmio_read(emmc_base + EMMC_CONTROL1) & SD_RESET_DAT) != 0) {
    log_error("DAT line did not reset properly");
    return -1;
  }
  return 0;
}

