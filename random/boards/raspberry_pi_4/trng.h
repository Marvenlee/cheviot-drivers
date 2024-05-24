/*
 * Copyright 2023  Marven Gilhespie
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

#ifndef BOARDS_RASPBERRY_PI_4_TRNG_H
#define BOARDS_RASPBERRY_PI_4_TRNG_H


#include <stdint.h>
#include <stdbool.h>


/* @brief   Registers of the bcm2711 IProc RNG200 peripheral
 */
struct bcm2711_trng_registers
{
	uint32_t ctrl;
	uint32_t rng_soft_reset;
	uint32_t rbg_soft_reset;	
	uint32_t total_bit_count;

	uint32_t total_bit_count_threshold;
	uint32_t unused;
	uint32_t interrupt_status;
	uint32_t interrupt_enable;
	
	uint32_t fifo_data;
	uint32_t fifo_count;
};

// trng_regs->ctrl
#define CTRL_RBGEN_MASK       0x00001FFF
#define CTRL_RBGEN_ENABLE     0x00000001
#define CTRL_RBGEN_DISABLE    0x00000000
#define CTRL_DIV_CTRL_SHIFT   13

// trng_regs->rng_soft_reset
#define RNG_SOFT_RESET        0x00000001

// trng_regs->rbg_soft_reset
#define RBG_SOFT_RESET        0x00000001

// trng_regs->interrupt_status
#define INT_STATUS_MASTER_FAIL_LOCKOUT_IRQ_MASK       0x80000000
#define INT_STATUS_STARTUP_TRANSITIONS_MET_IRQ_MASK   0x00020000
#define INT_STATUS_NIST_FAIL_IRQ_MASK                 0x00000020
#define INT_STATUS_TOTAL_BITS_COUNT_IRQ_MASK          0x00000001

// trng_regs->fifo_count
#define FIFO_COUNT_MASK                               0x000000FF
#define FIFO_COUNT_FIFO_THRESHOLD_SHIFT               8

// Warmup count
#define RNG_WARMUP_COUNT      0x40000


/* Prototypes
 */
int trng_hw_init(void);
void trng_reset(void);
int trng_data_read(uint32_t *buffer, size_t words);


#endif


