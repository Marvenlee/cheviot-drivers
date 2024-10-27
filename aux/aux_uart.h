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

#ifndef AUX_UART_H
#define AUX_UART_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <limits.h>
#include <sys/lists.h>
#include <sys/fsreq.h>
#include <sys/termios.h>
#include <sys/interrupts.h>
#include <sys/syscalls.h>
#include <sys/syslimits.h>
#include <task.h>
#include <machine/cheviot_hal.h>


// Constants
#define NMSG_BACKLOG 		                  3   // 1 read, 1 write and 1 ioctl/termios/synchronous command

#define AUX_KEVENT_TIMEOUT_SEC            0
#define AUX_KEVENT_TIMEOUT_NSEC   200000000    // 0.2seconds

#define TX_BUF_SZ   8192
#define RX_BUF_SZ   8192


/*
 * Aux driver Configuration settings
 */
struct Config
{
  char pathname[PATH_MAX + 1];
  uid_t uid;
  gid_t gid;
  mode_t mode;
	dev_t dev;
	
  int baud;
  int flow_control;
  int stop_bits;
  bool parity;
};


// config.flow_control
#define FLOW_CONTROL_NONE   0
#define FLOW_CONTROL_HW     1

// Inode number of tty to mount
#define TTY_INODE_NR        0

// Echo EFLAGS
#define EF_RAW    (1<<0)
#define EF_EOT    (1<<1)
#define EF_EOF    (1<<2)



/*
 * Common prototypes
 */

// init.c
void init(int argc, char *argv[]);
int process_args(int argc, char *argv[]);
int get_fdt_device_info(void);
int mount_device(void);

// main.c
void cmd_abort(msgid_t msgid);
void cmd_isatty(msgid_t msgid, struct fsreq *req);
void cmd_read(msgid_t msgid, struct fsreq *req);
void cmd_write(msgid_t msgid, struct fsreq *req);
void cmd_tcsetattr(msgid_t msgid, struct fsreq *req);
void cmd_tcgetattr(msgid_t msgid, struct fsreq *req);

void reader_task(void *arg);
void writer_task(void *arg);
void uart_tx_task(void *arg);
void uart_rx_task(void *arg);
void line_discipline(uint8_t ch);
int backspace(void);
void delete_line(void);
int get_line_length(void);
void echo(uint8_t ch, int eflags);

int add_to_rx_queue(uint8_t ch);
int rem_from_rx_queue(void);
int add_to_tx_queue(uint8_t ch);
int rem_from_tx_queue(void);

void sigterm_handler(int signo);

// aux_uart_hw.c
int aux_uart_configure(int baud);
void aux_uart_set_kevent_mask(int kq);
bool aux_uart_read_ready(void);
bool aux_uart_write_ready(void);
char aux_uart_read_byte(void);
void aux_uart_write_byte(char ch);
void aux_uart_handle_interrupt(uint32_t events);
void aux_uart_unmask_interrupt(void);

#endif

