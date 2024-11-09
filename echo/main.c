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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscalls.h>
#include <unistd.h>
#include <sys/iorequest.h>
#include <sys/debug.h>
#include <sys/event.h>
#include "echo.h"
#include "globals.h"


/* @brief   The "/dev/null" device driver
 */
void main(int argc, char *argv[])
{
  int sc;
  msgid_t msgid;
  iorequest_t req;
  int nevents;
  struct kevent ev;
  struct sigaction sact;

 	init(argc, argv);

  sact.sa_handler = &sigterm_handler;
  sigemptyset(&sact.sa_mask);
  sact.sa_flags = 0;
  
  if (sigaction(SIGTERM, &sact, NULL) != 0) {
    exit(-1);
  }
  
  EV_SET(&ev, portid, EVFILT_MSGPORT, EV_ADD | EV_ENABLE, 0, 0, 0); 
  kevent(kq, &ev, 1,  NULL, 0, NULL);

  while (1) {
    errno = 0;
    nevents = kevent(kq, NULL, 0, &ev, 1, NULL);

    if (nevents == 1 && ev.ident == portid && ev.filter == EVFILT_MSGPORT) {
      while ((sc = getmsg(portid, &msgid, &req, sizeof req)) == sizeof req) {
        switch (req.cmd) {
          case CMD_READ:
            cmd_read(msgid, &req);
            break;

          case CMD_WRITE:
            cmd_write(msgid, &req);
            break;

          case CMD_ISATTY:
            replymsg(portid, msgid, false, NULL, 0);
            break;

          default:
            log_warn("unknown command: %d", req.cmd);
            replymsg(portid, msgid, -ENOTSUP, NULL, 0);
            break;
        }
      }      
      
      if (sc != 0) {
        exit(EXIT_FAILURE);
      }
    }
  }
  
  exit(0);
}


/*
 *
 */
void cmd_write(msgid_t msgid, iorequest_t *req)
{
  ssize_t nbytes_written;
  size_t remaining;
  size_t left;
  size_t nbytes_to_copy;
  
  remaining = 0;
  nbytes_written = 0;
  
  printf("cmd_write, req.args.write.sz=%u\n", (uint32_t)req->args.write.sz);
  
  if (tx_free_sz == 0) {
    replymsg(portid, msgid, -EAGAIN, NULL, 0);
    return;
  }
  
  remaining = (tx_free_sz < req->args.write.sz) ? tx_free_sz : req->args.write.sz;

  while(remaining > 0)
  {  
    printf("nbytes_written so far:%d\n", nbytes_written);
    printf("remaining = %u\n", remaining);
    printf("tx_head = %u\n", tx_head);
    printf("tx_sz = %u\n", tx_sz);
    printf("tx_free_head = %u\n", tx_free_head);
    printf("tx_free_sz = %u\n", tx_free_sz);
        
    left = TX_BUF_SZ - tx_free_head;

    printf("left = %u\n", left);

    nbytes_to_copy = (remaining < left) ? remaining : left;

    printf("nbytes_to_copy = %u\n", nbytes_to_copy);
    
    readmsg(portid, msgid, &tx_buf[tx_free_head], nbytes_to_copy, nbytes_written);

    nbytes_written += nbytes_to_copy;          
    tx_free_sz -= nbytes_to_copy;
    tx_sz += nbytes_to_copy;
    remaining -= nbytes_to_copy;
    
    tx_free_head = (tx_free_head + nbytes_to_copy) % TX_BUF_SZ;
  }

  printf("nbytes_written total:%d\n", nbytes_written);
  replymsg(portid, msgid, nbytes_written, NULL, 0);
}


/*
 *
 */
void cmd_read(msgid_t msgid, iorequest_t *req)
{
  ssize_t nbytes_read;
  size_t remaining;
  size_t left;
  size_t nbytes_to_copy;

  printf("cmd_read, req.args.read.sz=%u\n", (uint32_t)req->args.read.sz);
  
  remaining = 0;
  nbytes_read = 0;

  if (tx_sz == 0) {
    char eot = 0x04;
    writemsg(portid, msgid, &eot, 1, 0);
    replymsg(portid, msgid, 0, NULL, 0);
    return;
  }
  
  remaining = (tx_sz < req->args.read.sz) ? tx_sz : req->args.read.sz;

  while(remaining > 0)
  {  
    printf("nbytes_read so far:%d\n", nbytes_read);
    printf("remaining = %u\n", remaining);
    printf("tx_head = %u\n", tx_head);
    printf("tx_sz = %u\n", tx_sz);
    printf("tx_free_head = %u\n", tx_free_head);
    printf("tx_free_sz = %u\n", tx_free_sz);

    left = TX_BUF_SZ - tx_head;

    printf("left = %u\n", left);

    nbytes_to_copy = (remaining < left) ? remaining : left;

    printf("nbytes_to_copy = %u\n", nbytes_to_copy);
    
    writemsg(portid, msgid, &tx_buf[tx_head], nbytes_to_copy, nbytes_read);

    nbytes_read += nbytes_to_copy;          
    tx_free_sz += nbytes_to_copy;
    tx_sz -= nbytes_to_copy;
    remaining -= nbytes_to_copy;
    
    tx_head = (tx_head + nbytes_to_copy) % TX_BUF_SZ;
  }

  printf("nbytes_read total:%d\n", nbytes_read);
  replymsg(portid, msgid, nbytes_read, NULL, 0);
}


/*
 *
 */
void sigterm_handler(int signo)
{
  shutdown = true;
}


