#ifndef ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DIAG_H
#define ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_genuine_permute.h"
#include "asian_genuine_price_delta_strip_diag.h"

#define ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC UINT32_C(0x51474141)
#define ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION UINT16_C(1)

enum {
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_PATHS = 4096,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_PACKETS = 128,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MIN_FIXINGS = 2,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAX_FIXINGS = 256,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DONOR_VALUES = 8192,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DONOR_BYTES = 8192 * 4,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_Q_BYTES = 4096 * 4,
};

enum asian_genuine_arithmetic_growth_only_status {
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_OK = 0,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID = -1,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_FIXINGS_UNSUPPORTED = -2,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIGNMENT = -3,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIAS = -4,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ROUTE_INVALID = -5,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DOMAIN = -6,
};

/*
 * One private cache line.  The Stage-1 Q-materializing leaf reads bytes
 * 0..31.  Immediate-consumer leaves read bytes 0..15 and 24..31 only: they
 * never read q_out at offset 16.  Magic, version and validation metadata are
 * cold and are never referenced in ranked packet or route loops.
 */
typedef struct __attribute__((aligned(64))) {
    const float *d1_growth;
    const asian_genuine_route_t *routes_d2;
    float *q_out;
    uint32_t fixing_count;
    float s0;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved0;
    uint8_t reserved[24];
} asian_genuine_arithmetic_growth_only_context_t;

_Static_assert(offsetof(asian_genuine_arithmetic_growth_only_context_t,
                        d1_growth) == 0, "D1 growth ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_growth_only_context_t,
                        routes_d2) == 8, "D2 route ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_growth_only_context_t,
                        q_out) == 16, "Q output ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_growth_only_context_t,
                        fixing_count) == 24, "fixing count ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_growth_only_context_t,
                        s0) == 28, "S0 ABI");
_Static_assert(sizeof(asian_genuine_arithmetic_growth_only_context_t) == 64,
               "growth-only hot context must be one cache line");

/* Test-only storage.  Ranked leaves never receive or materialize this. */
typedef struct __attribute__((aligned(64))) {
    float growth[ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAX_FIXINGS][32];
    float s[ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAX_FIXINGS][32];
    float q[ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAX_FIXINGS][32];
} asian_genuine_arithmetic_growth_only_packet_trace_t;

int asian_genuine_arithmetic_growth_only_prepare(
    asian_genuine_arithmetic_growth_only_context_t *out,
    const asian_genuine_route_t *routes, uint32_t fixing_count,
    float s0, const float *growth_donors, size_t growth_donor_bytes,
    float *q_out, size_t q_out_bytes);

void asian_genuine_arithmetic_growth_only_q_diag(
    const asian_genuine_arithmetic_growth_only_context_t *context);

/*
 * Private fixed-form immediate consumers.  The final scalar output-record
 * stores are required; no leaf stores or reloads Q or any vector state.
 */
typedef void (*asian_genuine_arithmetic_growth_only_immediate_leaf_t)(
    const asian_genuine_arithmetic_growth_only_context_t *growth_context,
    const asian_genuine_strip_context_t *strip_context,
    const asian_genuine_strip_strike_t *strikes,
    asian_genuine_strip_value_t *output);

void asian_genuine_arithmetic_growth_only_price_1_diag(
    const asian_genuine_arithmetic_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_genuine_arithmetic_growth_only_price_delta_1_diag(
    const asian_genuine_arithmetic_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_genuine_arithmetic_growth_only_price_2_diag(
    const asian_genuine_arithmetic_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_genuine_arithmetic_growth_only_price_delta_2_diag(
    const asian_genuine_arithmetic_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_genuine_arithmetic_growth_only_price_4_diag(
    const asian_genuine_arithmetic_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_genuine_arithmetic_growth_only_price_delta_4_diag(
    const asian_genuine_arithmetic_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);

void asian_genuine_arithmetic_growth_only_packet_probe_diag(
    const asian_genuine_arithmetic_growth_only_context_t *context,
    uint32_t packet,
    asian_genuine_arithmetic_growth_only_packet_trace_t *trace);

#endif
