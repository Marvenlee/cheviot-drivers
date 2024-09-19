#ifndef GLOBALS_H
#define GLOBALS_H

#include "sdcard.h"
#include <sys/syscalls.h>
#include <sys/types.h>
#include <fdthelper.h>


extern struct fdthelper helper;
extern void *emmc_vpu_base;
extern void *emmc_phys_base;
extern size_t emmc_reg_size;
extern void *mbox_vpu_base;
extern void *mbox_phys_base;
extern size_t mbox_reg_size;

extern uintptr_t mbox_base;
extern uintptr_t emmc_base;

extern struct block_device actual_device;
extern struct block_device *bdev;

extern uint8_t bootsector[512];
extern uint8_t *buf;
extern uint8_t *buf_phys;

extern bool buf_valid;
extern off64_t buf_start_block_no;

extern int kq;

extern struct Config config;

extern int nunits;
extern struct bdev_unit unit[5];


#endif
