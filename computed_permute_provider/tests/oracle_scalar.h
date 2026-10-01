#ifndef ORACLE_SCALAR_H
#define ORACLE_SCALAR_H

/* Independent scalar source-index oracle.
 *
 * Uses only the Sobol definition X_d(n) = XOR of V_d[j] over the set bits of
 * the Gray code n ^ (n >> 1), and a brute-force inverse of the donor block.
 * Shares no code or algebra with cpp_prepare_dimension.
 */

#include <stdint.h>

uint32_t oracle_sobol_word(const uint32_t v[32], uint32_t n);

/* inv_cell[c] = offset o of D1 block B whose word X_1(4096B + o) has top-12
 * bits c. Returns 0, or -1 if the block is not one point per 1/4096 cell. */
int oracle_block_inverse(const uint32_t v1[32], uint32_t block, uint16_t inv_cell[4096]);

/* src[k] = donor offset for destination k of dimension v, block B:
 * inv_cell[top12(X_d(4096B + k))]. */
void oracle_block_sources(const uint32_t v[32], uint32_t block,
                          const uint16_t inv_cell[4096], uint16_t src[4096]);

/* Block state from the direction words, not from a record:
 * c(B) = F(top12(X_d(4096B))) ^ [B odd] * 0xFFF, encoded as
 * (((c >> 4) << 6) << 16) | (c & 15). F is evaluated by brute-force inverse
 * of D1 block 0. */
uint32_t oracle_block_state(const uint32_t v[32], const uint16_t inv_cell_block0[4096],
                            uint32_t block);

#endif
