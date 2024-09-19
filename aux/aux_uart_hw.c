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

#define LOG_LEVEL_WARN

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
#include <sys/event.h>
#include <sys/panic.h>
#include "aux_uart.h"
#include "globals.h"
#include <libfdt.h>
#include "aux_uart.h"
#include "aux_uart_hw.h"
#include <fdthelper.h>
#include <fdt.h>

/*
 * Memory mapped IO locations
 */
struct bcm2835_aux_registers *aux_regs;
int isrid;
bool interrupt_masked = false;
struct fdthelper helper;
void *aux_vpu_base;
void *aux_phys_base;
size_t aux_reg_size;
int aux_irq;


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
 */ 
int aux_uart_configure(int baud)
{
  if (get_fdt_device_info() != 0) {
    return -EIO;
  }

  aux_regs = map_phys_mem(aux_phys_base, aux_reg_size,
                          PROT_READ | PROT_WRITE | CACHE_UNCACHEABLE, 
                          AUX_REGS_START_VADDR);

  if (aux_regs == NULL) {
    return -ENOMEM;
  }  

  isrid = addinterruptserver(aux_irq, EVENT_AUX_INT);

  if (isrid < 0) {
    return -EINVAL;
  }
  
  interrupt_masked = true;
  
  hal_mmio_write(&aux_regs->mu_cntl_reg, 0);                // Disable Tx and Rx
  hal_mmio_write(&aux_regs->mu_lcr_reg, LCR_8_BIT | 0x02);  // FIXME: Unsure of 0x02
  hal_mmio_write(&aux_regs->mu_mcr_reg, 0);
  hal_mmio_write(&aux_regs->mu_ier_reg, 0);
  hal_mmio_write(&aux_regs->mu_iir_reg, IIR_RX | IIR_TX | 0xC0);  // 0xC0 bits should be read-only
  hal_mmio_write(&aux_regs->mu_baud_reg, AUX_MU_BAUD(baud)); 

  hal_mmio_write(&aux_regs->mu_cntl_reg, CNTL_TX_EN | CNTL_RX_EN);
  
  // Enable the Aux UART interrupt but do not unmask it just yet.
  hal_mmio_write(&aux_regs->mu_ier_reg, IER_TX_INT_EN | IER_RX_INT_EN);

  return 0;
}


/* @brief   Read the device tree file and gather aux uart configuration
 *
 * Need some way of knowing which dtb to use
 * Specify on command line?  or add a kernel sys_get_dtb_name()
 * Passed in bootinfo.
 */
int get_fdt_device_info(void)
{
  int offset;
  int len;
  
  if (load_fdt("/lib/firmware/dt/rpi4.dtb", &helper) != 0) {
    return -EIO;
  }

  // check if the file is a valid fdt
  if (fdt_check_header(helper.fdt) != 0) {
    unload_fdt(&helper);
    return -EIO;
  }
     
  if ((offset = fdt_path_offset(helper.fdt, "/soc/aux")) < 0) {
    unload_fdt(&helper);
    return -EIO;
  }
  
  if (fdthelper_check_compat(helper.fdt, offset, "brcm,bcm2835-aux") != 0) {
    unload_fdt(&helper);
    return -EIO;
  }

  if (fdthelper_get_reg(helper.fdt, offset, &aux_vpu_base, &aux_reg_size) != 0) {
    unload_fdt(&helper);
    return -EIO;
  } 

  if (fdthelper_translate_address(helper.fdt, aux_vpu_base, &aux_phys_base) != 0) {
    unload_fdt(&helper);
    return -EIO;        
  }

  if (fdthelper_get_irq(helper.fdt, offset, &aux_irq) != 0) {
    unload_fdt(&helper);
    return -EIO;
  }   

  unload_fdt(&helper);
  return 0;  
}


/*
 *
 */
void aux_uart_set_kevent_mask(int kq)
{
  cthread_event_kevent_mask(kq, 1<<EVENT_AUX_INT);
}


/* @brief   Aux UART Bottom-Half interrupt handling
 *
 */
void aux_uart_handle_interrupt(uint32_t events)
{
  uint32_t iir;

  if (events & (1<<EVENT_AUX_INT)) {
    interrupt_masked = true;
    
    log_info("aux handle interrupt");
    
    iir = hal_mmio_read(&aux_regs->mu_iir_reg);      

#if 1
    if (iir & IIR_RX) {
      log_info("aux IIR_RX wakeup");
      taskwakeupall(&rx_rendez);
    }

    if (iir & IIR_TX) {
      log_info("aux IIR_TX wakeup");
      taskwakeupall(&tx_rendez);
    }
#else
    // TODO: Reduce number of rendez (tx_rendez and rx_rendez aren't needed).
    TaskSleep(&tx_rendez);
    TaskSleep(&rx_rendez);
    TaskSleep(&tx_free_rendez);
    TaskSleep(&rx_data_rendez);
    TaskSleep(&write_cmd_rendez);
    TaskSleep(&read_cmd_rendez);
#endif    
  }
}


/* @brief     Unmask the Aux UART's interrupt
 *
 */
void aux_uart_unmask_interrupt(void)
{
  if (interrupt_masked == true) {
    log_info("aux unmask interrupt");
    unmaskinterrupt(AUX_UART_IRQ);
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

