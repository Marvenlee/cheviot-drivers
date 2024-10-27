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
#include <libfdt.h>
#include <fdthelper.h>
#include <fdt.h>
#include "aux_uart.h"
#include "globals.h"

/*
 *
 */
void init (int argc, char *argv[])
{
	termios.c_iflag = BRKINT | ICRNL | IMAXBEL | IXON | IXANY;
	termios.c_oflag = OPOST | ONLCR | OXTABS;
	termios.c_lflag = ECHO | ICANON | ISIG | IEXTEN | ECHOE | ECHOKE | ECHOCTL;
	termios.c_cflag = CREAD | CS8 | HUPCL;

  termios.c_ispeed = B115200;
  termios.c_ospeed = B115200;
	
	for (int t=0; t < NCCS; t++) {
		termios.c_cc[t] = 0;
	}
	
	termios.c_cc[VEOF]   = 0x04;	// EOT   (ctrl-d)
	termios.c_cc[VEOL]   = 0x0A;  // LF ('\n')
	termios.c_cc[VEOL2]  = 0x0D;  // CR ('\r')
	termios.c_cc[VERASE] = 0x7F;  // DEL
	termios.c_cc[VINTR]  = 0x03;  // ETX   (ctrl-c)
	termios.c_cc[VKILL]  = 0x15;  // NAK   (ctrl-u)
	termios.c_cc[VMIN]   = 0;
	termios.c_cc[VQUIT]  = 0x1C;  // FS    (ctrl-/)
	termios.c_cc[VTIME]  = 0;
	termios.c_cc[VSUSP]  = 0x1A;  // SUB   (ctrl-z)
	termios.c_cc[VSTART] = 0x11;  // DC1   (ctrl-q)
	termios.c_cc[VSTOP]  = 0x13;  // DC3   (ctrl-s)
	termios.c_cc[VREPRINT] = 0x12;  // DC2 (ctrl-r)
	termios.c_cc[VLNEXT]   = 0x16;  // SYN (ctrl-v) 
	termios.c_cc[VDISCARD] = 0x0F;  // SI  (ctrl-o)
	termios.c_cc[VSTATUS]  = 0x14;  // DC4 (ctrl-t)  
  
  tx_head = 0;
  rx_head = 0;
  
  tx_free_head = 0;
  rx_free_head = 0;
  
  tx_sz = 0;
  rx_sz = 0;
  
  tx_free_sz = sizeof tx_buf;
  rx_free_sz = sizeof rx_buf;

  if (process_args(argc, argv) != 0) {
    exit(EXIT_FAILURE);
  }

  if (get_fdt_device_info() != 0) {
    log_error("Failed to read device tree");
    exit(EXIT_FAILURE);
  }
    
  if (aux_uart_configure(config.baud) != 0) {
    log_error("uart initialization failed, exiting");
    exit(EXIT_FAILURE);
  }

  if (mount_device() < 0) {
    log_error("mount device failed, exiting");
    exit(EXIT_FAILURE);
  }

  kq = kqueue();
  
  if (kq < 0) {
    log_error("create kqueue for serial failed");
    exit(EXIT_FAILURE);
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
  config.baud = 115200;
  config.stop_bits = 1;
  config.parity = true;
  config.flow_control = FLOW_CONTROL_NONE;
  
  if (argc <= 1) {
    return -1;
  }
  
  while ((c = getopt(argc, argv, "u:g:m:d:b:s")) != -1) {
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

    case 'b':
      config.baud = strtoul(optarg, NULL, 0);
      break;
    
    case 's':
      config.stop_bits = atoi(optarg);
      if (config.stop_bits < 0 || config.stop_bits > 2) {
        exit(-1);
      }
      break;
    
    case 'p':
      config.parity = true;
      break;  
      
    case 'f':
      if (strcmp(optarg, "hard") == 0) {
        config.flow_control = FLOW_CONTROL_HW;
      } else if (strcmp(optarg, "none") == 0) {
        config.flow_control = FLOW_CONTROL_NONE;
      } else {
        exit(-1);
      }
      
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
int mount_device(void)
{
  struct stat mnt_stat;

  mnt_stat.st_dev = config.dev;
  mnt_stat.st_ino = TTY_INODE_NR;
  mnt_stat.st_mode = S_IFCHR | (config.mode & 0777);
  mnt_stat.st_uid = config.uid;
  mnt_stat.st_gid = config.gid;
  mnt_stat.st_blksize = 0;
  mnt_stat.st_size = 0;
  mnt_stat.st_blocks = 0;
  
  portid = createmsgport(config.pathname, 0, &mnt_stat, NMSG_BACKLOG);
  
  if (portid < 0) {
    return -1;
  }

  return 0;
}


