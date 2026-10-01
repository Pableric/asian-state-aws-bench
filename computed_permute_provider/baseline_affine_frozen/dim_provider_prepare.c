#include "dim_provider.h"

#include <string.h>

static int is_permutation(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES])
{
    uint8_t seen[DIM_PROVIDER_BLOCK_VALUES];
    uint32_t i;
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < (uint32_t)DIM_PROVIDER_BLOCK_VALUES; ++i) {
        uint16_t s = source[i];
        if (s >= (uint16_t)DIM_PROVIDER_BLOCK_VALUES || seen[s] != 0u) {
            return 0;
        }
        seen[s] = 1u;
    }
    return 1;
}

int dim_provider_prepare_generic(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                                 dim_generic_ctx_t *out)
{
    uint32_t packet;
    uint32_t pattern_count = 0;
    if (source == NULL || out == NULL || ((uintptr_t)out & 63u) != 0u) {
        return -1;
    }
    if (!is_permutation(source)) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    for (packet = 0; packet < (uint32_t)DIM_PROVIDER_BLOCK_PACKETS; ++packet) {
        uint32_t half;
        for (half = 0; half < 2u; ++half) {
            uint32_t base = packet * (uint32_t)DIM_PROVIDER_PACKET_VALUES +
                            half * (uint32_t)DIM_PROVIDER_LANES;
            uint32_t line = (uint32_t)source[base] / (uint32_t)DIM_PROVIDER_LANES;
            uint32_t control[DIM_PROVIDER_LANES];
            uint32_t lane;
            uint32_t found;
            uint32_t pattern;
            for (lane = 0; lane < (uint32_t)DIM_PROVIDER_LANES; ++lane) {
                uint32_t src = (uint32_t)source[base + lane];
                if (src / (uint32_t)DIM_PROVIDER_LANES != line) {
                    return -1;
                }
                control[lane] = src % (uint32_t)DIM_PROVIDER_LANES;
            }
            found = pattern_count;
            for (pattern = 0; pattern < pattern_count; ++pattern) {
                unsigned match = 1u;
                for (lane = 0; lane < (uint32_t)DIM_PROVIDER_LANES; ++lane) {
                    if (out->patterns[pattern][lane] != control[lane]) {
                        match = 0u;
                        break;
                    }
                }
                if (match != 0u) {
                    found = pattern;
                    break;
                }
            }
            if (found == pattern_count) {
                if (pattern_count >= (uint32_t)DIM_PROVIDER_MAX_PATTERNS) {
                    return -1;
                }
                for (lane = 0; lane < (uint32_t)DIM_PROVIDER_LANES; ++lane) {
                    out->patterns[pattern_count][lane] = control[lane];
                }
                found = pattern_count;
                pattern_count += 1u;
            }
            out->select[packet][half] = (uint8_t)line;
            out->select[packet][2u + half] = (uint8_t)found;
        }
    }
    out->pattern_count = pattern_count;
    return 0;
}

int dim_provider_prepare_affine(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                                dim_affine_ctx_t *out)
{
    dim_generic_ctx_t gen __attribute__((aligned(64)));
    uint32_t npat;
    uint32_t j;
    uint32_t p;
    uint32_t c_of[DIM_PROVIDER_MAX_PATTERNS];
    uint32_t delta = 0;
    int have_delta = 0;
    if (out == NULL || ((uintptr_t)out & 63u) != 0u) {
        return -1;
    }
    if (dim_provider_prepare_generic(source, &gen) != 0) {
        return -1;
    }
    npat = gen.pattern_count;
    if (npat == 0u || npat > (uint32_t)DIM_PROVIDER_MAX_PATTERNS) {
        return -1;
    }
    for (j = 0; j < npat; ++j) {
        uint32_t c = gen.patterns[j][0] ^ gen.patterns[0][0];
        uint32_t lane;
        if (c > 15u) {
            return -1;
        }
        for (lane = 0; lane < (uint32_t)DIM_PROVIDER_LANES; ++lane) {
            if (gen.patterns[0][lane] > 15u ||
                gen.patterns[j][lane] != (gen.patterns[0][lane] ^ c)) {
                return -1;
            }
        }
        c_of[j] = c;
    }
    if (c_of[0] != 0u) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    for (p = 0; p < (uint32_t)DIM_PROVIDER_BLOCK_PACKETS; ++p) {
        uint32_t line_a = gen.select[p][0];
        uint32_t line_b = gen.select[p][1];
        uint32_t pa = gen.select[p][2];
        uint32_t pb = gen.select[p][3];
        uint32_t ca;
        uint32_t cb;
        uint32_t lane;
        if ((line_a ^ line_b) != 1u || pa >= npat || pb >= npat) {
            return -1;
        }
        ca = c_of[pa];
        cb = c_of[pb];
        if (!have_delta) {
            delta = ca ^ cb;
            have_delta = 1;
        } else if ((ca ^ cb) != delta) {
            return -1;
        }
        for (lane = 0; lane < (uint32_t)DIM_PROVIDER_LANES; ++lane) {
            uint32_t ctrl_a = gen.patterns[0][lane] ^ ca;
            uint32_t ctrl_b = ctrl_a ^ delta;
            if (ctrl_a > 15u || ctrl_b > 15u ||
                gen.patterns[pa][lane] != ctrl_a ||
                gen.patterns[pb][lane] != ctrl_b) {
                return -1;
            }
        }
        out->sel2[p][0] = (uint8_t)line_a;
        out->sel2[p][1] = (uint8_t)ca;
    }
    out->delta = delta;
    for (j = 0; j < (uint32_t)DIM_PROVIDER_LANES; ++j) {
        out->base_control[j] = gen.patterns[0][j];
        out->half_delta[j] = delta;
    }
    return 0;
}

int dim_provider_prepare_res2xor(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                                 dim_res2xor_ctx_t *out)
{
    /* Two live masks are exactly the affine representation. Dest-order
     * PACKET_RES2XOR XORs those masks when sel2[p].c_a changes. Consecutive
     * dest-window constancy is not required. */
    return dim_provider_prepare_affine(source, out);
}

static int gf2_pair_groups(const dim_generic_ctx_t *gen)
{
    uint32_t p;
    uint32_t n_pairs = 0;
    uint8_t pa_of[DIM_PROVIDER_BLOCK_PACKETS];
    uint8_t pb_of[DIM_PROVIDER_BLOCK_PACKETS];
    uint8_t seen_pair[256];
    uint32_t pair_id[DIM_PROVIDER_BLOCK_PACKETS];
    uint32_t pkts[DIM_PROVIDER_BLOCK_PACKETS];
    uint32_t count[DIM_PROVIDER_BLOCK_PACKETS];
    uint32_t id;
    memset(seen_pair, 0xFF, sizeof(seen_pair));
    memset(count, 0, sizeof(count));
    for (p = 0; p < (uint32_t)DIM_PROVIDER_BLOCK_PACKETS; ++p) {
        uint32_t key = ((uint32_t)gen->select[p][2] << 4) | (uint32_t)gen->select[p][3];
        if (seen_pair[key] == 0xFF) {
            if (n_pairs >= (uint32_t)DIM_PROVIDER_BLOCK_PACKETS) {
                return 0;
            }
            seen_pair[key] = (uint8_t)n_pairs;
            n_pairs += 1u;
        }
        pair_id[p] = seen_pair[key];
        pa_of[p] = gen->select[p][2];
        pb_of[p] = gen->select[p][3];
        count[pair_id[p]] += 1u;
    }
    for (id = 0; id < n_pairs; ++id) {
        uint32_t n = 0;
        uint32_t p0;
        uint32_t i;
        uint32_t span[32];
        uint32_t nspan = 1;
        uint32_t nbasis = 0;
        span[0] = 0;
        for (p = 0; p < (uint32_t)DIM_PROVIDER_BLOCK_PACKETS; ++p) {
            if (pair_id[p] == id) {
                pkts[n++] = p;
            }
        }
        if (n == 0u || n != count[id]) {
            return 0;
        }
        p0 = pkts[0];
        for (i = 0; i < n; ++i) {
            uint32_t v = pkts[i] ^ p0;
            uint32_t s;
            int found = 0;
            for (s = 0; s < nspan; ++s) {
                if (span[s] == v) {
                    found = 1;
                    break;
                }
            }
            if (found != 0) {
                continue;
            }
            if (nbasis >= 8u || nspan >= 16u) {
                return 0;
            }
            nbasis += 1u;
            {
                uint32_t old = nspan;
                for (s = 0; s < old; ++s) {
                    if (nspan >= 32u) {
                        return 0;
                    }
                    span[nspan++] = span[s] ^ v;
                }
            }
        }
        if ((1u << nbasis) != n || nspan != n) {
            return 0;
        }
        (void)pa_of;
        (void)pb_of;
    }
    return 1;
}

int dim_provider_diagnose(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                          dim_provider_diag_t *out)
{
    dim_generic_ctx_t gen __attribute__((aligned(64)));
    dim_affine_ctx_t aff __attribute__((aligned(64)));
    uint32_t p;
    uint32_t changes;
    uint32_t npat;
    uint32_t ninner;
    uint32_t g;
    int sequential;
    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->first_fail = DIM_PROVIDER_PRED_PERM;
    if (source == NULL || !is_permutation(source)) {
        return 0;
    }
    if (dim_provider_prepare_generic(source, &gen) != 0) {
        out->first_fail = DIM_PROVIDER_PRED_LINE;
        return 0;
    }
    out->npat = gen.pattern_count;
    if (gen.pattern_count == 0u ||
        gen.pattern_count > (uint32_t)DIM_PROVIDER_MAX_PATTERNS) {
        out->first_fail = DIM_PROVIDER_PRED_NPAT;
        return 0;
    }
    if (dim_provider_prepare_affine(source, &aff) != 0) {
        uint32_t j;
        uint32_t c_of[DIM_PROVIDER_MAX_PATTERNS];
        for (j = 0; j < gen.pattern_count; ++j) {
            uint32_t c = gen.patterns[j][0] ^ gen.patterns[0][0];
            uint32_t lane;
            if (c > 15u) {
                out->first_fail = DIM_PROVIDER_PRED_XOR_FAMILY;
                return 0;
            }
            for (lane = 0; lane < (uint32_t)DIM_PROVIDER_LANES; ++lane) {
                if (gen.patterns[0][lane] > 15u ||
                    gen.patterns[j][lane] != (gen.patterns[0][lane] ^ c)) {
                    out->first_fail = DIM_PROVIDER_PRED_XOR_FAMILY;
                    return 0;
                }
            }
            c_of[j] = c;
        }
        if (c_of[0] != 0u) {
            out->first_fail = DIM_PROVIDER_PRED_XOR_FAMILY;
            return 0;
        }
        for (p = 0; p < (uint32_t)DIM_PROVIDER_BLOCK_PACKETS; ++p) {
            uint32_t line_a = gen.select[p][0];
            uint32_t line_b = gen.select[p][1];
            uint32_t pa = gen.select[p][2];
            uint32_t pb = gen.select[p][3];
            uint32_t ca;
            uint32_t cb;
            uint32_t lane;
            uint32_t delta;
            out->fail_packet = p;
            if ((line_a ^ line_b) != 1u || pa >= gen.pattern_count ||
                pb >= gen.pattern_count) {
                out->first_fail = DIM_PROVIDER_PRED_TWO_MASK;
                return 0;
            }
            ca = c_of[pa];
            cb = c_of[pb];
            delta = ca ^ cb;
            for (lane = 0; lane < (uint32_t)DIM_PROVIDER_LANES; ++lane) {
                uint32_t ctrl_a = gen.patterns[0][lane] ^ ca;
                uint32_t ctrl_b = ctrl_a ^ delta;
                if (gen.patterns[pa][lane] != ctrl_a ||
                    gen.patterns[pb][lane] != ctrl_b) {
                    out->first_fail = DIM_PROVIDER_PRED_CONTROL;
                    return 0;
                }
                if ((uint32_t)source[p * 32u + lane] / 16u != line_a ||
                    (uint32_t)source[p * 32u + 16u + lane] / 16u != (line_a ^ 1u)) {
                    out->first_fail = DIM_PROVIDER_PRED_LINE_RECON;
                    return 0;
                }
            }
        }
        out->first_fail = DIM_PROVIDER_PRED_TWO_MASK;
        return 0;
    }
    out->delta = aff.delta;
    out->gf2_pair_groups = gf2_pair_groups(&gen);
    changes = 0;
    for (p = 1; p < (uint32_t)DIM_PROVIDER_BLOCK_PACKETS; ++p) {
        if (aff.sel2[p][1] != aff.sel2[p - 1u][1]) {
            changes += 1u;
        }
    }
    out->dest_c_changes = changes;
    npat = gen.pattern_count;
    sequential = 0;
    if (npat == 8u || npat == 16u) {
        uint32_t ngroups = npat;
        ninner = (uint32_t)DIM_PROVIDER_BLOCK_PACKETS / ngroups;
        sequential = 1;
        for (g = 0; g < ngroups; ++g) {
            uint32_t start = g * ninner;
            uint8_t c0 = aff.sel2[start][1];
            uint32_t inner;
            for (inner = 0; inner < ninner; ++inner) {
                if (aff.sel2[start + inner][1] != c0) {
                    sequential = 0;
                    out->fail_packet = start + inner;
                    break;
                }
            }
            if (sequential == 0) {
                break;
            }
        }
    }
    out->sequential_groups = sequential;
    out->first_fail = DIM_PROVIDER_PRED_OK;
    return 0;
}

void dim_provider_oracle_packet(const uint32_t *d1,
                                const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                                uint32_t packet,
                                uint32_t out[DIM_PROVIDER_PACKET_VALUES])
{
    uint32_t lane;
    uint32_t base = packet * (uint32_t)DIM_PROVIDER_PACKET_VALUES;
    for (lane = 0; lane < (uint32_t)DIM_PROVIDER_PACKET_VALUES; ++lane) {
        out[lane] = d1[source[base + lane]];
    }
}
