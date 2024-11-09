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

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/syscalls.h>
#include <unistd.h>
#include <sys/iorequest.h>
#include <sys/termios.h>
#include <libfdt.h>
#include <fdthelper.h>
#include <fdt.h>
#include "aux_uart.h"
#include "aux_uart_hw.h"


int portid;
int kq;
int sid = 0;

volatile bool shutdown = false;

struct kevent ev;
struct kevent setev;

uint32_t tx_head;
uint32_t tx_sz;
uint32_t tx_free_head;
uint32_t tx_free_sz;

uint8_t tx_buf[TX_BUF_SZ];

uint32_t rx_head;
size_t rx_sz;
uint32_t rx_free_head;
size_t rx_free_sz;

uint8_t rx_buf[RX_BUF_SZ];

uint32_t line_cnt;
uint32_t line_end;

bool write_pending;
bool read_pending;

msgid_t read_msgid;
msgid_t write_msgid;

Rendez tx_rendez;
Rendez rx_rendez;

Rendez read_cmd_rendez;
Rendez write_cmd_rendez;

iorequest_t read_ioreq;
iorequest_t write_ioreq;

struct termios termios;
struct Config config;

struct bcm2835_aux_registers *aux_regs;
int isrid;
bool interrupt_masked = false;
struct fdthelper helper;
void *aux_vpu_base;
void *aux_phys_base;
size_t aux_reg_size;
int aux_irq;

