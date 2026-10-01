#include "oracle_scalar.h"

#include <string.h>

uint32_t oracle_sobol_word(const uint32_t v[32], uint32_t n)
{
    uint32_t g = n ^ (n >> 1);
    uint32_t w = 0;
    uint32_t j;
    for (j = 0; j < 32u; ++j) {
        if ((g >> j) & 1u) {
            w ^= v[j];
        }
    }
    return w;
}

/* Words of points 4096B .. 4096B+4095: X(n) = X(n-1) ^ V[ctz(n)]. */
static void block_words(const uint32_t v[32], uint32_t block, uint32_t words[4096])
{
    uint32_t w = oracle_sobol_word(v, block * 4096u);
    uint32_t k;
    words[0] = w;
    for (k = 1; k < 4096u; ++k) {
        w ^= v[__builtin_ctz(k)];
        words[k] = w;
    }
}

int oracle_block_inverse(const uint32_t v1[32], uint32_t block, uint16_t inv_cell[4096])
{
    uint8_t seen[4096];
    uint32_t words[4096];
    uint32_t o;
    memset(seen, 0, sizeof(seen));
    block_words(v1, block, words);
    for (o = 0; o < 4096u; ++o) {
        uint32_t cell = words[o] >> 20;
        if (seen[cell] != 0u) {
            return -1;
        }
        seen[cell] = 1u;
        inv_cell[cell] = (uint16_t)o;
    }
    return 0;
}

void oracle_block_sources(const uint32_t v[32], uint32_t block,
                          const uint16_t inv_cell[4096], uint16_t src[4096])
{
    uint32_t words[4096];
    uint32_t k;
    block_words(v, block, words);
    for (k = 0; k < 4096u; ++k) {
        src[k] = inv_cell[words[k] >> 20];
    }
}

uint32_t oracle_block_state(const uint32_t v[32], const uint16_t inv_cell_block0[4096],
                            uint32_t block)
{
    uint32_t cell = oracle_sobol_word(v, block * 4096u) >> 20;
    uint32_t c = inv_cell_block0[cell];
    if (block & 1u) {
        c ^= 0xFFFu;
    }
    return (((c >> 4) << 6) << 16) | (c & 15u);
}
