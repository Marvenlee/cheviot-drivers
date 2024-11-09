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
#include "sysinfo.h"
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
 
  log_info("sysinfo driver started");
 
 	init(argc, argv);
  
  struct sigaction sact;
  sact.sa_handler = &sigterm_handler;
  sigemptyset(&sact.sa_mask);
  sact.sa_flags = 0;
  
  if (sigaction(SIGTERM, &sact, NULL) != 0) {
    exit(-1);
  }

  EV_SET(&ev, portid, EVFILT_MSGPORT, EV_ADD | EV_ENABLE, 0, 0, 0); 
  kevent(kq, &ev, 1,  NULL, 0, NULL);

  while (!shutdown) {
    errno = 0;
    nevents = kevent(kq, NULL, 0, &ev, 1, NULL);

    if (nevents == 1 && ev.ident == portid && ev.filter == EVFILT_MSGPORT) {
      while ((sc = getmsg(portid, &msgid, &req, sizeof req)) == sizeof req) {
        switch (req.cmd) {
          case CMD_SENDMSG:
            cmd_sendmsg(portid, msgid, &req);
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



void cmd_sendmsg(int portid, msgid_t msgid, iorequest_t *req)
{
  int sc;
  size_t req_sz;
  size_t resp_sz;
  size_t max_resp_sz;
  int subclass;
  char *cmd;
  
  
  subclass = req->args.sendmsg.subclass;
  req_sz = req->args.sendmsg.ssize;  
  max_resp_sz = req->args.sendmsg.rsize;  

  if (req_sz > sizeof req_buf) {
    log_error("sysinfo: req_sz %d > sizeof req_buf %d\n", req_sz, sizeof req_buf);
    replymsg(portid, msgid, -E2BIG, NULL, 0);
    return;
  }

  readmsg(portid, msgid, req_buf, req_sz, 0);
  
  req_buf[req_sz] = '\0';  
  resp_buf[0] = '\0';

  cmd = strtok(req_buf, " ");
  
  if (!(cmd == NULL || cmd[0] == '\0')) {
    if (strncmp("#", cmd, 1) == 0) {
      // Comment
    } else if (strcmp("help", cmd) == 0) {
      subcmd_help(portid, msgid, req);
    } else if (strcmp("getinfo", cmd) == 0) {
      subcmd_getinfo(portid, msgid, req);
    }
  } else {
    strlcpy(resp_buf, "ERROR\n", sizeof resp_buf);
  }
  
  resp_sz = strlen(resp_buf);
      
  writemsg(portid, msgid, resp_buf, resp_sz, 0);
  replymsg(portid, msgid, resp_sz, NULL, 0);
}


/*
 *
 */
void subcmd_help(int portid, msgid_t msgid, iorequest_t *req)
{
  strlcpy (resp_buf, "OK: help\n\n"
                     "help    - get command list\n"
                     "getinfo - get system info\n", sizeof resp_buf);
}


/*
 *
 */
void subcmd_getinfo(int portid, msgid_t msgid, iorequest_t *req)
{
  strlcpy (resp_buf, "OK: getinfo\n\n"
                     "BOARD: raspberry pi\n"
                     "RAM: 2GB\n", sizeof resp_buf);
}


/*
 *
 */
void sigterm_handler(int signo)
{
  shutdown = true;
}

