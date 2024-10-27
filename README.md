# cheviot-drivers

A repository containing device drivers for CheviotOS on the Raspberry Pi.

This repository is brought in to the cheviot-project base repository as a
git submodule.


# Overview of Device Drivers

Device drivers, along with the related file system handlers are normal
user-mode processes. The Virtual File System Switch within the kernel converts
standard system calls such as open(), read(), write() and lseek() into messages
that are passed to device driver via the kernel's inter-process communication mechanisms.

The gpio, mailbox and sysinfo drivers use the sendmsg() system call to perform custom
RPC commands.  For the gpio and mailbox drivers, the message format is binary.  For the
sysinfo driver, the message format is plain text.

We have started to use device tree files for acquiring configuration.

## Aux

Device driver for the Raspberry Pi's Aux UART. This is used to provide terminal
access via GPIO14 (TX, pin 8) and GPIO 16 (Rx, pin 10) on the Raspberry Pi's GPIO header.

## gpio

Driver to allow toggling of GPIOs through sendmsg commands.  The configuration of
GPIOs should be done either by the bootloader or this driver.  This will be moved to
an privileged ring when we move to implementing multiple protection rings.

## mailbox

Driver to allow sending commands to the Raspberry Pi's mailbox peripheral. This is
work in progress and needs additional commands implemented. This will be moved to an
privileged ring when we move to implementing multiple protection rings.

## null

The */dev/null* device driver. This shows what a minimal character device driver looks like.

## random

A character device for accessing the Raspberry Pi's True Random Number Generator (TRNG) hardware.

## sdcard

This is a block device driver for accessing the Raspberry Pi's SD-Card interface.
This is derived from code created by John Cronin, see the copyrights in the source
files.

## serial

Work-in-progress to implement a driver for the Pi's serial port as an alternative to the Aux
driver.

## sysinfo

Work-in-progress driver that will eventually have privileges to inspect kernel structures
for debugging purposes and report various system metrics, memory, processes, threads counts.


# Licenses and Acknowledgements

The SD-Card driver makes use of ocde created by John Cronin, see the copyrights within the sources.



