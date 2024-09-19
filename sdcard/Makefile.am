
sbin_PROGRAMS = sdcard

sdcard_SOURCES = \
  emmc.c \
  emmc_init.c \
  emmc_misc.c \
  emmc_rw.c \
  emmc_globals.c \
  globals.c \
  init.c \
  main.c \
  mmio.c \
  timer.c 

sdcard_LDADD = -lrpimailbox -lrpigpio -lrpihal -lfdthelper -lfdt

AM_CFLAGS = -O2 -std=c99 -g0
AM_CPPFLAGS = -I$(srcdir)
AM_CCASFLAGS = -r -I$(srcdir)

