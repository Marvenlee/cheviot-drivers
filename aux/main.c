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
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/syscalls.h>
#include <sys/iorequest.h>
#include <sys/debug.h>
#include <sys/lists.h>
#include <sys/event.h>
#include <sys/panic.h>
#include <task.h>
#include "globals.h"



/* @brief   Main task of the driver for the Raspberry Pi's Aux-UART.
 *
 * Libtask coroutines are used for concurrency in the serial driver
 * We follow Djikstra's Secretaries and Directors pattern of 
 * Cooperating Sequential Processes.  We have the main task acting
 * as the secretary, waiting for external events such as messages
 * arriving on the message port, timers or when implemented, interrupts.
 * This then wakes up director tasks or sub-secretary tasks as needed.
 *
 * We perform a repeated taskyield at the end to let these other tasks
 * run. Once that returns 0, indicating no other tasks are in a runnable
 * state then this main task (secretary) goes back to waiting for
 * incoming events.
 *
 * See EWD310 - Hierarchical Ordering of Sequential Processes at the
 * Djikstra archive.
 */
void taskmain(int argc, char *argv[])
{
  iorequest_t req;
  int sc;
  int nevents;
  msgid_t msgid;
  struct timespec timeout;
  struct sigaction sact;  
  
  init(argc, argv);

  sact.sa_handler = &sigterm_handler;
  sigemptyset(&sact.sa_mask);
  sact.sa_flags = 0;
  
  if (sigaction(SIGTERM, &sact, NULL) != 0) {
    exit(-1);
  }
  
  taskcreate(reader_task, NULL, 8192);
  taskcreate(writer_task, NULL, 8192);
  taskcreate(uart_tx_task, NULL, 8192);
  taskcreate(uart_rx_task, NULL, 8192);

  timeout.tv_sec = AUX_KEVENT_TIMEOUT_SEC;
  timeout.tv_nsec = AUX_KEVENT_TIMEOUT_NSEC;

  aux_uart_set_kevent_mask(kq);
      
  EV_SET(&setev, portid, EVFILT_MSGPORT, EV_ADD | EV_ENABLE, 0, 0, 0); 
  kevent(kq, &setev, 1,  NULL, 0, NULL);

  aux_uart_unmask_interrupt();

  while (!shutdown) {
    errno = 0;
    nevents = kevent(kq, NULL, 0, &ev, 1, &timeout);

#if 1
//    if (nevents < 0) {
      // timeout, wakeup rx and tx tasks, see if they can read or write the fifos
      taskwakeupall(&rx_rendez);
      taskwakeupall(&tx_rendez);
//    }
#endif

    if ((nevents == 1 && ev.filter == EVFILT_THREAD_EVENT)) {
      /* Check for interrupts and awaken Tx and/or Rx tasks */
      aux_uart_handle_interrupt(ev.fflags);
    }
    
    if (nevents == 1 && ev.ident == portid && ev.filter == EVFILT_MSGPORT) {
      while ((sc = getmsg(portid, &msgid, &req, sizeof req)) == sizeof req) {
        switch (req.cmd) {
          case CMD_ABORT:
            cmd_abort(msgid);
            break;

          case CMD_READ:
            cmd_read(msgid, &req);
            break;

          case CMD_WRITE:
            cmd_write(msgid, &req);
            break;

          case CMD_ISATTY:
            cmd_isatty(msgid, &req);
            break;

          case CMD_TCSETATTR:
            cmd_tcsetattr(msgid, &req);
            break;

          case CMD_TCGETATTR:
            cmd_tcgetattr(msgid, &req);
            break;

          default:
            log_warn("aux: unknown command: %d", req.cmd);
            replymsg(portid, msgid, -ENOTSUP, NULL, 0);
            break;
        }
      }      
      
      if (sc < 0) {
        log_error("aux: getmsg sc=%d %s", sc, strerror(errno));
        exit(EXIT_FAILURE);
      }
    }
        
    while (taskyield() != 0);

    if ((nevents == 1 && ev.filter == EVFILT_THREAD_EVENT)) {
      aux_uart_unmask_interrupt();
    }
    
  }

  // TODO: Flush and data to output queue, drain input queue and RX FIFO
  // Remove interrupt handler, disable UART.
  exit(0);
}


/*
 *
 */
void cmd_isatty (msgid_t msgid, iorequest_t *req)
{
  replymsg(portid, msgid, 1, NULL, 0);
}


/*
 *
 */
void cmd_tcgetattr(msgid_t msgid, iorequest_t *req)
{	
  writemsg(portid, msgid, &termios, sizeof termios, 0);
  replymsg(portid, msgid, 0, NULL, 0);
}


/*
 * FIX: add actions to specify when/what should be modified/flushed.
 */ 
void cmd_tcsetattr(msgid_t msgid, iorequest_t *req)
{
  log_info("**** tcsetattr ****");
 
  readmsg(portid, msgid, &termios, sizeof termios, 0);

  // TODO: Flush any buffers, change stream mode to canonical etc

	replymsg(portid, msgid, 0, NULL, 0);
}


/* @brief   Abort an in-progress message.
 *
 * TODO: Return any remaining bytes read or bytes already written.
 * Alternatively, set read_cancel = true and signal the rendez to
 * let the tasks perform the cleanup and return of remaining data.
 */
void cmd_abort(msgid_t msgid)
{  
  if (read_pending == true && read_msgid == msgid) {
    read_pending = false;
    read_msgid = -1;

    taskwakeup(&read_cmd_rendez);
    taskwakeupall(&rx_rendez);    
    replymsg(portid, msgid, -EINTR, NULL, 0);
    
  } else if (write_pending == true && write_msgid == msgid) {
    write_pending = false;
    write_msgid = -1;
    
    taskwakeup(&write_cmd_rendez);
    taskwakeupall(&tx_rendez);
  	replymsg(portid, msgid, -EINTR, NULL, 0);  

  } else {
    panic("aux: abortmsg on non-existant message");  
  }
}


/*
 * UID belongs to sending thread (for char devices)
 * Can only be 1 thread doing read at a time.
 *
 * Only 1 reader and only 1 writer and only 1 tc/isatty cmd
 */
void cmd_read(msgid_t msgid, iorequest_t *req)
{
  read_pending = true;
  read_msgid = msgid;
  memcpy (&read_ioreq, req, sizeof read_ioreq);
  taskwakeup(&read_cmd_rendez);
}


/*
 * 
 */
void cmd_write(msgid_t msgid, iorequest_t *req)
{
  write_pending = true;
  write_msgid = msgid;
  memcpy (&write_ioreq, req, sizeof write_ioreq);
  taskwakeup(&write_cmd_rendez);
}


/* @brief   Handle a read message request from cmd_read.
 *
 * This is a "director" coroutine/task in the "secretaties and directors" model of 
 * cooperating sequential processes (CSP).
 */
void reader_task (void *arg)
{
  ssize_t nbytes_read;
  size_t line_length;
  size_t remaining;
  size_t left;
  size_t nbytes_to_copy;

  while (!shutdown) {
    while (read_pending == false) {
      tasksleep (&read_cmd_rendez);
    }
        
    while (rx_sz == 0 && read_pending == true) {
      tasksleep(&rx_rendez);
    }

    if (read_pending == false) {
      continue;
    }

    if (termios.c_lflag & ICANON) {
      while(line_cnt == 0 && read_pending == true) {
        tasksleep(&rx_rendez);
      }
    
      if (read_pending == false) {
        continue;
      }
    
      // Could we remove line_length calculation each time ?
      line_length = get_line_length();
      remaining = (line_length < read_ioreq.args.read.sz) ? line_length : read_ioreq.args.read.sz;
    } else {
      remaining = (rx_sz < read_ioreq.args.read.sz) ? rx_sz : read_ioreq.args.read.sz;
    }

    nbytes_read = 0;

    while(remaining > 0)
    {  
      left = RX_BUF_SZ - rx_head;

      nbytes_to_copy = (remaining < left) ? remaining : left;
      
      writemsg(portid, read_msgid, &rx_buf[rx_head], nbytes_to_copy, nbytes_read);

      nbytes_read += nbytes_to_copy;          
      tx_free_sz += nbytes_to_copy;
      rx_sz -= nbytes_to_copy;
      remaining -= nbytes_to_copy;
      
      rx_head = (rx_head + nbytes_to_copy) % RX_BUF_SZ;
    }
    
    if (termios.c_lflag & ICANON) {
      if (nbytes_read == line_length) {
        line_cnt--;
      }
    }
          
    taskwakeupall(&rx_rendez);
    
    replymsg(portid, read_msgid, nbytes_read, NULL, 0);

    read_msgid = -1;
    read_pending = false;
  }
}


/* @brief   Handle a write message request from cmd_write.
 *
 * This is a "director" coroutine/task in the "secretaties and directors" model of 
 * cooperating sequential processes (CSP).
 */
void writer_task (void *arg)
{
  ssize_t nbytes_written;
  size_t remaining;
  size_t left;
  size_t nbytes_to_copy;
  
  while (1) {
    while (write_pending == false) {
      tasksleep (&write_cmd_rendez);
    }

    while (tx_free_sz == 0 && write_pending == true) {
      tasksleep(&tx_rendez);
    }

    if (write_pending == false) {
      // Command aborted
      continue;
    }

    nbytes_written = 0;    
    remaining = (tx_free_sz < write_ioreq.args.write.sz) ? tx_free_sz : write_ioreq.args.write.sz;
  
    while(remaining > 0)
    {  
      left = TX_BUF_SZ - tx_free_head;

      nbytes_to_copy = (remaining < left) ? remaining : left;
      
      readmsg(portid, write_msgid, &tx_buf[tx_free_head], nbytes_to_copy, nbytes_written);

      nbytes_written += nbytes_to_copy;          
      tx_free_sz -= nbytes_to_copy;
      tx_sz += nbytes_to_copy;
      remaining -= nbytes_to_copy;
      
      tx_free_head = (tx_free_head + nbytes_to_copy) % TX_BUF_SZ;
    }
    
    replymsg(portid, write_msgid, nbytes_written, NULL, 0);
    
    write_msgid = -1;
    write_pending = false;

    taskwakeupall(&tx_rendez);
  }
}


/* @brief   Handle loading of characters into the Aux UART's Tx FIFO
 *
 * This can be thought of as an interrupt handler.  When notified of a change in the
 * Aux UART's FIFO, this task is awakened.
 *
 * This is a "subsecretary" coroutine/task in the "secretaties and directors" model of 
 * cooperating sequential processes (CSP).
 */
void uart_tx_task(void *arg)
{
  while (!shutdown) {
    while (tx_sz == 0 || aux_uart_write_ready() == false) {
      tasksleep(&tx_rendez);
    }
  
    while (tx_sz > 0) {
      aux_uart_write_byte(tx_buf[tx_head]);

      tx_head = (tx_head + 1) % TX_BUF_SZ;
      tx_sz--;
      tx_free_sz++;
    }

    if (tx_free_sz > 0) {
      taskwakeupall(&tx_rendez);
      // TODO: knotei() indicate we have free space in output buffer to write to
    }   
  }
}


/* @brief   Handle the reading of characters from the Aux UART's Rx FIFO
 *
 * This can be thought of as an interrupt handler.  When notified of a change in the
 * Aux UART's Rx FIFO, this task is awakened.
 *
 * This is a "subsecretary" coroutine/task in the "secretaties and directors" model of 
 * cooperating sequential processes (CSP).
 */
void uart_rx_task(void *arg)
{
  uint32_t flags;
  uint8_t ch;
  
  while(!shutdown)
  {
    while (rx_free_sz == 0 || aux_uart_read_ready() == false) {
      tasksleep (&rx_rendez);
    }

    while(rx_free_sz > 0 && aux_uart_read_ready() == true) {
      ch = aux_uart_read_byte();
      line_discipline(ch);         
    }  

    if (termios.c_lflag & ICANON) {
      if (line_cnt > 0) {
        taskwakeupall(&rx_rendez);
      }
    }
    else if (rx_sz > 0) {
      taskwakeupall(&rx_rendez);
      // TODO: knotei() indicate we have data to read
    }
  }
}


/* @brief   Line buffer processing when in canonical mode and ctrl character handling
 *
 */
void line_discipline(uint8_t ch)
{
  int sig = -1;
  int eflags = 0;
  
  if (termios.c_iflag & ISTRIP) {
    ch &= 0x7F;
  }
  
	if (ch == '\n' && termios.c_iflag & INLCR) {
    ch = '\r';
	} else if (ch == '\r' && termios.c_iflag & ICRNL) {
    ch = '\n';
  } else if (ch == '\r' && termios.c_iflag & IGNCR) {
    return;
	}
		
	if (termios.c_lflag & ICANON) {
		if (ch == termios.c_cc[VERASE]) {
			backspace();
			
			if (!(termios.c_lflag & ECHOE)) {
				echo(ch, 0);
			}
						
			return;
		}

		if (ch == termios.c_cc[VKILL]) {
		  delete_line();
			
			if (!(termios.c_lflag & ECHOE)) {
				echo(ch, 0);
				
				if (termios.c_lflag & ECHOK) {
					echo('\n', EF_RAW);
				}
			}
						
			return;
		}

		if (ch == '\n') {
		  eflags |= EF_EOT;
    }
    
		if (ch == termios.c_cc[VEOL]) {
		  eflags |= EF_EOT;
		}

		if (ch == termios.c_cc[VEOF]) {
	    eflags |= EF_EOT | EF_EOF;
    }    
	}

	if (termios.c_lflag & ISIG) {
		if (ch == termios.c_cc[VINTR]) {
			sig = SIGINT;
			signalnotify(portid, TTY_INODE_NR, sig);
			echo('^', eflags);
			echo('C', eflags);
      return;
      
		} else if(ch == termios.c_cc[VQUIT]) {
			sig = SIGQUIT;
			signalnotify(portid, TTY_INODE_NR, sig);
			echo('^', eflags);
			echo('\\', eflags);
			return;
		}
	}
 
 	if (termios.c_lflag & (ECHO | ECHONL)) {
	  echo(ch, eflags);
  }
  
  add_to_rx_queue(ch);
  
	if (eflags & EF_EOT) {
	  line_cnt++;
  }  
}


/* @brief   Delete character from current line
 *
 * @return  1 if character deleted, 0 if already at start of line.
 */
int backspace(void)
{
  uint8_t last_char;
  
  last_char = rx_buf[(rx_head + rx_sz - 1) % RX_BUF_SZ];
  if (last_char != termios.c_cc[VEOL] && last_char != termios.c_cc[VEOL2]) {
    rem_from_rx_queue();
    echo('\b', 0);    // FIXME : Should we use termios.c_cc[VERASE] ???
    echo(' ', 0);
    echo('\b', 0);
    return 1;
  }
  
  return 0;
}


/* @brief   Delete an entire line
 *
 */
void delete_line(void)
{
  while (backspace());			
}


/*
 *
 */
int get_line_length(void)
{  
  for (int t=0; t < rx_sz; t++) {    
    if (rx_buf[(rx_head + t) % RX_BUF_SZ] == termios.c_cc[VEOL] ||
        rx_buf[(rx_head + t) % RX_BUF_SZ] == termios.c_cc[VEOL2]) {
      return t+1;
    }
  }
  
  return rx_sz;
}


/*
 *
 */
void echo(uint8_t ch, int eflags)
{
  if (eflags & EF_RAW && termios.c_lflag & ECHO) {
    add_to_tx_queue(ch);
    return;
  }

  if (!(termios.c_lflag & ECHO)) {
  	if (ch == '\n' && (eflags & EF_EOT)
  	    && (termios.c_lflag & (ICANON | ECHONL)) == (ICANON | ECHONL)) {
		  add_to_tx_queue('\n');
  	}
  	
  	return;
  }
    
  add_to_tx_queue(ch);
  
  if (eflags & EF_EOF) {
    // TODO: Flush output on end of file, wait for buffer to empty
  }  
}


/*
 *
 */
int add_to_rx_queue(uint8_t ch)
{
  if (rx_free_sz > 0) {
    rx_buf[rx_free_head] = ch;
    rx_sz++;
    rx_free_head = (rx_free_head + 1) % RX_BUF_SZ;
    rx_free_sz--;
  }

  taskwakeupall(&rx_rendez);
  
  return 0;
}


/*
 *
 */
int rem_from_rx_queue(void)
{
  if (rx_sz > 0) {
    rx_sz --;
    rx_free_head -= 1;
    
    if (rx_free_head >= RX_BUF_SZ) {
      rx_free_head = RX_BUF_SZ - 1;
    }
    
    rx_free_sz++;    
  }
  
  return 0;
}


/*
 *
 */
int add_to_tx_queue(uint8_t ch)
{
  while (tx_free_sz == 0) {
    tasksleep(&tx_rendez);
  }

  tx_buf[tx_free_head] = ch;
  tx_free_head = (tx_free_head + 1) % TX_BUF_SZ;
  tx_free_sz--;
  tx_sz++;

  taskwakeupall(&tx_rendez);

  return 0;
}


/*
 *
 */
void sigterm_handler(int signo)
{
  shutdown = true;
}


