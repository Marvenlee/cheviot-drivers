driversdir = $(prefix)/system/drivers
drivers_PROGRAMS = mailbox

mailbox_SOURCES = \
  globals.c \
  init.c \
  mailbox.c \
  mailbox_clock.c \
  mailbox_power.c \
  main.c 

mailbox_LDADD = -lrpihal -lfdthelper -lfdt
  
AM_CFLAGS = -O2 -std=c99 -g0
AM_CPPFLAGS = -I$(srcdir) 
AM_CCASFLAGS = -r -I$(srcdir) 

