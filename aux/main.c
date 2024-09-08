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
#include <sys/fsreq.h>
#include <sys/debug.h>
#include <sys/lists.h>
#include <sys/event.h>
#include <sys/panic.h>
#include <task.h>
#include "common.h"
#include "globals.h"


/* @brief   Main task of the serial driver for Pi's mini-uart
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
 */
void taskmain(int argc, char *argv[])
{
  struct fsreq req;
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
  
  taskcreate(reader_task, NULL, 8092);
  taskcreate(writer_task, NULL, 8092);
  taskcreate(uart_tx_task, NULL, 8092);
  taskcreate(uart_rx_task, NULL, 8092);

  timeout.tv_sec = 0;
  timeout.tv_nsec = AUX_KEVENT_TIMEOUT_NS;

  aux_uart_set_kevent_mask(kq);
      
  EV_SET(&setev, portid, EVFILT_MSGPORT, EV_ADD | EV_ENABLE, 0, 0, 0); 
  kevent(kq, &setev, 1,  NULL, 0, NULL);

  aux_uart_unmask_interrupt();

  while (1) {
    errno = 0;
    nevents = kevent(kq, NULL, 0, &ev, 1, &timeout);

    if (nevents == -ETIMEDOUT || (nevents == 1 && ev.filter == EVFILT_THREAD_EVENT)) {
      /* Check for interrupts and awaken Tx and/or Rx tasks
       * Yield until no other task is running. */   
      aux_uart_handle_interrupt(ev.fflags);
      aux_uart_unmask_interrupt();
    }
    
    if (nevents == 1 && ev.ident == portid && ev.filter == EVFILT_MSGPORT) {
      while ((sc = getmsg(portid, &msgid, &req, sizeof req)) == sizeof req) {
        switch (req.cmd) {
          case CMD_ABORT:
            log_error("CMD_ABORT");
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
      
      if (sc != 0) {
        log_error("aux: getmsg sc=%d %s", sc, strerror(errno));
        exit(EXIT_FAILURE);
      }
    }
    
    if (shutdown == true) {
      log_info("shutdown");
      break;
    }
    
    while (taskyield() != 0);
  }

  // TODO: Flush and data to output queue, drain input queue and RX FIFO
  // Remove interrupt handler, disable UART.
  exit(0);
}


/*
 *
 */
void cmd_isatty (msgid_t msgid, struct fsreq *fsreq)
{
  replymsg(portid, msgid, 1, NULL, 0);
}


/*
 *
 */
void cmd_tcgetattr(msgid_t msgid, struct fsreq *fsreq)
{	
  replymsg(portid, msgid, 0, &termios, sizeof termios);
}


/*
 * FIX: add actions to specify when/what should be modified/flushed.
 */ 
void cmd_tcsetattr(msgid_t msgid, struct fsreq *fsreq)
{
  log_info("**** tcsetattr ****");
 
  readmsg(portid, msgid, &termios, sizeof termios, sizeof *fsreq);

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
    taskwakeup(&rx_data_rendez);    
    replymsg(portid, msgid, -EINTR, NULL, 0);
    
  } else if (write_pending == true && write_msgid == msgid) {
    write_pending = false;
    write_msgid = -1;
    
    taskwakeup(&write_cmd_rendez);
    taskwakeup(&tx_rendez);
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
void cmd_read(msgid_t msgid, struct fsreq *req)
{
  read_pending = true;
  read_msgid = msgid;
  memcpy (&read_fsreq, req, sizeof read_fsreq);
  taskwakeup (&read_cmd_rendez);
}


/*
 * 
 */
void cmd_write(msgid_t msgid, struct fsreq *req)
{
  write_pending = true;
  write_msgid = msgid;
  memcpy (&write_fsreq, req, sizeof write_fsreq);
  taskwakeup(&write_cmd_rendez);
}


/*
 *
 */
void reader_task (void *arg)
{
  ssize_t nbytes_read;
  size_t line_length;
  size_t remaining;
  uint8_t *buf;
  size_t sz;

  while (1) {
    while (read_pending == false) {
      tasksleep (&read_cmd_rendez);
    }
    
    sz = 0;
    remaining = 0;
    buf = NULL;
    nbytes_read = 0;
    
    while (rx_sz == 0 && read_pending == true) {
      tasksleep(&rx_data_rendez);
    }

    if (read_pending == false) {
      continue;
    }

    if (termios.c_lflag & ICANON) {
      while(line_cnt == 0 && read_pending == true) {
        tasksleep(&rx_data_rendez);
      }
    
      if (read_pending == false) {
        continue;
      }
    
      // Could we remove line_length calculation each time ?
      line_length = get_line_length();
      remaining = (line_length < read_fsreq.args.read.sz) ? line_length : read_fsreq.args.read.sz;
    } else {
      remaining = (rx_sz < read_fsreq.args.read.sz) ? rx_sz : read_fsreq.args.read.sz;
    }
         
    
    if (rx_head + remaining > sizeof rx_buf) {
      sz = sizeof rx_buf - rx_head;
      buf = &rx_buf[rx_head]; 
      
      writemsg(portid, read_msgid, buf, sz, 0);

      nbytes_read = sz;
    
      sz = remaining - sz;
      buf = &rx_buf[0];
    } else {
	    nbytes_read = 0;
      sz = remaining;
      buf = &rx_buf[rx_head];
    }
        
    writemsg(portid, read_msgid, buf, sz, nbytes_read);

    nbytes_read += sz;

    rx_head = (rx_head + nbytes_read) % sizeof rx_buf;
    rx_free_sz += nbytes_read;
    rx_sz -= nbytes_read;
    
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


/*
 *
 */
void writer_task (void *arg)
{
  ssize_t nbytes_written;
  size_t remaining;
  uint8_t *buf = NULL;
  size_t sz;
  
  while (1) {
    while (write_pending == false) {
      tasksleep (&write_cmd_rendez);
    }
    
    sz = 0;
    remaining = 0;
    buf = NULL;
    nbytes_written = 0;
    
    if (tx_free_sz < 0) {
      log_error("tx_free_sz = %d, < 0", tx_free_sz);
      exit(7);
    }
    
    while (tx_free_sz == 0 && write_pending == true) {
      tasksleep(&tx_free_rendez);
    }

    if (write_pending == false) {
      continue;
    }

    remaining = (tx_free_sz < write_fsreq.args.write.sz) ? tx_free_sz : write_fsreq.args.write.sz;
    nbytes_written = 0;
        
    if (tx_free_head + remaining > sizeof tx_buf) {
      sz = sizeof tx_buf - tx_free_head;
      buf = &tx_buf[tx_free_head]; 

      readmsg(portid, write_msgid, buf, sz, sizeof (struct fsreq));

      nbytes_written += sz;      
      sz = remaining - sz;
      buf = &tx_buf[0];
    } else {
      sz = remaining;
      buf = &tx_buf[tx_free_head];
    }

    if (sz > sizeof tx_buf) {
      log_error("write sz > tx_buf");
      exit (8);
    }
    
    if (sz == 0) {
      log_error("write sz == 0");
      exit (8);
    }
    
    readmsg(portid, write_msgid, buf, sz, sizeof (struct fsreq) + nbytes_written);
    nbytes_written += sz;

    tx_free_head = (tx_free_head + nbytes_written) % sizeof tx_buf;
    tx_free_sz -= nbytes_written;
    tx_sz += nbytes_written;

    replymsg(portid, write_msgid, nbytes_written, NULL, 0);
    
    write_msgid = -1;
    write_pending = false;

    taskwakeupall(&tx_rendez);
  }
}


/*
 * Effectively a deferred procedure call for interrupts running on task state
 */
void uart_tx_task(void *arg)
{
  while (1) {
    while (tx_sz == 0 || aux_uart_write_ready() == false) {
      tasksleep(&tx_rendez);
    }
  
    if (tx_sz + tx_free_sz > sizeof tx_buf) {
      log_error("exit failure tx_sz");
      exit(EXIT_FAILURE);
    }
    
    while (tx_sz > 0 /* && uart_tx_ready() == true */) {
        
        if (tx_head >= sizeof tx_buf) {
          exit(-1);
        }

        aux_uart_write_byte(tx_buf[tx_head]);
        tx_head = (tx_head + 1) % sizeof tx_buf;
        tx_sz--;
        tx_free_sz++;
    }

    if (tx_free_sz > 0) {
      // TODO: knotei() indicate we have free space in output buffer to write to
      taskwakeupall(&tx_free_rendez);
    }   
  }
}


/*
 * Effectively a deferred procedure call for interrupts running on task state
 */
void uart_rx_task(void *arg)
{
  uint32_t flags;
  uint8_t ch;
  
  while(1)
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
        taskwakeupall(&rx_data_rendez);
      }
    }
    else if (rx_sz > 0) {
        taskwakeupall(&rx_data_rendez);
        // TODO: knotei() indicate we have data to read
    }
  }
}


/*
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
  
  last_char = rx_buf[(rx_head + rx_sz - 1) % sizeof rx_buf];
  if (last_char != termios.c_cc[VEOL] && last_char != termios.c_cc[VEOL2]) {
    rem_from_rx_queue();
    echo('\b', 0);    // FIXME : Should we use [VEOL] ????
    echo(' ', 0);
    echo('\b', 0);
    return 1;
  }
  
  return 0;
}


/*
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
    if (rx_buf[(rx_head + t) % sizeof rx_buf] == termios.c_cc[VEOL] ||
        rx_buf[(rx_head + t) % sizeof rx_buf] == termios.c_cc[VEOL2]) {
      return t+1;
    }
  }
  
  // Shouldn't get here
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
    rx_free_head = (rx_free_head + 1) % sizeof rx_buf;
    rx_free_sz--;
  }

  taskwakeupall(&rx_data_rendez);
  
  return 0;
}


/*
 *
 */
int rem_from_rx_queue(void)
{
  if (rx_sz > 0) {
    rx_sz --;
    rx_free_head = (rx_free_head - 1) % sizeof rx_buf;
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
  tx_free_head = (tx_free_head + 1) % sizeof tx_buf;
  tx_free_sz--;
  tx_sz++;

  taskwakeupall(&tx_rendez);

  return 0;
}


/*
 *
 */
int rem_from_tx_queue(void)
{
  if (tx_sz > 0) {
    tx_sz --;
    tx_free_head = (tx_free_head - 1) % sizeof tx_buf;
    tx_free_sz++;    
  }
  
  return 0;
}


/*
 *
 */
void sigterm_handler(int signo)
{
  shutdown = true;
}

