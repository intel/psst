/*
 * cpu_workloads.h: pluggable SSE/AVX workload functions for psst
 *
 * Copyright (c) 2026, Intel Corporation.
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
 */

#ifndef _CPU_WORKLOADS_H_
#define _CPU_WORKLOADS_H_

/*
 * Workload function signature: matches cpu_work() in psst.c.
 */
typedef void (*workload_fn_t)(void);

/*
 * Workload categories by x86 ISA extension.
 *
 *  Scalar:
 *    scalar-int    - 64-bit integer multiply-accumulate (no SIMD)
 *    scalar-fp     - 64-bit double-precision scalar FMA
 *
 *  SSE (128-bit XMM registers):
 *    sse-fp32      - packed single-precision FP (SSE)
 *    sse-fp64      - packed double-precision FP (SSE2)
 *    sse-int8      - packed 8-bit integer arithmetic (SSE2)
 *    sse-int32     - packed 32-bit integer multiply (SSE4.1)
 *
 *  AVX (256-bit YMM registers):
 *    avx-fp32      - packed single-precision FP (AVX)
 *    avx-fp64      - packed double-precision FP (AVX)
 *    avx2-int      - packed 32-bit integer multiply (AVX2)
 *
 *  FMA (fused multiply-add, 256-bit):
 *    fma-fp32      - packed single-precision FMA (FMA3 + AVX)
 *    fma-fp64      - packed double-precision FMA (FMA3 + AVX)
 *
 *  AVX-512 (512-bit ZMM registers):
 *    avx512-fp32   - packed single-precision FMA (AVX-512F)
 *    avx512-fp64   - packed double-precision FMA (AVX-512F)
 *
 * Pass "list" to resolve_workload() or as -w list on the command line
 * to print all available workloads and exit.
 */

/*
 * resolve_workload - look up a workload by name and verify CPU support.
 *
 * Returns the matching workload_fn_t, or the scalar-int fallback when the
 * name is unknown or the CPU does not support the required ISA extension.
 * Returns NULL when name is "list" (caller must print the list and exit).
 */
workload_fn_t resolve_workload(const char *name);

/* list_workloads - print a table of all workloads to stdout */
void list_workloads(void);

#endif /* _CPU_WORKLOADS_H_ */
