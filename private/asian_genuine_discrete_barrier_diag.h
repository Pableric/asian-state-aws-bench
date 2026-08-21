#ifndef ASIAN_GENUINE_DISCRETE_BARRIER_DIAG_H
#define ASIAN_GENUINE_DISCRETE_BARRIER_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "../asian_genuine_discrete_barrier_carrier/private/asian_genuine_permute.h"

enum {
    ASIAN_BARRIER_PATHS = 4096,
    ASIAN_BARRIER_PACKETS = 128,
    ASIAN_BARRIER_HALF_MASKS = 256,
    ASIAN_BARRIER_MASK_TABLE_BYTES = 512,
    ASIAN_BARRIER_MAX_MONITORING = 256
};

typedef struct __attribute__((aligned(16))) {
    const float *growth_base;
    const fragment_map_t *map;
} asian_barrier_growth_route_t;

_Static_assert(sizeof(asian_barrier_growth_route_t) == 16,
               "compact growth route ABI");

typedef struct __attribute__((aligned(64))) {
    const asian_barrier_growth_route_t *routes; /* D2..DN; entry zero is D2. */
    const float *d1_growth;                     /* D1 in chronological order. */
    uint16_t *mask_table;                       /* table challenger only. */
    uint32_t route_count;
    uint32_t monitoring_count;
    float initial_spot;
    float barrier;
    float strike;
    float reserved_f32;
    double payoff_scale;                        /* discount / 4096. */
    uint64_t reserved[1];
} asian_barrier_context_t;

_Static_assert(sizeof(asian_barrier_context_t) == 64,
               "barrier context cache-line ABI");
_Static_assert(offsetof(asian_barrier_context_t, routes) == 0,
               "routes ABI");
_Static_assert(offsetof(asian_barrier_context_t, d1_growth) == 8,
               "D1 ABI");
_Static_assert(offsetof(asian_barrier_context_t, mask_table) == 16,
               "mask table ABI");
_Static_assert(offsetof(asian_barrier_context_t, route_count) == 24,
               "route count ABI");
_Static_assert(offsetof(asian_barrier_context_t, initial_spot) == 32,
               "spot ABI");
_Static_assert(offsetof(asian_barrier_context_t, payoff_scale) == 48,
               "scale ABI");

int asian_barrier_prepare_compact(
    const asian_genuine_route_t *qualified_routes,
    uint32_t monitoring_count,
    float initial_spot,
    float barrier,
    float strike,
    double discount,
    uint16_t *mask_table,
    asian_barrier_growth_route_t *compact_routes,
    asian_barrier_context_t *context);

/* Stage-1 copied S/Q/L recurrence plus down-and-out observation. */
void asian_barrier_sql_resident_diag(const asian_genuine_route_t *, uint32_t,
                                     asian_genuine_state_t *, float,
                                     uint16_t final_masks[256]);
void asian_barrier_sql_table_diag(const asian_genuine_route_t *, uint32_t,
                                  asian_genuine_state_t *, float,
                                  uint16_t masks[256]);
void asian_barrier_s_only_probe_diag(const asian_barrier_context_t *,
                                     float terminal_s[4096],
                                     uint16_t final_masks[256]);
void asian_barrier_s_only_trace_diag(const asian_barrier_context_t *,
                                     float *date_major_s,
                                     uint16_t *date_major_masks);

/* Matched S-only vanilla leaves. */
double asian_barrier_vanilla_call_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_vanilla_put_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_vanilla_call_interleaved_diag(const asian_barrier_context_t *);
double asian_barrier_vanilla_put_interleaved_diag(const asian_barrier_context_t *);

/* Resident down-and-out challengers. */
double asian_barrier_down_call_self_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_down_put_self_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_down_call_self_interleaved_diag(const asian_barrier_context_t *);
double asian_barrier_down_put_self_interleaved_diag(const asian_barrier_context_t *);
double asian_barrier_down_call_explicit_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_down_put_explicit_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_down_call_explicit_interleaved_diag(const asian_barrier_context_t *);
double asian_barrier_down_put_explicit_interleaved_diag(const asian_barrier_context_t *);

/* Matched 512-byte mask-table challengers. */
double asian_barrier_down_call_table_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_down_put_table_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_down_call_table_interleaved_diag(const asian_barrier_context_t *);
double asian_barrier_down_put_table_interleaved_diag(const asian_barrier_context_t *);

/* Up-and-out is correctness-only in this package. */
double asian_barrier_up_call_self_grouped_diag(const asian_barrier_context_t *);
double asian_barrier_up_put_self_grouped_diag(const asian_barrier_context_t *);

/* Matched point-major oneMKL consumer; x contains drift+diffusion*Z. */
double asian_barrier_onemkl_point_major_down_call_diag(
    const float *x, const asian_barrier_context_t *);
double asian_barrier_onemkl_point_major_down_put_diag(
    const float *x, const asian_barrier_context_t *);

#endif
