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
#include <sys/fsreq.h>
#include <sys/debug.h>
#include <sys/event.h>
#include <machine/cheviot_hal.h>
#include <sys/rpi_gpio.h>
#include "gpio.h"
#include "globals.h"


/* @brief   The "/dev/null" device driver
 */
void main(int argc, char *argv[])
{
  int sc;
  msgid_t msgid;
  struct fsreq req;
  int nevents;
  struct kevent ev;
  struct sigaction sact;
  
  log_info("gpio driver started");
  
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
          case CMD_SENDMSG:
            cmd_sendmsg(portid, msgid, &req);
            break;

          default:
            log_warn("unknown command: %d", req.cmd);
            replymsg(portid, msgid, -ENOTSUP, NULL, 0);
            break;
        }
      }      
      
      if (sc < 0) {
        exit(EXIT_FAILURE);
      }
    }
  }
  
  exit(0);
}


/*
 *
 */
void cmd_sendmsg(int portid, msgid_t msgid, struct fsreq *req)
{
  int sc;
  size_t req_sz;
  int subclass;
  struct msg_gpio_req gpio_req;
  
  subclass = req->args.sendmsg.subclass;

  if (subclass != MSG_SUBCLASS_GPIO) {
    replymsg(portid, msgid, -EINVAL, NULL, 0);
    return;
  }

  req_sz = readmsg(portid, msgid, &gpio_req, sizeof gpio_req, 0);

  if (req_sz < sizeof gpio_req) {
    replymsg(portid, msgid, -EFAULT, NULL, 0);
    return;
  }

  switch(gpio_req.cmd) {
    case MSG_CMD_SETGPIO:
      sc = cmd_set_gpio(portid, msgid, &gpio_req);
      break;
    case MSG_CMD_GETGPIO:
      sc = cmd_get_gpio(portid, msgid, &gpio_req);
      break;
    default:
      sc = -ENOSYS;
      break;
  }
  
  replymsg(portid, msgid, sc, NULL, 0);
}


/* @brief   Set or clear output of pin
 */
int cmd_set_gpio(int portid, msgid_t msgid, struct msg_gpio_req *gpio_req)
{
  int pin = gpio_req->u.setgpio.gpio;
  int state = gpio_req->u.setgpio.state;
    
	if (pin > MAX_GPIO_PIN) {
		return -EINVAL;
	}

  if (state) {
    hal_mmio_write(&gpio_regs->set[pin / 32], 1U << (pin % 32));
  } else {
    hal_mmio_write(&gpio_regs->clr[pin / 32], 1U << (pin % 32));
  }
  
  return 0;
}


/*
 *
 */
int cmd_get_gpio(int portid, msgid_t msgid, struct msg_gpio_req *gpio_req)
{
  int pin = gpio_req->u.getgpio.gpio;
  int state;
  
	if (pin > MAX_GPIO_PIN) {
		return -EINVAL;
	}

  uint32_t lev = hal_mmio_read(&gpio_regs->lev[pin / 32]);

  if (lev & (1U << (pin % 32)) != 0) {
    state = 1;
  } else {
    state = 0;
  }
  
  return state;
}


/*
 *
 */
void sigterm_handler(int signo)
{
  shutdown = true;
}

