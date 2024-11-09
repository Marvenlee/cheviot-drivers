driversdir = $(prefix)/system/drivers
drivers_PROGRAMS = aux

aux_SOURCES = \
  globals.c \
  init.c \
  main.c \
  aux_uart_hw.c
  
aux_LDADD = -ltask -lrpihal -lfdthelper -lfdt
AM_CFLAGS = -O0 -std=c99 -g2 -march=armv7ve

AM_CPPFLAGS = -I$(srcdir)
AM_CCASFLAGS = -r -I$(srcdir)


