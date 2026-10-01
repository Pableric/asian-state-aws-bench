#include "cpp_provider.h"

#include <string.h>

/* F(c) = G12^-1(bitrev12(c)): 1/4096 cell -> donor offset in an even block. */
static uint32_t bitrev12(uint32_t x)
{
    uint32_t r = 0;
    uint32_t i;
    for (i = 0; i < 12u; ++i) {
        r |= ((x >> i) & 1u) << (11u - i);
    }
    return r;
}

static uint32_t gray_inv12(uint32_t g)
{
    g ^= g >> 1;
    g ^= g >> 2;
    g ^= g >> 4;
    g ^= g >> 8;
    return g & 0xFFFu;
}

static uint32_t cell_to_offset(uint32_t cell)
{
    return gray_inv12(bitrev12(cell & 0xFFFu));
}

/* Top 12 bits of the dim word at index 2^i: g(2^i) = 2^i ^ 2^(i-1). */
static uint32_t unit_cell(const uint32_t v[32], uint32_t i)
{
    uint32_t w = v[i] ^ (i != 0u ? v[i - 1u] : 0u);
    return w >> 20;
}

static uint32_t encode_block_state(uint32_t u)
{
    return (((u >> 4) << 6) << 16) | (u & 15u);
}

int cpp_prepare_dimension(const uint32_t v[32], uint32_t dimension,
                          cpp_dim_record_t *out)
{
    uint32_t col[12];
    uint32_t i;
    uint32_t t;
    uint32_t acc;
    if (v == NULL || out == NULL || ((uintptr_t)out & 63u) != 0u) {
        return -1;
    }
    if (v[0] != 0x80000000u) {
        return -1;
    }
    for (i = 0; i < 12u; ++i) {
        if (v[i] == 0u || (uint32_t)__builtin_ctz(v[i]) != 31u - i) {
            return -1;
        }
    }
    for (i = 0; i < 12u; ++i) {
        col[i] = cell_to_offset(unit_cell(v, i));
    }
    /* Lanes 0..15 stay inside one donor line; destination bit 4 is line ^ 1. */
    for (i = 0; i < 4u; ++i) {
        if (col[i] >= 16u) {
            return -1;
        }
    }
    if ((col[4] >> 4) != 1u) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    for (i = 0; i < (uint32_t)CPP_LANES; ++i) {
        uint32_t c = 0;
        uint32_t b;
        for (b = 0; b < 4u; ++b) {
            if ((i >> b) & 1u) {
                c ^= col[b];
            }
        }
        out->ctl[i] = (uint8_t)c;
    }
    out->half = col[4] & 15u;
    out->dimension = dimension;
    out->version = CPP_RECORD_VERSION;
    /* Packet p = 32-destination group; entering p XORs A*(32*(p ^ (p-1))),
     * and p ^ (p-1) = 2^(t+1) - 1 with t = ctz(p): destination bits 5..5+t. */
    acc = 0;
    for (t = 0; t < 7u; ++t) {
        acc ^= col[5u + t];
        out->daddr[t] = (acc >> 4) << 6;
        out->dlane[t] = acc & 15u;
    }
    /* c(2^i) = F(top12(X(4096 * 2^i))) ^ [i == 0] * 0xFFF, i = 0..19.
     * Entering block B XORs c(2^(t+1) - 1), t = ctz(B). */
    acc = 0;
    for (t = 0; t < (uint32_t)CPP_BLOCK_STEPS; ++t) {
        uint32_t c = cell_to_offset(unit_cell(v, 12u + t));
        if (t == 0u) {
            c ^= 0xFFFu;
        }
        acc ^= c;
        out->cstep[t] = encode_block_state(acc);
    }
    return 0;
}

uint32_t cpp_block_state(const cpp_dim_record_t *rec, uint32_t block)
{
    uint32_t g = block ^ (block >> 1);
    uint32_t s = 0;
    uint32_t j;
    for (j = 0; j < (uint32_t)CPP_BLOCK_STEPS; ++j) {
        if ((g >> j) & 1u) {
            s ^= rec->cstep[j];
        }
    }
    return s;
}

void cpp_packet_state(const cpp_dim_record_t *rec, uint32_t block_state,
                      uint32_t packet, cpp_packet_state_t *out)
{
    uint32_t g = packet ^ (packet >> 1);
    uint32_t ga = block_state >> 16;
    uint32_t lane = block_state & 15u;
    uint32_t j;
    for (j = 0; j < 7u; ++j) {
        if ((g >> j) & 1u) {
            ga ^= rec->daddr[j];
            lane ^= rec->dlane[j];
        }
    }
    out->ga = ga;
    out->gb = ga ^ 64u;
    out->lane_xor = lane;
}

uint32_t cpp_record_source(const cpp_dim_record_t *rec, uint32_t block, uint32_t k)
{
    cpp_packet_state_t st;
    uint32_t lane = k & 15u;
    uint32_t second = (k >> 4) & 1u;
    uint32_t ctl;
    uint32_t line_offset;
    cpp_packet_state(rec, cpp_block_state(rec, block), k >> 5, &st);
    ctl = ((uint32_t)rec->ctl[lane] ^ st.lane_xor ^ (second ? rec->half : 0u)) & 15u;
    line_offset = second ? st.gb : st.ga;
    return line_offset / 4u + ctl;
}
