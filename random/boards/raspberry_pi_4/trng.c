/*
 * Copyright 2019  Marven Gilhespie
 *
 * Licensed under the Apache License, segment_id 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscalls.h>
#include <unistd.h>
#include <sys/debug.h>
#include <sys/lists.h>
#include <machine/cheviot_hal.h>
#include "peripheral_base.h"
#include "trng.h"



/*
 * Memory mapped IO locations
 */
struct bcm2711_trng_registers *trng_regs;


/* @brief   Initialize the IProc RNG200 TRNG hardware
 *
 * @return  0 on success, negative errno on failure
 */ 
int trng_hw_init(void)
{
  uint32_t val;

  trng_regs = virtualallocphys((void *)0x50000000, 4096,
                          PROT_READ | PROT_WRITE | CACHE_UNCACHEABLE,
                          (void *)(RNG_BASE));  
  
  if (trng_regs == NULL) {
    return -ENOMEM;
  }  

  trng_reset();

	/* Discard initial numbers generated as they are less random */	
	val = RNG_WARMUP_COUNT;
	hal_mmio_write(&trng_regs->total_bit_count_threshold, val);

	val = 2 << FIFO_COUNT_FIFO_THRESHOLD_SHIFT;
	hal_mmio_write(&trng_regs->fifo_count, val);
	
	val = (0x3 << CTRL_DIV_CTRL_SHIFT) | CTRL_RBGEN_MASK;
	hal_mmio_write(&trng_regs->ctrl, val);
	
  return 0;
}


/* @brief   Reset the IProc RNG200 TRNG hardware
 *
 */
void trng_reset(void)
{
	uint32_t val;

	/* Disable RBG */
	val = 	hal_mmio_read(&trng_regs->ctrl);
	val &= ~CTRL_RBGEN_MASK;
	val |= CTRL_RBGEN_DISABLE;
	hal_mmio_write(&trng_regs->ctrl, val);
	
	/* Clear interrupt status register */
	hal_mmio_write(&trng_regs->interrupt_status, 0xFFFFFFFFUL);

	/* Reset RNG and RBG */
	val = hal_mmio_read(&trng_regs->rbg_soft_reset);
	val |= RBG_SOFT_RESET;
	hal_mmio_write(&trng_regs->rbg_soft_reset, val);
	
	val = hal_mmio_read(&trng_regs->rng_soft_reset);
	val |= RNG_SOFT_RESET;
	hal_mmio_write(&trng_regs->rng_soft_reset, val);

	val = hal_mmio_read(&trng_regs->rng_soft_reset);
	val &= ~RNG_SOFT_RESET;
	hal_mmio_write(&trng_regs->rng_soft_reset, val);

	val = hal_mmio_read(&trng_regs->rbg_soft_reset);
	val &= ~RBG_SOFT_RESET;
	hal_mmio_write(&trng_regs->rbg_soft_reset, val);

	/* Enable RBG */
	val = hal_mmio_read(&trng_regs->ctrl);
	val &= ~CTRL_RBGEN_MASK;
	val |= CTRL_RBGEN_ENABLE;
	hal_mmio_write(&trng_regs->ctrl, val);
}

/* @brief   Read values from the HW random number generator
 *
 * TODO: May want an oflags to check for O_NONBLOCK
 * Either abort if no data or maybe option to read at least 1 byte.
 */
int trng_data_read(uint32_t *buffer, size_t max)
{
	int fifo_count, count;
	uint32_t val;	
  struct timespec ts = {
    .tv_sec = 0,
    .tv_nsec = 1000000, 
  };
  
	while (1) {
		val = hal_mmio_read(&trng_regs->total_bit_count);    
    if (val > 16) {
      break;
    }    
		nanosleep(&ts, NULL);
	}

  while (1) {
	  fifo_count = hal_mmio_read(&trng_regs->fifo_count) & FIFO_COUNT_MASK;
	  if (fifo_count != 0) {
		  break;
    }
  
		nanosleep(&ts, NULL);
	}

  if (fifo_count < max) {
    max = fifo_count;
  }
  
  for (count = 0; count < max; count++) {
	  buffer[count] = hal_mmio_read(&trng_regs->fifo_data);
	}

	return count;
}

