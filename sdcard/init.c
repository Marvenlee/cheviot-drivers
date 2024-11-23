#define LOG_LEVEL_WARN

#include "sdcard.h"
#include "globals.h"
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
#include <machine/cheviot_hal.h>
#include <sys/rpi_mailbox.h>
#include <sys/rpi_gpio.h>
#include <fdthelper.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <machine/param.h>
#include <libfdt.h>
#include <sys/sched.h>

/* @brief   Initialize the sdcard device driver
 *
 * @param   argc, number of command line arguments
 * @param   argv, array of command line arguments
 */
void init(int argc, char *argv[])
{
  int sc;

  sc = process_args(argc, argv);
  if (sc != 0) {
    log_error("process_args failed, sc = %d", sc);
    exit(-1);
  }

	sc = enable_power_and_clocks();
	if (sc != 0) {
		log_error("enable_power_and_clocks failed, sc = %d", sc);
		exit(-1);
	}

  sc = map_io_registers();
  if (sc != 0) {
    log_error("map_io_registers failed, sc = %d", sc);
    exit(-1);
  }
  	
  bdev = NULL;

  sc = sd_card_init(&bdev);
  if (sc < 0) {
    log_error("sd_card_init failed, sc = %d", sc);
    exit(-1);
  }

  kq = kqueue();
  if (kq < 0) {
    log_error("failed to create kqueue");
    exit(-1);
  }
  
  if (create_device_mount() != 0) {
    log_error("failed to make base block device mount");
    exit(-1);
  }

  if (create_partition_mounts() != 0) {
    log_error("failed to make parition block device mounts");
    exit(-1);
  }

  buf = mmap((void *)MMAP_START_BASE, BUF_SZ, PROT_READ | PROT_WRITE, 0, -1, 0);

  if (buf == MAP_FAILED) {
    log_error("failed to create 4k buffer");
    exit(-1);
  }
  
  buf_phys = virtualtophysaddr(buf);
  
  _swi_setschedparams(SCHED_RR, SDCARD_TASK_PRIORITY);
}


/*
 * -u default user-id
 * -g default gid
 * -m default mod bits
 * -D debug level ?
 * mount path (default arg)
 */
int process_args(int argc, char *argv[]) 
{
  int c;

	config.uid = 0;
	config.gid = 0;
	config.dev = -1;
	config.mode = 0600;

  if (argc <= 1) {
    log_error("process_args argc <=1, %d", argc);
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
      config.dev = strtoul(optarg, NULL, 0);
      break;

    }
  }

  if (optind >= argc) {
    log_error("process_args failed, optind = %d, argc = %d", optind, argc);
    return -1;
  }

  strncpy(config.pathname, argv[optind], sizeof config.pathname);
  return 0;
}


/*
 *
 */
int enable_power_and_clocks(void)
{
#if 0
	if (init_mailbox() != 0) {
	  return -EIO;
	}
	
	rpi_mailbox_set_power_state(MBOX_DEVICE_ID_SDCARD, MBOX_POWER_STATE_ON);
	rpi_mailbox_set_clock_state(MBOX_CLOCK_ID_EMMC2, MBOX_CLOCK_STATE_ON);  
#endif

  return 0;
}


/*
 *
 * 
 */
int map_io_registers(void)
{
  if (get_fdt_device_info() != 0) {
    return -EIO;
  }

  emmc_base = (uintptr_t)map_phys_mem(emmc_phys_base, emmc_reg_size,
                           PROT_READ | PROT_WRITE, CACHE_UNCACHEABLE, 
                           EMMC_REGS_START_VADDR);



  if (emmc_base == (uintptr_t)NULL) {
    return -ENOMEM;
  }  

  return 0;
}


/*
 *
 */
int get_fdt_device_info(void)
{
  int offset;
  int len;
  
  if (load_fdt("/lib/firmware/dt/rpi4.dtb", &helper) != 0) {
    log_error("cannot open device tree file rpi4.dtb");
    return -EIO;
  }

  if (fdt_check_header(helper.fdt) != 0) {
    log_error("fdt check failed");
    unload_fdt(&helper);
    return -EIO;
  }

  if ((offset = fdt_path_offset(helper.fdt, "/soc/emmc2")) < 0) {
    log_error("cannot find emmc2 in device tree, rc:%d", offset);
    unload_fdt(&helper);
    return -EIO;
  }

  if (fdthelper_check_compat(helper.fdt, offset, "brcm,bcm2711-emmc2") != 0) {
    log_error("not compatible");
    unload_fdt(&helper);
    return -EIO;
  }

  if (fdthelper_get_reg(helper.fdt, offset, &emmc_vpu_base, &emmc_reg_size) != 0) {
    log_error("cannot get register");
    unload_fdt(&helper);
    return -EIO;
  } 


  if (fdthelper_translate_address(helper.fdt, emmc_vpu_base, &emmc_phys_base) != 0) {
    log_error("cannot translate address");
    unload_fdt(&helper);
    return -EIO;        
  }

  unload_fdt(&helper);
  return 0;  
}


/* @brief   Create a block special device mount covering the whole disk
 *
 * @returns 0 on success, -1 on failure
 *
 * FIXME: For now set it to 16GB with 512 byte blocks 
 */
int create_device_mount(void)
{
  struct stat mnt_stat;
  struct kevent ev;
  
  if (snprintf(unit[0].path, sizeof unit[0].path, "%s", config.pathname) >= sizeof unit[0].path) {
    return -1;
  }

  unit[0].start = 0;
  unit[0].size = 33554432ull * 512;  // Where has 3354432 came from ?  4GB ?
  unit[0].blocks = 33554432ull;
  
  mnt_stat.st_dev = config.dev;
  mnt_stat.st_ino = 0;
  mnt_stat.st_mode = _IFBLK | (config.mode & 0777);
  mnt_stat.st_uid = config.uid;
  mnt_stat.st_gid = config.gid;
  mnt_stat.st_blksize = 512;
  mnt_stat.st_size = 0xFFFFFFFF;     // FIXME: Needs stat64 for drives 4GB and over
  mnt_stat.st_blocks = 33554432ull;

  if (mknod2(unit[0].path, 0, &mnt_stat) != 0) {
    log_error("failed to make node %s\n", unit[0].path);
    return -1;
  }
  
  unit[0].portid = createmsgport(unit[0].path, 0, &mnt_stat);

  if (unit[0].portid < 0) {
    log_error("mounting device: %s failed\n", unit[0].path);
    return -1;
  }

  log_info("mount sdcard: %s", unit[0].path);
  
  EV_SET(&ev, unit[0].portid, EVFILT_MSGPORT, EV_ADD | EV_ENABLE, 0, 0 ,&unit[0]); 
  kevent(kq, &ev, 1,  NULL, 0, NULL);
  nunits = 1;
  return 0;
}


/* @brief     Create block special device for each partition on the disk
 *
 * @return    0 if one or more successful mounts, -1 on failure
 */
int create_partition_mounts(void)
{
  int sc;
  struct kevent ev;
  struct mbr_partition_table_entry mbr_partition_table[4];  
  struct stat mnt_stat;
   
  sc = sd_read(bdev, bootsector, 512, 0);
  
  if (sc < 0) {
    log_error("failed to read bootsector");
    return -1;
  }
  
  memcpy(mbr_partition_table, bootsector + 446, sizeof mbr_partition_table);

  nunits = 1;   // First unit is used by whole block device

  for (int t=0; t<4; t++) {
    if (mbr_partition_table[t].type != 0) {

      if (snprintf(unit[nunits].path, sizeof unit[nunits].path, "%s%d", config.pathname, nunits) >= sizeof unit[nunits].path) {
        return -1;
      }

      unit[nunits].start = mbr_partition_table[t].start_lba;
      unit[nunits].size = mbr_partition_table[t].size * 512;
      unit[nunits].blocks = mbr_partition_table[t].size;
      
      mnt_stat.st_dev = config.dev + t;
      mnt_stat.st_ino = 0;
      mnt_stat.st_mode = _IFBLK | (config.mode & 0777);
      mnt_stat.st_uid = config.uid;
      mnt_stat.st_gid = config.gid;
      mnt_stat.st_blksize = 512;
      mnt_stat.st_size = unit[nunits].size;  
      mnt_stat.st_blocks = unit[nunits].blocks;

      if (mknod2(unit[nunits].path, 0, &mnt_stat) != 0) {
        log_error("failed to make node %s\n", unit[nunits].path);
        return -1;
      }

      log_info("mount partition: %s", unit[nunits].path);
        
      unit[nunits].portid = createmsgport(unit[nunits].path, 0, &mnt_stat);
      
      if (unit[nunits].portid >= 0) {
        EV_SET(&ev, unit[nunits].portid, EVFILT_MSGPORT, EV_ADD | EV_ENABLE, 0, 0 ,&unit[nunits]);
        kevent(kq, &ev, 1,  NULL, 0, NULL);                
        nunits++;
        
      } else {
        log_error("mounting %s failed\n", unit[nunits].path);
      }
    }
  }
  
  return 0;
}



