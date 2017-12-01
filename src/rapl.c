/*
 * Interface functions to intel rapl.
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

#include "logger.h"
static long long prev_pkg0, prev_pkg1, prev_pkg2, prev_pkg3;
static long long prev_cpu;
static long long prev_gpu;
static long long prev_dram;

#define VAR(a, b) (a##b)
#define generate_rapl_ediff(scope)					       \
int rapl_ediff_##scope(long long cur_ewma)				       \
{									       \
	long long ediff;						       \
	ediff = (VAR(prev_, scope) == 0) ? 0 : (cur_ewma - VAR(prev_, scope)); \
	VAR(prev_, scope) = cur_ewma;					       \
	if (ediff <= 0) {						       \
		return 0;						       \
	}								       \
	return (int)ediff;						       \
}

/* These functions return energy diff in micro-joules since last sample */
generate_rapl_ediff(pkg0);
generate_rapl_ediff(pkg1);
generate_rapl_ediff(pkg2);
generate_rapl_ediff(pkg3);
generate_rapl_ediff(cpu);
generate_rapl_ediff(gpu);
generate_rapl_ediff(dram);
