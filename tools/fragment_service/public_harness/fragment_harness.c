#include <cpuid.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>

enum { LANES = 32, SAMPLES = 15, ITERATIONS = 20000 };

extern void fragment_permd(uint32_t*, const uint32_t*, const uint32_t*,
                           const uint32_t*, const uint32_t*);
extern void fragment_permi2d(uint32_t*, const uint32_t*, const uint32_t*,
                             const uint32_t*, const uint32_t*);
extern void fragment_gen(uint32_t*, const uint32_t*, const uint32_t*,
                         const uint32_t*);
extern void fragment_gen_load(uint32_t*, const uint32_t*, const uint32_t*,
                              const uint32_t*);

static uint32_t bit_reverse32(uint32_t value) {
    uint32_t result = 0;
    for (unsigned bit = 0; bit < 32; ++bit) {
        result |= ((value >> bit) & 1u) << (31u - bit);
    }
    return result;
}

static uint32_t d1(uint32_t index) {
    return bit_reverse32(index ^ (index >> 1));
}

static int avx512_available(void) {
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return 0;
    const unsigned needed = bit_AVX512F | bit_AVX512BW;
    return (ebx & needed) == needed;
}

static uint64_t ticks(void) {
    unsigned aux;
    _mm_lfence();
    const uint64_t value = __rdtscp(&aux);
    _mm_lfence();
    return value;
}

static int compare_u32(const void* lhs, const void* rhs) {
    const uint64_t a = *(const uint64_t*)lhs;
    const uint64_t b = *(const uint64_t*)rhs;
    return (a > b) - (a < b);
}

typedef void (*loaded_fn)(uint32_t*, const uint32_t*, const uint32_t*,
                          const uint32_t*, const uint32_t*);
typedef void (*generated_fn)(uint32_t*, const uint32_t*, const uint32_t*,
                             const uint32_t*);

static double time_loaded(loaded_fn fn, uint32_t* out, const uint32_t* a,
                          const uint32_t* b, const uint32_t* ca,
                          const uint32_t* cb) {
    uint64_t times[SAMPLES];
    for (unsigned sample = 0; sample < SAMPLES; ++sample) {
        const uint64_t begin = ticks();
        for (unsigned i = 0; i < ITERATIONS; ++i) fn(out, a, b, ca, cb);
        times[sample] = ticks() - begin;
    }
    qsort(times, SAMPLES, sizeof(times[0]), compare_u32);
    return (double)times[SAMPLES / 2] / ITERATIONS;
}

static double time_generated(generated_fn fn, uint32_t* out,
                             const uint32_t* a, const uint32_t* b,
                             const uint32_t* constants) {
    uint64_t times[SAMPLES];
    for (unsigned sample = 0; sample < SAMPLES; ++sample) {
        const uint64_t begin = ticks();
        for (unsigned i = 0; i < ITERATIONS; ++i) fn(out, a, b, constants);
        times[sample] = ticks() - begin;
    }
    qsort(times, SAMPLES, sizeof(times[0]), compare_u32);
    return (double)times[SAMPLES / 2] / ITERATIONS;
}

int main(void) {
    if (!avx512_available()) {
        puts("FRAGMENT_REPORT_JSON {\"status\":\"UNSUPPORTED\",\"reason\":\"AVX-512F/BW unavailable\"}");
        return 2;
    }

    uint32_t table[256] __attribute__((aligned(64)));
    uint32_t out[LANES] __attribute__((aligned(64)));
    uint32_t controls_a[16] __attribute__((aligned(64)));
    uint32_t controls_b[16] __attribute__((aligned(64)));
    uint32_t controls2_a[16] __attribute__((aligned(64)));
    uint32_t controls2_b[16] __attribute__((aligned(64)));
    uint32_t indices_a[16] __attribute__((aligned(64)));
    uint32_t indices_b[16] __attribute__((aligned(64)));
    uint32_t constants[48] __attribute__((aligned(64)));

    for (unsigned i = 0; i < 256; ++i) table[i] = d1(i);
    for (unsigned lane = 0; lane < 16; ++lane) {
        controls_a[lane] = (5u * lane + 3u) & 15u;
        controls_b[lane] = (11u * lane + 7u) & 15u;
        controls2_a[lane] = (3u * lane + 5u) & 31u;
        controls2_b[lane] = (13u * lane + 1u) & 31u;
        indices_a[lane] = 8192u + lane;
        indices_b[lane] = 8208u + lane;
    }
    for (unsigned lane = 0; lane < 16; ++lane) constants[lane] = 0x0f0f0f0fu;
    static const uint8_t byte_swap[16] = {
        3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12
    };
    static const uint8_t nibble_reverse[16] = {
        0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15
    };
    for (unsigned lane = 0; lane < 64; ++lane) {
        ((uint8_t*)(constants + 16))[lane] = byte_swap[lane & 15u];
        ((uint8_t*)(constants + 32))[lane] = nibble_reverse[lane & 15u];
    }

    unsigned mismatches = 0;
    fragment_permd(out, table + 64, table + 80, controls_a, controls_b);
    for (unsigned lane = 0; lane < 16; ++lane) {
        mismatches += out[lane] != table[64 + controls_a[lane]];
        mismatches += out[16 + lane] != table[80 + controls_b[lane]];
    }
    fragment_permi2d(out, table + 128, table + 144, controls2_a, controls2_b);
    for (unsigned lane = 0; lane < 16; ++lane) {
        mismatches += out[lane] != table[128 + controls2_a[lane]];
        mismatches += out[16 + lane] != table[128 + controls2_b[lane]];
    }
    fragment_gen(out, indices_a, indices_b, constants);
    for (unsigned lane = 0; lane < 16; ++lane) {
        mismatches += out[lane] != d1(indices_a[lane]);
        mismatches += out[16 + lane] != d1(indices_b[lane]);
    }
    fragment_gen_load(out, indices_a, indices_b, constants);
    for (unsigned lane = 0; lane < 16; ++lane) {
        mismatches += out[lane] != d1(indices_a[lane]);
        mismatches += out[16 + lane] != d1(indices_b[lane]);
    }

    const double permd = time_loaded(fragment_permd, out, table + 64,
                                     table + 80, controls_a, controls_b);
    const double permi2d = time_loaded(fragment_permi2d, out, table + 128,
                                       table + 144, controls2_a, controls2_b);
    const double gen = time_generated(fragment_gen, out, indices_a, indices_b,
                                      constants);
    const double gen_load = time_generated(fragment_gen_load, out, indices_a,
                                           indices_b, constants);
    printf("FRAGMENT_REPORT_JSON {\"status\":\"%s\",\"mismatches\":%u,"
           "\"cycles_per_call\":{\"permd\":%.4f,\"permi2d\":%.4f,"
           "\"gen\":%.4f,\"gen_load\":%.4f}}\n",
           mismatches ? "FAIL" : "PASS", mismatches,
           permd, permi2d, gen, gen_load);
    return mismatches ? 1 : 0;
}
