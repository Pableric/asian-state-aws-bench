#ifndef CPP_BENCH_AB_H
#define CPP_BENCH_AB_H

#include "cpp_provider.h"

#include <stddef.h>
#include <stdint.h>

/* One timed pass: for each block b in [0, nblocks), for each dimension d in
 * [0, ndims): 128 destination-ordered packets of block block_first + b,
 * consumed by the selected consumer. A and B read the same donor bank,
 * visit the same (block, dimension, packet) order and run the same consumer.
 * Offsets are mirrored by .equ constants in bench_kernels.S. */
typedef struct {
    const uint32_t *donor_bank;       /*  0: nblocks x 4096 values */
    const void *a_contexts;           /*  8: A: [b][d] 448-byte contexts */
    const cpp_dim_record_t *records;  /* 16: B: [d] 192-byte records */
    uint32_t *block_state;            /* 24: B: [d] state of the previous block */
    float *consumer_state;            /* 32: fused: S[4096], Q[4096], L[4096] */
    uint32_t *sink;                   /* 40: sink: two ZMM accumulators */
    uint64_t block_first;             /* 48 */
    uint64_t nblocks;                 /* 56 */
    uint64_t ndims;                   /* 64 */
    const float *weights;             /* 72: fused: [d] FMA weight */
} bench_job_t;

_Static_assert(offsetof(bench_job_t, donor_bank) == 0, "job layout");
_Static_assert(offsetof(bench_job_t, a_contexts) == 8, "job layout");
_Static_assert(offsetof(bench_job_t, records) == 16, "job layout");
_Static_assert(offsetof(bench_job_t, block_state) == 24, "job layout");
_Static_assert(offsetof(bench_job_t, consumer_state) == 32, "job layout");
_Static_assert(offsetof(bench_job_t, sink) == 40, "job layout");
_Static_assert(offsetof(bench_job_t, block_first) == 48, "job layout");
_Static_assert(offsetof(bench_job_t, nblocks) == 56, "job layout");
_Static_assert(offsetof(bench_job_t, ndims) == 64, "job layout");
_Static_assert(offsetof(bench_job_t, weights) == 72, "job layout");

void bench_a_sink(const bench_job_t *job);
void bench_b_sink(const bench_job_t *job);
void bench_a_fused(const bench_job_t *job);
void bench_b_fused(const bench_job_t *job);

#endif
