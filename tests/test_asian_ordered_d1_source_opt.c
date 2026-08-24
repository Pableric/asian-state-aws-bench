#define _POSIX_C_SOURCE 200112L
#include "private/asian_ordered_d1_source_opt.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VALUES = ASIAN_ORDERED_D1_SOURCE_OPT_VALUES, GUARD = 16 };

static void *alloc64(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer, 64, bytes) != 0) {
        return NULL;
    }
    return pointer;
}

static uint64_t hash_bytes(const void *data, size_t bytes)
{
    const unsigned char *input = data;
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t index = 0; index < bytes; ++index) {
        hash ^= input[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int check_guard(const float *array)
{
    const uint32_t *bits = (const uint32_t *)array;
    for (size_t index = VALUES; index < VALUES + GUARD; ++index) {
        if (bits[index] != UINT32_C(0x7fc12345)) {
            return -1;
        }
    }
    return 0;
}

static int compare_bits(
    const char *name,
    const float *reference,
    const float *candidate,
    uint32_t fixing_count)
{
    const uint32_t *want = (const uint32_t *)reference;
    const uint32_t *got = (const uint32_t *)candidate;
    for (size_t index = 0; index < VALUES; ++index) {
        if (want[index] != got[index]) {
            fprintf(
                stderr,
                "%s mismatch N=%u index=%zu want=0x%08x got=0x%08x\n",
                name,
                fixing_count,
                index,
                want[index],
                got[index]);
            return -1;
        }
    }
    return 0;
}

int main(void)
{
    static const uint32_t fixing_counts[] = {16, 32, 64, 128, 256};
    ordered_d1_diag_context_t *context = alloc64(sizeof(*context));
    float *reference_x = alloc64((VALUES + GUARD) * sizeof(float));
    float *reference_growth = alloc64((VALUES + GUARD) * sizeof(float));
    float *candidate_x = alloc64((VALUES + GUARD) * sizeof(float));
    float *candidate_growth = alloc64((VALUES + GUARD) * sizeof(float));
    if (context == NULL || reference_x == NULL || reference_growth == NULL ||
        candidate_x == NULL || candidate_growth == NULL) {
        return 2;
    }

    for (size_t scenario = 0;
         scenario < sizeof(fixing_counts) / sizeof(fixing_counts[0]);
         ++scenario) {
        const uint32_t fixing_count = fixing_counts[scenario];
        const float drift = (float)((0.03 - 0.5 * 0.20 * 0.20) / fixing_count);
        const float diffusion = (float)(0.20 / __builtin_sqrt((double)fixing_count));
        if (ordered_d1_diag_prepare(
                context,
                drift,
                diffusion,
                VALUES,
                ORDERED_D1_DIAG_PREPARE_X3 |
                    ORDERED_D1_DIAG_PREPARE_GROWTH3,
                fixing_count) != 0) {
            return 2;
        }
        const uint64_t context_hash = hash_bytes(context, sizeof(*context));
        for (size_t index = 0; index < VALUES + GUARD; ++index) {
            ((uint32_t *)reference_x)[index] = UINT32_C(0x7fc12345);
            ((uint32_t *)reference_growth)[index] = UINT32_C(0x7fc12345);
            ((uint32_t *)candidate_x)[index] = UINT32_C(0x7fc12345);
            ((uint32_t *)candidate_growth)[index] = UINT32_C(0x7fc12345);
        }

        ordered_d1_x_growth_local_diag(
            VALUES / 32, context, reference_x, reference_growth);
        asian_ordered_d1_x_growth_static_8192_diag(
            context, candidate_x, candidate_growth);
        if (compare_bits("x", reference_x, candidate_x, fixing_count) != 0 ||
            compare_bits(
                "growth", reference_growth, candidate_growth, fixing_count) != 0 ||
            check_guard(reference_x) != 0 || check_guard(reference_growth) != 0 ||
            check_guard(candidate_x) != 0 || check_guard(candidate_growth) != 0 ||
            hash_bytes(context, sizeof(*context)) != context_hash) {
            return 1;
        }

        const uint64_t x_hash = hash_bytes(candidate_x, VALUES * sizeof(float));
        const uint64_t growth_hash =
            hash_bytes(candidate_growth, VALUES * sizeof(float));
        asian_ordered_d1_x_growth_static_8192_diag(
            context, candidate_x, candidate_growth);
        if (hash_bytes(candidate_x, VALUES * sizeof(float)) != x_hash ||
            hash_bytes(candidate_growth, VALUES * sizeof(float)) != growth_hash ||
            hash_bytes(context, sizeof(*context)) != context_hash) {
            fprintf(stderr, "repeat or input mutation N=%u\n", fixing_count);
            return 1;
        }

        if (ordered_d1_diag_prepare(
                context,
                drift,
                diffusion,
                VALUES,
                ORDERED_D1_DIAG_PREPARE_X3,
                fixing_count) != 0) {
            return 2;
        }
        const uint64_t x_only_context_hash = hash_bytes(context, sizeof(*context));
        for (size_t index = 0; index < VALUES + GUARD; ++index) {
            ((uint32_t *)reference_x)[index] = UINT32_C(0x7fc12345);
            ((uint32_t *)candidate_x)[index] = UINT32_C(0x7fc12345);
        }
        ordered_d1_x_only_diag(VALUES / 32, context, reference_x);
        asian_ordered_d1_x_static_8192_diag(context, candidate_x);
        if (compare_bits("x-only", reference_x, candidate_x, fixing_count) != 0 ||
            check_guard(reference_x) != 0 || check_guard(candidate_x) != 0 ||
            hash_bytes(context, sizeof(*context)) != x_only_context_hash) {
            return 1;
        }
    }

    free(candidate_growth);
    free(candidate_x);
    free(reference_growth);
    free(reference_x);
    free(context);
    puts("asian_ordered_d1_source_opt=PASS dual_and_x_only_bit_exact_8192 "
         "N=16,32,64,128,256");
    return 0;
}
