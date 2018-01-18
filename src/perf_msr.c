/*
 * perf_msr.c: Intel cpu aperf/mperf msr counter interface
 *
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "perf_msr.h"

uint64_t read_msr(int fd, uint32_t reg)
{
	uint64_t data;
	if (pread(fd, &data, sizeof(data), reg) != sizeof(data)) {
		dbg_print("rdmsr: pread reg: %0x\n", reg);
		return -1;
	}
	return data;
}

int dev_msr_supported = -1;
int cpu_khz;
int initialize_dev_msr(int c)
{
	int fd;
	char msr_file[128];

	sprintf(msr_file, "/dev/cpu/%d/msr", c);
	fd = open(msr_file, O_RDONLY);
	if (fd < 0) {
		perror("rdmsr: open");
		return -1;
	}

	return fd;
}
int initialize_cpu_khz(int fd)
{
	uint64_t msr_val;

	msr_val = read_msr(fd, (uint32_t)MSR_PLATFORM_INFO);
	if (msr_val != -1) {
		/* most x86 have cpu_clk = ratio * freq_multiplier */
		cpu_khz = ((msr_val >> 8) & 0xffUll) * 1000;
	} else {
		printf("***cant read MSR_PLATFORM_INFO***\n");
		return -1;
	}

	return 0;
}

/* routine to evaluate & store a global msr value's diff */
static uint64_t last_tsc;
#define VAR(a, b) (a##b)
#define generate_msr_diff(scope)					       \
unsigned int get_diff_##scope(uint64_t cur_value)			       \
{									       \
	int64_t diff;							       \
	diff = (VAR(last_, scope) == 0) ? 0 : (cur_value - VAR(last_, scope)); \
	VAR(last_, scope) = cur_value;					       \
	if (diff < 0) {							       \
		return 1;						       \
	}								       \
	return (unsigned int)diff;					       \
}
generate_msr_diff(tsc);

static uint64_t last_aperf[MAX_CPU_REPORTS];
static uint64_t last_mperf[MAX_CPU_REPORTS];
static uint64_t last_pperf[MAX_CPU_REPORTS];

/* routine to evaluate & store a per-cpu msr value's diff */
#define VARI(a, b, i) a##b[i]
#define cpu_generate_msr_diff(scope)					       \
unsigned int cpu_get_diff_##scope(uint64_t cur_value, int instance)	       \
{									       \
	int64_t diff;							       \
	diff = (VARI(last_, scope, instance) == 0) ?			       \
				0 : (cur_value - VARI(last_, scope, instance));\
	VARI(last_, scope, instance) = cur_value;			       \
	if (diff < 0) {							       \
		return 1;						       \
	}								       \
	return (unsigned int)diff;					       \
}

cpu_generate_msr_diff(aperf);
cpu_generate_msr_diff(mperf);
cpu_generate_msr_diff(pperf);

