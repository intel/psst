/*
 * cpu_workloads.c: pluggable SSE/AVX workload functions for psst.
 *
 * Each workload category exercises a distinct class of x86 SIMD execution
 * units.  Per-function GCC target attributes enable the required ISA without
 * requiring global -mavx/-mavx512f CFLAGS, so the binary runs on any x86-64
 * host and only dispatches advanced paths after a runtime CPUID check.
 *
 * Each function uses 8 independent accumulator chains to expose maximum
 * instruction-level parallelism and fill all available SIMD execution ports.
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
 * Note: GCC >= 4.9 is required for __attribute__((target())) on individual
 * functions.  GCC >= 5 is required for AVX-512 target attribute support.
 */

#define _GNU_SOURCE
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cpu_workloads.h"

/*
 * Iterations per cpu_work(): keep compute-time low but with meaningful work.
 */
#define WL_ITERS 128

/*
 * Volatile sinks prevent the compiler from dead-code-eliminating the SIMD
 * computation even under -O2.  A single write per call is sufficient.
 */
static volatile float    wl_sink_f;
static volatile double   wl_sink_d;
static volatile uint64_t wl_sink_i;

/* ------------------------------------------------------------------ */
/* Scalar workloads — no SIMD, always available                         */
/* ------------------------------------------------------------------ */

static void workload_scalar_int(void)
{
	uint64_t a  = 0x0102030405060708ULL;
	uint64_t b  = 0xFEDCBA9876543210ULL;
	uint64_t c0 = 1, c1 = 2, c2 = 3, c3 = 4;
	uint64_t c4 = 5, c5 = 6, c6 = 7, c7 = 8;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = c0 * a + b;
		c1 = c1 * a + b;
		c2 = c2 * a + b;
		c3 = c3 * a + b;
		c4 = c4 * a + b;
		c5 = c5 * a + b;
		c6 = c6 * a + b;
		c7 = c7 * a + b;
	}
	wl_sink_i = c0 ^ c1 ^ c2 ^ c3 ^ c4 ^ c5 ^ c6 ^ c7;
}

static void workload_scalar_fp(void)
{
	double a  = 1.0000001, b  = 0.9999999;
	double c0 = 1.0, c1 = 2.0, c2 = 3.0, c3 = 4.0;
	double c4 = 5.0, c5 = 6.0, c6 = 7.0, c7 = 8.0;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = c0 * a + b;
		c1 = c1 * a + b;
		c2 = c2 * a + b;
		c3 = c3 * a + b;
		c4 = c4 * a + b;
		c5 = c5 * a + b;
		c6 = c6 * a + b;
		c7 = c7 * a + b;
	}
	wl_sink_d = c0 + c1 + c2 + c3 + c4 + c5 + c6 + c7;
}

/* ------------------------------------------------------------------ */
/* SSE workloads — 128-bit XMM registers                                */
/* ------------------------------------------------------------------ */

/* SSE: 4 x fp32 packed multiply-add */
__attribute__((target("sse")))
static void workload_sse_fp32(void)
{
	__m128 a  = _mm_set1_ps(1.0000001f);
	__m128 b  = _mm_set1_ps(0.9999999f);
	__m128 c0 = _mm_set1_ps(1.0f), c1 = _mm_set1_ps(2.0f);
	__m128 c2 = _mm_set1_ps(3.0f), c3 = _mm_set1_ps(4.0f);
	__m128 c4 = _mm_set1_ps(5.0f), c5 = _mm_set1_ps(6.0f);
	__m128 c6 = _mm_set1_ps(7.0f), c7 = _mm_set1_ps(8.0f);
	__m128 s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm_add_ps(_mm_mul_ps(c0, a), b);
		c1 = _mm_add_ps(_mm_mul_ps(c1, a), b);
		c2 = _mm_add_ps(_mm_mul_ps(c2, a), b);
		c3 = _mm_add_ps(_mm_mul_ps(c3, a), b);
		c4 = _mm_add_ps(_mm_mul_ps(c4, a), b);
		c5 = _mm_add_ps(_mm_mul_ps(c5, a), b);
		c6 = _mm_add_ps(_mm_mul_ps(c6, a), b);
		c7 = _mm_add_ps(_mm_mul_ps(c7, a), b);
	}
	s = _mm_add_ps(_mm_add_ps(_mm_add_ps(c0, c1), _mm_add_ps(c2, c3)),
	               _mm_add_ps(_mm_add_ps(c4, c5), _mm_add_ps(c6, c7)));
	wl_sink_f = _mm_cvtss_f32(s);
}

/* SSE2: 2 x fp64 packed multiply-add */
__attribute__((target("sse2")))
static void workload_sse_fp64(void)
{
	__m128d a  = _mm_set1_pd(1.0000001);
	__m128d b  = _mm_set1_pd(0.9999999);
	__m128d c0 = _mm_set1_pd(1.0), c1 = _mm_set1_pd(2.0);
	__m128d c2 = _mm_set1_pd(3.0), c3 = _mm_set1_pd(4.0);
	__m128d c4 = _mm_set1_pd(5.0), c5 = _mm_set1_pd(6.0);
	__m128d c6 = _mm_set1_pd(7.0), c7 = _mm_set1_pd(8.0);
	__m128d s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm_add_pd(_mm_mul_pd(c0, a), b);
		c1 = _mm_add_pd(_mm_mul_pd(c1, a), b);
		c2 = _mm_add_pd(_mm_mul_pd(c2, a), b);
		c3 = _mm_add_pd(_mm_mul_pd(c3, a), b);
		c4 = _mm_add_pd(_mm_mul_pd(c4, a), b);
		c5 = _mm_add_pd(_mm_mul_pd(c5, a), b);
		c6 = _mm_add_pd(_mm_mul_pd(c6, a), b);
		c7 = _mm_add_pd(_mm_mul_pd(c7, a), b);
	}
	s = _mm_add_pd(_mm_add_pd(_mm_add_pd(c0, c1), _mm_add_pd(c2, c3)),
	               _mm_add_pd(_mm_add_pd(c4, c5), _mm_add_pd(c6, c7)));
	wl_sink_d = _mm_cvtsd_f64(s);
}

/* SSE2: 16 x int8 packed saturating add */
__attribute__((target("sse2")))
static void workload_sse_int8(void)
{
	__m128i a  = _mm_set1_epi8(3);
	__m128i b  = _mm_set1_epi8(7);
	__m128i c0 = _mm_set1_epi8(1), c1 = _mm_set1_epi8(2);
	__m128i c2 = _mm_set1_epi8(3), c3 = _mm_set1_epi8(4);
	__m128i c4 = _mm_set1_epi8(5), c5 = _mm_set1_epi8(6);
	__m128i c6 = _mm_set1_epi8(7), c7 = _mm_set1_epi8(8);
	__m128i s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm_add_epi8(_mm_adds_epi8(c0, a), b);
		c1 = _mm_add_epi8(_mm_adds_epi8(c1, a), b);
		c2 = _mm_add_epi8(_mm_adds_epi8(c2, a), b);
		c3 = _mm_add_epi8(_mm_adds_epi8(c3, a), b);
		c4 = _mm_add_epi8(_mm_adds_epi8(c4, a), b);
		c5 = _mm_add_epi8(_mm_adds_epi8(c5, a), b);
		c6 = _mm_add_epi8(_mm_adds_epi8(c6, a), b);
		c7 = _mm_add_epi8(_mm_adds_epi8(c7, a), b);
	}
	s = _mm_add_epi8(_mm_add_epi8(_mm_add_epi8(c0, c1), _mm_add_epi8(c2, c3)),
	                 _mm_add_epi8(_mm_add_epi8(c4, c5), _mm_add_epi8(c6, c7)));
	wl_sink_i = (uint64_t)(uint32_t)_mm_cvtsi128_si32(s);
}

/* SSE4.1: 4 x int32 packed multiply-add */
__attribute__((target("sse4.1")))
static void workload_sse_int32(void)
{
	__m128i a  = _mm_set1_epi32(3);
	__m128i b  = _mm_set1_epi32(7);
	__m128i c0 = _mm_set1_epi32(1), c1 = _mm_set1_epi32(2);
	__m128i c2 = _mm_set1_epi32(3), c3 = _mm_set1_epi32(4);
	__m128i c4 = _mm_set1_epi32(5), c5 = _mm_set1_epi32(6);
	__m128i c6 = _mm_set1_epi32(7), c7 = _mm_set1_epi32(8);
	__m128i s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm_add_epi32(_mm_mullo_epi32(c0, a), b);
		c1 = _mm_add_epi32(_mm_mullo_epi32(c1, a), b);
		c2 = _mm_add_epi32(_mm_mullo_epi32(c2, a), b);
		c3 = _mm_add_epi32(_mm_mullo_epi32(c3, a), b);
		c4 = _mm_add_epi32(_mm_mullo_epi32(c4, a), b);
		c5 = _mm_add_epi32(_mm_mullo_epi32(c5, a), b);
		c6 = _mm_add_epi32(_mm_mullo_epi32(c6, a), b);
		c7 = _mm_add_epi32(_mm_mullo_epi32(c7, a), b);
	}
	s = _mm_add_epi32(_mm_add_epi32(_mm_add_epi32(c0, c1), _mm_add_epi32(c2, c3)),
	                  _mm_add_epi32(_mm_add_epi32(c4, c5), _mm_add_epi32(c6, c7)));
	wl_sink_i = (uint64_t)(uint32_t)_mm_cvtsi128_si32(s);
}

/* ------------------------------------------------------------------ */
/* AVX workloads — 256-bit YMM registers                                */
/* ------------------------------------------------------------------ */

/* AVX: 8 x fp32 packed multiply-add */
__attribute__((target("avx")))
static void workload_avx_fp32(void)
{
	__m256 a  = _mm256_set1_ps(1.0000001f);
	__m256 b  = _mm256_set1_ps(0.9999999f);
	__m256 c0 = _mm256_set1_ps(1.0f), c1 = _mm256_set1_ps(2.0f);
	__m256 c2 = _mm256_set1_ps(3.0f), c3 = _mm256_set1_ps(4.0f);
	__m256 c4 = _mm256_set1_ps(5.0f), c5 = _mm256_set1_ps(6.0f);
	__m256 c6 = _mm256_set1_ps(7.0f), c7 = _mm256_set1_ps(8.0f);
	__m256 s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm256_add_ps(_mm256_mul_ps(c0, a), b);
		c1 = _mm256_add_ps(_mm256_mul_ps(c1, a), b);
		c2 = _mm256_add_ps(_mm256_mul_ps(c2, a), b);
		c3 = _mm256_add_ps(_mm256_mul_ps(c3, a), b);
		c4 = _mm256_add_ps(_mm256_mul_ps(c4, a), b);
		c5 = _mm256_add_ps(_mm256_mul_ps(c5, a), b);
		c6 = _mm256_add_ps(_mm256_mul_ps(c6, a), b);
		c7 = _mm256_add_ps(_mm256_mul_ps(c7, a), b);
	}
	s = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(c0, c1), _mm256_add_ps(c2, c3)),
	                  _mm256_add_ps(_mm256_add_ps(c4, c5), _mm256_add_ps(c6, c7)));
	wl_sink_f = _mm_cvtss_f32(_mm256_castps256_ps128(s));
}

/* AVX: 4 x fp64 packed multiply-add */
__attribute__((target("avx")))
static void workload_avx_fp64(void)
{
	__m256d a  = _mm256_set1_pd(1.0000001);
	__m256d b  = _mm256_set1_pd(0.9999999);
	__m256d c0 = _mm256_set1_pd(1.0), c1 = _mm256_set1_pd(2.0);
	__m256d c2 = _mm256_set1_pd(3.0), c3 = _mm256_set1_pd(4.0);
	__m256d c4 = _mm256_set1_pd(5.0), c5 = _mm256_set1_pd(6.0);
	__m256d c6 = _mm256_set1_pd(7.0), c7 = _mm256_set1_pd(8.0);
	__m256d s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm256_add_pd(_mm256_mul_pd(c0, a), b);
		c1 = _mm256_add_pd(_mm256_mul_pd(c1, a), b);
		c2 = _mm256_add_pd(_mm256_mul_pd(c2, a), b);
		c3 = _mm256_add_pd(_mm256_mul_pd(c3, a), b);
		c4 = _mm256_add_pd(_mm256_mul_pd(c4, a), b);
		c5 = _mm256_add_pd(_mm256_mul_pd(c5, a), b);
		c6 = _mm256_add_pd(_mm256_mul_pd(c6, a), b);
		c7 = _mm256_add_pd(_mm256_mul_pd(c7, a), b);
	}
	s = _mm256_add_pd(_mm256_add_pd(_mm256_add_pd(c0, c1), _mm256_add_pd(c2, c3)),
	                  _mm256_add_pd(_mm256_add_pd(c4, c5), _mm256_add_pd(c6, c7)));
	wl_sink_d = _mm_cvtsd_f64(_mm256_castpd256_pd128(s));
}

/* AVX2: 8 x int32 packed multiply-add */
__attribute__((target("avx2")))
static void workload_avx2_int(void)
{
	__m256i a  = _mm256_set1_epi32(3);
	__m256i b  = _mm256_set1_epi32(7);
	__m256i c0 = _mm256_set1_epi32(1), c1 = _mm256_set1_epi32(2);
	__m256i c2 = _mm256_set1_epi32(3), c3 = _mm256_set1_epi32(4);
	__m256i c4 = _mm256_set1_epi32(5), c5 = _mm256_set1_epi32(6);
	__m256i c6 = _mm256_set1_epi32(7), c7 = _mm256_set1_epi32(8);
	__m256i s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm256_add_epi32(_mm256_mullo_epi32(c0, a), b);
		c1 = _mm256_add_epi32(_mm256_mullo_epi32(c1, a), b);
		c2 = _mm256_add_epi32(_mm256_mullo_epi32(c2, a), b);
		c3 = _mm256_add_epi32(_mm256_mullo_epi32(c3, a), b);
		c4 = _mm256_add_epi32(_mm256_mullo_epi32(c4, a), b);
		c5 = _mm256_add_epi32(_mm256_mullo_epi32(c5, a), b);
		c6 = _mm256_add_epi32(_mm256_mullo_epi32(c6, a), b);
		c7 = _mm256_add_epi32(_mm256_mullo_epi32(c7, a), b);
	}
	s = _mm256_add_epi32(
		_mm256_add_epi32(_mm256_add_epi32(c0, c1), _mm256_add_epi32(c2, c3)),
		_mm256_add_epi32(_mm256_add_epi32(c4, c5), _mm256_add_epi32(c6, c7)));
	wl_sink_i = (uint64_t)(uint32_t)_mm_cvtsi128_si32(_mm256_castsi256_si128(s));
}

/* ------------------------------------------------------------------ */
/* FMA workloads — 256-bit fused multiply-add (FMA3 + AVX)              */
/* ------------------------------------------------------------------ */

/* FMA3: 8 x fp32 fused multiply-add */
__attribute__((target("avx,fma")))
static void workload_fma_fp32(void)
{
	__m256 a  = _mm256_set1_ps(1.0000001f);
	__m256 b  = _mm256_set1_ps(0.9999999f);
	__m256 c0 = _mm256_set1_ps(1.0f), c1 = _mm256_set1_ps(2.0f);
	__m256 c2 = _mm256_set1_ps(3.0f), c3 = _mm256_set1_ps(4.0f);
	__m256 c4 = _mm256_set1_ps(5.0f), c5 = _mm256_set1_ps(6.0f);
	__m256 c6 = _mm256_set1_ps(7.0f), c7 = _mm256_set1_ps(8.0f);
	__m256 s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm256_fmadd_ps(c0, a, b);
		c1 = _mm256_fmadd_ps(c1, a, b);
		c2 = _mm256_fmadd_ps(c2, a, b);
		c3 = _mm256_fmadd_ps(c3, a, b);
		c4 = _mm256_fmadd_ps(c4, a, b);
		c5 = _mm256_fmadd_ps(c5, a, b);
		c6 = _mm256_fmadd_ps(c6, a, b);
		c7 = _mm256_fmadd_ps(c7, a, b);
	}
	s = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(c0, c1), _mm256_add_ps(c2, c3)),
	                  _mm256_add_ps(_mm256_add_ps(c4, c5), _mm256_add_ps(c6, c7)));
	wl_sink_f = _mm_cvtss_f32(_mm256_castps256_ps128(s));
}

/* FMA3: 4 x fp64 fused multiply-add */
__attribute__((target("avx,fma")))
static void workload_fma_fp64(void)
{
	__m256d a  = _mm256_set1_pd(1.0000001);
	__m256d b  = _mm256_set1_pd(0.9999999);
	__m256d c0 = _mm256_set1_pd(1.0), c1 = _mm256_set1_pd(2.0);
	__m256d c2 = _mm256_set1_pd(3.0), c3 = _mm256_set1_pd(4.0);
	__m256d c4 = _mm256_set1_pd(5.0), c5 = _mm256_set1_pd(6.0);
	__m256d c6 = _mm256_set1_pd(7.0), c7 = _mm256_set1_pd(8.0);
	__m256d s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm256_fmadd_pd(c0, a, b);
		c1 = _mm256_fmadd_pd(c1, a, b);
		c2 = _mm256_fmadd_pd(c2, a, b);
		c3 = _mm256_fmadd_pd(c3, a, b);
		c4 = _mm256_fmadd_pd(c4, a, b);
		c5 = _mm256_fmadd_pd(c5, a, b);
		c6 = _mm256_fmadd_pd(c6, a, b);
		c7 = _mm256_fmadd_pd(c7, a, b);
	}
	s = _mm256_add_pd(_mm256_add_pd(_mm256_add_pd(c0, c1), _mm256_add_pd(c2, c3)),
	                  _mm256_add_pd(_mm256_add_pd(c4, c5), _mm256_add_pd(c6, c7)));
	wl_sink_d = _mm_cvtsd_f64(_mm256_castpd256_pd128(s));
}

/* ------------------------------------------------------------------ */
/* AVX-512 workloads — 512-bit ZMM registers (AVX-512F)                 */
/* ------------------------------------------------------------------ */

/* AVX-512F: 16 x fp32 fused multiply-add */
__attribute__((target("avx512f")))
static void workload_avx512_fp32(void)
{
	__m512 a  = _mm512_set1_ps(1.0000001f);
	__m512 b  = _mm512_set1_ps(0.9999999f);
	__m512 c0 = _mm512_set1_ps(1.0f), c1 = _mm512_set1_ps(2.0f);
	__m512 c2 = _mm512_set1_ps(3.0f), c3 = _mm512_set1_ps(4.0f);
	__m512 c4 = _mm512_set1_ps(5.0f), c5 = _mm512_set1_ps(6.0f);
	__m512 c6 = _mm512_set1_ps(7.0f), c7 = _mm512_set1_ps(8.0f);
	__m512 s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm512_fmadd_ps(c0, a, b);
		c1 = _mm512_fmadd_ps(c1, a, b);
		c2 = _mm512_fmadd_ps(c2, a, b);
		c3 = _mm512_fmadd_ps(c3, a, b);
		c4 = _mm512_fmadd_ps(c4, a, b);
		c5 = _mm512_fmadd_ps(c5, a, b);
		c6 = _mm512_fmadd_ps(c6, a, b);
		c7 = _mm512_fmadd_ps(c7, a, b);
	}
	s = _mm512_add_ps(_mm512_add_ps(_mm512_add_ps(c0, c1), _mm512_add_ps(c2, c3)),
	                  _mm512_add_ps(_mm512_add_ps(c4, c5), _mm512_add_ps(c6, c7)));
	wl_sink_f = _mm_cvtss_f32(_mm512_castps512_ps128(s));
}

/* AVX-512F: 8 x fp64 fused multiply-add */
__attribute__((target("avx512f")))
static void workload_avx512_fp64(void)
{
	__m512d a  = _mm512_set1_pd(1.0000001);
	__m512d b  = _mm512_set1_pd(0.9999999);
	__m512d c0 = _mm512_set1_pd(1.0), c1 = _mm512_set1_pd(2.0);
	__m512d c2 = _mm512_set1_pd(3.0), c3 = _mm512_set1_pd(4.0);
	__m512d c4 = _mm512_set1_pd(5.0), c5 = _mm512_set1_pd(6.0);
	__m512d c6 = _mm512_set1_pd(7.0), c7 = _mm512_set1_pd(8.0);
	__m512d s;
	int i;

	for (i = 0; i < WL_ITERS; i++) {
		c0 = _mm512_fmadd_pd(c0, a, b);
		c1 = _mm512_fmadd_pd(c1, a, b);
		c2 = _mm512_fmadd_pd(c2, a, b);
		c3 = _mm512_fmadd_pd(c3, a, b);
		c4 = _mm512_fmadd_pd(c4, a, b);
		c5 = _mm512_fmadd_pd(c5, a, b);
		c6 = _mm512_fmadd_pd(c6, a, b);
		c7 = _mm512_fmadd_pd(c7, a, b);
	}
	s = _mm512_add_pd(_mm512_add_pd(_mm512_add_pd(c0, c1), _mm512_add_pd(c2, c3)),
	                  _mm512_add_pd(_mm512_add_pd(c4, c5), _mm512_add_pd(c6, c7)));
	wl_sink_d = _mm_cvtsd_f64(_mm512_castpd512_pd128(s));
}

/* ------------------------------------------------------------------ */
/* Workload dispatch table                                               */
/* ------------------------------------------------------------------ */

struct workload_desc {
	const char    *name;
	const char    *desc;
	workload_fn_t  fn;
	const char    *cpu_feature; /* __builtin_cpu_supports() key, NULL = always */
};

static const struct workload_desc workload_table[] = {
	/* Scalar — no ISA requirement */
	{ "scalar-int",   "64-bit integer multiply-accumulate (no SIMD)",
	  workload_scalar_int,   NULL        },
	{ "scalar-fp",    "64-bit double-precision scalar FMA",
	  workload_scalar_fp,    NULL        },

	/* SSE — 128-bit XMM */
	{ "sse-fp32",     "128-bit packed single-precision FP (SSE)",
	  workload_sse_fp32,     "sse"       },
	{ "sse-fp64",     "128-bit packed double-precision FP (SSE2)",
	  workload_sse_fp64,     "sse2"      },
	{ "sse-int8",     "128-bit packed 8-bit integer arithmetic (SSE2)",
	  workload_sse_int8,     "sse2"      },
	{ "sse-int32",    "128-bit packed 32-bit integer multiply (SSE4.1)",
	  workload_sse_int32,    "sse4.1"    },

	/* AVX — 256-bit YMM */
	{ "avx-fp32",     "256-bit packed single-precision FP (AVX)",
	  workload_avx_fp32,     "avx"       },
	{ "avx-fp64",     "256-bit packed double-precision FP (AVX)",
	  workload_avx_fp64,     "avx"       },
	{ "avx2-int",     "256-bit packed 32-bit integer multiply (AVX2)",
	  workload_avx2_int,     "avx2"      },

	/* FMA — 256-bit fused multiply-add */
	{ "fma-fp32",     "256-bit packed single-precision FMA (FMA3+AVX)",
	  workload_fma_fp32,     "fma"       },
	{ "fma-fp64",     "256-bit packed double-precision FMA (FMA3+AVX)",
	  workload_fma_fp64,     "fma"       },

	/* AVX-512 — 512-bit ZMM */
	{ "avx512-fp32",  "512-bit packed single-precision FMA (AVX-512F)",
	  workload_avx512_fp32,  "avx512f"   },
	{ "avx512-fp64",  "512-bit packed double-precision FMA (AVX-512F)",
	  workload_avx512_fp64,  "avx512f"   },
};

#define WORKLOAD_COUNT  ((int)(sizeof(workload_table) / sizeof(workload_table[0])))

void list_workloads(void)
{
	int i;

	printf("Available CPU workloads (-w <name>):\n");
	printf("  %-16s  %-12s  %s\n", "Name", "Requires", "Description");
	printf("  %-16s  %-12s  %s\n",
	       "----------------", "------------",
	       "-------------------------------------------");
	for (i = 0; i < WORKLOAD_COUNT; i++) {
		const char *feat = workload_table[i].cpu_feature
		                   ? workload_table[i].cpu_feature
		                   : "always";
		printf("  %-16s  %-12s  %s\n",
		       workload_table[i].name, feat,
		       workload_table[i].desc);
	}
}

/*
 * workload_cpu_supported - runtime CPUID check for a workload table entry.
 *
 * __builtin_cpu_supports() requires a string literal, so we use a switch
 * that maps table indices to their corresponding literal feature strings.
 * The order must stay in sync with workload_table[] below.
 */
static int workload_cpu_supported(int idx)
{
	switch (idx) {
	case 0:  /* scalar-int  — no ISA requirement */
	case 1:  /* scalar-fp   — no ISA requirement */
		return 1;
	case 2:  /* sse-fp32  */
		return __builtin_cpu_supports("sse");
	case 3:  /* sse-fp64  */
	case 4:  /* sse-int8  */
		return __builtin_cpu_supports("sse2");
	case 5:  /* sse-int32 */
		return __builtin_cpu_supports("sse4.1");
	case 6:  /* avx-fp32  */
	case 7:  /* avx-fp64  */
		return __builtin_cpu_supports("avx");
	case 8:  /* avx2-int  */
		return __builtin_cpu_supports("avx2");
	case 9:  /* fma-fp32  */
	case 10: /* fma-fp64  */
		return __builtin_cpu_supports("fma");
	case 11: /* avx512-fp32 */
	case 12: /* avx512-fp64 */
		return __builtin_cpu_supports("avx512f");
	default:
		return 0;
	}
}

workload_fn_t resolve_workload(const char *name)
{
	int i;

	/* NULL or empty name → default */
	if (!name || !name[0])
		return workload_table[0].fn;

	/* Special keyword: print table and signal caller to exit */
	if (strcmp(name, "list") == 0) {
		list_workloads();
		return NULL;
	}

	for (i = 0; i < WORKLOAD_COUNT; i++) {
		if (strcmp(workload_table[i].name, name) != 0)
			continue;

		/* Runtime CPUID check via literal-string helper */
		if (!workload_cpu_supported(i)) {
			fprintf(stderr,
			        "psst: CPU does not support '%s' (requires %s); "
			        "falling back to scalar-int\n",
			        name, workload_table[i].cpu_feature);
			return workload_table[0].fn;
		}
		return workload_table[i].fn;
	}

	fprintf(stderr,
	        "psst: unknown workload '%s'; use -w list for available options\n",
	        name);
	return workload_table[0].fn;
}
