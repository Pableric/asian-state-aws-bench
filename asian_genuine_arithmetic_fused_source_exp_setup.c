#include "private/asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "private/asian_genuine_arithmetic_growth_only_sha256.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int span(const void *pointer, size_t bytes, uintptr_t *first,
                uintptr_t *last)
{
    const uintptr_t begin = (uintptr_t)pointer;
    if (pointer == NULL || bytes == 0u || bytes - 1u > UINTPTR_MAX - begin)
        return -1;
    *first = begin;
    *last = begin + bytes;
    return 0;
}

static int overlap(const void *a, size_t a_bytes, const void *b, size_t b_bytes)
{
    uintptr_t af, al, bf, bl;
    if (span(a, a_bytes, &af, &al) != 0 ||
        span(b, b_bytes, &bf, &bl) != 0)
        return 1;
    return af < bl && bf < al;
}

int asian_genuine_arithmetic_fused_source_exp_prepare(
    asian_genuine_arithmetic_fused_source_exp_context_t *out,
    const asian_genuine_fixed_block_source_request_t *request,
    float *growth_out, size_t growth_out_bytes)
{
    static const char expected_sha256[] =
        "ecf3bb854e98bedcf724d0743438457ccf8b600e1264cb537741ce0b9d90d98d";
    asian_genuine_fixed_block_source_context_t qualified
        __attribute__((aligned(64)));
    char digest[65];

    if (out == NULL || request == NULL || growth_out == NULL)
        return ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_INVALID;
    memset(out, 0, sizeof(*out));
    if (((uintptr_t)out & 63u) != 0u ||
        ((uintptr_t)growth_out & 63u) != 0u)
        return ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ALIGNMENT;
    if (growth_out_bytes !=
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)
        return ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_INVALID;

    /*
     * Market admissibility remains exactly the qualified fixed-source and
     * vector-exp contract.  In particular, no new universal drift or
     * diffusion bounds are imposed here.
     */
    if (asian_genuine_fixed_block_source_prepare(&qualified, request) !=
        ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK)
        return ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_SOURCE_REJECTED;
    if (qualified.signed_z != asian_genuine_fixed_block_signed_z ||
        request->signed_z != asian_genuine_fixed_block_signed_z ||
        request->signed_z_bytes !=
          ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES ||
        asian_genuine_arithmetic_growth_only_memory_sha256(
          request->signed_z, request->signed_z_bytes, digest) != 0 ||
        strcmp(digest, expected_sha256) != 0)
        return ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_TABLE_INVALID;
    if (overlap(out, sizeof(*out), request->signed_z,
                request->signed_z_bytes) ||
        overlap(out, sizeof(*out), growth_out, growth_out_bytes) ||
        overlap(request->signed_z, request->signed_z_bytes,
                growth_out, growth_out_bytes))
        return ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ALIAS;

    /* The unchanged vector exponential is qualified on finite [-87, 88]. */
    for (uint32_t i = 0;
         i < ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_TOTAL_VALUES; ++i) {
        const float x = fmaf(qualified.diffusion, qualified.signed_z[i],
                             qualified.drift);
        if (!isfinite(x) || !(x >= -87.0f) || !(x <= 88.0f))
            return ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_DOMAIN;
    }

    asian_genuine_arithmetic_fused_source_exp_context_t prepared;
    memset(&prepared, 0, sizeof(prepared));
    prepared.signed_z = qualified.signed_z;
    prepared.growth_out = growth_out;
    prepared.drift = qualified.drift;
    prepared.diffusion = qualified.diffusion;
    prepared.fixing_count = request->fixing_count;
    prepared.path_count = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_PATHS;
    prepared.region_count = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_REGIONS;
    prepared.values_per_region =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_VALUES_PER_REGION;
    prepared.first_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    prepared.total_values =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_TOTAL_VALUES;
    prepared.magic = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
    prepared.abi_version =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
    memcpy(out, &prepared, sizeof(prepared));
    return ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_OK;
}
