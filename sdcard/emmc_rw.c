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
//#define ENABLE_READ_BENCHMARK_LOGGING
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


/* @brief   Read from an SD card
 *
 */
int sd_read(struct block_device *dev, uint8_t *buf, size_t buf_size, uint32_t block_no)
{
#ifdef ENABLE_READ_BENCHMARK_LOGGING
	struct timespec startts, endts, diffts;
#endif
	
  // Check the status of the card
  struct emmc_block_dev *edev = (struct emmc_block_dev *)dev;
  if (sd_ensure_data_mode(edev) != 0)
    return -1;

#ifdef ENABLE_READ_BENCHMARK_LOGGING
  clock_gettime(CLOCK_MONOTONIC_RAW, &startts);  
#endif

  if (sd_do_data_command(edev, 0, buf, buf_size, block_no) < 0) {
    log_error("failed to read block %d ***", (uint32_t)block_no);
    return -1;
  }

#ifdef ENABLE_READ_BENCHMARK_LOGGING
  clock_gettime(CLOCK_MONOTONIC_RAW, &endts);  
  diff_timespec(&diffts, &endts, &startts);  	
  log_info("read from block %u, sz:%u", block_no, buf_size);
  log_info("time = %u.%06u", (uint32_t)diffts.tv_sec, (uint32_t)diffts.tv_nsec/1000);
#endif
  
  return buf_size;
}

/* @brief   Write to an SD card
 *
 */
#ifdef SD_WRITE_SUPPORT
int sd_write(struct block_device *dev, uint8_t *buf, size_t buf_size, uint32_t block_no)
{
  // Check the status of the card
  struct emmc_block_dev *edev = (struct emmc_block_dev *)dev;
  if (sd_ensure_data_mode(edev) != 0)
    return -1;

  log_info("sd_write() block %d, buf:%08x, sz:%d", (uint32_t)block_no, (uint32_t)buf, buf_size);

  if (sd_do_data_command(edev, 1, buf, buf_size, block_no) < 0)
  {
  	log_error("failed to write block %d ***", (uint32_t)block_no);
    return -1;
	}
	
  return buf_size;
}
#endif

