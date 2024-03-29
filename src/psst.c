/*
 * psst.c: main, creates threads & delegate work to SoC.
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
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <math.h>
#include <linux/param.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "parse_config.h"
#include "psst.h"
#include "logger.h"
#include "rapl.h"
#include "perf_msr.h"


void print_version(void)
{
	printf("psst version %s\n", VERSION);
}

int nr_threads;

/*
 * Any additional stress function goes here.
 * However, the motive of this tool is reasonable peak power & its
 * controllabilty. both motives are met using meaningful work.
 */
static void cpu_work(int on_time_us)
{
	(void)on_time_us; /* please the compiler */
	return;
}

int ts_compare(struct timespec *time1, struct timespec *time2)
{
	if (time1->tv_sec < time2->tv_sec)
		return -1;	/* Less than. */
	else if (time1->tv_sec > time2->tv_sec)
		return 1;	/* Greater than. */
	else if (time1->tv_nsec < time2->tv_nsec)
		return -1;	/* Less than. */
	else if (time1->tv_nsec > time2->tv_nsec)
		return 1;	/* Greater than. */
	else
		return 0;	/* Equal. */
}

int is_time_remaining(clockid_t clk, struct timespec *ts_last,
						int sec, int nsec)
{
	struct timespec ts_now, ts_later;
	ts_later.tv_sec = ts_last->tv_sec + sec;
	ts_later.tv_nsec = ts_last->tv_nsec + nsec;
	if (ts_later.tv_nsec > NSEC_PER_SEC) {
		ts_later.tv_sec++;
		ts_later.tv_nsec -= NSEC_PER_SEC;
	}
	if (clock_gettime(clk, &ts_now))
		perror("clock_gettime");
	if (ts_compare(&ts_now, &ts_later) < 0)
		return 1;
	else
		return 0;
}

int timespec_to_msec(struct timespec *t)
{
	return t->tv_sec*1000 + t->tv_nsec/1000000;
}
unsigned long clockdiff_now_ns(clockid_t clk,  struct timespec *ts_then)
{
	struct timespec ts_now;
	if (clock_gettime(clk, &ts_now))
		perror("clock_gettime");
	return diff_ns(ts_then, &ts_now);
}

int set_affinity(int pr)
{
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(pr, &mask);
	if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) != 0)
		perror("sched_setaffiny:");

	return 0;
}

int set_sched_priority(int min_max)
{
	int policy;
	struct sched_param param;

	pthread_getschedparam(pthread_self(), &policy, &param);
	if (min_max)
		param.sched_priority = sched_get_priority_max(policy);
	else
		param.sched_priority = sched_get_priority_min(policy);

	pthread_setschedparam(pthread_self(), policy, &param);
	return 0;
}

int cap_v_unit(float *v_unitp, float max, float min)
{
	if (*v_unitp >= (float)max) {
		*v_unitp = max;
		return 1;
	} else if (*v_unitp <= (float)min) {
		*v_unitp = min;
		return 1;
	}
	return 0;
}

#define PS_MIN_POLL_MS (50)
int power_shaping(ps_t *ps, float *v_unit)
{
	float y_delta, rad;
	int x_delta = 0;
	long long time_ms;
	switch (ps->psn) {
	case LINEAR_RAMP:
		/* recalculate every PS_MIN_POLL_MS */
		x_delta = PS_MIN_POLL_MS;
		if (is_time_remaining(CLOCK_MONOTONIC, &ps->last, 0, x_delta * 1000000))
			return 0;
		y_delta = ps->psa.linear_ramp.slope_y_per_sec / ((float)MSEC_PER_SEC/x_delta);
		*v_unit = *v_unit + y_delta;
		if (cap_v_unit(v_unit, MAX_LOAD, MIN_LOAD))
			return 0;
		break;
	case SAW_TOOTH:
		x_delta = PS_MIN_POLL_MS;
		if (is_time_remaining(CLOCK_MONOTONIC, &ps->last, 0, x_delta * 1000000))
			return 0;
		if ((*v_unit >= (float)ps->psa.saw_tooth.max_y) ||
					(*v_unit <= (float)MIN_LOAD)) {
			ps->psa.linear_ramp.slope_y_per_sec *= -1;
		}
		y_delta = ps->psa.linear_ramp.slope_y_per_sec / ((float)MSEC_PER_SEC/x_delta);
		*v_unit = *v_unit + y_delta;
		cap_v_unit(v_unit, ps->psa.saw_tooth.max_y, MIN_LOAD);
		break;
	case STAIR_CASE:
		/* recalculate every step-stride seconds */
		x_delta = ps->psa.staircase.x_length;
		if (is_time_remaining(CLOCK_MONOTONIC, &ps->last, x_delta, 0))
			return 0;
		y_delta = ps->psa.staircase.y_height;
		*v_unit = *v_unit + y_delta;
		cap_v_unit(v_unit, MAX_LOAD, MIN_LOAD);
		break;
	case SINOSOID:
		x_delta = PS_MIN_POLL_MS;
		if (is_time_remaining(CLOCK_MONOTONIC, &ps->last, 0, x_delta * 1000000))
			return 0;
		/* 2*pi radians == 360 degree == 1 wavelength*/
		time_ms	= ps->last.tv_sec * 1000 + ps->last.tv_nsec/1000000;
		x_delta = time_ms % (ps->psa.sinosoid.x_wavelength * 1000);
		rad = (float)(2*3.14159 * x_delta)/(ps->psa.sinosoid.x_wavelength * 1000);
		/* scale sin(x) to +/-amplituted/2 excursions */
		*v_unit = ps->psa.sinosoid.y_amplitude * (1 + sinf(rad))/2;
		/* duty cycle of 0.00 does not make sense. offset by +1% */
		*v_unit += 1;
		break;
	case SINGLE_PULSE:
		/* rising edge detection */
		if (!ps->begin.tv_sec) {
			if (clock_gettime(CLOCK_MONOTONIC, &ps->begin))
				perror("clock_gettime");
		}

		x_delta = (int)ps->psa.single_pulse.x_length;
		*v_unit = ps->psa.single_pulse.y_height;

		/* falling edge detection */
		if (is_time_remaining(CLOCK_MONOTONIC, &ps->begin, x_delta, 0))
			return 1;
		 else
			*v_unit = MIN_LOAD;
		break;
	case NONE:
		break;
	default:
		/* single step */
		*v_unit = ps->psa.single_step.v_units;
		break;
	}

	/* if we din't return from above cases, we changed shape. Update ps */
	if (clock_gettime(CLOCK_MONOTONIC, &ps->last))
		perror("clock_gettime");
	return 1;

}

#define START_DELAY 0

static void work_fn(void *data)
{
	int start_pending = 0;
	int tick_usec = DEFAULT_TICK_USEC;
	int ret, on_time_us, off_time_us, pr;
	int cpu_work_exist = 0;
	float duty_cycle, dummy;
	struct timespec ts;
	static int start_ms;
	data_t *data_ptr = (data_t*)data;
	ps_t ps;

	sigset_t maskset;
	sigfillset(&maskset);
	ret = pthread_sigmask(SIG_BLOCK, &maskset, NULL);
	if (ret)
		printf("Couldn't mask signals in work_fn. err:%d\n", ret);

	duty_cycle = data_ptr->duty_cycle;
	pr = data_ptr->affinity_pr;
	ps.psn = data_ptr->psn;
	ps.psa = data_ptr->psa;
	ps.begin.tv_sec = 0;

	/*
	 * if this thread is launched for non-cpu work (e,g gpu work requestor)
	 * let it run like any normal thread in system
	 */
	if (CPU_ISSET(pr, &configpv.cpumask)) {
		cpu_work_exist = 1;
		set_affinity(pr);
		set_sched_priority(1);
		/* <this> thread could override gpu or other XX_TICK_USEC */
		tick_usec = IA_TICK_USEC;
	}

	/* fix duty cycle to to non-zero min value */
	duty_cycle = (fabsf(duty_cycle - MIN_LOAD) < MIN_LOAD/10) ? MIN_LOAD : duty_cycle;

	if (pr == 0) {
		plog_poll_sec = MSEC_TO_SEC(configpv.poll_period);
		plog_poll_nsec = (plog_poll_sec > 0) ?
					REMAINING_MS_TO_NS(configpv.poll_period) :
					configpv.poll_period * 1000000;

		duration_sec = MSEC_TO_SEC(configpv.duration);
		duration_nsec = (duration_sec > 0) ?
				REMAINING_MS_TO_NS(configpv.duration) :
				configpv.duration * 1000000;
		dbg_print("thread %d sec: %d nsec %d\n",
				pr, duration_sec, duration_nsec);
		initialize_log_clock();
		update_perf_diffs(&dummy);
		if (dont_stress_cpu0) {
			duty_cycle = MIN_LOAD;
			ps.psn = NONE;
		}
	}
	/* initial on/off time calculation based on duty cycle */
	on_time_us = (tick_usec * duty_cycle / 100);
	off_time_us = tick_usec - on_time_us;
	dbg_print("Thread:%x DutyCycle:%f ontime:%duS, idletime:%duS\n",
			(unsigned int)pthread_self(),
			duty_cycle, on_time_us, off_time_us);

	/* monotonic clock initial reference. updated during power_shaping */
	if (clock_gettime(CLOCK_MONOTONIC, &ps.last))
		perror("clock_gettime 1");

	start_ms = timespec_to_msec(&ps.last);

	do {
		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts))
			perror("clock_gettime 2");
		while (is_time_remaining(CLOCK_THREAD_CPUTIME_ID, &ts, 0,
							on_time_us*1000)) {
			if (timespec_to_msec(&ps.last) - start_ms < START_DELAY) {
				if (clock_gettime(CLOCK_MONOTONIC, &ps.last))
					perror("clock_gettime 3");
				start_pending = 1;
			} else {
				start_pending = 0;
			}

			if (!start_pending) {
				/*
				 * add as much work as required in this loop.
				 * it will be accounted for good.
				 */
				data_ptr->duty_cycle = duty_cycle;
				if (power_shaping(&ps, &duty_cycle)) {
					on_time_us = tick_usec * duty_cycle/100;
					off_time_us = tick_usec - on_time_us;
				}
			}

			if (!start_pending) {
				/* No work for cpu0 if it was just submitter */
				if ((dont_stress_cpu0 || (pr != 0)) && cpu_work_exist) {
					cpu_work(on_time_us);
				}
			}

			if (pr == 0) {
				do_logging(duty_cycle);
				/* XXX: gfx, mem work */
			}
		}

		if (exit_cpu_thread)
			continue;
		/* now for OFF cycle */
		ts.tv_sec = 0;
		ts.tv_nsec = off_time_us * 1000;
		nanosleep(&ts, NULL);
	} while(!exit_cpu_thread && cpu_work_exist);

	/* report out energy index details before exit */
	int N;
	int time_ms;
	float soc_r_avg, pp0_r_avg;
	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		perror("clock_gettime 1");
	time_ms = timespec_to_msec(&ts) - start_ms;
	N = (int)time_ms/configpv.poll_period;

	if (pr == 0) {
		printf("\nDuration: %d ms. poll: %d ms. samples: %d\n",
					    time_ms, configpv.poll_period, N);
		if (rapl_pp0_supported) {
			soc_r_avg = (float)(soc_diff_uj[0])/(time_ms*1000);
			pp0_r_avg = (float)(pp0_diff_uj)/(time_ms*1000);
			printf("Applicable to SOC\n");
			printf("\tAvg soc power: %.3f W\n", soc_r_avg);
			printf("\tEnergy consumed (soc): %.3f mJ\n",
						 (float)soc_diff_uj[0]/1000);
			printf("Applicable to CPU\n");

			printf("\tAvg cpu power: %.3f W\n", pp0_r_avg);
			printf("\tEnergy consumed (cpu): %.3f mJ\n",
						 (float)pp0_diff_uj/1000);
		}
	}
	pthread_exit(NULL);
	return;
}


/* signal handler: terminate all threads on cpu */
static void psst_signal_handler(int sig)
{
	dbg_print("caught signal %d\n", sig);

	switch (sig) {
	case SIGTERM:
	case SIGKILL:
	case SIGINT:
		exit_cpu_thread = 1;
		break;
	default:
		break;
	}
}

static pthread_t *thread_ptr;
static data_t *data_ptr;
perf_stats_t *perf_stats;

int main(int argc, char *argv[])
{
	int c, t = 0, ret;
	float duty;
	void *res;
	data_t *pst;
	struct config *cfg;

	exit_cpu_thread = 0;
	cfg = &configpv;

	if (!parse_cmd_config(argc, argv, cfg)) {
		printf("failed to parse_cmd_config\n");
		exit(EXIT_FAILURE);
	}

	if (cfg->version) {
		print_version();
		exit(EXIT_SUCCESS);
	}

	if (geteuid() != 0) {
		printf("run as root\n");
		exit(EXIT_FAILURE);
	}

	if (!populate_default_config(cfg)) {
		printf("failed to populate_default_config\n");
		exit(EXIT_FAILURE);
	}

	pthread_t io_thread;
	pthread_attr_t attr_io;
	if (pthread_attr_init(&attr_io)) {
		perror("io thread attr");
		exit(EXIT_FAILURE);
	}
	pthread_attr_setdetachstate(&attr_io, PTHREAD_CREATE_JOINABLE);
	pthread_attr_t attr_t;

	data_t base_data;
	/* shape func is common to all threads */
	pst = &base_data;

	/* Android lib does not support suboption(). parse it manually */
	if (!parse_power_shape(cfg->shape_func, pst)) {
		printf("failed parse_power_shape \"%s\"\n", cfg->shape_func);
		printf("see --help for usage\n");
		exit(EXIT_FAILURE);
	}

	/* default/starting duty cycle */
	duty = MIN_LOAD;
	nr_threads = CPU_COUNT(&cfg->cpumask);

	if (!init_delta_vars(nr_threads)){
		exit(EXIT_FAILURE);
	}

	thread_ptr = malloc(sizeof(pthread_t) * nr_threads);
	if (!thread_ptr) {
		perror("malloc thread_ptr");
		goto bail;
	}
	data_ptr = malloc(sizeof(data_t) * nr_threads);
	if (!data_ptr) {
		perror("malloc data_ptr");
		goto bail;
	}
	perf_stats = malloc(sizeof(perf_stats_t) * nr_threads);
	if (!perf_stats) {
		perror("malloc perf_stat");
		goto bail;
	}

	for (c = 0, t = 0; c < CPU_SETSIZE && t < nr_threads; c++) {
		if (!CPU_ISSET(c, &cfg->cpumask))
			continue;
		ret = initialize_dev_msr(c);
		if (ret < 0) {
			perf_stats[t].dev_msr_supported = 0;
			printf("*** No /dev/cpu%d/msr. check CONFIG_X86_MSR support ***\n\n", c);
			break;
		} else {
			perf_stats[t].cpu = c;
			perf_stats[t].dev_msr_fd = ret;
			perf_stats[t].dev_msr_supported = 1;
		}
		t++;
	}
	if (initialize_cpu_hfm_mhz(perf_stats[0].dev_msr_fd))
		goto bail;

	/* thread for deferred disk IO of logs */
	if (pthread_create(&io_thread, &attr_io,
			(void *)&page_write_disk, (void *)cfg)) {
		perror("io thread create");
		goto bail;
	}
	if (!nr_threads) {
		nr_threads = 1;
		CPU_SET(0, &cfg->cpumask);
	}

	ret = pthread_attr_init(&attr_t);
	if (ret) {
		perror("pthread_attr_init:");
		goto bail;
	}

	pthread_attr_setdetachstate(&attr_t, PTHREAD_CREATE_JOINABLE);
	/* fork pthreads for each logical cpu selected & set affinity to cpu. */
	for (c = 0, t = 0; c < CPU_SETSIZE && t < nr_threads; c++) {
		if (!CPU_ISSET(c, &cfg->cpumask))
			continue;
		data_ptr[t].duty_cycle = duty;
		/* setaffinity to specific processor */
		data_ptr[t].affinity_pr = c;
		data_ptr[t].psn = pst->psn;
		data_ptr[t].psa = pst->psa;
		ret = pthread_create(&thread_ptr[t], &attr_t, (void *)&work_fn,
							(void *)&data_ptr[t]);
		if (ret) {
			perror("Failed pthread create");
			goto bail;
		}
		t++;
	}

	if (signal(SIGINT, psst_signal_handler) == SIG_ERR)
		printf("Cannot handle SIGINT\n");

	/* attr not needed after create */
	pthread_attr_destroy(&attr_t);
	dbg_print("Created %d Thread + 1 io thread\n", t);
	while (0 < t--) {
		pthread_join(thread_ptr[t], &res);
		close(perf_stats[t].dev_msr_fd);
		dbg_print("Thread %d cleaned\n", t);
	}

	/* we exit the logger thread above. time to flush any remaining data */
	exit_io_thread = 1;
	trigger_disk_io();

	pthread_attr_destroy(&attr_io);
	pthread_join(io_thread, &res);
	dbg_print("IO Thread cleaned\n");

bail:
	return 1;
}
