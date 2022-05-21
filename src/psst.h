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

#ifndef _PSST_H_
#define _PSST_H_
#include <stdint.h>

#define MSEC_PER_SEC (1000)
#define USEC_PER_SEC (1000000)
#define NSEC_PER_SEC (1000000000)
/*
 * kernel's USER_HZ is not exported to user space
 * typically platforms have kernel (HZ == USER_HZ == 1000 per sec)
 */
#define IA_DUTY_CYCLE_PER_SEC (50)
#define IA_TICK_USEC (USEC_PER_SEC / IA_DUTY_CYCLE_PER_SEC)

#define DEFAULT_TICK_USEC (IA_TICK_USEC)

#define MIN_LOAD (0.10)
#define MAX_LOAD (100)

enum power_shape_name {
	SINGLE_STEP,
	SINOSOID,
	STAIR_CASE,
	SINGLE_PULSE,
	LINEAR_RAMP,
	SAW_TOOTH,
	GROWTH_CURVE,
	DECAY_CURVE,
	NONE
};

typedef union {
	struct single_step_t {
		float v_units;
	} single_step;
	struct staircase_t {
		float y_height;
		int x_length;
	} staircase;
	struct sinosoid_t {
		float y_amplitude;
		int x_wavelength;
	} sinosoid;
	struct singlepulse_t {
		float y_height;
		int x_length;
	} single_pulse;
	struct linear_ramp_t {
		float slope_y_per_sec;
	} linear_ramp;
	struct saw_tooth_t {
		float slope_y_per_sec;
		float max_y;
	} saw_tooth;
	struct growth_curve_t {
	} growth_curve;
	struct decay_curve_t {
	} decay_curve;
} power_shape_attr_t;

typedef struct {
	enum power_shape_name psn;
	power_shape_attr_t psa;
	struct timespec last;
} ps_t;

typedef struct {
	float duty_cycle;
	int affinity_pr;
	enum power_shape_name psn;
	power_shape_attr_t psa;
} data_t;

typedef struct {
	int last_time_taken;
	struct timespec ts;
} perf_t;

typedef struct {
        int cpu;
        int dev_msr_fd;
        int dev_msr_supported;
        uint64_t aperf_diff;
        uint64_t mperf_diff;
        uint64_t pperf_diff;
        uint64_t tsc_diff;
        uint64_t nperf;
} perf_stats_t;

extern int is_time_remaining(clockid_t, struct timespec *, int, int);
extern unsigned int *perf_time;
extern uint64_t pp0_diff_uj, soc_diff_uj[4];
extern int exit_cpu_thread, exit_io_thread;

#endif
