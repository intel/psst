/*
 * parse_config.c: deals with cmd line arg parsing & default value population
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
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "parse_config.h"
#include "logger.h"

static struct option long_options[] = {
	{"cpumask",     1,      0,      'C'},
	{"duration",    1,      0,      'd'},
	{"gpumask",     1,      0,      'G'},
	{"log-file",    1,      0,      'l'},
	{"poll-period", 1,      0,      'p'},
	{"shape-func",  1,      0,      's'},
	{"verbose",     0,      0,      'v'},
	{"super-verbose",0,     0,      'S'},
	{"version",     0,      0,      'V'},
	{"help",        0,      0,      'h'},
	{0, 0, 0, 0}
};

void print_usage(char *prog)
{
	printf("usage:\n");
	printf("%s [options <value>]\n", prog);
	printf("\tSupported options are:\n");
	printf("\t-C|--cpumask\t\t<CPUMASK> hex bit mask of cpu# to be selected.\n");
	printf("\t\t\t\t(e.g., a1 selects cpu 0,5,7. default: every online cpu. Max:400 [1024])\n");
	printf("\t-p|--poll-period\t<pollperiod> (ms) for logging (default: 500 ms)\n");
	printf("\t-d|--duration\t\t<duration> (ms) to run the tool (default: 3600000 i.e., 1hr)\n");
	printf("\t-l|--log-file\t\t</path/to/log-file> (default: %s)\n", default_log_file);
	printf("\t-v|--verbose\t\tenables verbose mode (default: disabled when args specified)\n");
	printf("\t-S|--super-verbose\tprint per-core info (e.g., util) to log file (default: disabled)\n");
	printf("\t-V|--version\t\tprints version when specified\n");
	printf("\t-h|--help\t\tprints usage when specified\n");
	printf("\t-s|--shape-func\t\t<shape-func,arg> (default: single-step,0.1)\n");
	printf("\tSupported power shape functions & args are:\n");
	printf("\t\t<single-step,v>\t\t");
	printf("where v is load step height.\n");
	printf("\t\t<sinosoid,w,a>\t\t");
	printf("where w is wavelength [seconds] and a is the max amplitude (load %%)\n");
	printf("\t\t<stair-case,v,u>\t");
	printf("where v is load step height, u is step length (sec)\n");
	printf("\t\t<single-pulse,v,u>\t");
	printf("where v is load step height, u is step length (sec)\n");
	printf("\t\t<linear-ramp,m>\t\twhere m is the slope (load/sec)\n");
	printf("\t\t<saw-tooth,m,a>\t\tslope m (load/sec);reversed after max a%% or min(0.1)%%\n");
	printf("\nexample 1: use psst just for logging system power/thermal parameters with minimum overhead\n");
	printf("\t   $ sudo ./psst	 #implied default args: -s single-step,0.1 -p 500 -v\n");
	printf("\nexample 2: linear ramp CPU power with slope 3 (i.e., 3%% usage increase every sec)"
			" applied for cpu0, cpu1 & cpu3.\n\t   poll and report"
			" every 700mS. output on terminal. run for 33 sec\n");
	printf("\t   $ sudo ./psst -s linear-ramp,3 -C b -p 700 -d 33000 -v\n");
}

static int populate_online_cpumask(cpu_set_t *cpumask);
static void verbose_prints(struct config *configp);

int populate_default_config(struct config *configp)
{
	if (!configp->shape_func[0])
		strncpy(configp->shape_func, "single-step,0.1", 16);

	if (!configp->v_unit)
		configp->v_unit = 'C';

	if (cpu_stress_opt == UNDEFINED)
		populate_online_cpumask(&configp->cpumask);

	if (!configp->gpumask)
		configp->gpumask = 0x0;
	if (!configp->memmask)
		configp->memmask = 0x0;

	if (configp->memmask || configp->gpumask) {
		/* we want to use cpu0 for non-cpu submitter
		 * hence we can't have any regular stress function
		 * request on cpu 0 at the same time
		 */
		if (CPU_ISSET(0, &configp->cpumask) &&
				(cpu_stress_opt == WELL_DEFINED)) {
			printf("can't stress cpu0 (-C xx) along with -G or -M\n");
			return 0;
		} else {
			dont_stress_cpu0 = 1;
			CPU_SET(0, &configp->cpumask);
		}
	}
	/*
	 * cpu0 is special. It has to be always enabled. Move the
	 * user intention as cpu_stress_opt reason.
	 */
	if (!CPU_ISSET(0, &configp->cpumask)) {
		dont_stress_cpu0 = 1;
		CPU_SET(0, &configp->cpumask);
	}

	if (!configp->log_file_name[0]) {
		strncpy(configp->log_file_name, default_log_file,
				sizeof(default_log_file)+1);
	}
	/* verbose & version option are not turned on by default */

	if (!configp->cpu_freq)
		configp->cpu_freq = -1;

	if (!configp->log_file_fd) {
		configp->log_file_fd = open(configp->log_file_name,
				O_RDWR|O_CREAT|O_TRUNC,
				S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
		if (configp->log_file_fd == -1)
			perror("log file");
	}
	if (!configp->poll_period)
		configp->poll_period = 500; /* (ms) */
	if (!configp->duration)
		configp->duration = 3600000; /* default 60min */

	initialize_logger();
	if (configp->verbose | configp->super_verbose)
		verbose_prints(configp);

	return 1;
}

static int xchar_to_int(char x)
{
	if (isalpha(x))
		return toupper(x) - 55;
	if (isdigit(x))
		return x - 48;
	return -1;
}

/* cpuset procfs reports online cpu in this format:
 * 0-4,7 : to mean 0,1,2,3,4 & 7 are online
 */
static int cpuset_to_bitmap(char *buf, cpu_set_t *cpumask)
{
	int k;
	char *token, *subtoken, *pos;
	char *save1, *save2, *token_copy;

	pos = strchr(buf, '\n');
	if (!pos)
		return 0;
	pos[0] = '\0';
	/* e.g:  3,5-11 */
	token = strtok_r(buf, ",", &save1);
	do {
		if (!token)
			break;
		token_copy = strdup(token);
		subtoken = strtok_r(token_copy, "-", &save2);
		/* update 3 (and 5 in next pass) ... */
		if (!subtoken) {
			k = atoi(token);
			CPU_SET(k, cpumask);
			free(token_copy);
			continue;
		} else {
			k = atoi(subtoken);
			CPU_SET(k, cpumask);
			pos = token + strlen(subtoken) + 1;
			while (++k <= atoi(pos))
				CPU_SET(k, cpumask);
			free(token_copy);
		}
		token = strtok_r(NULL, ",", &save1);
	} while (token);
	return 0;
}

static int populate_online_cpumask(cpu_set_t *cpumask)
{
	char buf[65];
	FILE *fp;
	size_t sz;

	/* Open the command for reading in pipe. */
	fp = fopen("/sys/devices/system/cpu/online", "r");
	if (fp == NULL) {
		printf("Failed to get online cpu list\n");
		return -1;
	}
	sz = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	if (sz == 0) {
		printf("populate_online_cpumask: fread failed\n");
		return -1;
	}
	buf[sz] = '\0';
	cpuset_to_bitmap(buf, cpumask);
	return 0;
}

cpu_stress_opt_t cpu_stress_opt = UNDEFINED;
int dont_stress_cpu0;

static int set_cpu_mask(char *buf, struct config *configp)
{
	int arg_bytes = strlen(buf);
	if ((arg_bytes == 1) && (buf[0] == '0')) {
		/* user wants to: not stress any cpu.
		 * lets translate that to cpu0 submitter work
		 */
		dont_stress_cpu0 = 1;
		cpu_stress_opt = WELL_DEFINED;
		CPU_SET(0, &(configp->cpumask));
		return 0;
	}

	if ((arg_bytes * 4) > CPU_SETSIZE) {
		printf("max cpu supported is %d\n", CPU_SETSIZE);
		return -1;
	} else {
	/* arg "a1" or 0000.1010 0000.0001 selects cpu 0,5,7 */
		int i, j, k = 0;
		for (i = arg_bytes - 1; i > -1; i--) {
			for (j = 0; j < 4; j++, k++) {
				if (!isxdigit(buf[i])) {
					printf("Invalid arg to -C\n");
					return -1;
				}
				if (xchar_to_int(buf[i]) & (1<<j))
					CPU_SET(k, &(configp->cpumask));
			}
		}
		cpu_stress_opt = WELL_DEFINED;
	}
	return 0;
}

int parse_cmd_config(int ac, char **av, struct config *configp)
{
	int c, option_index;
	char buf[128];
	size_t len;

	memset(configp, 0, sizeof(struct config));
	CPU_ZERO(&configp->cpumask);

	if (ac == 1)
		configp->verbose = 1;

	while ((c = getopt_long(ac, av, "s:G:C:E:l:p:d:hvVTS",
			long_options, &option_index)) != -1) {
		/* XXX check optarg valid */
		switch (c) {
		case 'l':
			len = sizeof(configp->log_file_name);
			strncpy(configp->log_file_name, optarg, len);
			configp->log_file_name[len - 1] = '\0';
			break;
		case 'p':
			sscanf(optarg, "%d", &configp->poll_period);
			if (configp->poll_period <= 0)
				return 0;
			break;
		case 'd':
			sscanf(optarg, "%d", &configp->duration);
			if (configp->duration <= 0)
				return 0;
			break;
		case 'C':
			sscanf(optarg, "%127s", buf);
			if (set_cpu_mask(buf, configp) < 0)
				return 0;
			break;
		case 'G':
			sscanf(optarg, "%x", &configp->gpumask);
			break;
		case 'v':
			configp->verbose = 1;
			break;
		case 'S':
			configp->super_verbose = 1;
			break;
		case 'V':
			configp->version = 1;
			break;
		case 's':
			len = sizeof(configp->shape_func);
			strncpy(configp->shape_func, optarg, len);
			configp->shape_func[len - 1] = '\0';
			break;
		case 'h':
		case '?':
		default:
			print_usage("psst");
			return 0;
		} /* switch */
	} /* while */

	if (optind < ac) {
		print_usage("psst");
		return 0;
	}

	return 1;
}

static void verbose_prints(struct config *configp)
{
	int i;
	printf("Verbose mode ON\n");
	dbg_print("v-unit is: %c\n", configp->v_unit);
	if (configp->v_unit != 'C')
		dbg_print("option not supported\n");
	printf("CPU domain. Following %d cpu selected:\n",
					CPU_COUNT(&configp->cpumask));

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, &configp->cpumask)) {
			printf("\tcpu %d", i);
			if (i == 0)
				printf("\t[%s]\n",
				 dont_stress_cpu0 ?
				 "as work submitter" : "was online or chosen");
			else
				printf("\t[%s]\n", "was online or chosen");
		}
	}

	printf("\n");
	printf("poll period %dms\n", configp->poll_period);
	printf("run duration %dms\n", configp->duration);
	printf("Log file path: %s\n", configp->log_file_name);
	printf("power curve shape: %s\n", configp->shape_func);
	printf("\n");
}

int parse_power_shape(char *shape, data_t *pst)
{
	char *token;
	char *delimiter = ",";
	char *running;

	if (!strcmp(shape, ""))
		return 0;
	/* use strdupa to auto free on stack exit */
	running = strdup(shape);
	token = strtok(running, delimiter);
	if (!token) {
		free(running);
		return 1;
	}
	if (!strcmp(token, "single-step")) {
		token = strtok(NULL, delimiter);
		if (token) {
			pst->psn = SINGLE_STEP;
			sscanf(token,"%f",&pst->psa.single_step.v_units);
			dbg_print("single step yunit %f\n",
					pst->psa.single_step.v_units);
			free(running);
		} else {
			free(running);
			return 0;
		}
	} else if (!strcmp(token, "stair-case")) {
		token = strtok(NULL, delimiter);
		if (token) {
			pst->psn = STAIR_CASE;
			sscanf(token,"%f",&pst->psa.staircase.y_height);
			token = strtok(NULL, delimiter);
			if (token) {
				pst->psa.staircase.x_length = atof(token);
				dbg_print("staircase x, y %f ,%d\n",
						pst->psa.staircase.y_height,
						pst->psa.staircase.x_length);
			}
			free(running);
		} else {
			free(running);
			return 0;
		}
	} else if (!strcmp(token, "sinosoid")) {
		token = strtok(NULL, delimiter);
		if (token) {
			pst->psn = SINOSOID;
			sscanf(token,"%d",&pst->psa.sinosoid.x_wavelength);
			token = strtok(NULL, delimiter);
			if (token) {
				sscanf(token,"%f",&pst->psa.sinosoid.y_amplitude);
				dbg_print("sine wavelength %d amplitude %f\n",
						pst->psa.sinosoid.x_wavelength,
						pst->psa.sinosoid.y_amplitude);
			}
			free(running);
		} else {
			free(running);
			return 0;
		}
	} else if (!strcmp(token, "single-pulse")) {
		token = strtok(NULL, delimiter);
		if (token) {
			pst->psn = SINGLE_PULSE;
			sscanf(token,"%f",&pst->psa.single_pulse.y_height);
			token = strtok(NULL, delimiter);
			if (token) {
				sscanf(token,"%d",&pst->psa.single_pulse.x_length);
				dbg_print("singlepulse x, y %f ,%d\n",
						pst->psa.single_pulse.y_height,
						pst->psa.single_pulse.x_length);
			}
			free(running);
		} else {
			free(running);
			return 0;
		}
	} else if (!strcmp(token, "linear-ramp")) {
		token = strtok(NULL, delimiter);
		if (token) {
			pst->psn = LINEAR_RAMP;
			sscanf(token,"%f",&pst->psa.linear_ramp.slope_y_per_sec);
			printf(" liner ramp %f\n",
					pst->psa.linear_ramp.slope_y_per_sec);
			free(running);
		} else {
			free(running);
			return 0;
		}
	} else if (!strcmp(token, "saw-tooth")) {
		token = strtok(NULL, delimiter);
		if (token) {
			pst->psn = SAW_TOOTH;
			sscanf(token,"%f",&pst->psa.saw_tooth.slope_y_per_sec);
			token = strtok(NULL, delimiter);
			if (token)
				sscanf(token,"%f",&pst->psa.saw_tooth.max_y);
			dbg_print(" saw tooth slope %.3f, max %.3f\n",
					pst->psa.saw_tooth.slope_y_per_sec,
					pst->psa.saw_tooth.max_y);
			free(running);
		} else {
			free(running);
			return 0;
		}
	} else {
		free(running);
		return 0;
	}
	return 1;
}
