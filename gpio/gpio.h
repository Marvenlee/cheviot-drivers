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

#ifndef _GPIO_H
#define _GPIO_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <limits.h>
#include <sys/lists.h>
#include <sys/iorequest.h>
#include <sys/termios.h>
#include <sys/interrupts.h>
#include <sys/syscalls.h>
#include <sys/syslimits.h>
#include <sys/rpi_gpio.h>
#include <machine/cheviot_hal.h>
#include <string.h>


// Constants
#define MAX_GPIO_PIN        64      // TODO: Get this from device tree
#define MMAP_START_BASE     0x60000000


/*
 * Random driver Configuration settings
 */
struct Config
{
  char pathname[PATH_MAX + 1];
  uid_t uid;
  gid_t gid;
  mode_t mode;
  dev_t dev;
};


struct bcm2711_gpio_registers
{
  uint32_t fsel[6];     // 0x00
  uint32_t resvd1;      // 0x18
  uint32_t set[2];      // 0x1C
  uint32_t resvd2;      // 0x24
  uint32_t clr[2];      // 0x28
  uint32_t resvd3;      // 0x30
  uint32_t lev[2];      // 0x34
  uint32_t resvd4;      // 0x3C
  uint32_t eds[2];      // 0x40
  uint32_t resvd5;      // 0x48
  uint32_t ren[2];      // 0x4C
  uint32_t resvd6;      // 0x54
  uint32_t fen[2];      // 0x58
  uint32_t resvd7;      // 0x60
  uint32_t hen[2];      // 0x64
  uint32_t resvd8;      // 0x6C
  uint32_t len[2];      // 0x70
  uint32_t resvd9;      // 0x78
  uint32_t aren[2];     // 0x7C
  uint32_t resvd10;     // 0x84
  uint32_t afen[2];     // 0x88
  
  uint32_t resvd11[21];     // 0x90  
  uint32_t pup_pdn_cntrl[4];  // 0xE4

//  uint32_t pud;         // 0x94
//  uint32_t pud_clk[2];  // 0x98
};




/*
 * Common prototypes
 */
void init(int argc, char *argv[]);
int process_args(int argc, char *argv[]);
int mount_device(void);
int get_fdt_device_info(void);
int init_gpio_regs(void);

void cmd_sendio(int portid, msgid_t msgid, iorequest_t *req);
int cmd_set_gpio(int portid, msgid_t msgid, struct msg_gpio_req *gpio_req);
int cmd_get_gpio(int portid, msgid_t msgid, struct msg_gpio_req *gpio_req);
void sigterm_handler(int signo);

#endif

