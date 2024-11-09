
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
#include "emmc_internal.h"
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
void cmd_debug(struct bdev_unit *unit, msgid_t msgid, iorequest_t *req)
{
  char *cmd = strtok(NULL, " ");
  
  if (cmd == NULL) {
    strlcpy(resp_buf, "ERROR: no subcommand\n", sizeof resp_buf);
    return;
  }
  
  if (strcmp("registers", cmd) == 0) {
    cmd_debug_registers(unit, msgid, req);
  } else {
    strlcpy(resp_buf, "ERROR: unknown subcommand\n", sizeof resp_buf);
  } 
}


/*
 *
 */
void cmd_debug_registers(struct bdev_unit *unit, msgid_t msgid, iorequest_t *req)
{
  char tmp[64];
  uint32_t val;
  
  strlcpy(resp_buf, "OK: registers\n", sizeof resp_buf);
  
  val = mmio_read(emmc_base + EMMC_ARG2);
  snprintf(tmp, sizeof tmp, "EMMC_ARG2           : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_BLKSIZECNT);
  snprintf(tmp, sizeof tmp, "EMMC_BLKSIZECNT     : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_ARG1);
  snprintf(tmp, sizeof tmp, "EMMC_ARG1           : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_CMDTM);
  snprintf(tmp, sizeof tmp, "EMMC_CMDTM          : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_RESP0);
  snprintf(tmp, sizeof tmp, "EMMC_RESP0          : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_RESP1);
  snprintf(tmp, sizeof tmp, "EMMC_RESP1          : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_RESP2);
  snprintf(tmp, sizeof tmp, "EMMC_RESP2          : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_RESP3);
  snprintf(tmp, sizeof tmp, "EMMC_RESP3          : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_DATA);
  snprintf(tmp, sizeof tmp, "EMMC_DATA           : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_STATUS);
  snprintf(tmp, sizeof tmp, "EMMC_STATUS         : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_CONTROL0);
  snprintf(tmp, sizeof tmp, "EMMC_CONTROL0       : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_CONTROL1);
  snprintf(tmp, sizeof tmp, "EMMC_CONTROL1       : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_INTERRUPT);
  snprintf(tmp, sizeof tmp, "EMMC_INTERRUPT      : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_IRPT_MASK);
  snprintf(tmp, sizeof tmp, "EMMC_IRPT_MASK      : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_IRPT_EN);
  snprintf(tmp, sizeof tmp, "EMMC_IRPT_EN        : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_CONTROL2);
  snprintf(tmp, sizeof tmp, "EMMC_CONTROL2       : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_CAPABILITIES_0);
  snprintf(tmp, sizeof tmp, "EMMC_CAPABILITIES_0 : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_CAPABILITIES_1);
  snprintf(tmp, sizeof tmp, "EMMC_CAPABILITIES_1 : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_FORCE_IRPT);
  snprintf(tmp, sizeof tmp, "EMMC_FORCE_IRPT     : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_BOOT_TIMEOUT);
  snprintf(tmp, sizeof tmp, "EMMC_BOOT_TIMEOUT   : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_DBG_SEL);
  snprintf(tmp, sizeof tmp, "EMMC_DBG_SEL        : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_EXRDFIFO_CFG);
  snprintf(tmp, sizeof tmp, "EMMC_EXRDFIFO_CFG   : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_EXRDFIFO_EN);
  snprintf(tmp, sizeof tmp, "EMMC_EXRDFIFO_EN    : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_TUNE_STEP);
  snprintf(tmp, sizeof tmp, "EMMC_TUNE_STEP      : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_TUNE_STEPS_STD);
  snprintf(tmp, sizeof tmp, "EMMC_TUNE_STEPS_STD : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_TUNE_STEPS_DDR);
  snprintf(tmp, sizeof tmp, "EMMC_TUNE_STEPS_DDR : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_SPI_INT_SPT);
  snprintf(tmp, sizeof tmp, "EMMC_SPI_INT_SPT    : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);

  val = mmio_read(emmc_base + EMMC_SLOTISR_VER);
  snprintf(tmp, sizeof tmp, "EMMC_SLOTISR_VER    : %08lx\n", val);
  strlcat(resp_buf, tmp, sizeof resp_buf);
}

