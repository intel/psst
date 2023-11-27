/*
 * logger.c functions around logging polled values of open file descriptors
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

#define _GNU_SOURCE
#include <getopt.h>
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "psst.h"
#include "logger.h"
#include "rapl.h"
#include "perf_msr.h"
#include "parse_config.h"
#ifdef _LINUX_
#include <time.h>
#elif defined _ANDROID_
#include <sys/time.h>
#endif

#define INIT_COL(e, h, u, f, m, p, v) { \
		.report_enabled = e,	\
		.header_name = #h,	\
		.unit = #u,		\
		.fmt = #f,		\
		.unit_multiplier = m,	\
		.fd_type = p,		\
		.value = v,		\
		}

/* fixed columns of the log output */
struct log_col_desc col_desc[] = {
	/* TIME_STAMP_MS: time stamp in millisec */
	INIT_COL(1, Time, [ms], 9.0, 1, NO_FD, 0),
	/* FREQ_REALIZED: average frequency (cpu0) since last poll */
	INIT_COL(1, Freq, [MHz], 8.2, 1, MSR_FD, 0),
	/* MAX_FREQ_CPU: smp cpu that delivered max freq in last sample */
	INIT_COL(1, MaxCPU, [#], 6.0, 1, NO_FD, 0),
	/* LOAD_REQUEST: cpu overhead requested by this program */
	INIT_COL(1, LoadRq, [C0_%], 6.2, 1, NO_FD, 0),
	/* LOAD_REALIZED: actual overall cpu overhead in the system */
	INIT_COL(1, Load, [C0_%], 7.2, 1, MSR_FD, 0),
	/* SCALE_FACTOR: workload scaling factor on a cpu */
	INIT_COL(1, ScaleF, [%], 7.2, 1, MSR_FD, 0),
	/* Normalized productive perf */
	INIT_COL(1, Qperf, [perf/uS], 9.2, 1, MSR_FD, 0),
	/* PKG_POWER_RAPL: sysfs rapl power package scope. */
	INIT_COL(1, pwrPkg0, [mWatt], 8.2, 1, NORMAL_FD, 0),
	INIT_COL(1, pwrPkg1, [mWatt], 8.2, 1, NORMAL_FD, 0),
	INIT_COL(1, pwrPkg2, [mWatt], 8.2, 1, NORMAL_FD, 0),
	INIT_COL(1, pwrPkg3, [mWatt], 8.2, 1, NORMAL_FD, 0),
	/* PKG_POWER_LIMIT: sysfs rapl power limit (pkg). */
	INIT_COL(0, PkgLmt, [mWatt], 7.2, 0.001, NORMAL_FD, 0),
	/* PP0_POWER_RAPL: sysfs rapl power PP0 or core scope. */
	INIT_COL(1, PwrCore, [mWatt], 8.2, 1, NORMAL_FD, 0),
	/* PP1_POWER_RAPL: sysfs rapl power PP1 or uncore scope. */
	INIT_COL(1, PwrGpu, [mWatt], 8.2, 1, NORMAL_FD, 0),
	/* DRAM_POWER_RAPL: sysfs rapl power PP1 or uncore scope. */
	INIT_COL(1, PwrDram, [mWatt], 7.2, 1, NORMAL_FD, 0),
	/* CPU_DTS: cpu die temp  */
	INIT_COL(1, CpuDts, [DegC], 6.2, 0.001, NORMAL_FD, 0),
	/* SOC_DTS: cpu die temp  */
	INIT_COL(1, SocDts, [DegC], 6.2, 0.001, NORMAL_FD, 0),
};

int complete_path(char *path, char *compl)
{
	FILE *fp;
	int sz;
	fp = popen(path, "r");
	if (!fp) {
		perror("complete_path()");
		dbg_print("popen failed. path %s\n", path);
		return -1;
	}
	sz = fread(compl, 1, 256, fp);
	if (!ferror(fp) && (sz != 0)) {
		compl[sz-1] = '\0';
	} else {
		dbg_print("fread failed. path %s\n", path);
		pclose(fp);
		return -1;
	}
	pclose(fp);
	return 0;
}

int count_tzone_paths(char *base, char *match)
{
	int sz;
	FILE *fp;
	/* generally sufficient */
	char cmd[MAX_LEN];
	char result[8];

	/* find ${base}* -name <name> 2>/dev/null*/
	sprintf(cmd, "find %s* -name %s 2>/dev/null | wc -l", base, match);
	fp = popen(cmd, "r");
	if (!fp) {
		perror("find_tzone_path()");
		dbg_print("popen failed. base %s match %s\n", base, match);
		return -1;
	}
	sz = fread(cmd, 1, sizeof(cmd), fp);
	if (!sz) {
		pclose(fp);
		dbg_print("fread failed. cmd %s\n", cmd);
		return -1;
	}
	strncpy(result, cmd, sz);
	result[sz - 1] = '\0';
	pclose(fp);
	return atoi(result);
}

int get_node_name(char *base, char *node, char *result)
{
	int sz;
	FILE *fp;
	char path[MAX_LEN];

	sprintf(path, "cat %s/%s 2>/dev/null", base, node);
	fp = popen(path, "r");
	if (!fp) {
		perror("get_node_name()");
		dbg_print("popen failed. path %s\n", path);
		return -1;
	}
	sz = fread(path, 1, sizeof(path), fp);
	if (!sz) {
		pclose(fp);
		dbg_print("fread failed. path %s\n", path);
		return -1;
	}
	if (result) {
		strncpy(result, path, sz);
		result[sz - 1] = '\0';
	}
	pclose(fp);
	return 0;
}

int find_path(char *base, char *node, char *match, char *replace, char *buf)
{
	int sz, replace_sz, fd, found = 0;
	FILE *fp;
	char value[64], path[MAX_LEN];
	char list[2048] = {0};
	char *token, *loc;

	sprintf(path, "find %s* -name %s 2>/dev/null", base, node);
	fp = popen(path, "r");
	if (!fp) {
		perror("find_path()");
		dbg_print("popen failed. base %s\n", base);
		return -1;
	}

	sz = fread(list, 1, sizeof(list), fp);
	pclose(fp);
	if (!sz) {
		dbg_print("fread failed. path %s\n", path);
		return -1;
	} else
		list[sz] = '\0';

	token = strtok(list, "\n");
	if (!token)
		return -1;
	do {
		fd = open(token, O_RDONLY);
		if (fd != -1) {
			sz = read(fd, value, sizeof(value));
			close(fd);
		}
		if (sz > 0) {
			value[sz - 1] = '\0';
			if (strcmp(value, match) == 0) {
				found = 1;
				break;
			}
		}
		token = strtok(NULL, "\n");
	} while (token);

	if (!found || !token)
		return -1;

	loc = strstr(token, node);
	if (!loc)
		return -1;

	sz = loc - token;
	strncpy(buf, token, sz);
	replace_sz = strnlen(replace, MAX_LEN-sz-1);
	strncpy(buf+sz, replace, replace_sz);

	buf[sz + replace_sz] = '\0';
	return 0;
}

int exit_cpu_thread, exit_io_thread;
#define PAGE_SIZE_BYTES 4096
static char *page[2];
static char *active_pg;
static char *dirty_pg;
static int active_pg_filled, dirty_pg_filled;
static int io_inprogress;

static pthread_mutex_t pmutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pcond = PTHREAD_COND_INITIALIZER;

void initialize_log_page(void)
{
	page[0] = (char *)malloc(PAGE_SIZE_BYTES * 8);
	page[1] = (char *)malloc(PAGE_SIZE_BYTES * 8);
	active_pg = page[0];
	dirty_pg = page[1];
}

void trigger_disk_io(void)
{
	/* trigger write IO */
	pthread_mutex_lock(&pmutex);
	pthread_cond_signal(&pcond);
	pthread_mutex_unlock(&pmutex);
}

void accumulate_flush_record(char *record, int sz)
{
	if ((PAGE_SIZE_BYTES - active_pg_filled < sz) || exit_cpu_thread)
		return;

	/*
	 * each old record has nul terminator. write new one starting
	 * on last nul terminator. we just need one nul at the end
	 */
	if (active_pg_filled != 0)
		active_pg_filled -= 1;

	memcpy(active_pg + active_pg_filled, record, sz);
	active_pg_filled += sz;

	if (PAGE_SIZE_BYTES - active_pg_filled	<= sz) {
		/*
		 * swap active with dirty. Note this is not mutex locked.
		 * the idea for separate buffer of some size is that they
		 * never conflict. if we have conflict, the purpose of
		 * delegating io operation to separate thread is defeated.
		 */
		if (!io_inprogress) {
			/* swap buffers */
			dirty_pg = active_pg;
			dirty_pg_filled = active_pg_filled;

			active_pg = dirty_pg;
			active_pg_filled = 0;

			/* IO the other page */
			dbg_print("actvpg:%x, drtypg:%x\n", (void *)active_pg,
							    (void *)dirty_pg);
			trigger_disk_io();
		} else {
			/* purpose defeated. too-much/too-slow IO ? */
			printf("**IO err: fix buffer size or too slow IO **\n");
			return;
		}
	}
}

void page_write_disk(void *confg)
{
	int ret;
	struct config *cfg = (struct config *)confg;
	sigset_t sigmask;
	sigfillset(&sigmask);

	ret = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
	if (ret)
		printf("page_write_disk: couldn't mask signals. err:%d\n", ret);

	do {
		int wr_sz;
		UNUSED(wr_sz);
		pthread_mutex_lock(&pmutex);
		pthread_cond_wait(&pcond, &pmutex);
		pthread_mutex_unlock(&pmutex);

		io_inprogress  = 1;
		/* if we are exiting, just dump the active page */
		if (!exit_cpu_thread) {
			wr_sz = write(cfg->log_file_fd, dirty_pg,
							dirty_pg_filled - 1);
			if (wr_sz == -1)
				perror("fail dirty pg write");
			dbg_print("wrote %d io page bytes to log.\n", wr_sz);
		} else {
			/* reset to top of page */
			wr_sz = write(cfg->log_file_fd, active_pg,
							active_pg_filled - 1);
			if (wr_sz == -1)
				perror("fail active pg write");
			/* NULL terminate whatever data we have written */
			dbg_print("wrote %d active pg bytes to log.\n", wr_sz);
		}
		io_inprogress  = 0;

	} while (!exit_io_thread);

	pthread_cond_destroy(&pcond);
	pthread_mutex_destroy(&pmutex);
	pthread_exit(NULL);
}

struct config configpv;
void initialize_logger(void)
{
	int i;
	char path[MAX_LEN] = "";

	for (i = 0; i < MAX_COL_NUM; i++) {
		if (!col_desc[i].report_enabled) {
			dbg_print(" %d.report_disabled for %s\n",
					i, col_desc[i].header_name);
			continue;
		}

		switch (i) {
		case FREQ_REALIZED:
		case LOAD_REALIZED:
		case SCALE_FACTOR:
		case NORM_PERF:
		/* XXX: for gfx C0, create separate columns */
			if (get_node_name("/dev/cpu/0", "msr", NULL) < 0) {
				col_desc[i].report_enabled = 0;
			}
			continue;  /* No file descritor required */
		case MAX_FREQ_CPU:
			if (get_node_name("/dev/cpu/0", "msr", NULL) < 0)
				col_desc[i].report_enabled = 0;
			continue;  /* No file descritor required */
		case TIME_STAMP_MS:
		case LOAD_REQUEST:
			continue;  /* No file descritor required */
		case PKG0_POWER_RAPL:
			if (find_path(BASE_PATH_RAPL, "name", "package-0",
							"energy_uj", path)) {
				col_desc[i].report_enabled = 0;
				rapl_pp0_supported = 0;
			} else {
				rapl_pp0_supported = 1;
			}
			break;
		case PKG1_POWER_RAPL:
			if (find_path(BASE_PATH_RAPL, "name", "package-1",
							"energy_uj", path)) {
				col_desc[i].report_enabled = 0;
			}
			break;
		case PKG2_POWER_RAPL:
			if (find_path(BASE_PATH_RAPL, "name", "package-2",
							"energy_uj", path)) {
				col_desc[i].report_enabled = 0;
			}
			break;
		case PKG3_POWER_RAPL:
			if (find_path(BASE_PATH_RAPL, "name", "package-3",
							"energy_uj", path)) {
				col_desc[i].report_enabled = 0;
			}
			break;
		case PP0_POWER_RAPL:
			if (find_path(BASE_PATH_RAPL, "name", "core",
							"energy_uj", path))
				col_desc[i].report_enabled = 0;
			break;
		case PP1_POWER_RAPL:
			if (find_path(BASE_PATH_RAPL, "name", "uncore",
							"energy_uj", path))
				col_desc[i].report_enabled = 0;
			break;
		case DRAM_POWER_RAPL:
			if (find_path(BASE_PATH_RAPL, "name", "dram",
							"energy_uj", path))
				col_desc[i].report_enabled = 0;
			break;
		case CPU_DTS:
			if (find_path(BASE_PATH_CPUDTS, "name", "coretemp",
							"temp2_input", path))
				col_desc[i].report_enabled = 0;
			break;
		case SOC_DTS:
			if (find_path(BASE_PATH_TZONE, "type", "x86_pkg_temp",
							"temp", path))
				col_desc[i].report_enabled = 0;
			break;
		}
		/* close only on exit */
		col_desc[i].poll_fd = open(path, 0, "r");
		if (col_desc[i].poll_fd < 0) {
			dbg_print("disabling column %s\n",
					 col_desc[i].header_name);
			col_desc[i].report_enabled = 0;
		}
	}

	initialize_log_page();
	return;
}

static char *log_header;

struct timespec plog_last_tm, first_tm;
int plog_poll_sec, plog_poll_nsec;
int duration_sec, duration_nsec;

void initialize_log_clock(void)
{
	struct timespec tm;
	if (clock_gettime(CLOCK_MONOTONIC, &tm))
		perror("clock_gettime");
	first_tm.tv_sec = plog_last_tm.tv_sec = tm.tv_sec;
	first_tm.tv_nsec = plog_last_tm.tv_nsec = tm.tv_nsec;
}

uint64_t diff_ns(struct timespec *ts_then, struct timespec *ts_now)
{
	uint64_t diff = 0;
	if (ts_now->tv_sec > ts_then->tv_sec) {
		diff = (ts_now->tv_sec - ts_then->tv_sec) * NSEC_PER_SEC;
		diff = diff - ts_then->tv_nsec + ts_now->tv_nsec;
	} else {
		diff += ts_now->tv_nsec - ts_then->tv_nsec;
	}
	return diff;
}

int update_perf_diffs(float *sum_norm_perf)
{
	int fd, maxed_cpu_idx;
	float max_load, next_max_load;
	float _sum_nperf = 0, nperf = 0;
	uint64_t aperf_raw, mperf_raw, pperf_raw, tsc_raw, poll_cpu_us;

	for (int t = 0; t < nr_threads; t++) {
		fd = perf_stats[t].dev_msr_fd;

		/*
		 * XXX: per-cpu IPI wakes for msr read will cost power. Need to
		 * skip poll for idle bound cpus (e.g, using C-state count).
		 * note: all-core sum perf considers per-respective poll time
		 */
		read_msr(fd, (uint32_t)MSR_IA32_PPERF, &pperf_raw);
		read_msr(fd, (uint32_t)MSR_IA32_APERF, &aperf_raw);
		read_msr(fd, (uint32_t)MSR_IA32_MPERF, &mperf_raw);
		read_msr(fd, (uint32_t)MSR_IA32_TSC, &tsc_raw);

		perf_stats[t].pperf_diff = cpu_get_diff_pperf(pperf_raw, t);
		perf_stats[t].aperf_diff = cpu_get_diff_aperf(aperf_raw, t);
		perf_stats[t].mperf_diff = cpu_get_diff_mperf(mperf_raw, t);
		perf_stats[t].tsc_diff = cpu_get_diff_tsc(tsc_raw, t);

		poll_cpu_us = perf_stats[t].tsc_diff/cpu_hfm_mhz;

		/*
		 * Normalized perf metric defined as pperf per load per time.
		 * The rationale is detailed in the followingi paper:
		 * github.com/intel/psst >whitepapers >Generic_perf_per_watt.pdf
		 * Given that delta_load = delta_mperf/delta_tsc, we can rewrite
		 * as given below.
		 */
		if (perf_stats[t].mperf_diff) {
			nperf = (float) perf_stats[t].pperf_diff/poll_cpu_us;
			nperf = (float) nperf * perf_stats[t].tsc_diff;
			nperf = (float) nperf/(perf_stats[t].mperf_diff);
			perf_stats[t].nperf = (uint64_t)nperf;
			_sum_nperf += (float) nperf;
		}
	}
	*sum_norm_perf = _sum_nperf;

	max_load = 100*(float)perf_stats[0].mperf_diff/perf_stats[0].tsc_diff;
	maxed_cpu_idx = 0;

	 for (int t = 1; t < nr_threads; t++) {
		next_max_load = 100 * (float) perf_stats[t].mperf_diff /
						perf_stats[t].tsc_diff;

		/* float comparison with some meaningful difference */
		if (max_load > (next_max_load + 0.01))
			continue;
		else {
			max_load = next_max_load;
			maxed_cpu_idx = t;
		}
	}

	return maxed_cpu_idx;
}
#define LOG_HEADER_SZ 2048

int first_log = 1;
uint64_t pp0_initial_energy, soc_initial_energy[4];
uint64_t pp0_diff_uj, soc_diff_uj[4];

int rapl_pp0_supported;

void do_logging(float dc)
{
	char buf[64];
	char final_buf[1024];
	char val_fmt[16];
	char delim[] = ",    ";
	char delim_short[] = ",  ";
	log_col_t i;
	int sz, sz1, pkg_num;
	int max_cpu = 0;
	int m = 0;
	float sum_norm_perf = 0;
	struct timespec tm;

	*buf = '\0';

	if (clock_gettime(CLOCK_MONOTONIC, &tm))
		perror("clock_gettime");

	/* duration_* is the total time this tool runs */
	if (!is_time_remaining(CLOCK_MONOTONIC, &first_tm, duration_sec,
				duration_nsec))
		exit_cpu_thread = 1;

	/* we log once in plog_poll_* interval */
	if (!first_log && is_time_remaining(CLOCK_MONOTONIC, &plog_last_tm,
					plog_poll_sec, plog_poll_nsec))
		return;

	plog_last_tm.tv_sec = tm.tv_sec;
	plog_last_tm.tv_nsec = tm.tv_nsec;

	/*
	 * When dev_msr not supported, the diffs are not populated.
	 * In these cases the associated columns have been disabled anyway.
	 */
	if (perf_stats->dev_msr_supported) {
		m = update_perf_diffs(&sum_norm_perf);
		max_cpu = perf_stats[m].cpu;
	}

	for (i = 0; i < MAX_COL_NUM; i++) {
		if (!col_desc[i].report_enabled)
				continue;

		if (col_desc[i].fd_type == NORMAL_FD) {
			lseek(col_desc[i].poll_fd, 0L, SEEK_SET);
			sz = read(col_desc[i].poll_fd, buf, 64);
			if (sz == -1) {
				perror("read poll_fd 1");
				printf(" col desc read fd err %d\n", i);
			}
		}

		switch (i) {
		case TIME_STAMP_MS:
			col_desc[i].value =
				diff_ns(&first_tm, &plog_last_tm)/1000000;
			break;
		case LOAD_REQUEST:
			col_desc[i].value = dc;
			break;
		case LOAD_REALIZED:
			/* real C0 = delta-mperf/delta-tsc */
			col_desc[i].value = (float) perf_stats[m].mperf_diff *
					      100/perf_stats[m].tsc_diff;
			break;
		case SCALE_FACTOR:
			col_desc[i].value = (float) perf_stats[m].pperf_diff *
					      100/perf_stats[m].aperf_diff;
			break;
		case NORM_PERF:
			col_desc[i].value = sum_norm_perf;
			sum_norm_perf = 0;
			break;
		case MAX_FREQ_CPU:
			col_desc[i].value = max_cpu;
			break;
		case FREQ_REALIZED:
			/* real freq = TSC* delta-aperf/delta-mperf */
			col_desc[i].value = (float) perf_stats[m].aperf_diff /
					   perf_stats[m].mperf_diff*cpu_hfm_mhz;
			break;
		case PKG0_POWER_RAPL:
			pkg_num = i - PKG0_POWER_RAPL;
			if (first_log)
				soc_initial_energy[pkg_num] = atoll(buf);

			soc_diff_uj[pkg_num] = atoll(buf) - soc_initial_energy[pkg_num];

			col_desc[i].value = (float) rapl_ediff_pkg0(atoll(buf))/
						configpv.poll_period;
			break;

		case PKG1_POWER_RAPL:
			pkg_num = i - PKG0_POWER_RAPL;
			if (first_log)
				soc_initial_energy[pkg_num] = atoll(buf);

			soc_diff_uj[pkg_num] = atoll(buf) - soc_initial_energy[pkg_num];

			col_desc[i].value = (float) rapl_ediff_pkg1(atoll(buf))/
						configpv.poll_period;
			break;
		case PKG2_POWER_RAPL:
			pkg_num = i - PKG0_POWER_RAPL;
			if (first_log)
				soc_initial_energy[pkg_num] = atoll(buf);

			soc_diff_uj[pkg_num] = atoll(buf) - soc_initial_energy[pkg_num];

			col_desc[i].value = (float) rapl_ediff_pkg2(atoll(buf))/
						configpv.poll_period;
			break;
		case PKG3_POWER_RAPL:
			pkg_num = i - PKG0_POWER_RAPL;
			if (first_log)
				soc_initial_energy[pkg_num] = atoll(buf);

			soc_diff_uj[pkg_num] = atoll(buf) - soc_initial_energy[pkg_num];

			col_desc[i].value = (float) rapl_ediff_pkg3(atoll(buf))/
						configpv.poll_period;
			break;
		case PP0_POWER_RAPL:
			if (first_log)
				pp0_initial_energy = atoll(buf);

			pp0_diff_uj = atoll(buf) - pp0_initial_energy;

			col_desc[i].value = (float) rapl_ediff_cpu(atoll(buf))/
						configpv.poll_period;
			break;
		case PP1_POWER_RAPL:
			col_desc[i].value = (float) rapl_ediff_gpu(atoll(buf))/
						configpv.poll_period;
			break;
		case DRAM_POWER_RAPL:
			col_desc[i].value = (float) rapl_ediff_dram(atoll(buf))/
						configpv.poll_period;
			break;

		case PKG_POWER_LIMIT:
		case CPU_DTS:
		case SOC_DTS:
			col_desc[i].value = atoi(buf);
			break;
		/* dead code. happy compiler */
		case MAX_COL_NUM:
			break;

		}
		col_desc[i].value *= col_desc[i].unit_multiplier;
	}

	if (!log_header) {
		log_header = malloc(LOG_HEADER_SZ * sizeof(char));
		if (!log_header) {
			perror("Failed to malloc log_header");
			exit(EXIT_FAILURE);
		}
		char hdr_fmt[32];

		/* add header names */
		sprintf(log_header, "%c", '#');
		sz = 1;
		for (i = 0; i < MAX_COL_NUM; i++) {
			if (!col_desc[i].report_enabled)
				continue;
			sprintf(hdr_fmt, "%%%ds%s",
					atoi(col_desc[i].fmt), delim);
			sz1 = sprintf(log_header + sz, hdr_fmt,
					col_desc[i].header_name);
			sz += sz1;
		}

		if (configpv.super_verbose && col_desc[LOAD_REALIZED].report_enabled) {
			i = NORM_PERF;
			for (int j = 0; j < nr_threads; j++) {
				sprintf(hdr_fmt, "%%%ds%.2d%s",
					atoi(col_desc[i].fmt), perf_stats[j].cpu, delim_short);
				sz1 = sprintf(log_header + sz, hdr_fmt,
						col_desc[i].header_name);
				sz += sz1;
			}
			i = LOAD_REALIZED;
			for (int j = 0; j < nr_threads; j++) {
				sprintf(hdr_fmt, "%%%ds%.2d%s",
					atoi(col_desc[i].fmt), perf_stats[j].cpu, delim_short);
				sz1 = sprintf(log_header + sz, hdr_fmt,
						col_desc[i].header_name);
				sz += sz1;
			}
			i = FREQ_REALIZED;
			for (int j = 0; j < nr_threads; j++) {
				sprintf(hdr_fmt, "%%%ds%.2d%s",
					atoi(col_desc[i].fmt), perf_stats[j].cpu, delim_short);
				sz1 = sprintf(log_header + sz, hdr_fmt,
						col_desc[i].header_name);
				sz += sz1;
			}
			sz = sz - sizeof(delim_short) + 2;
		} else {
			sz = sz - sizeof(delim) + 2;
		}

		log_header[sz - 1] = '\n';

		/* add header unit of measurement */
		sprintf(log_header+sz, "%c", '#');
		sz += 1;
		for (i = 0; i < MAX_COL_NUM; i++) {
			if (!col_desc[i].report_enabled)
				continue;
			sprintf(hdr_fmt, "%%%ds%s",
					atoi(col_desc[i].fmt), delim);
			sz1 = sprintf(log_header + sz, hdr_fmt,
						col_desc[i].unit);
			sz += sz1;
		}
		if (configpv.super_verbose && col_desc[LOAD_REALIZED].report_enabled) {
			i = NORM_PERF;
			for (int j = 0; j < nr_threads; j++) {
				/* add 2 digits for cpu# */
				sprintf(hdr_fmt, "%%%ds%s",
						atoi(col_desc[i].fmt)+2, delim_short);
				sz1 = sprintf(log_header + sz, hdr_fmt,
							col_desc[i].unit);
				sz += sz1;
			}
			i = LOAD_REALIZED;
			for (int j = 0; j < nr_threads; j++) {
				sprintf(hdr_fmt, "%%%ds%s",
						atoi(col_desc[i].fmt)+2, delim_short);
				sz1 = sprintf(log_header + sz, hdr_fmt,
							col_desc[i].unit);
				sz += sz1;
			}
			i = FREQ_REALIZED;
			for (int j = 0; j < nr_threads; j++) {
				sprintf(hdr_fmt, "%%%ds%s",
						atoi(col_desc[i].fmt)+2, delim_short);
				sz1 = sprintf(log_header + sz, hdr_fmt,
							col_desc[i].unit);
				sz += sz1;
			}
			sz = sz - sizeof(delim_short) + 2;
		} else {
			sz = sz - sizeof(delim) + 2;
		}
		log_header[sz - 1] = '\n';

		/* write-out to file */
		sz = write(configpv.log_file_fd, log_header, sz);
		if (sz == -1)
			perror("log_header write");

		printf("report being logged to %s... ^C to exit.\n", configpv.log_file_name);
		if (configpv.verbose && !configpv.super_verbose)
			printf("%s\n", log_header);
	}

	sz = 0;
	for (i = 0; i < MAX_COL_NUM; i++) {
		if (!col_desc[i].report_enabled)
			continue;
		sprintf(val_fmt, "%%%sf%s", col_desc[i].fmt, delim);
		sz1 = sprintf(final_buf + sz, val_fmt,
						col_desc[i].value);
		sz += sz1;
	}

	if (configpv.super_verbose && col_desc[LOAD_REALIZED].report_enabled) {
		int sz2;
		i = NORM_PERF;
		for (int j = 0; j < nr_threads; j++) {
			sprintf(val_fmt, "%%%.3sf%s", col_desc[i].fmt, delim);
			sz2 = sprintf(final_buf+sz, val_fmt, (float)perf_stats[j].nperf);
			sz += sz2;
		}

		i = LOAD_REALIZED;
		for (int j = 0; j < nr_threads; j++) {
			sprintf(val_fmt, "%%%.3sf%s", col_desc[i].fmt, delim);
			sz2 = sprintf(final_buf+sz, val_fmt, (float)perf_stats[j].mperf_diff
					*100/perf_stats[j].tsc_diff);
			sz += sz2;
		}

		i = FREQ_REALIZED;
		for (int j = 0; j < nr_threads; j++) {
			sprintf(val_fmt, "%%%.3sf%s", col_desc[i].fmt, delim);
			sz2 = sprintf(final_buf+sz, val_fmt, (float)perf_stats[j].aperf_diff /
					perf_stats[j].mperf_diff*cpu_hfm_mhz);
			sz += sz2;
		}
	}

	/* erase the last delimiter */
	sz = sz - sizeof(delim) + 2;
	final_buf[sz-1] = '\n';
	final_buf[sz] = '\0';

	if (configpv.verbose && !configpv.super_verbose)
		printf("%s", final_buf);

	accumulate_flush_record(final_buf, sz+1);

	first_log = 0;
}
