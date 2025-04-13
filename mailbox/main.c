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
#include <machine/cheviot_hal.h>
#include <sys/rpi_mailbox.h>
#include "mailbox.h"
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
 
  log_info("mailbox driver started");
 
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
          case CMD_SENDIO:
            cmd_sendio(portid, msgid, &req);
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




void cmd_sendio(int portid, msgid_t msgid, iorequest_t *req)
{
  int sc;
  size_t req_sz;
  int subclass;
  struct mailbox_req mailbox_req;
  
  subclass = req->args.sendio.subclass;

  if (subclass != MSG_SUBCLASS_RPIMAILBOX) {
    replymsg(portid, msgid, -EINVAL, NULL, 0);
    return;
  }

  req_sz = readmsg(portid, msgid, &mailbox_req, sizeof mailbox_req, 0);
  
  if (req_sz < sizeof mailbox_req) {
    replymsg(portid, msgid, -EINVAL, NULL, 0);
    return;
  }
  
  switch(mailbox_req.cmd) {
    case MBOX_TAG_GET_CLOCK_STATE:
      sc = cmd_mailbox_get_clock_state(portid, msgid, &mailbox_req);
      break;
    case MBOX_TAG_SET_CLOCK_STATE:
      sc = cmd_mailbox_set_clock_state(portid, msgid, &mailbox_req);
      break;
    case MBOX_TAG_GET_CLOCK_RATE:
      sc = cmd_mailbox_get_clock_rate(portid, msgid, &mailbox_req);
      break;
    case MBOX_TAG_GET_POWER_STATE:
      sc = cmd_mailbox_get_power_state(portid, msgid, &mailbox_req);
      break;
    case MBOX_TAG_SET_POWER_STATE:
      sc = cmd_mailbox_set_power_state(portid, msgid, &mailbox_req);
      break;

    default:
      break;
  }
  

  replymsg(portid, msgid, sc, NULL, 0);
}


/*
 *
 */
void sigterm_handler(int signo)
{
  shutdown = true;
}

