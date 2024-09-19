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

#ifndef AUX_UART_HW_H
#define AUX_UART_HW_H

#include <stdint.h>
#include <stdbool.h>


// Constants and macros
#define EVENT_AUX_INT           1       // Event bit to set on an interrupt occuring

#define AUX_UART_BAUD           115200
#define AUX_UART_IRQ            (29 + 96)
#define AUX_UART_GPIO_ALT_FN    FN5         // Use alternate function FN5 for GPIO 14 and 15
#define AUX_UART_CLOCK          500000000
#define AUX_MU_BAUD(baud)       ((AUX_UART_CLOCK/(baud*8))-1)

#define IER_TX_INT_EN           (1<<0)
#define IER_RX_INT_EN           (1<<1)

#define IIR_RX                  (1<<2)  // On read indicates Rx FIFO has data, on write clears Rx FIFO
#define IIR_TX                  (1<<1)  // On read indicates Tx FIFO is empty, on write clears Tx FIFO
#define IIR_PENDING             (1<<0)

#define CNTL_RX_EN              (1<<0)
#define CNTL_TX_EN              (1<<1)

#define LCR_8_BIT               (1<<0)
#define LCR_7_BIT               (0<<0)

#define LSR_RX_READY            (1<<0)
#define LSR_TX_EMPTY            (1<<5)

// Virtual address to search from when mapping the device registers
#define AUX_REGS_START_VADDR  (void *)0x50000000

/* @brief   Aux mini-UART registers of the BCM2835
 */
struct bcm2835_aux_registers
{
  uint32_t irq;
  uint32_t enables;
  uint32_t resvd1[14];
  uint32_t mu_io_reg;
  uint32_t mu_ier_reg;
  uint32_t mu_iir_reg;
  uint32_t mu_lcr_reg;
  uint32_t mu_mcr_reg;
  uint32_t mu_lsr_reg;
  uint32_t mu_msr_reg;
  uint32_t mu_scratch_reg;
  uint32_t mu_cntl_reg;
  uint32_t mu_stat_reg;
  uint32_t mu_baud_reg;
};



#endif


