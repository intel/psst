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

#ifndef _RAPL_H_
#define _RAPL_H_

extern int rapl_ediff_pkg0(long long);
extern int rapl_ediff_pkg1(long long);
extern int rapl_ediff_pkg2(long long);
extern int rapl_ediff_pkg3(long long);
extern int rapl_ediff_soc(long long);
extern int rapl_ediff_cpu(long long);
extern int rapl_ediff_gpu(long long);
extern int rapl_ediff_dram(long long);
#endif

