/*
 * Copyright (c) 2017, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Author: Noor ul Mubeen <noor.u.mubeen@intel.com>
 */

#ifndef _PERF_MSR_
#define _PERF_MSR_
#include <unistd.h>
#include <stdint.h>
#include "logger.h"

#define MSR_IA32_MPERF		0xe7
#define MSR_IA32_APERF		0xe8
#define MSR_IA32_PPERF		0x64e
#define MSR_IA32_TSC		0x10
#define MSR_PLATFORM_INFO	0xce
#define MSR_PERF_STATUS		0x198

extern int cpu_khz;
extern int dev_msr_fd[MAX_CPU_REPORTS];
extern int dev_msr_supported;
extern uint64_t read_msr(int, uint32_t);
extern int initialize_dev_msr(int c);
extern int initialize_cpu_khz(int fd);
extern unsigned int cpu_get_diff_aperf(uint64_t a, int i);
extern unsigned int cpu_get_diff_mperf(uint64_t m, int i);
extern unsigned int cpu_get_diff_pperf(uint64_t p, int i);
extern unsigned int get_diff_tsc(uint64_t t);
#endif
