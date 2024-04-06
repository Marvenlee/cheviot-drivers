# cheviot-drivers

A repository containing device drivers for CheviotOS on the Raspberry Pi.

This repository is brought in to the cheviot-project base repository as a
git submodule.


# Overview of Device Drivers

Device drivers, along with the related file system handlers are normal
user-mode processes. The Virtual File System Switch within the kernel converts
standard system calls such as open(), read(), write() and lseek() into messages
that are passed to device driver via the kernel's inter-process communication mechanisms.

Most drivers have variants for the Pi 1 and Pi 4. Eventually these will be handled by
a devicetree file. The Pi 1 code is untested after switching to a Pi 4 for development.

## Aux

Device driver for the Raspberry Pi's Aux UART. This is used to provide terminal
access via GPIO14 (TX, pin 8) and GPIO 16 (Rx, pin 10) on the Raspberry Pi's GPIO header.

## sdcard

This is a block device driver for accessing the Raspberry Pi's SD-Card interface.
This is derived from code created by John Cronin, see the copyrights in the source
files.

## null

The */dev/null* device driver. This shows what a minimal character device driver looks like.

## random

A character device for accessing the Raspberry Pi's True Random Number Generator (TRNG) hardware.

## serial

Work-in-progress to implement a driver for the Pi's serial port as an alternative to the Aux
driver.

# Licenses and Acknowledgements

The SD-Card driver makes use of ocde created by John Cronin, see the copyrights within the sources.



