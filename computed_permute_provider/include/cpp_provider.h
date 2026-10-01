#ifndef CPP_PROVIDER_H
#define CPP_PROVIDER_H

/* Computed-on-the-fly permutation provider (v1).
 *
 * donor ZMMs -> destination-ordered dimension ZMMs, for the same-block
 * range-matched ordering: target block B of dimension d is a permutation of
 * donor (D1) block B, chosen so destination k receives the donor value whose
 * 1/4096 cell equals the canonical dim-d Sobol cell of point 4096*B + k.
 * See README.md for the derivation and the asm register contract.
 */

#include <stddef.h>
#include <stdint.h>

enum {
    CPP_BLOCK_VALUES = 4096,
    CPP_PACKET_VALUES = 32,
    CPP_LANES = 16,
    CPP_BLOCK_PACKETS = 128,
    CPP_PACKET_STEPS = 8,       /* t = ctz(p), p = 1..127 uses t = 0..6 */
    CPP_BLOCK_STEPS = 20,       /* t = ctz(B), B = 1..2^20-1 uses t = 0..19 */
    CPP_MAX_BLOCKS_LOG2 = 20,   /* 32-bit direction numbers: 2^32 points */
    CPP_RECORD_BYTES = 192,
    CPP_RECORD_VERSION = 0x43505031u /* "CPP1" */
};

/* One immutable record per dimension. 64-byte aligned, stride 192 bytes.
 * Records may be stored back to back: 192 is a multiple of 64.
 *
 *   ctl[l]     lane control of destination lane l in packet 0, block 0
 *   half       lane-control XOR for destinations 16..31 of every packet
 *   daddr[t]   donor byte-offset XOR applied when entering packet p, t = ctz(p)
 *   dlane[t]   lane-control XOR applied when entering packet p, t = ctz(p)
 *   cstep[t]   block-state XOR applied when entering block B, t = ctz(B);
 *              encoding (byte_offset << 16) | lane_xor
 */
typedef struct __attribute__((aligned(64))) {
    uint8_t ctl[CPP_LANES];
    uint32_t half;
    uint32_t dimension;
    uint32_t version;
    uint32_t reserved0;
    uint32_t daddr[CPP_PACKET_STEPS];
    uint32_t dlane[CPP_PACKET_STEPS];
    uint32_t cstep[CPP_BLOCK_STEPS];
    uint8_t reserved1[16];
} cpp_dim_record_t;

_Static_assert(offsetof(cpp_dim_record_t, ctl) == 0, "ctl at 0");
_Static_assert(offsetof(cpp_dim_record_t, half) == 16, "half at 16");
_Static_assert(offsetof(cpp_dim_record_t, daddr) == 32, "daddr at 32");
_Static_assert(offsetof(cpp_dim_record_t, dlane) == 64, "dlane at 64");
_Static_assert(offsetof(cpp_dim_record_t, cstep) == 96, "cstep at 96");
_Static_assert(sizeof(cpp_dim_record_t) == CPP_RECORD_BYTES, "record 192");

/* Build the record of one dimension from its 32 Joe-Kuo direction words
 * (V[0] = 0x80000000, lowest set bit of V[j] at 31-j for j < 12).
 * Returns 0 on success, -1 if the direction words violate that structure
 * or out is not 64-byte aligned. On failure *out is undefined. */
int cpp_prepare_dimension(const uint32_t v[32], uint32_t dimension,
                          cpp_dim_record_t *out);

/* Block state of block B (0 <= B < 2^20) by direct seek over the Gray code
 * of B. Equals the CPP_BLOCK_ADVANCE recurrence started from 0 at block 0. */
uint32_t cpp_block_state(const cpp_dim_record_t *rec, uint32_t block);

/* Live packet state of packet p (0..127) inside a block, by direct seek.
 * Used to enter a block at a packet other than 0. */
typedef struct {
    uint32_t ga;        /* donor byte offset of the first 16 destinations */
    uint32_t gb;        /* ga ^ 64 */
    uint32_t lane_xor;  /* low 4 bits XORed into every lane of ctl */
} cpp_packet_state_t;

void cpp_packet_state(const cpp_dim_record_t *rec, uint32_t block_state,
                      uint32_t packet, cpp_packet_state_t *out);

/* Scalar model of the asm provider: donor source offset of destination k
 * (0..4095) of block B, computed from the record alone. */
uint32_t cpp_record_source(const cpp_dim_record_t *rec, uint32_t block, uint32_t k);

#endif
