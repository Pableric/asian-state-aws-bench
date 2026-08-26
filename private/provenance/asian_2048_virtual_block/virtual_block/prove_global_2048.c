/* Prove or refute global 2048/2048 route closure. Standalone; no production kernels. */
#include "../d1_gf2_workbench/c/joe_kuo_v_1_256.h"
#include "virtual_block.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

enum { N = 4096, D = 256, BITS = 12 };

static uint32_t pi[D][N];
static uint8_t region[D];
static int check_bit11_any_block(uint32_t dest_base, int *fail_dim, int *fail_why);

static int region_of(uint32_t g) {
    if (g >= 8192u && g < 12288u) return 2;
    if (g >= 12288u && g < 16384u) return 3;
    return -1;
}

static int build_block(uint32_t dest_base) {
    uint32_t d, k;
    for (d = 0; d < D; ++d) {
        uint32_t dirs[32], g0 = 0;
        int r = -1;
        if (vb_directions((uint32_t)(d + 1u), dirs) != 0) return -1;
        for (k = 0; k < N; ++k) {
            uint32_t word = vb_sobol_word(dest_base + k, dirs);
            uint32_t g = vb_d1_index_from_word(word);
            int rg = region_of(g);
            if (rg < 0) return -2;
            if (k == 0) r = rg;
            else if (rg != r) return -3;
            pi[d][k] = (uint32_t)(g - (r == 2 ? 8192u : 12288u));
            if (k == 0) g0 = 1;
        }
        (void)g0;
        region[d] = (uint8_t)r;
        {
            uint8_t seen[N];
            memset(seen, 0, sizeof seen);
            for (k = 0; k < N; ++k) {
                if (pi[d][k] >= N || seen[pi[d][k]]) return -4;
                seen[pi[d][k]] = 1;
            }
        }
    }
    return 0;
}

static uint32_t dot12(uint32_t a, uint32_t x) {
    uint32_t t = (a & 0xFFFu) & x, n = 0;
    while (t) { n ^= 1u; t &= t - 1u; }
    return n;
}

/* Recover affine π(x)=A x + b on GF(2)^12. Return 0 if exact. */
static int recover_affine(const uint32_t *p, uint32_t A[12], uint32_t *b) {
    uint32_t i, x;
    *b = p[0];
    for (i = 0; i < 12u; ++i) {
        uint32_t e = 1u << i;
        A[i] = p[e] ^ *b;
    }
    for (x = 0; x < N; ++x) {
        uint32_t y = *b, i;
        for (i = 0; i < 12u; ++i)
            if (x & (1u << i)) y ^= A[i];
        if (y != p[x]) return (int)x + 1;
    }
    return 0;
}

/* u · (A_d e_i + e_i) = 0 for all d, i  <=>  u^T (A_d - I) = 0. */
static void gf2_12_solve(const uint32_t A[D][12], uint32_t b[D],
                         uint32_t *kernel, int *nk) {
    uint32_t M[12];
    int rank = 0, d, i, c, j, nf = 0;
    int free_v[12];
    uint32_t assigned;
    memset(M, 0, sizeof M);
    (void)b;
    for (d = 0; d < D; ++d) {
        for (i = 0; i < 12; ++i) {
            uint32_t r = (A[d][i] ^ (1u << i)) & 0xFFFu;
            for (c = 0; c < 12; ++c) {
                if (!((r >> c) & 1u)) continue;
                if (!M[c]) {
                    M[c] = r;
                    rank++;
                    break;
                }
                r ^= M[c];
            }
        }
    }
    for (c = 0; c < 12; ++c)
        if (!M[c]) free_v[nf++] = c;
    *nk = 0;
    for (assigned = 0; assigned < (1u << nf); ++assigned) {
        uint32_t u = 0;
        for (j = 0; j < nf; ++j)
            if (assigned & (1u << j)) u |= 1u << free_v[j];
        for (c = 11; c >= 0; --c) {
            uint32_t t, z, n;
            if (!M[c]) continue;
            t = M[c] ^ (1u << c);
            z = t & u;
            n = 0;
            while (z) {
                n ^= 1u;
                z &= z - 1u;
            }
            if (n) u |= 1u << c;
            else u &= ~(1u << c);
        }
        kernel[(*nk)++] = u & 0xFFFu;
    }
    fprintf(stderr, "affine left-kernel size %d nf=%d rank~%d\n", *nk, nf, rank);
}

static int label_ok(const uint32_t *p, const uint8_t *f) {
    uint8_t s = (uint8_t)(f[p[0]] ^ f[0]);
    uint32_t x;
    for (x = 1; x < N; ++x)
        if ((uint8_t)(f[p[x]] ^ f[x]) != s) return 0;
    return 1;
}

int main(int argc, char **argv) {
    uint32_t dest_base = 8192;
    uint32_t A[D][12], b[D], kernel[64];
    int nk = 0, d, rc, i;
    uint32_t k;
    int affine_ok = 1;
    int first_non_affine_d = 0, first_non_affine_x = 0;

    if (argc > 1) dest_base = (uint32_t)strtoul(argv[1], 0, 10);

    rc = build_block(dest_base);
    if (rc) {
        printf("GLOBAL_2048_ROUTE_CLOSURE=NO\n");
        printf("build_failed rc=%d dest_base=%u\n", rc, dest_base);
        return 1;
    }
    if (region[0] != 2) {
        printf("GLOBAL_2048_ROUTE_CLOSURE=NO\nD1_not_region2 dest_base=%u region=%u\n",
               dest_base, region[0]);
        return 1;
    }
    for (k = 0; k < N; ++k)
        if (pi[0][k] != k) {
            printf("GLOBAL_2048_ROUTE_CLOSURE=NO\nD1_not_identity dest_base=%u k=%u pi=%u\n",
                   dest_base, k, pi[0][k]);
            return 1;
        }

    for (d = 0; d < D; ++d) {
        int bad = recover_affine(pi[d], A[d], &b[d]);
        if (bad) {
            affine_ok = 0;
            first_non_affine_d = d + 1;
            first_non_affine_x = bad - 1;
            break;
        }
    }
    {
        int r2 = 0, r3 = 0;
        for (d = 0; d < D; ++d) {
            if (region[d] == 2) r2++;
            else r3++;
        }
        printf("dest_base=%u affine_on_binary_offset=%d region2=%d region3=%d\n",
               dest_base, affine_ok, r2, r3);
    }

    if (!affine_ok) {
        printf("GLOBAL_2048_ROUTE_CLOSURE=UNDECIDED_BY_AFFINE\n");
        printf("first_non_affine D%d dest_offset=%d\n",
               first_non_affine_d, first_non_affine_x);
        /* still try hyperplanes */
    } else {
        gf2_12_solve(A, b, kernel, &nk);
        {
            int found = 0;
            uint32_t u, ab;
            for (i = 0; i < nk; ++i) {
                u = kernel[i];
                if (u == 0) continue;
                for (ab = 0; ab < 2; ++ab) {
                    uint8_t f[N];
                    uint32_t x, z = 0;
                    int ok = 1;
                    for (x = 0; x < N; ++x) {
                        f[x] = (uint8_t)(dot12(u, x) ^ ab);
                        if (f[x] == 0) z++;
                    }
                    if (z != 2048) continue;
                    for (d = 0; d < D && ok; ++d)
                        if (!label_ok(pi[d], f)) ok = 0;
                    if (ok) {
                        printf("GLOBAL_2048_ROUTE_CLOSURE=YES\n");
                        printf("linear_mask=0x%x affine_bit=%u zeros=%u\n", u, ab, z);
                        printf("swap_bits=");
                        for (d = 0; d < D; ++d)
                            printf("%u", (unsigned)(f[pi[d][0]] ^ f[0]));
                        printf("\nregions=");
                        for (d = 0; d < D; ++d) printf("%u", region[d]);
                        printf("\n");
                        found = 1;
                        break;
                    }
                }
                if (found) break;
            }
            if (!found) {
                printf("GLOBAL_2048_ROUTE_CLOSURE=NO\n");
                printf("reason=no_common_left_1-eigenvector_gives_weight_2048_labelling\n");
                printf("kernel_size=%d (includes 0)\n", nk);
            }
        }
    }

    /* Contiguous dest tile 0..2047 as the naive split people would ship. */
    {
        uint8_t f[N];
        uint32_t x;
        int ok = 1, fail_d = 0;
        uint32_t inter = 0;
        for (x = 0; x < N; ++x) f[x] = (uint8_t)(x >= 2048u);
        for (d = 0; d < D; ++d) {
            if (!label_ok(pi[d], f)) {
                uint32_t c = 0;
                for (x = 0; x < 2048u; ++x)
                    if (pi[d][x] < 2048u) c++;
                ok = 0;
                fail_d = d + 1;
                inter = c;
                break;
            }
        }
        printf("contiguous_dest_0_2047_ok=%d first_fail_dim=%d inter_low=%u region=%u\n",
               ok, fail_d, inter, fail_d ? region[fail_d - 1] : 0);
    }
    printf("bit11_variable_blocks=");
    {
        int b;
        for (b = 0; b < 16; ++b) {
            int fd = 0, fw = 0;
            int okb = check_bit11_any_block((uint32_t)b * 4096u, &fd, &fw);
            printf(" %d:%s", b, okb ? "YES" : "NO");
            if (!okb)
                printf("(D%d,why=%d)", fd, fw);
        }
        printf("\n");
        printf("why 2=two_donor_4096_windows 3=duplicate 4=mixed_halves\n");
    }
    return 0;
}

/* Offset partition inside an arbitrary 4096-aligned donor window. */
static int check_bit11_any_block(uint32_t dest_base, int *fail_dim, int *fail_why) {
    uint32_t d, k;
    *fail_dim = 0;
    *fail_why = 0;
    for (d = 0; d < D; ++d) {
        uint32_t dirs[32], donor_block = 0, off[N];
        uint8_t seen0[2048], seen1[2048];
        uint32_t n0 = 0, n1 = 0;
        if (vb_directions((uint32_t)(d + 1u), dirs) != 0) {
            *fail_dim = (int)(d + 1);
            *fail_why = 1;
            return 0;
        }
        for (k = 0; k < N; ++k) {
            uint32_t g = vb_d1_index_from_word(vb_sobol_word(dest_base + k, dirs));
            uint32_t blk = g / (uint32_t)N;
            if (k == 0) donor_block = blk;
            else if (blk != donor_block) {
                *fail_dim = (int)(d + 1);
                *fail_why = 2; /* dest block maps to two donor 4096-windows */
                return 0;
            }
            off[k] = g % (uint32_t)N;
        }
        memset(seen0, 0, sizeof seen0);
        memset(seen1, 0, sizeof seen1);
        for (k = 0; k < 2048u; ++k) {
            if (off[k] < 2048u) {
                if (seen0[off[k]]) {
                    *fail_dim = (int)(d + 1);
                    *fail_why = 3;
                    return 0;
                }
                seen0[off[k]] = 1;
                n0++;
            } else {
                uint32_t r = off[k] - 2048u;
                if (seen1[r]) {
                    *fail_dim = (int)(d + 1);
                    *fail_why = 3;
                    return 0;
                }
                seen1[r] = 1;
                n1++;
            }
        }
        if (!((n0 == 2048u && n1 == 0u) || (n0 == 0u && n1 == 2048u))) {
            *fail_dim = (int)(d + 1);
            *fail_why = 4; /* image of P0 is mixed halves */
            return 0;
        }
        /* complement P1 must fill the other half: implied by bijection of all 4096 */
    }
    return 1;
}
