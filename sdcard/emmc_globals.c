/* Copyright (C) 2013 by John Cronin <jncronin@tysos.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* Provides an interface to the EMMC controller and commands for interacting
 * with an sd card */

/* References:
 *
 * PLSS 	- SD Group Physical Layer Simplified Specification ver 3.00
 * HCSS		- SD Group Host Controller Simplified Specification ver 3.00
 *
 * Broadcom BCM2835 Peripherals Guide
 */

//#define NDEBUG
//#define EMMC_DEBUG
#define LOG_LEVEL_DEBUG

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "emmc_internal.h"


char driver_name[] = "emmc";
char device_name[] =
    "emmc0"; // We use a single device name as there is only
// one card slot in the RPi

uint32_t hci_ver = 0;
uint32_t capabilities_0 = 0;
uint32_t capabilities_1 = 0;


char *sd_versions[] = {"unknown", "1.0 and 1.01", "1.10",
                              "2.00",    "3.0x",         "4.xx"};

#ifdef EMMC_DEBUG
char *err_irpts[] = {"CMD_TIMEOUT",  "CMD_CRC",       "CMD_END_BIT",
                            "CMD_INDEX",    "DATA_TIMEOUT",  "DATA_CRC",
                            "DATA_END_BIT", "CURRENT_LIMIT", "AUTO_CMD12",
                            "ADMA",         "TUNING",        "RSVD"};
#endif


uint32_t sd_commands[] = {
    SD_CMD_INDEX(0), SD_CMD_RESERVED(1), SD_CMD_INDEX(2) | SD_RESP_R2,
    SD_CMD_INDEX(3) | SD_RESP_R6, SD_CMD_INDEX(4), SD_CMD_INDEX(5) | SD_RESP_R4,
    SD_CMD_INDEX(6) | SD_RESP_R1, SD_CMD_INDEX(7) | SD_RESP_R1b,
    SD_CMD_INDEX(8) | SD_RESP_R7, SD_CMD_INDEX(9) | SD_RESP_R2,
    SD_CMD_INDEX(10) | SD_RESP_R2, SD_CMD_INDEX(11) | SD_RESP_R1,
    SD_CMD_INDEX(12) | SD_RESP_R1b | SD_CMD_TYPE_ABORT,
    SD_CMD_INDEX(13) | SD_RESP_R1, SD_CMD_RESERVED(14), SD_CMD_INDEX(15),
    SD_CMD_INDEX(16) | SD_RESP_R1, SD_CMD_INDEX(17) | SD_RESP_R1 | SD_DATA_READ,
    SD_CMD_INDEX(18) | SD_RESP_R1 | SD_DATA_READ | SD_CMD_MULTI_BLOCK |
        SD_CMD_BLKCNT_EN,
    SD_CMD_INDEX(19) | SD_RESP_R1 | SD_DATA_READ,
    SD_CMD_INDEX(20) | SD_RESP_R1b, SD_CMD_RESERVED(21), SD_CMD_RESERVED(22),
    SD_CMD_INDEX(23) | SD_RESP_R1,
    SD_CMD_INDEX(24) | SD_RESP_R1 | SD_DATA_WRITE,
    SD_CMD_INDEX(25) | SD_RESP_R1 | SD_DATA_WRITE | SD_CMD_MULTI_BLOCK |
        SD_CMD_BLKCNT_EN,
    SD_CMD_RESERVED(26), SD_CMD_INDEX(27) | SD_RESP_R1 | SD_DATA_WRITE,
    SD_CMD_INDEX(28) | SD_RESP_R1b, SD_CMD_INDEX(29) | SD_RESP_R1b,
    SD_CMD_INDEX(30) | SD_RESP_R1 | SD_DATA_READ, SD_CMD_RESERVED(31),
    SD_CMD_INDEX(32) | SD_RESP_R1, SD_CMD_INDEX(33) | SD_RESP_R1,
    SD_CMD_RESERVED(34), SD_CMD_RESERVED(35), SD_CMD_RESERVED(36),
    SD_CMD_RESERVED(37), SD_CMD_INDEX(38) | SD_RESP_R1b, SD_CMD_RESERVED(39),
    SD_CMD_RESERVED(40), SD_CMD_RESERVED(41), SD_CMD_RESERVED(42) | SD_RESP_R1,
    SD_CMD_RESERVED(43), SD_CMD_RESERVED(44), SD_CMD_RESERVED(45),
    SD_CMD_RESERVED(46), SD_CMD_RESERVED(47), SD_CMD_RESERVED(48),
    SD_CMD_RESERVED(49), SD_CMD_RESERVED(50), SD_CMD_RESERVED(51),
    SD_CMD_RESERVED(52), SD_CMD_RESERVED(53), SD_CMD_RESERVED(54),
    SD_CMD_INDEX(55) | SD_RESP_R1,
    SD_CMD_INDEX(56) | SD_RESP_R1 | SD_CMD_ISDATA, SD_CMD_RESERVED(57),
    SD_CMD_RESERVED(58), SD_CMD_RESERVED(59), SD_CMD_RESERVED(60),
    SD_CMD_RESERVED(61), SD_CMD_RESERVED(62), SD_CMD_RESERVED(63)};

uint32_t sd_acommands[] = {SD_CMD_RESERVED(0),
                                  SD_CMD_RESERVED(1),
                                  SD_CMD_RESERVED(2),
                                  SD_CMD_RESERVED(3),
                                  SD_CMD_RESERVED(4),
                                  SD_CMD_RESERVED(5),
                                  SD_CMD_INDEX(6) | SD_RESP_R1,
                                  SD_CMD_RESERVED(7),
                                  SD_CMD_RESERVED(8),
                                  SD_CMD_RESERVED(9),
                                  SD_CMD_RESERVED(10),
                                  SD_CMD_RESERVED(11),
                                  SD_CMD_RESERVED(12),
                                  SD_CMD_INDEX(13) | SD_RESP_R1,
                                  SD_CMD_RESERVED(14),
                                  SD_CMD_RESERVED(15),
                                  SD_CMD_RESERVED(16),
                                  SD_CMD_RESERVED(17),
                                  SD_CMD_RESERVED(18),
                                  SD_CMD_RESERVED(19),
                                  SD_CMD_RESERVED(20),
                                  SD_CMD_RESERVED(21),
                                  SD_CMD_INDEX(22) | SD_RESP_R1 | SD_DATA_READ,
                                  SD_CMD_INDEX(23) | SD_RESP_R1,
                                  SD_CMD_RESERVED(24),
                                  SD_CMD_RESERVED(25),
                                  SD_CMD_RESERVED(26),
                                  SD_CMD_RESERVED(27),
                                  SD_CMD_RESERVED(28),
                                  SD_CMD_RESERVED(29),
                                  SD_CMD_RESERVED(30),
                                  SD_CMD_RESERVED(31),
                                  SD_CMD_RESERVED(32),
                                  SD_CMD_RESERVED(33),
                                  SD_CMD_RESERVED(34),
                                  SD_CMD_RESERVED(35),
                                  SD_CMD_RESERVED(36),
                                  SD_CMD_RESERVED(37),
                                  SD_CMD_RESERVED(38),
                                  SD_CMD_RESERVED(39),
                                  SD_CMD_RESERVED(40),
                                  SD_CMD_INDEX(41) | SD_RESP_R3,
                                  SD_CMD_INDEX(42) | SD_RESP_R1,
                                  SD_CMD_RESERVED(43),
                                  SD_CMD_RESERVED(44),
                                  SD_CMD_RESERVED(45),
                                  SD_CMD_RESERVED(46),
                                  SD_CMD_RESERVED(47),
                                  SD_CMD_RESERVED(48),
                                  SD_CMD_RESERVED(49),
                                  SD_CMD_RESERVED(50),
                                  SD_CMD_INDEX(51) | SD_RESP_R1 | SD_DATA_READ,
                                  SD_CMD_RESERVED(52),
                                  SD_CMD_RESERVED(53),
                                  SD_CMD_RESERVED(54),
                                  SD_CMD_RESERVED(55),
                                  SD_CMD_RESERVED(56),
                                  SD_CMD_RESERVED(57),
                                  SD_CMD_RESERVED(58),
                                  SD_CMD_RESERVED(59),
                                  SD_CMD_RESERVED(60),
                                  SD_CMD_RESERVED(61),
                                  SD_CMD_RESERVED(62),
                                  SD_CMD_RESERVED(63)};

size_t sd_commands_sz = sizeof sd_commands;
size_t sd_acommands_sz = sizeof sd_acommands;

