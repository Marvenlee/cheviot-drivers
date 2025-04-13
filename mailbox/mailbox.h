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

#ifndef _MAILBOX_H
#define _MAILBOX_H

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
#include <machine/cheviot_hal.h>
#include <string.h>
#include <sys/rpi_mailbox.h>


// Constants
#define PERIPHERAL_BASE       0xFE000000                        /* Peripheral ARM phyiscal address */
#define MBOX_BASE             (PERIPHERAL_BASE + 0x0000B880)    /* VideoCore Mailbox base */
#define MBOX_BASE_PAGE        (PERIPHERAL_BASE + 0x0000B000)    /* Page-aligned base */
#define MBOX_BASE_OFFSET      0x880                             /* Offset within page aligned base */
#define MMAP_START_BASE       0x60000000
#define MMAP_START_BASE2      0x70000000

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


/*
 * Common prototypes
 */
void init(int argc, char *argv[]);
int process_args(int argc, char *argv[]);
int mount_device(void);
int get_fdt_device_info(void);
int init_mailbox(void);

void cmd_sendio(int portid, msgid_t msgid, iorequest_t *req);

int post_mailbox(uint32_t tag, void *request, int req_sz, void *response, int response_sz);

int cmd_mailbox_get_clock_state(int portid, int msgid, struct mailbox_req *req);
int cmd_mailbox_set_clock_state(int portid, int msgid, struct mailbox_req *req);
int cmd_mailbox_get_clock_rate(int portid, int msgid, struct mailbox_req *req);

int cmd_mailbox_get_power_state(int portid, int msgid, struct mailbox_req *req);
int cmd_mailbox_set_power_state(int portid, int msgid, struct mailbox_req *req);

void sigterm_handler(int signo);

#endif

