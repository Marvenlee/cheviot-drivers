#include "globals.h"
#include "sdcard.h"
#include <sys/syscalls.h>
#include <sys/profiling.h>
#include <sys/types.h>
#include <fdthelper.h>

struct fdthelper helper;
void *emmc_vpu_base;
void *emmc_phys_base;
size_t emmc_reg_size;
void *mbox_vpu_base;
void *mbox_phys_base;
size_t mbox_reg_size;

uintptr_t emmc_base;             // virtual address where the emmc registers are mapped
uintptr_t mbox_base;             // virtual address where the mailbox registers are mapped (needed?)

struct block_device actual_device;
struct block_device *bdev;

uint8_t bootsector[512];        // buffer to read bootsector into
uint8_t *buf;
uint8_t *buf_phys;

bool buf_valid = false;
off64_t buf_start_block_no = 0;

int kq;                         // kqueue handle

struct Config config;

int nunits;                     // number of discovered units and partitions.
struct bdev_unit unit[5];      // Maximum 5 units, e.g. sda, sda1, sda2, sda3 and sda5

// sendmsg

char req_buf[ARG_MAX];
char resp_buf[ARG_MAX];

// Profiling
bool profiling = true;
struct profiling_samples profiling_reads;

struct profiling_samples profiling_writes;
struct profiling_samples profiling_kevents;
int profiling_read_counter;
int profiling_write_counter;
struct timespec profile_read_start_ts;
struct timespec profile_read_end_ts;
struct timespec profile_write_start_ts;
struct timespec profile_write_end_ts;

bool shutdown;




