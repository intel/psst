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

#ifndef _PARSECONFIG_H_
#define _PARSECONFIG_H_

#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include "psst.h"
#include "logger.h"

#define MAX_LEN 512
#define BASE_PATH_RAPL \
	"/sys/devices/virtual/powercap/intel-rapl/intel-rapl"
#define BASE_PATH_TZONE "/sys/devices/virtual/thermal/thermal_zone"
#define BASE_PATH_CPUDTS \
	"/sys/devices/platform/coretemp.0"

/* paths & cmd specific to Android */
#if defined(_ANDROID_)
#define default_log_file  "/data/psst.csv"
/* paths & cmd specific to non-android Linux */
#elif defined(_LINUX_)
#define default_log_file  "/var/log/psst.csv"
#else
#define default_log_file  "./psst.csv"
#endif

struct config {
	char v_unit;
	cpu_set_t cpumask;
	unsigned int gpumask;
	unsigned int memmask;
	unsigned int cpu_freq;
	unsigned int verbose;
	unsigned int super_verbose;
	unsigned int version;
	char log_file_name[80];
	unsigned int log_file_fd;
	char shape_func[20];
	unsigned int poll_period;
	unsigned int duration;
};

extern int dont_stress_cpu0;
typedef enum cpu_stress_option { UNDEFINED,
				 WELL_DEFINED } cpu_stress_opt_t;
extern cpu_stress_opt_t cpu_stress_opt;
extern int parse_cmd_config(int ac, char **av, struct config *configp);
extern int populate_default_config(struct config *configp);
extern int parse_power_shape(char *shape, data_t *pst);
extern int avail_freq_item(int item);

#endif
