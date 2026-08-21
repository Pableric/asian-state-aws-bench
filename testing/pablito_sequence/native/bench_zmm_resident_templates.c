#define _GNU_SOURCE
#include <errno.h>
#include <immintrin.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zmm_resident_templates_generated.h"

enum { DIRECTIONS = 32, MAX_DIMS = 256, MAX_BLOCKS = 64, SAMPLES_MAX = 51 };
static const uint32_t BLOCK_COUNTS[] = {1, 2, 4, 8, 16, 32, 64};
static const uint32_t PRODUCTION_START = 8192;
static volatile uint32_t sink;

typedef struct {
    const zrt_kernel_desc_t *kernel;
    uint32_t blocks;
    uint32_t repetitions;
    uint64_t ticks[SAMPLES_MAX];
    uint64_t wall[SAMPLES_MAX];
} bench_case_t;

static uint32_t directions[MAX_DIMS][DIRECTIONS];
static uint32_t templates16[MAX_DIMS][16] __attribute__((aligned(64)));
static uint32_t templates32[MAX_DIMS][32] __attribute__((aligned(64)));
static uint32_t delta32[MAX_DIMS] __attribute__((aligned(64)));
static uint32_t shifts[MAX_DIMS] __attribute__((aligned(64)));
static uint32_t *bases;
static uint32_t *output;

static uint64_t tsc_begin(void) {
    _mm_lfence();
    return __rdtsc();
}

static uint64_t tsc_end(void) {
    unsigned aux;
    uint64_t result = __rdtscp(&aux);
    _mm_lfence();
    return result;
}

static uint64_t wall_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value)) abort();
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static uint32_t mix32(uint32_t value) {
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    return value ^ (value >> 16);
}

static uint32_t sobol_word(uint32_t dimension, uint32_t index) {
    uint32_t gray = index ^ (index >> 1), word = 0, bit = 0;
    while (gray) {
        if (gray & 1u) word ^= directions[dimension][bit];
        gray >>= 1;
        ++bit;
    }
    return word;
}

static int load_directions(const char *path) {
    FILE *stream = fopen(path, "rb");
    if (!stream) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    for (uint32_t dimension = 0; dimension < MAX_DIMS; ++dimension) {
        uint32_t count = 0;
        if (fread(&count, sizeof(count), 1, stream) != 1 || count != DIRECTIONS ||
            fread(directions[dimension], sizeof(uint32_t), DIRECTIONS, stream) != DIRECTIONS) {
            fclose(stream);
            fprintf(stderr, "invalid direction row %u\n", dimension + 1);
            return -1;
        }
    }
    fclose(stream);
    for (uint32_t dimension = 0; dimension < MAX_DIMS; ++dimension) {
        shifts[dimension] = mix32(UINT32_C(0x53484946) + dimension);
        for (uint32_t lane = 0; lane < 16; ++lane)
            templates16[dimension][lane] = sobol_word(dimension, lane);
        for (uint32_t lane = 0; lane < 32; ++lane)
            templates32[dimension][lane] = sobol_word(dimension, lane);
        delta32[dimension] = templates32[dimension][16];
        for (uint32_t lane = 0; lane < 16; ++lane) {
            if (templates32[dimension][16 + lane] !=
                (templates16[dimension][lane] ^ delta32[dimension])) {
                fprintf(stderr, "32-half identity mismatch D%u lane %u\n", dimension + 1, lane);
                return -1;
            }
        }
    }
    return 0;
}

static void prepare_bases(const zrt_kernel_desc_t *kernel, uint32_t blocks) {
    for (uint32_t dimension = 0; dimension < kernel->dimensions; ++dimension) {
        for (uint32_t block = 0; block < blocks; ++block) {
            size_t offset = strcmp(kernel->layout, "dimension_outer") == 0
                ? (size_t)dimension * blocks + block
                : (size_t)block * kernel->dimensions + dimension;
            uint32_t index = PRODUCTION_START + block * kernel->lanes;
            bases[offset] = sobol_word(dimension, index) ^ shifts[dimension];
        }
    }
}

static int verify_output(const zrt_kernel_desc_t *kernel, uint32_t blocks) {
    const uint32_t *templates = kernel->template_words == 32
        ? &templates32[0][0] : &templates16[0][0];
    prepare_bases(kernel, blocks);
    memset(output, 0xa5, (size_t)kernel->dimensions * blocks * kernel->lanes * sizeof(uint32_t));
    kernel->fn(output, bases, templates, delta32, blocks);
    for (uint32_t dimension = 0; dimension < kernel->dimensions; ++dimension) {
        for (uint32_t block = 0; block < blocks; ++block) {
            for (uint32_t lane = 0; lane < kernel->lanes; ++lane) {
                size_t vector = strcmp(kernel->layout, "dimension_outer") == 0
                    ? (size_t)dimension * blocks + block
                    : (size_t)block * kernel->dimensions + dimension;
                size_t offset = vector * kernel->lanes + lane;
                uint32_t index = PRODUCTION_START + block * kernel->lanes + lane;
                uint32_t expected = sobol_word(dimension, index) ^ shifts[dimension];
                if (output[offset] != expected) {
                    fprintf(stderr,
                        "mismatch symbol=%s D%u block=%u lane=%u got=%08" PRIx32
                        " expected=%08" PRIx32 "\n",
                        kernel->symbol, dimension + 1, block, lane, output[offset], expected);
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int u64_compare(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t quantile(const uint64_t *values, uint32_t samples, uint32_t numerator) {
    uint64_t copy[SAMPLES_MAX];
    memcpy(copy, values, samples * sizeof(uint64_t));
    qsort(copy, samples, sizeof(uint64_t), u64_compare);
    return copy[((samples - 1) * numerator) / 10];
}

static uint64_t random_step(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    return *state ^= *state << 17;
}

static int pin_first_cpu(void) {
    cpu_set_t allowed, selected;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed)) return -1;
    int cpu = 0;
    while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &allowed)) ++cpu;
    if (cpu == CPU_SETSIZE) return -1;
    CPU_ZERO(&selected);
    CPU_SET(cpu, &selected);
    return sched_setaffinity(0, sizeof(selected), &selected) ? -1 : cpu;
}

static void cpu_model(char out[256]) {
    strcpy(out, "unknown");
    FILE *stream = fopen("/proc/cpuinfo", "r");
    if (!stream) return;
    char line[512];
    while (fgets(line, sizeof(line), stream)) {
        if (strncmp(line, "model name", 10) != 0) continue;
        char *colon = strchr(line, ':');
        if (!colon) break;
        while (*++colon == ' ') {}
        size_t length = strcspn(colon, "\r\n");
        if (length > 255) length = 255;
        memcpy(out, colon, length);
        out[length] = 0;
        break;
    }
    fclose(stream);
}

static void run_one(bench_case_t *item) {
    const zrt_kernel_desc_t *kernel = item->kernel;
    const uint32_t *templates = kernel->template_words == 32
        ? &templates32[0][0] : &templates16[0][0];
    for (uint32_t repetition = 0; repetition < item->repetitions; ++repetition)
        kernel->fn(output, bases, templates, delta32, item->blocks);
    sink ^= output[((size_t)kernel->dimensions * item->blocks * kernel->lanes) - 1];
}

int main(int argc, char **argv) {
    const char *table = "asian-aws-publish.lxGUbF/direction_numbers/joe_kuo_6_21201.bin";
    const char *json_path = NULL;
    uint32_t samples = 31, warmups = 2;
    int check_only = 0;
    for (int argument = 1; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--check-only") == 0) check_only = 1;
        else if (strcmp(argv[argument], "--table") == 0 && ++argument < argc) table = argv[argument];
        else if (strcmp(argv[argument], "--json") == 0 && ++argument < argc) json_path = argv[argument];
        else if (strcmp(argv[argument], "--samples") == 0 && ++argument < argc) samples = (uint32_t)atoi(argv[argument]);
        else if (strcmp(argv[argument], "--warmups") == 0 && ++argument < argc) warmups = (uint32_t)atoi(argv[argument]);
        else return 2;
    }
    if (samples < 3 || samples > SAMPLES_MAX || warmups > 100) return 2;
    if (load_directions(table)) return 3;
    bases = aligned_alloc(64, (size_t)MAX_DIMS * MAX_BLOCKS * sizeof(uint32_t));
    output = aligned_alloc(64, (size_t)MAX_DIMS * MAX_BLOCKS * 32 * sizeof(uint32_t));
    if (!bases || !output) return 3;

    for (size_t kernel_index = 0; kernel_index < ZRT_KERNEL_COUNT; ++kernel_index)
        for (size_t block_index = 0; block_index < sizeof(BLOCK_COUNTS) / sizeof(BLOCK_COUNTS[0]); ++block_index)
            if (verify_output(&ZRT_KERNELS[kernel_index], BLOCK_COUNTS[block_index])) return 4;
    puts("zmm_resident_templates exact_check=PASS D1-D256 lanes=16,32 blocks=1,2,4,8,16,32,64 shift=per_dimension_u32");
    if (check_only) return 0;
    if (!json_path) return 2;

    int cpu = pin_first_cpu();
    if (cpu < 0) return 5;
    const size_t block_count = sizeof(BLOCK_COUNTS) / sizeof(BLOCK_COUNTS[0]);
    const size_t case_count = ZRT_KERNEL_COUNT * block_count;
    bench_case_t *cases = calloc(case_count, sizeof(*cases));
    size_t *order = malloc(case_count * sizeof(*order));
    if (!cases || !order) return 3;
    size_t case_index = 0;
    for (size_t kernel_index = 0; kernel_index < ZRT_KERNEL_COUNT; ++kernel_index) {
        for (size_t block_index = 0; block_index < block_count; ++block_index) {
            bench_case_t *item = &cases[case_index++];
            item->kernel = &ZRT_KERNELS[kernel_index];
            item->blocks = BLOCK_COUNTS[block_index];
            uint64_t words = (uint64_t)item->kernel->dimensions * item->blocks * item->kernel->lanes;
            item->repetitions = (uint32_t)((UINT64_C(65536) + words - 1) / words);
            if (item->repetitions == 0) item->repetitions = 1;
        }
    }
    for (uint32_t warmup = 0; warmup < warmups; ++warmup) {
        for (size_t index = 0; index < case_count; ++index) {
            prepare_bases(cases[index].kernel, cases[index].blocks);
            run_one(&cases[index]);
        }
    }
    uint64_t random_state = UINT64_C(0x5a4d4d5245534944);
    for (uint32_t sample = 0; sample < samples; ++sample) {
        for (size_t index = 0; index < case_count; ++index) order[index] = index;
        for (size_t index = case_count - 1; index > 0; --index) {
            size_t other = random_step(&random_state) % (index + 1);
            size_t temporary = order[index]; order[index] = order[other]; order[other] = temporary;
        }
        for (size_t position = 0; position < case_count; ++position) {
            bench_case_t *item = &cases[order[position]];
            prepare_bases(item->kernel, item->blocks);
            uint64_t wall0 = wall_ns(), tick0 = tsc_begin();
            run_one(item);
            uint64_t tick1 = tsc_end(), wall1 = wall_ns();
            item->ticks[sample] = tick1 - tick0;
            item->wall[sample] = wall1 - wall0;
        }
    }

    FILE *json = fopen(json_path, "w");
    if (!json) return 6;
    char model[256]; cpu_model(model);
    fprintf(json,
        "{\"status\":\"PASS\",\"cpu\":%d,\"cpu_model\":\"%s\","
        "\"production_start\":%u,\"samples\":%u,\"warmups\":%u,"
        "\"randomized_case_order\":true,\"results\":[",
        cpu, model, PRODUCTION_START, samples, warmups);
    for (size_t index = 0; index < case_count; ++index) {
        bench_case_t *item = &cases[index];
        const zrt_kernel_desc_t *kernel = item->kernel;
        double invocations = item->repetitions;
        fprintf(json,
            "%s{\"symbol\":\"%s\",\"candidate\":\"%s\",\"layout\":\"%s\","
            "\"lanes\":%u,\"dimensions\":%u,\"blocks\":%u,\"resident_budget\":%u,"
            "\"resident_initialization_bytes\":%u,\"repetitions_per_sample\":%u,"
            "\"ticks_p10_per_invocation\":%.9g,\"ticks_median_per_invocation\":%.9g,"
            "\"ticks_p90_per_invocation\":%.9g,\"wall_ns_p10_per_invocation\":%.9g,"
            "\"wall_ns_median_per_invocation\":%.9g,\"wall_ns_p90_per_invocation\":%.9g}",
            index ? "," : "", kernel->symbol, kernel->candidate, kernel->layout,
            kernel->lanes, kernel->dimensions, item->blocks, kernel->resident_budget,
            kernel->resident_budget * 64, item->repetitions,
            quantile(item->ticks, samples, 1) / invocations,
            quantile(item->ticks, samples, 5) / invocations,
            quantile(item->ticks, samples, 9) / invocations,
            quantile(item->wall, samples, 1) / invocations,
            quantile(item->wall, samples, 5) / invocations,
            quantile(item->wall, samples, 9) / invocations);
    }
    fprintf(json, "],\"checksum\":%u}\n", sink);
    fclose(json);
    printf("zmm_resident_templates native_benchmark=PASS cases=%zu json=%s\n", case_count, json_path);
    free(order); free(cases); free(output); free(bases);
    return 0;
}
