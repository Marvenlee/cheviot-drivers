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

#ifndef SYSINFO_GLOBALS_H
#define SYSINFO_GLOBALS_H

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
#include <limits.h>
#include <machine/cheviot_hal.h>
#include <sys/rpi_gpio.h>
#include "gpio.h"


extern int portid;
extern int kq;

extern struct Config config;

extern void *gpio_vpu_base;
extern size_t gpio_reg_size;
extern void *gpio_phys_base;

extern struct bcm2711_gpio_registers *gpio_regs;

extern bool shutdown;

#endif

