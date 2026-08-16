#include <cpuid.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>

enum {
    BLOCK_VALUES = 4096,
    BLOCK_BYTES = BLOCK_VALUES * (int)sizeof(uint32_t),
    ROW_WORDS = 33,
    DIRECTION_WORDS = 32,
    TIMING_SAMPLES = 17,
    TIMING_REPEATS = 32,
};

extern void sobol_build_block_4096(uint32_t*, uint32_t, const uint32_t*);

static int avx512_available(void) {
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return 0;
    const unsigned needed = bit_AVX512F | bit_AVX512BW;
    return (ebx & needed) == needed;
}

static uint32_t scalar_word(uint32_t index, const uint32_t directions[32]) {
    uint32_t gray = index ^ (index >> 1);
    uint32_t value = 0;
    for (unsigned bit = 0; gray; ++bit, gray >>= 1) {
        if (gray & 1u) value ^= directions[bit];
    }
    return value;
}

static int load_directions(FILE* file, uint32_t dimension,
                           uint32_t directions[32]) {
    const uint64_t offset = (uint64_t)(dimension - 1u) * ROW_WORDS * sizeof(uint32_t);
    uint32_t header;
    if (fseek(file, (long)offset, SEEK_SET) != 0 ||
        fread(&header, sizeof(header), 1, file) != 1 || header != 32u ||
        fread(directions, sizeof(*directions), DIRECTION_WORDS, file) != DIRECTION_WORDS) {
        return -1;
    }
    return 0;
}

static uint64_t ticks(void) {
    unsigned aux;
    _mm_lfence();
    const uint64_t value = __rdtscp(&aux);
    _mm_lfence();
    return value;
}

static int compare_u64(const void* lhs, const void* rhs) {
    const uint64_t a = *(const uint64_t*)lhs;
    const uint64_t b = *(const uint64_t*)rhs;
    return (a > b) - (a < b);
}

static uint32_t consume_block(const uint32_t* block) {
    uint32_t checksum = 0;
    for (unsigned i = 0; i < BLOCK_VALUES; ++i) checksum ^= block[i];
    return checksum;
}

int main(void) {
    if (!avx512_available()) {
        puts("BLOCK_REPORT_JSON {\"status\":\"UNSUPPORTED\",\"reason\":\"AVX-512F/BW unavailable\"}");
        return 2;
    }
    FILE* table = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
    if (!table) {
        puts("BLOCK_REPORT_JSON {\"status\":\"HARNESS_ERROR\",\"reason\":\"direction table unavailable\"}");
        return 2;
    }
    uint32_t* allocation = aligned_alloc(64, BLOCK_BYTES + 128);
    uint32_t* block = allocation ? allocation + 16 : NULL;
    uint32_t* directions = aligned_alloc(64, DIRECTION_WORDS * sizeof(uint32_t));
    if (!block || !directions) {
        fclose(table);
        free(allocation);
        free(directions);
        return 2;
    }

    static const uint32_t dimensions[] = {1, 2, 7, 32, 1024, 21201};
    static const uint32_t starts[] = {0, 1, 31, 4095, 4096, 8191, 1048573};
    uint64_t mismatches = 0;
    uint64_t guard_failures = 0;
    for (unsigned d = 0; d < sizeof(dimensions) / sizeof(dimensions[0]); ++d) {
        if (load_directions(table, dimensions[d], directions) != 0) {
            guard_failures++;
            continue;
        }
        for (unsigned s = 0; s < sizeof(starts) / sizeof(starts[0]); ++s) {
            for (unsigned i = 0; i < 16; ++i) {
                block[-16 + (int)i] = 0x6b8b4567u;
                block[BLOCK_VALUES + i] = 0x327b23c6u;
            }
            memset(block, 0xa5, BLOCK_BYTES);
            sobol_build_block_4096(block, starts[s], directions);
            for (unsigned i = 0; i < BLOCK_VALUES; ++i) {
                mismatches += block[i] != scalar_word(starts[s] + i, directions);
            }
            for (unsigned i = 0; i < 16; ++i) {
                guard_failures += block[-16 + (int)i] != 0x6b8b4567u;
                guard_failures += block[BLOCK_VALUES + i] != 0x327b23c6u;
            }
        }
    }

    if (load_directions(table, 2, directions) != 0) guard_failures++;
    fclose(table);
    uint64_t timing[TIMING_SAMPLES];
    volatile uint32_t checksum = 0;
    for (unsigned sample = 0; sample < TIMING_SAMPLES; ++sample) {
        const uint64_t begin = ticks();
        for (unsigned repeat = 0; repeat < TIMING_REPEATS; ++repeat) {
            sobol_build_block_4096(block, 4096u * repeat, directions);
            checksum ^= consume_block(block);
        }
        timing[sample] = ticks() - begin;
    }
    qsort(timing, TIMING_SAMPLES, sizeof(timing[0]), compare_u64);
    const double cycles_per_block =
        (double)timing[TIMING_SAMPLES / 2] / TIMING_REPEATS;
    const char* status = mismatches || guard_failures ? "FAIL" : "PASS";
    printf("BLOCK_REPORT_JSON {\"status\":\"%s\",\"mismatches\":%" PRIu64
           ",\"guard_failures\":%" PRIu64 ",\"block_values\":%d,"
           "\"block_bytes\":%d,\"packets\":128,\"cycles_per_block\":%.4f,"
           "\"cycles_per_value\":%.8f,\"checksum\":%u}\n",
           status, mismatches, guard_failures, BLOCK_VALUES, BLOCK_BYTES,
           cycles_per_block, cycles_per_block / BLOCK_VALUES, checksum);
    free(allocation);
    free(directions);
    return strcmp(status, "PASS") == 0 ? 0 : 1;
}
