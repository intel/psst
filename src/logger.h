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

#ifndef _LOGGER_H_
#define _LOGGER_H_
#include <stdint.h>
#include <time.h>

#ifdef DEBUG
#define dbg_print(fmt...)  printf(fmt)
#else
#define dbg_print(fmt...)  ((void)0)
#endif

#define UNUSED(expr) do { (void)(expr); } while (0)

#define MSEC_TO_SEC(x) (x/1000)
#define REMAINING_MS_TO_NS(x) ((x % 1000) * 1000000)

#define SMP_MAX_FREQ_ENABLED 1
#define MAX_CPU_REPORTS (4 * SMP_MAX_FREQ_ENABLED)

typedef enum log_col {TIME_STAMP_MS,
		      FREQ_REALIZED,
		      MAX_FREQ_CPU,
		      LOAD_REQUEST,
		      LOAD_REALIZED,
		      SCALE_FACTOR,
		      PKG0_POWER_RAPL,
		      PKG1_POWER_RAPL,
		      PKG2_POWER_RAPL,
		      PKG3_POWER_RAPL,
		      PKG_POWER_LIMIT,
		      PP0_POWER_RAPL,
		      PP1_POWER_RAPL,
		      DRAM_POWER_RAPL,
		      CPU_DTS,
		      SOC_DTS,
		      MAX_COL_NUM,} log_col_t;

enum col_processing { NO_FD, NORMAL_FD, MSR_FD };

struct log_col_desc {
	int report_enabled;
	char header_name[32];
	char unit[32];
	char fmt[32];
	float unit_multiplier;
	enum col_processing fd_type;
	int poll_fd;
	float value;
};

extern int rapl_pp0_supported;
extern int need_maxed_cpu;
extern int plog_poll_sec, plog_poll_nsec, duration_sec, duration_nsec;
extern struct config configpv;

extern void do_logging(float *);
extern void initialize_logger(void);
extern void initialize_log_clock(void);
extern void page_write_disk(void *);
extern void trigger_disk_io(void);
extern uint64_t diff_ns(struct timespec *, struct timespec *);
extern int update_perf_diffs(unsigned int *a, unsigned int *m, unsigned int *p,
			     unsigned int *t, int max);
#endif
