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
#include "globals.h"
#include "tty.h"


/*
 *
 */
void init (int argc, char *argv[])
{	
  if (process_args(argc, argv) != 0) {
    panic("failed to process command line arguments");
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
  config.mode = 0777 | S_IFCHR;
  config.dev = DEV_T_DEV_TTY;

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
      config.dev = strtol(optarg, NULL, 0);
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
  
  portid = createmsgport(config.pathname, 0, &mnt_stat, NMSG_BACKLOG);
  
  if (portid < 0) {
    return -1;
  }

  return 0;
}

