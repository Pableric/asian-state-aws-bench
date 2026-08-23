#ifndef ASIAN_GEOMETRIC_CV_IMMEDIATE_DIAG_H
#define ASIAN_GEOMETRIC_CV_IMMEDIATE_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_geometric_cv_packet_local_diag.h"

#define ASIAN_GEOMETRIC_CV_IMMEDIATE_MAGIC UINT32_C(0x49435647)
#define ASIAN_GEOMETRIC_CV_IMMEDIATE_ABI_VERSION UINT16_C(1)

enum asian_geometric_cv_immediate_status {
    ASIAN_GEOMETRIC_CV_IMMEDIATE_OK = 0,
    ASIAN_GEOMETRIC_CV_IMMEDIATE_INVALID = -1,
    ASIAN_GEOMETRIC_CV_IMMEDIATE_ALIGNMENT = -2,
    ASIAN_GEOMETRIC_CV_IMMEDIATE_CONTEXT = -3,
    ASIAN_GEOMETRIC_CV_IMMEDIATE_FALLBACK = -4,
};

/*
 * Bytes 0..39 are the complete ranked-leaf input.  The preparation copies the
 * two qualified binary32 seasoning values without recomputing either one.
 * In particular, the parent Q/G output pointers are never copied or read.
 */
typedef struct __attribute__((aligned(64))) {
    const float *d1_x;
    const float *d1_growth;
    const asian_genuine_route_t *routes_d2;
    uint32_t fixing_count;
    float s0;
    uint32_t d1_weight_bits;
    uint32_t terminal_log_base_bits;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved0;
    uint8_t reserved1[16];
} asian_geometric_cv_immediate_context_t;

_Static_assert(offsetof(asian_geometric_cv_immediate_context_t,d1_x)==0,
               "immediate D1 x offset");
_Static_assert(offsetof(asian_geometric_cv_immediate_context_t,d1_growth)==8,
               "immediate D1 growth offset");
_Static_assert(offsetof(asian_geometric_cv_immediate_context_t,routes_d2)==16,
               "immediate routes offset");
_Static_assert(offsetof(asian_geometric_cv_immediate_context_t,fixing_count)==24,
               "immediate fixing count offset");
_Static_assert(offsetof(asian_geometric_cv_immediate_context_t,s0)==28,
               "immediate S0 offset");
_Static_assert(offsetof(asian_geometric_cv_immediate_context_t,d1_weight_bits)==32,
               "immediate D1 weight offset");
_Static_assert(offsetof(asian_geometric_cv_immediate_context_t,
                        terminal_log_base_bits)==36,
               "immediate terminal base offset");
_Static_assert(sizeof(asian_geometric_cv_immediate_context_t)==64,
               "immediate context size");

int asian_geometric_cv_immediate_prepare(
    asian_geometric_cv_immediate_context_t *out,
    const asian_geometric_cv_packet_local_context_t *qualified,
    const asian_genuine_strip_context_t *strip);

int asian_geometric_cv_immediate_run(
    const asian_geometric_cv_immediate_context_t *context,
    const asian_genuine_strip_context_t *strip,uint32_t requested_count,
    int price_delta,asian_genuine_strip_output_t *output);

/* SysV-safe invocation thunks for the private extended-clobber leaves. */
#define ASIAN_IMMEDIATE_INVOKE_DECL(mode,width) \
void asian_geometric_cv_immediate_invoke_##mode##_##width( \
    const asian_geometric_cv_immediate_context_t *, \
    const asian_genuine_strip_context_t *, \
    const asian_genuine_strip_strike_t *,asian_genuine_strip_output_t *)

ASIAN_IMMEDIATE_INVOKE_DECL(price,1);
ASIAN_IMMEDIATE_INVOKE_DECL(price_delta,1);
ASIAN_IMMEDIATE_INVOKE_DECL(price,2);
ASIAN_IMMEDIATE_INVOKE_DECL(price_delta,2);
ASIAN_IMMEDIATE_INVOKE_DECL(price,4);
ASIAN_IMMEDIATE_INVOKE_DECL(price_delta,4);
#undef ASIAN_IMMEDIATE_INVOKE_DECL

#endif
