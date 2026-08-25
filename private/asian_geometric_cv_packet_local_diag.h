#ifndef ASIAN_GEOMETRIC_CV_PACKET_LOCAL_DIAG_H
#define ASIAN_GEOMETRIC_CV_PACKET_LOCAL_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_genuine_price_delta_strip_diag.h"
#include "asian_meta_direction_affine_route.h"

#define ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAGIC UINT32_C(0x47514c50)
#define ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ABI_VERSION UINT16_C(1)

enum {
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_PATHS = 4096,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_PACKETS = 128,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MIN_FIXINGS = 2,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAX_FIXINGS = 256,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_DONOR_BYTES = 8192 * 4,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_OUTPUT_BYTES = 4096 * 4,
};

enum asian_geometric_cv_packet_local_status {
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_OK = 0,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_INVALID = -1,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_FIXINGS_UNSUPPORTED = -2,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ALIGNMENT = -3,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ALIAS = -4,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ROUTE_INVALID = -5,
    ASIAN_GEOMETRIC_CV_PACKET_LOCAL_DOMAIN = -6,
};

/*
 * The ranked leaf consumes bytes 0..55.  D1 weight and terminal log-base are
 * copied bit-for-bit from the qualified baseline preparation.  Metadata is
 * cold and is not referenced by either hot loop.
 */
typedef struct __attribute__((aligned(64))) {
    const float *d1_x;
    const float *d1_growth;
    const asian_meta_affine_route_t *routes_d2;
    float *q_out;
    float *g_out;
    uint32_t fixing_count;
    float s0;
    uint32_t d1_weight_bits;
    uint32_t terminal_log_base_bits;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved;
} asian_geometric_cv_packet_local_context_t;

_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t, d1_x) == 0,
               "packet-local D1 x offset");
_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t, d1_growth) == 8,
               "packet-local D1 growth offset");
_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t, routes_d2) == 16,
               "packet-local D2 routes offset");
_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t, q_out) == 24,
               "packet-local Q output offset");
_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t, g_out) == 32,
               "packet-local G output offset");
_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t, fixing_count) == 40,
               "packet-local fixing count offset");
_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t, s0) == 44,
               "packet-local S0 offset");
_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t, d1_weight_bits) == 48,
               "packet-local D1 weight offset");
_Static_assert(offsetof(asian_geometric_cv_packet_local_context_t,
                        terminal_log_base_bits) == 52,
               "packet-local terminal base offset");
_Static_assert(sizeof(asian_geometric_cv_packet_local_context_t) == 64,
               "packet-local context size");

typedef struct __attribute__((aligned(64))) {
    float s[ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAX_FIXINGS][32];
    float q[ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAX_FIXINGS][32];
    float l[ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAX_FIXINGS][32];
} asian_geometric_cv_packet_local_packet_trace_t;

int asian_geometric_cv_packet_local_prepare(
    asian_geometric_cv_packet_local_context_t *out,
    const asian_meta_affine_route_t *qualified_routes, uint32_t fixing_count,
    float s0, const float *x_donors, size_t x_donor_bytes,
    const float *growth_donors, size_t growth_donor_bytes,
    const asian_genuine_strip_context_t *qualified_strip,
    float *q_out, size_t q_out_bytes, float *g_out, size_t g_out_bytes);

int asian_geometric_cv_packet_local_strip_prepare_padded(
    asian_genuine_strip_context_t *out,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t future_fixings, uint32_t completed_fixings,
    double initial_q, double past_log_sum,
    const float *strikes, uint32_t strike_count, uint32_t *padded_count);

int asian_geometric_cv_packet_local_strip_consume_padded(
    const float q_future[ASIAN_GEOMETRIC_CV_PACKET_LOCAL_PATHS],
    const float g[ASIAN_GEOMETRIC_CV_PACKET_LOCAL_PATHS],
    const asian_genuine_strip_context_t *context,
    uint32_t requested_count, int price_delta,
    asian_genuine_strip_output_t *output);

void asian_geometric_cv_packet_local_qg_diag(
    const asian_geometric_cv_packet_local_context_t *context);

void asian_geometric_cv_packet_local_packet_probe_diag(
    const asian_geometric_cv_packet_local_context_t *context,
    uint32_t packet, asian_geometric_cv_packet_local_packet_trace_t *trace);

#endif
