driversdir = $(prefix)/system/drivers
drivers_PROGRAMS = sdcard

sdcard_SOURCES = \
  debug.c \
  emmc.c \
  emmc_init.c \
  emmc_misc.c \
  emmc_rw.c \
  emmc_globals.c \
  globals.c \
  init.c \
  main.c \
  mmio.c \
  profiling.c \
  timer.c 

sdcard_LDADD = -lprofiling -lrpimailbox -lrpigpio -lrpihal -lfdthelper -lfdt

AM_CFLAGS = -O2 -std=c99 -g0 -Wall
AM_CPPFLAGS = -I$(srcdir)
AM_CCASFLAGS = -r -I$(srcdir)

