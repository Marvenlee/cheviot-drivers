
sbin_PROGRAMS = gpio

gpio_SOURCES = \
  globals.c \
  init.c \
  main.c

gpio_LDADD = -lrpihal -lfdthelper -lfdt
  
AM_CFLAGS = -O2 -std=c99 -g0
AM_CPPFLAGS = -I$(srcdir) 
AM_CCASFLAGS = -r -I$(srcdir) 

