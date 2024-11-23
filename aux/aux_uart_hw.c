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

#define LOG_LEVEL_INFO

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/debug.h>
#include <sys/syscalls.h>
#include <sys/mman.h>
#include <sys/event.h>
#include <sys/panic.h>
#include "aux_uart.h"
#include "globals.h"
#include "aux_uart.h"
#include "aux_uart_hw.h"



/* @brief   Configure the Aux UART
 *
 * GPIO configuration must be done elsewhere, either in bootloader, kernel or
 * gpio driver.
 * 
 * GPIO pins 14 and 15 must be configured as alternate function FN5 for Aux UART 
 * configure_gpio(14, AUX_UART_GPIO_ALT_FN, PULL_NONE);
 * configure_gpio(15, AUX_UART_GPIO_ALT_FN, PULL_NONE);
 *
 * TODO: kernel logging should be disabled whilst reconfiguring AUX UART to avoid
 * any deadlocks when the Tx is disabled.
 *
 * TODO: See elinux BCM2835 datasheet errata page
 *
 * TODO: We could enable the real TX int IF we have more data in the driver's tx_buf            
 */

int aux_uart_configure(int baud)
{
  aux_regs = map_phys_mem(aux_phys_base, aux_reg_size,
                          PROT_READ | PROT_WRITE, CACHE_UNCACHEABLE,
                          AUX_REGS_START_VADDR);

  if (aux_regs == NULL) {
    return -ENOMEM;
  }  

  isrid = addinterruptserver(aux_irq, EVENT_AUX_INT);

  if (isrid < 0) {
    return -EINVAL;
  }

  interrupt_masked = true;

  hal_mmio_write(&aux_regs->mu_cntl_reg, 0);                  // Disable Tx and Rx
  hal_mmio_write(&aux_regs->mu_lcr_reg, LCR_8_BIT);
  hal_mmio_write(&aux_regs->mu_mcr_reg, 0);
  hal_mmio_write(&aux_regs->mu_ier_reg, 0);
  hal_mmio_write(&aux_regs->mu_iir_reg, IIR_RX | IIR_TX);
  hal_mmio_write(&aux_regs->mu_baud_reg, AUX_MU_BAUD(baud)); 
  hal_mmio_write(&aux_regs->mu_cntl_reg, CNTL_TX_EN | CNTL_RX_EN);

  // Enable the Aux UART RX interrupt but do not unmask it just yet.            
  hal_mmio_write(&aux_regs->mu_ier_reg, IER_RX_INT_EN);

  return 0;
}


/*
 * TODO: Can we replace cthread_event_kevent_mask with the mask being set in EV_SET for thread events?
 * Can only set the caller's thread events.
 */
void aux_uart_set_kevent_mask(int kq)
{
  cthread_event_kevent_mask(kq, 1<<EVENT_AUX_INT);
}


/* @brief   Aux UART Bottom-Half interrupt handling
 *
 * TODO: Check the IIR register values in the BCM2835 datasheet errata on elinux site.
 * Currently we are polling the output register instead of yielding to other tasks.
 * This is why we get away with not using the transmit interrupt.
 */
void aux_uart_handle_interrupt(uint32_t events)
{
  uint32_t iir;
  uint32_t irq_reg;

  if (events & (1<<EVENT_AUX_INT)) {
    interrupt_masked = true;

    irq_reg = hal_mmio_read(&aux_regs->irq);        

    if(irq_reg & (1<<0)) {

      iir = hal_mmio_read(&aux_regs->mu_iir_reg);      

      if ((iir & (0x01)) == 0) {
        taskwakeupall(&rx_rendez);
        taskwakeupall(&tx_rendez);
      }
    }
  }
}


/* @brief     Unmask the Aux UART's interrupt
 *
 */
void aux_uart_unmask_interrupt(void)
{
  if (interrupt_masked == true) {    
    unmaskinterrupt(aux_irq);
    interrupt_masked = false;
  }
}


/*
 *
 */
bool aux_uart_write_ready(void)
{ 
  return (hal_mmio_read(&aux_regs->mu_lsr_reg) & LSR_TX_EMPTY) ? true : false;  
}


/*
 *
 */
bool aux_uart_read_ready(void)
{ 
  return (hal_mmio_read(&aux_regs->mu_lsr_reg) & LSR_RX_READY) ? true : false;  
}


/*
 *
 */
char aux_uart_read_byte(void)
{
  while (!aux_uart_read_ready());
  return hal_mmio_read(&aux_regs->mu_io_reg);
}


/*
 *
 */
void aux_uart_write_byte(char ch)
{
    while (!aux_uart_write_ready()); 
    hal_mmio_write(&aux_regs->mu_io_reg, ch);
}

