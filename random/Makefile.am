driversdir = $(prefix)/system/drivers
drivers_PROGRAMS = random


random_SOURCES = \
  globals.c \
  init.c \
  main.c \
  trng_hw.c
  
random_LDADD = -lrpihal -lfdthelper -lfdt 
AM_CFLAGS = -O0 -std=c99 -g2

AM_CPPFLAGS = -I$(srcdir) -I$(srcdir)/boards/raspberry_pi_4 -DBOARD_RASPBERRY_PI_4
AM_CCASFLAGS = -r -I$(srcdir) -I$(srcdir)/boards/raspberry_pi_4 -DBOARD_RASPBERRY_PI_4




