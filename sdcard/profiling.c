
#define LOG_LEVEL_INFO

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
#include <sys/profiling.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>


/*
 *
 */
void cmd_profiling(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
{
  char *cmd = strtok(NULL, " ");
  
  if (cmd == NULL) {
    strlcpy(resp_buf, "ERROR: no subcommand\n", sizeof resp_buf);
    return;
  }
  
  if (strcmp("stats", cmd) == 0) {
    cmd_profiling_stats(unit, msgid, req);
  } else if (strcmp("enable", cmd) == 0) {
    cmd_profiling_enable(unit, msgid, req);
  } else if (strcmp("disable", cmd) == 0) {
    cmd_profiling_disable(unit, msgid, req);
  } else if (strcmp("reset", cmd) == 0) {
    cmd_profiling_reset(unit, msgid, req);
  } else {
    strlcpy(resp_buf, "ERROR: unknown subcommand\n", sizeof resp_buf);
  } 
}


/*
 *
 */
void cmd_profiling_stats(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
{
  char tmp[32];
    
  snprintf(resp_buf, sizeof resp_buf, "OK: stats\n"
            "reads: %d\n"
            "writes: %d\n"
            "read time  avg:%d, min: %d, max: %d (us)\n"
            "write time avg:%d, min: %d, max: %d (us)\n",
            profiling_read_counter,
            profiling_write_counter,
            profiling_reads.avg,
            profiling_reads.min,
            profiling_reads.max,
            profiling_writes.avg,
            profiling_writes.min,           
            profiling_writes.max
            );            
}


/*
 *
 */
void cmd_profiling_enable(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
{
  profiling = true;
  strlcpy(resp_buf, "OK: enabled\n", sizeof resp_buf);
}


/*
 *
 */
void cmd_profiling_disable(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
{
  profiling = false;
  strlcpy(resp_buf, "OK: disabled\n", sizeof resp_buf);
}


/*
 *
 */
void cmd_profiling_reset(struct bdev_unit *unit, msgid_t msgid, struct fsreq *req)
{
  profiling_read_counter = 0;
  profiling_write_counter = 0;

  memset(&profiling_reads, 0, sizeof profiling_reads);
  memset(&profiling_writes, 0, sizeof profiling_writes);

  strlcpy(resp_buf, "OK: reset\n", sizeof resp_buf);
}


