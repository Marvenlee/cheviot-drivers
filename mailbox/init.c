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
#include <sys/rpi_mailbox.h>
#include <fdthelper.h>
#include <sys/param.h>
#include <machine/param.h>
#include <libfdt.h>
#include "mailbox.h"
#include "globals.h"


/*
 *
 */
void init (int argc, char *argv[])
{	
  if (process_args(argc, argv) != 0) {
    panic("failed to process command line arguments");
  }

  if (get_fdt_device_info() != 0) {
    panic("failed to read device tree blob");
  }

  if (init_mailbox() != 0) {
    panic("init mailbox failed");
  }

  if (mount_device() < 0) {
    panic("mount device failed");
  }

  kq = kqueue();
  
  if (kq < 0) {
    panic("create kqueue failed");
  }
}


/*
 *
 */
int process_args(int argc, char *argv[])
{
  int c;

  config.uid = 0;
  config.gid = 0;
  config.dev = -1;
  config.mode = 0777 | S_IFCHR;
  
  if (argc < 1) {
    log_error("no command line arguments, argc:%d", argc);
    return -1;
  }
  
  while ((c = getopt(argc, argv, "u:g:m:d:")) != -1) {
    switch (c) {
    case 'u':
      config.uid = strtoul(optarg, NULL, 0);
      break;

    case 'g':
      config.gid = strtoul(optarg, NULL, 0);
      break;

    case 'm':
      config.mode = strtoul(optarg, NULL, 0);
      break;

    case 'd':
      config.dev = strtoul(optarg, NULL, 0);
      break;
      
    default:
      break;
    }
  }

  if (optind >= argc) {
    panic("missing mount pathname");
  }

  strncpy(config.pathname, argv[optind], sizeof config.pathname);
  return 0;
}


/*
 *
 */
int mount_device(void)
{
  struct stat mnt_stat;

  mnt_stat.st_dev = config.dev;
  mnt_stat.st_ino = 0;
  mnt_stat.st_mode = S_IFCHR | (config.mode & 0777);

  // default to read/write of device-driver uid/gid.
  mnt_stat.st_uid = config.uid;
  mnt_stat.st_gid = config.gid;
  mnt_stat.st_blksize = 0;
  mnt_stat.st_size = 0;
  mnt_stat.st_blocks = 0;
  
  portid = createmsgport(config.pathname, 0, &mnt_stat);
  
  if (portid < 0) {
    log_error("failed to create msgport");
    return -1;
  }

  return 0;
}


/* @brief   Read the device tree file and gather gpio configuration
 *
 * Need some way of knowing which dtb to use
 * Specify on command line?  or add a kernel sys_get_dtb_name()
 * Passed in bootinfo.
 */
int get_fdt_device_info(void)
{
  int offset;
  int len;
  struct fdthelper helper;
  
  if (load_fdt("/lib/firmware/dt/rpi4.dtb", &helper) != 0) {
    log_error("failed to open device tree\n");
    return -EIO;
  }

  // check if the file is a valid fdt
  if (fdt_check_header(helper.fdt) != 0) {
    log_error("header not valid\n");
    unload_fdt(&helper);
    return -EIO;
  }
     
  if ((offset = fdt_path_offset(helper.fdt, "/soc/mailbox")) < 0) {
    log_error("failed to find /soc/mailbox\n");
    unload_fdt(&helper);
    return -EIO;
  }
  
  if (fdthelper_check_compat(helper.fdt, offset, "brcm,bcm2835-mbox") != 0) {
    log_error("mailbox not compatible\n");
    unload_fdt(&helper);
    return -EIO;
  }

  if (fdthelper_get_reg(helper.fdt, offset, &mailbox_vpu_base, &mailbox_reg_size) != 0) {
    log_error("failed to get register base");
    unload_fdt(&helper);
    return -EIO;
  } 

  if (fdthelper_translate_address(helper.fdt, mailbox_vpu_base, &mailbox_phys_base) != 0) {
    log_error("failed to translate register base");
    unload_fdt(&helper);
    return -EIO;        
  }

  unload_fdt(&helper);
  return 0;  
}


int init_mailbox(void)
{
#if 1
  void *mailbox_phys_page;
  
  mailbox_phys_page = (void *)rounddown((uintptr_t)mailbox_phys_base, PAGE_SIZE);

  // map mailbuffer registers
  mailbox_base = virtualallocphys((void *)0x60000000, 4096, PROT_READWRITE, mailbox_phys_page);

  if (mailbox_base == NULL) {
    return -1;
  }
  hal_set_mbox_base((void *)mailbox_base + MBOX_BASE_OFFSET);

#else
  mailbox_base = map_phys_mem(mailbox_base_page, mailbox_regs_size, PROT_READWRITE, (void *)0x60000000);
#endif
  
  mailbuffer = virtualalloc((void *)0x70000000, 4096, PROT_READWRITE);

  if (mailbuffer == NULL) {
    return -1;
  }

  mailbuffer_phys = virtualtophysaddr(mailbuffer);
    
  return 0;
}









