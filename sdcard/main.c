
#define LOG_LEVEL_WARN

#include "sys/debug.h"
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
#include <sys/syscalls.h>
#include <poll.h>
#include <unistd.h>
#include <sys/event.h>
#include "sdcard.h"
#include "globals.h"
#include <sys/rpi_mailbox.h>
#include <sys/rpi_gpio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/profiling.h>


/* @brief   The SDCard block device driver
 *
 * @param   argc, argument count passed on command line
 * @param   argv, arguments passed on command line
 * @return  0 on success, non-zero on failure
 *
 * The SDCard driver mounts the whole block device and also
 * individual detected partitions as separate mount points.
 */
int main(int argc, char *argv[])
{
  struct fsreq req;
  int sc;
  struct kevent ev;
  int portid;
  msgid_t msgid;
  struct bdev_unit *unit;
   
  init(argc, argv);  

  struct sigaction sact;
  sact.sa_handler = &sigterm_handler;
  sigemptyset(&sact.sa_mask);
  sact.sa_flags = 0;
  
  if (sigaction(SIGTERM, &sact, NULL) != 0) {
    exit(-1);
  }

  while (!shutdown) {
    kevent(kq, NULL, 0, &ev, 1, NULL);
		    
    if (ev.filter == EVFILT_MSGPORT) {
      portid = ev.ident;
      unit = ev.udata;

      while ((sc = getmsg(portid, &msgid, &req, sizeof req)) == sizeof req) {
        switch (req.cmd) {
          case CMD_READ:
            sdcard_read(unit, msgid, &req);
            break;

          case CMD_WRITE:
            sdcard_write(unit, msgid, &req);
            break;

          case CMD_SENDMSG:
            sdcard_sendmsg(unit, msgid, &req);
            break;

          default:
            log_warn("sdcard: unknown command: %d", req.cmd);
            replymsg(portid, msgid, -ENOTSUP, NULL, 0);
            break;
        }
      }
      
      if (sc != 0) {
        log_error("sdcard: getmsg sc=%d %s", sc, strerror(errno));
        exit(EXIT_FAILURE);
      }
    } else {
      log_warn("unhandled kevent filter:%d", ev.filter);
    }
  }

  exit(0);
}


/* @brief   Handle the CMD_READ message to read a block
 *
 * @param   unit, parameters and state of the whole device or a partition
 * @param   msgid, message id returned by receivemsg
 * @param   req, filesystem request message header
 *
 * This assumes blocks are 512 bytes in size 
 * sdcard_read() currently implements a simple BUF_SZ (4096 byte) cache.
 * Read operations read a full BUF_SZ into the buffer.
 *
 * TODO: Check for block alignment of offset and size
 * TODO: Check within range of unit 
 */
void sdcard_read(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
{
  off64_t block_no;
  off64_t offset;
  size_t remaining;
  off_t chunk_start;
  size_t chunk_size;  
  size_t left;
  size_t xfered;
  size_t block_read_sz;
  int sc;

  if (profiling) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &profile_read_start_ts);
  }

  xfered = 0;
  offset = req->args.read.offset;
  remaining = req->args.read.sz;  

  while (remaining > 0) {
    block_no = ((off64_t)unit->start + (rounddown(offset, BUF_SZ)) / 512 );
    chunk_start = offset % BUF_SZ;
    left = BUF_SZ - chunk_start;

    if (buf_valid != true || block_no != buf_start_block_no) {
      sc = sd_read(bdev, buf, BUF_SZ, block_no);
      buf_start_block_no = block_no;
      buf_valid = true;
    }

    chunk_size = (left < remaining) ? left : remaining;
    
    writemsg(unit->portid, msgid, buf+chunk_start, chunk_size, xfered);

    xfered += chunk_size;
    offset += chunk_size;
    remaining -= chunk_size;
  }

  replymsg(unit->portid, msgid, xfered, NULL, 0);

  if (profiling) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &profile_read_end_ts);
    profiling_microsecs(&profiling_reads, &profile_read_start_ts, &profile_read_end_ts);
    profiling_read_counter++;
  }
}


/* @brief   Handle the CMD_WRITE message to write a block 
 *
 * @param   unit, parameters and state of the whole device or a partition
 * @param   msgid, message id returned by receivemsg
 * @param   req, filesystem request message header
 *
 * TODO: Check for block alignment of offset and size
 * TODO: Check within range of unit
 *
 * FIXME: We break down writes into 512 byte chunks as larger writes
 * seem to cause an error where the controller is reinitialized by
 * calling sd)card_init.
 */
void sdcard_write(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
{
  off64_t block_no;
  off64_t offset;
  size_t remaining;
  off_t chunk_start;
  size_t chunk_size;  
  size_t left;
  size_t xfered;
  size_t block_write_sz;
  int sc;

  if (profiling) {
    clock_gettime(CLOCK_MONOTONIC_RAW, &profile_write_start_ts);  
  }

  xfered = 0;
  offset = req->args.write.offset;
  remaining = req->args.write.sz;  

  buf_valid = false;
  
  while (remaining > 0) {
    block_no = ((off64_t)unit->start + (offset / 512));
    chunk_start = offset % 512;
    left = BUF_SZ - chunk_start;

    chunk_size = (left < remaining) ? left : remaining;

    block_write_sz = roundup(chunk_start + chunk_size, 512);

    if (chunk_start != 0 || (chunk_size % 512) != 0) {
      for (int xfered = 0; xfered < block_write_sz; xfered += 512) {
        sc = sd_read(bdev, (uint8_t *)buf + xfered, 512, block_no + xfered/512);
      }    
    }

    readmsg(unit->portid, msgid, buf+chunk_start, chunk_size, xfered);

    for (int xfered = 0; xfered < block_write_sz; xfered += 512) {
      sc = sd_write(bdev, (uint8_t *)buf + xfered, 512, block_no + xfered/512);
    }    

    xfered += chunk_size;
    offset += chunk_size;
    remaining -= chunk_size;
  }

  replymsg(unit->portid, msgid, xfered, NULL, 0);

  if (profiling) {    
    clock_gettime(CLOCK_MONOTONIC_RAW, &profile_write_end_ts);  
    profiling_microsecs(&profiling_writes, &profile_write_start_ts, &profile_write_end_ts);
    profiling_write_counter++;    
  }
}


/*
 *
 */ 
void sdcard_sendmsg(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
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
    replymsg(unit->portid, msgid, -E2BIG, NULL, 0);
    return;
  }

  readmsg(unit->portid, msgid, req_buf, req_sz, 0);
  
  req_buf[req_sz] = '\0';  
  resp_buf[0] = '\0';

  cmd = strtok(req_buf, " ");
  
  if (!(cmd == NULL || cmd[0] == '\0')) {
    if (strcmp("help", cmd) == 0) {
      cmd_help(unit, msgid, req);
    } else if (strcmp("profiling", cmd) == 0) {
      cmd_profiling(unit, msgid, req);
    } else if (strcmp("debug", cmd) == 0) {
      cmd_debug(unit, msgid, req);
    } else {
      strlcpy(resp_buf, "ERROR: unknown command\n", sizeof resp_buf);   
    }
  } else {
    strlcpy(resp_buf, "ERROR: no command\n", sizeof resp_buf);   
  }
  
  resp_sz = strlen(resp_buf);
      
  writemsg(unit->portid, msgid, resp_buf, resp_sz, 0);
  replymsg(unit->portid, msgid, resp_sz, NULL, 0);  
}


/*
 *
 */
void cmd_help(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
{
  strlcpy (resp_buf, "OK: help\n"
                     "help              - get command list\n"
                     "profiling stats   - get statistics\n"
                     "profiling enable  - enable profiling\n" 
                     "profiling disable - diable profiling\n" 
                     "profiling reset   - reset statistics\n"
                     "debug registers   - dump registers\n",
                     sizeof resp_buf);
}


/*
 *
 */
void sigterm_handler(int signo)
{
  shutdown = true;
}

