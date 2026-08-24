#define _POSIX_C_SOURCE 200112L

#include "asian_meta_direction_affine_route.h"
#include "asian_genuine_permute.h"

#include <stdlib.h>
#include <string.h>

enum { JOE_KUO_RECORD_BYTES = 33 * 4 };

extern const unsigned char asian_arithmetic_joe_kuo_256_records[];

struct asian_meta_qsort_control_plan {
    uint32_t magic;
    uint8_t donor_region[ASIAN_META_DIRECTIONS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_META_DIRECTIONS];
};

static void *aligned_zero(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer, 64u, bytes) != 0)
        return NULL;
    memset(pointer, 0, bytes);
    return pointer;
}

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray = index ^ (index >> 1);
    uint32_t word = 0u;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u)
            word ^= directions[bit];
    return word;
}

static int load_row(uint32_t dimension, uint32_t directions[32])
{
    const unsigned char *record = asian_arithmetic_joe_kuo_256_records +
        (size_t)dimension * JOE_KUO_RECORD_BYTES;
    uint32_t count;
    memcpy(&count, record, sizeof(count));
    if (count != 32u)
        return -1;
    memcpy(directions, record + sizeof(count), 32u * sizeof(*directions));
    return 0;
}

int asian_meta_qsort_control_plan_create(asian_meta_qsort_control_plan_t **out)
{
    if (out == NULL)
        return -1;
    *out = NULL;
    asian_meta_qsort_control_plan_t *plan = aligned_zero(sizeof(*plan));
    uint32_t *source0 = aligned_zero(ASIAN_META_PATHS * sizeof(*source0));
    uint32_t *source1 = aligned_zero(ASIAN_META_PATHS * sizeof(*source1));
    uint32_t *target = aligned_zero(ASIAN_META_PATHS * sizeof(*target));
    float *payload = aligned_zero(2u * ASIAN_META_PATHS * sizeof(*payload));
    if (plan == NULL || source0 == NULL || source1 == NULL || target == NULL ||
        payload == NULL) {
        free(payload); free(target); free(source1); free(source0); free(plan);
        return -1;
    }

    uint32_t directions[32];
    if (load_row(0u, directions) != 0) {
        free(payload); free(target); free(source1); free(source0); free(plan);
        return -1;
    }
    for (uint32_t path = 0; path < ASIAN_META_PATHS; ++path) {
        source0[path] = sobol_word(8192u + path, directions);
        source1[path] = sobol_word(12288u + path, directions);
    }
    const uint32_t *source_words[2] = {source0, source1};
    const float *payloads[2] = {payload, payload + ASIAN_META_PATHS};

    for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
         ++dimension) {
        if (load_row(dimension, directions) != 0) {
            free(payload); free(target); free(source1); free(source0); free(plan);
            return -1;
        }
        for (uint32_t path = 0; path < ASIAN_META_PATHS; ++path)
            target[path] = sobol_word(8192u + path, directions);
        asian_genuine_route_t route;
        if (asian_genuine_prepare_route(source_words, 2u, payloads, payloads,
                target, dimension, ASIAN_META_DIRECTIONS,
                &plan->maps[dimension], &route) != 0) {
            free(payload); free(target); free(source1); free(source0); free(plan);
            return -1;
        }
        plan->donor_region[dimension] = route.growth_base == payloads[1];
    }
    free(payload); free(target); free(source1); free(source0);
    plan->magic = UINT32_C(0x54525351);
    *out = plan;
    return 0;
}

void asian_meta_qsort_control_plan_destroy(asian_meta_qsort_control_plan_t *plan)
{
    if (plan != NULL) {
        plan->magic = 0u;
        free(plan);
    }
}
