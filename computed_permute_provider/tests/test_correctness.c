#define _GNU_SOURCE

#include "cpp_directions.h"
#include "cpp_provider.h"
#include "dim_provider.h"
#include "oracle_scalar.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

typedef void (*block_fn)(const uint32_t *donor, const cpp_dim_record_t *rec, uint32_t *cst_io,
                         uint64_t block, uint32_t *out, uint64_t advance);
void cpp_test_block_main(const uint32_t *, const cpp_dim_record_t *, uint32_t *, uint64_t,
                         uint32_t *, uint64_t);
void cpp_test_block_alt1(const uint32_t *, const cpp_dim_record_t *, uint32_t *, uint64_t,
                         uint32_t *, uint64_t);
void cpp_test_block_alt2(const uint32_t *, const cpp_dim_record_t *, uint32_t *, uint64_t,
                         uint32_t *, uint64_t);
void cpp_test_block_affine(const uint32_t *donor, const dim_affine_ctx_t *ctx, uint32_t *out);
void cpp_test_advance_sweep(const cpp_dim_record_t *rec, uint64_t nblocks, uint32_t *states);

typedef struct __attribute__((aligned(64))) {
    uint32_t zmm[32][16];
    uint64_t gpr[16];
} cpp_probe_io_t;
typedef void (*probe_fn)(const cpp_probe_io_t *in, cpp_probe_io_t *out);

#define DECLARE_PROBES(s)                                                 \
    void cpp_probe_preload_##s(const cpp_probe_io_t *, cpp_probe_io_t *); \
    void cpp_probe_advance_##s(const cpp_probe_io_t *, cpp_probe_io_t *); \
    void cpp_probe_enter_##s(const cpp_probe_io_t *, cpp_probe_io_t *);   \
    void cpp_probe_first_##s(const cpp_probe_io_t *, cpp_probe_io_t *);   \
    void cpp_probe_next_##s(const cpp_probe_io_t *, cpp_probe_io_t *);
DECLARE_PROBES(main)
DECLARE_PROBES(alt1)
DECLARE_PROBES(alt2)

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };

typedef struct {
    const char *name;
    block_fn block;
    probe_fn preload, advance, enter, first, next;
    int d1, rec, p, ga, gb, t, cst, blk;
    int base, half, live, oa, ob;
} reg_map_t;

/* Must match the instantiations in test_wrappers.S. */
static const reg_map_t MAPS[3] = {
    {"main", cpp_test_block_main, cpp_probe_preload_main, cpp_probe_advance_main,
     cpp_probe_enter_main, cpp_probe_first_main, cpp_probe_next_main,
     RDI, RSI, R8, R9, R10, R11, R12, R13, 16, 17, 18, 0, 1},
    {"alt1", cpp_test_block_alt1, cpp_probe_preload_alt1, cpp_probe_advance_alt1,
     cpp_probe_enter_alt1, cpp_probe_first_alt1, cpp_probe_next_alt1,
     RBX, RBP, R15, R14, R13, R12, R11, R10, 31, 30, 29, 28, 27},
    {"alt2", cpp_test_block_alt2, cpp_probe_preload_alt2, cpp_probe_advance_alt2,
     cpp_probe_enter_alt2, cpp_probe_first_alt2, cpp_probe_next_alt2,
     RCX, RDX, R9, R12, R15, R8, R14, R11, 5, 9, 3, 20, 7},
};

static uint32_t (*g_v)[32];
static cpp_dim_record_t *g_rec;
static uint32_t g_dims_loaded;
static uint16_t g_inv0[4096];

/* Negative controls: each must make the run FAIL. */
static int g_fault_donor_block;
static int g_fault_skip_advance;

static uint32_t *g_donor;
static uint32_t *g_out_a;
static uint32_t *g_out_b;
static uint32_t *g_out_t;
static uint32_t *g_expected;
static dim_affine_ctx_t *g_ctx;

static void *xalloc(size_t align, size_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, align, bytes) != 0) {
        fprintf(stderr, "alloc %zu failed\n", bytes);
        exit(2);
    }
    memset(p, 0, bytes);
    return p;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

typedef struct {
    char name[32];
    uint32_t dim_first, dim_last;
    char blocks[160];
    uint64_t pairs;
    uint64_t packets;
    uint64_t lanes_a, lanes_b;
    uint64_t mismatch_a, mismatch_b;
    uint64_t prepare_fail_a;
    uint64_t oracle_fail;
    uint64_t lanes_first_packet, mismatch_first_packet;
    uint64_t lanes_last_packet, mismatch_last_packet;
    uint64_t donor_selection_fail;
    uint64_t permutation_fail;
    uint64_t state_checks, state_fail;
    uint64_t block_transitions;
    uint64_t repeat_runs, repeat_mismatch;
    uint64_t alt_runs, alt_mismatch;
    uint64_t reverse_runs, reverse_mismatch;
    uint64_t model_lanes, model_mismatch;
    char outputs_sha256[65];
    char contexts_sha256[65];
    double seconds;
    int ran;
} scope_result_t;

static uint32_t encode_u(uint32_t u)
{
    return (((u >> 4) << 6) << 16) | (u & 15u);
}

static uint64_t count_mismatch(const uint32_t *a, const uint32_t *b, uint32_t first, uint32_t n)
{
    uint64_t m = 0;
    uint32_t i;
    for (i = first; i < first + n; ++i) {
        m += (a[i] != b[i]);
    }
    return m;
}

typedef struct {
    int with_alt;
    int with_model;
    uint32_t *keep;
    sha256_ctx *out_hash;
    sha256_ctx *ctx_hash;
} pair_opts_t;

/* One (dimension, block): oracle, frozen affine baseline, computed provider.
 * cst_prev is the block state of block B-1 (ignored for B = 0); returns the
 * state of block B produced by the asm recurrence. */
static uint32_t run_pair(scope_result_t *r, uint32_t d, uint32_t blk, const uint16_t *inv,
                         uint32_t cst_prev, const pair_opts_t *o)
{
    static uint16_t src[4096];
    static uint8_t seen[4096];
    const cpp_dim_record_t *rec = &g_rec[d];
    uint32_t k;
    uint32_t cst;
    uint32_t adv = blk != 0u;
    uint32_t m;

    oracle_block_sources(g_v[d], blk, inv, src);
    for (k = 0; k < 4096u; ++k) {
        g_expected[k] = g_donor[src[k]];
    }

    if (dim_provider_prepare_affine(src, g_ctx) != 0) {
        r->prepare_fail_a += 1;
    } else {
        cpp_test_block_affine(g_donor, g_ctx, g_out_a);
        r->lanes_a += 4096;
        r->mismatch_a += count_mismatch(g_out_a, g_expected, 0, 4096);
        if (o->ctx_hash != NULL) {
            sha256_update(o->ctx_hash, g_ctx, sizeof(*g_ctx));
        }
    }

    cst = adv ? cst_prev : 0u;
    cpp_test_block_main(g_donor, rec, &cst, blk, g_out_b, g_fault_skip_advance ? 0u : adv);
    r->block_transitions += adv;
    r->state_checks += 1;
    if (cst != cpp_block_state(rec, blk) || cst != oracle_block_state(g_v[d], g_inv0, blk) ||
        cst != encode_u(src[0])) {
        r->state_fail += 1;
    }

    r->lanes_b += 4096;
    r->mismatch_b += count_mismatch(g_out_b, g_expected, 0, 4096);
    r->lanes_first_packet += 32;
    r->mismatch_first_packet += count_mismatch(g_out_b, g_expected, 0, 32);
    r->lanes_last_packet += 32;
    r->mismatch_last_packet += count_mismatch(g_out_b, g_expected, 4064, 32);

    memset(seen, 0, sizeof(seen));
    m = 0;
    for (k = 0; k < 4096u; ++k) {
        if ((g_out_b[k] >> 12) != blk) {
            r->donor_selection_fail += 1;
        }
        m += seen[g_out_b[k] & 4095u];
        seen[g_out_b[k] & 4095u] = 1u;
    }
    r->permutation_fail += (m != 0u);

    {
        uint32_t again = cst;
        cpp_test_block_main(g_donor, rec, &again, blk, g_out_t, 0);
        r->repeat_runs += 1;
        r->repeat_mismatch += count_mismatch(g_out_t, g_out_b, 0, 4096) + (again != cst);
    }

    if (o->with_alt) {
        uint32_t i;
        for (i = 1; i < 3u; ++i) {
            uint32_t ca = adv ? cst_prev : 0u;
            memset(g_out_t, 0, 16384);
            MAPS[i].block(g_donor, rec, &ca, blk, g_out_t, adv);
            r->alt_runs += 1;
            r->alt_mismatch += count_mismatch(g_out_t, g_expected, 0, 4096) + (ca != cst);
        }
    }

    if (o->with_model) {
        for (k = 0; k < 4096u; ++k) {
            r->model_mismatch += (cpp_record_source(rec, blk, k) != src[k]);
        }
        r->model_lanes += 4096;
    }

    if (o->out_hash != NULL) {
        sha256_update(o->out_hash, g_out_b, 16384);
    }
    if (o->keep != NULL) {
        memcpy(o->keep, g_out_b, 16384);
    }
    r->pairs += 1;
    r->packets += 128;
    return cst;
}

static void fill_donor(uint32_t blk)
{
    uint32_t o;
    uint32_t tag = g_fault_donor_block ? (blk ^ 1u) : blk;
    for (o = 0; o < 4096u; ++o) {
        g_donor[o] = tag * 4096u + o;
    }
}

static int block_inverse(scope_result_t *r, uint32_t blk, uint16_t *inv)
{
    if (oracle_block_inverse(g_v[0], blk, inv) != 0) {
        r->oracle_fail += 1;
        return -1;
    }
    return 0;
}

static void scope_canonical(scope_result_t *r, uint32_t dims, uint32_t nblocks)
{
    static uint16_t inv[4096];
    uint32_t *state = xalloc(64, dims * sizeof(uint32_t));
    uint32_t *keep = xalloc(64, (size_t)dims * 16384u);
    sha256_ctx oh;
    sha256_ctx ch;
    uint32_t b;
    uint32_t d;
    double t0 = now_s();
    snprintf(r->name, sizeof(r->name), "canonical");
    r->dim_first = 1;
    r->dim_last = dims;
    snprintf(r->blocks, sizeof(r->blocks), "0..%u consecutive (block state by recurrence from 0)",
             nblocks - 1u);
    sha256_init(&oh);
    sha256_init(&ch);
    for (b = 0; b < nblocks; ++b) {
        pair_opts_t o = {1, 1, NULL, &oh, &ch};
        if (block_inverse(r, b, inv) != 0) {
            continue;
        }
        fill_donor(b);
        for (d = 0; d < dims; ++d) {
            o.keep = keep + (size_t)d * 4096u;
            state[d] = run_pair(r, d, b, inv, state[d], &o);
        }
        for (d = dims; d-- > 0;) {
            uint32_t cst = state[d];
            cpp_test_block_main(g_donor, &g_rec[d], &cst, b, g_out_t, 0);
            r->reverse_runs += 1;
            r->reverse_mismatch += count_mismatch(g_out_t, keep + (size_t)d * 4096u, 0, 4096);
        }
    }
    sha256_hex(&oh, r->outputs_sha256);
    sha256_hex(&ch, r->contexts_sha256);
    r->seconds = now_s() - t0;
    r->ran = 1;
    free(state);
    free(keep);
}

static void scope_extended(scope_result_t *r, uint32_t dims, uint32_t b0, uint32_t b1)
{
    static uint16_t inv[4096];
    uint32_t *state = xalloc(64, dims * sizeof(uint32_t));
    sha256_ctx oh;
    sha256_ctx ch;
    uint32_t b;
    uint32_t d;
    double t0 = now_s();
    snprintf(r->name, sizeof(r->name), "extended");
    r->dim_first = 1;
    r->dim_last = dims;
    snprintf(r->blocks, sizeof(r->blocks),
             "%u..%u consecutive (state of block %u seeded from the oracle, then recurrence)",
             b0, b1 - 1u, b0 == 0u ? 0u : b0 - 1u);
    for (d = 0; d < dims; ++d) {
        state[d] = b0 == 0u ? 0u : oracle_block_state(g_v[d], g_inv0, b0 - 1u);
    }
    sha256_init(&oh);
    sha256_init(&ch);
    for (b = b0; b < b1; ++b) {
        pair_opts_t o = {0, 0, NULL, &oh, &ch};
        if (block_inverse(r, b, inv) != 0) {
            continue;
        }
        fill_donor(b);
        for (d = 0; d < dims; ++d) {
            state[d] = run_pair(r, d, b, inv, state[d], &o);
        }
    }
    sha256_hex(&oh, r->outputs_sha256);
    sha256_hex(&ch, r->contexts_sha256);
    r->seconds = now_s() - t0;
    r->ran = 1;
    free(state);
}

static uint32_t far_blocks(uint32_t *list)
{
    uint32_t n = 0;
    uint32_t k;
    uint64_t s = 0x243F6A8885A308D3ull;
    for (k = 1; k < 20u; ++k) {
        list[n++] = (1u << k) - 1u;
        list[n++] = 1u << k;
    }
    list[n++] = (1u << 20) - 2u;
    list[n++] = (1u << 20) - 1u;
    for (k = 0; k < 16u; ++k) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        list[n++] = 256u + (uint32_t)((s >> 33) % ((1u << 20) - 256u));
    }
    return n;
}

static void scope_far(scope_result_t *r, uint32_t dims)
{
    static uint16_t inv[4096];
    uint32_t list[64];
    uint32_t n = far_blocks(list);
    sha256_ctx oh;
    sha256_ctx ch;
    uint32_t i;
    uint32_t d;
    int len = 0;
    double t0 = now_s();
    snprintf(r->name, sizeof(r->name), "far_blocks");
    r->dim_first = 1;
    r->dim_last = dims;
    len = snprintf(r->blocks, sizeof(r->blocks),
                   "%u blocks: 2^k-1 and 2^k for k=1..19, 2^20-2, 2^20-1, 16 pseudo-random; "
                   "each entered by asm advance from oracle state of B-1", n);
    (void)len;
    sha256_init(&oh);
    sha256_init(&ch);
    for (i = 0; i < n; ++i) {
        uint32_t b = list[i];
        pair_opts_t o = {0, 0, NULL, &oh, &ch};
        if (block_inverse(r, b, inv) != 0) {
            continue;
        }
        fill_donor(b);
        for (d = 0; d < dims; ++d) {
            uint32_t prev = b == 0u ? 0u : oracle_block_state(g_v[d], g_inv0, b - 1u);
            (void)run_pair(r, d, b, inv, prev, &o);
        }
    }
    sha256_hex(&oh, r->outputs_sha256);
    sha256_hex(&ch, r->contexts_sha256);
    r->seconds = now_s() - t0;
    r->ran = 1;
}

typedef struct {
    uint32_t dims;
    uint32_t nblocks;
    uint64_t checks;
    uint64_t fail_vs_oracle;
    uint64_t seek_checks;
    uint64_t fail_vs_seek;
    double seconds;
    int ran;
} sweep_result_t;

/* All block states 0 .. 2^20-1 of every dimension: asm recurrence versus the
 * oracle definition c(B) = F(top12(X(4096B))) ^ [B odd] * 0xFFF, where
 * X(4096B) = X(4096(B-1)) ^ V[11] ^ V[12 + ctz B]. */
static void scope_sweep(sweep_result_t *r, uint32_t dims)
{
    const uint32_t nblocks = 1u << 20;
    uint32_t *states = xalloc(64, (size_t)nblocks * sizeof(uint32_t));
    uint32_t d;
    double t0 = now_s();
    r->dims = dims;
    r->nblocks = nblocks;
    for (d = 0; d < dims; ++d) {
        const uint32_t *v = g_v[d];
        uint32_t x = 0;
        uint32_t b;
        cpp_test_advance_sweep(&g_rec[d], nblocks, states);
        for (b = 0; b < nblocks; ++b) {
            uint32_t c;
            if (b != 0u) {
                x ^= v[11] ^ v[12 + __builtin_ctz(b)];
            }
            c = g_inv0[x >> 20] ^ ((b & 1u) ? 0xFFFu : 0u);
            r->checks += 1;
            r->fail_vs_oracle += (states[b] != encode_u(c));
            if ((b % 61u) == 0u || b == nblocks - 1u) {
                r->seek_checks += 1;
                r->fail_vs_seek += (states[b] != cpp_block_state(&g_rec[d], b));
            }
        }
    }
    r->seconds = now_s() - t0;
    r->ran = 1;
    free(states);
}

typedef struct {
    uint64_t probes;
    uint64_t registers_checked;
    uint64_t unexpected_change;
    uint64_t wrong_output;
    int ran;
    char failures[512];
} contract_result_t;

static void note_failure(contract_result_t *r, const char *what)
{
    size_t n = strlen(r->failures);
    if (n + strlen(what) + 2 < sizeof(r->failures)) {
        if (n != 0) {
            strcat(r->failures, ";");
        }
        strcat(r->failures, what);
    }
}

static void sentinel_fill(cpp_probe_io_t *io)
{
    uint32_t z;
    uint32_t l;
    for (z = 0; z < 32u; ++z) {
        for (l = 0; l < 16u; ++l) {
            io->zmm[z][l] = 0xA5000000u | (z << 8) | l;
        }
    }
    for (z = 0; z < 16u; ++z) {
        io->gpr[z] = 0x5EED5EED00000000ull | ((uint64_t)z << 16) | 0x00C0u;
    }
}

/* Compare every register except the written ones; then the written ones
 * against expected. */
static void contract_check(contract_result_t *r, const char *probe, const cpp_probe_io_t *in,
                           const cpp_probe_io_t *out, const cpp_probe_io_t *expect,
                           const int *zw, int nzw, const int *gw, int ngw)
{
    int z;
    int g;
    char buf[96];
    for (z = 0; z < 32; ++z) {
        int written = 0;
        int i;
        for (i = 0; i < nzw; ++i) {
            written |= zw[i] == z;
        }
        r->registers_checked += 1;
        if (memcmp(out->zmm[z], written ? expect->zmm[z] : in->zmm[z], 64) != 0) {
            if (written) {
                r->wrong_output += 1;
            } else {
                r->unexpected_change += 1;
            }
            snprintf(buf, sizeof(buf), "%s:zmm%d", probe, z);
            note_failure(r, buf);
        }
    }
    for (g = 0; g < 16; ++g) {
        int written = 0;
        int i;
        if (g == RSP) {
            continue;
        }
        for (i = 0; i < ngw; ++i) {
            written |= gw[i] == g;
        }
        r->registers_checked += 1;
        if (out->gpr[g] != (written ? expect->gpr[g] : in->gpr[g])) {
            if (written) {
                r->wrong_output += 1;
            } else {
                r->unexpected_change += 1;
            }
            snprintf(buf, sizeof(buf), "%s:gpr%d", probe, g);
            note_failure(r, buf);
        }
    }
    r->probes += 1;
}

static void model_packet(const uint32_t *donor, const uint32_t *live, const uint32_t *half,
                         uint32_t ga, uint32_t gb, uint32_t *oa, uint32_t *ob)
{
    uint32_t l;
    for (l = 0; l < 16u; ++l) {
        oa[l] = donor[ga / 4u + (live[l] & 15u)];
        ob[l] = donor[gb / 4u + ((live[l] ^ half[l]) & 15u)];
    }
}

static void scope_contract(contract_result_t *r)
{
    static cpp_probe_io_t in;
    static cpp_probe_io_t out;
    static cpp_probe_io_t ex;
    static const uint32_t dims_probe[3] = {1, 2, 200};
    static const uint32_t blocks_probe[4] = {1, 6, 64, (1u << 19)};
    static const uint32_t packets_probe[6] = {1, 2, 3, 32, 64, 127};
    uint32_t mi;
    char name[64];
    fill_donor(7);
    for (mi = 0; mi < 3u; ++mi) {
        const reg_map_t *m = &MAPS[mi];
        uint32_t di;
        for (di = 0; di < 3u; ++di) {
            const cpp_dim_record_t *rec = &g_rec[dims_probe[di] - 1u];
            uint32_t l;
            uint32_t bi;
            uint32_t pi;

            sentinel_fill(&in);
            in.gpr[m->rec] = (uint64_t)(uintptr_t)rec;
            ex = in;
            for (l = 0; l < 16u; ++l) {
                ex.zmm[m->base][l] = rec->ctl[l];
                ex.zmm[m->half][l] = rec->half;
            }
            m->preload(&in, &out);
            snprintf(name, sizeof(name), "%s.preload.D%u", m->name, dims_probe[di]);
            contract_check(r, name, &in, &out, &ex, (int[]){m->base, m->half}, 2, NULL, 0);

            for (bi = 0; bi < 4u; ++bi) {
                uint32_t s = cpp_block_state(rec, blocks_probe[bi] - 1u);
                uint32_t tz = (uint32_t)__builtin_ctz(blocks_probe[bi]);
                sentinel_fill(&in);
                in.gpr[m->rec] = (uint64_t)(uintptr_t)rec;
                in.gpr[m->blk] = 0xFFFFFFFF00000000ull | blocks_probe[bi];
                in.gpr[m->cst] = 0xFFFFFFFF00000000ull | s;
                ex = in;
                ex.gpr[m->cst] = (uint64_t)(s ^ rec->cstep[tz]);
                ex.gpr[m->t] = tz;
                m->advance(&in, &out);
                snprintf(name, sizeof(name), "%s.advance.D%u.B%u", m->name, dims_probe[di],
                         blocks_probe[bi]);
                contract_check(r, name, &in, &out, &ex, NULL, 0, (int[]){m->cst, m->t}, 2);
                if ((uint32_t)out.gpr[m->cst] != cpp_block_state(rec, blocks_probe[bi])) {
                    r->wrong_output += 1;
                    note_failure(r, "advance!=seek");
                }
            }

            {
                uint32_t s = cpp_block_state(rec, 6);
                sentinel_fill(&in);
                in.gpr[m->cst] = 0xFFFFFFFF00000000ull | s;
                for (l = 0; l < 16u; ++l) {
                    in.zmm[m->base][l] = rec->ctl[l];
                }
                ex = in;
                ex.gpr[m->ga] = s >> 16;
                ex.gpr[m->gb] = (s >> 16) ^ 64u;
                for (l = 0; l < 16u; ++l) {
                    ex.zmm[m->live][l] = rec->ctl[l] ^ s;
                }
                m->enter(&in, &out);
                snprintf(name, sizeof(name), "%s.enter.D%u", m->name, dims_probe[di]);
                contract_check(r, name, &in, &out, &ex, (int[]){m->live}, 1,
                               (int[]){m->ga, m->gb}, 2);
            }

            {
                cpp_packet_state_t st;
                uint32_t s = cpp_block_state(rec, 7);
                cpp_packet_state(rec, s, 0, &st);
                sentinel_fill(&in);
                in.gpr[m->d1] = (uint64_t)(uintptr_t)g_donor;
                in.gpr[m->ga] = st.ga;
                in.gpr[m->gb] = st.gb;
                for (l = 0; l < 16u; ++l) {
                    in.zmm[m->live][l] = rec->ctl[l] ^ st.lane_xor ^ 0x5A5A5A50u;
                    in.zmm[m->half][l] = rec->half ^ 0x0F0F0F00u;
                }
                ex = in;
                model_packet(g_donor, in.zmm[m->live], in.zmm[m->half], st.ga, st.gb,
                             ex.zmm[m->oa], ex.zmm[m->ob]);
                m->first(&in, &out);
                snprintf(name, sizeof(name), "%s.first.D%u", m->name, dims_probe[di]);
                contract_check(r, name, &in, &out, &ex, (int[]){m->oa, m->ob}, 2, NULL, 0);
            }

            for (pi = 0; pi < 6u; ++pi) {
                cpp_packet_state_t prev;
                cpp_packet_state_t st;
                uint32_t p = packets_probe[pi];
                uint32_t tz = (uint32_t)__builtin_ctz(p);
                uint32_t s = cpp_block_state(rec, 7);
                cpp_packet_state(rec, s, p - 1u, &prev);
                cpp_packet_state(rec, s, p, &st);
                sentinel_fill(&in);
                in.gpr[m->d1] = (uint64_t)(uintptr_t)g_donor;
                in.gpr[m->rec] = (uint64_t)(uintptr_t)rec;
                in.gpr[m->p] = 0xFFFFFFFF00000000ull | p;
                in.gpr[m->ga] = prev.ga;
                in.gpr[m->gb] = prev.gb;
                for (l = 0; l < 16u; ++l) {
                    in.zmm[m->live][l] = rec->ctl[l] ^ prev.lane_xor ^ 0x77777770u;
                    in.zmm[m->half][l] = rec->half;
                }
                ex = in;
                ex.gpr[m->t] = tz;
                ex.gpr[m->ga] = st.ga;
                ex.gpr[m->gb] = st.gb;
                for (l = 0; l < 16u; ++l) {
                    ex.zmm[m->live][l] = in.zmm[m->live][l] ^ rec->dlane[tz];
                }
                if (st.ga != (prev.ga ^ rec->daddr[tz])) {
                    r->wrong_output += 1;
                    note_failure(r, "packet seek != recurrence");
                }
                model_packet(g_donor, ex.zmm[m->live], in.zmm[m->half], st.ga, st.gb,
                             ex.zmm[m->oa], ex.zmm[m->ob]);
                m->next(&in, &out);
                snprintf(name, sizeof(name), "%s.next.D%u.p%u", m->name, dims_probe[di], p);
                contract_check(r, name, &in, &out, &ex, (int[]){m->live, m->oa, m->ob}, 3,
                               (int[]){m->t, m->ga, m->gb}, 3);
            }
        }
    }
    r->ran = 1;
}

static void json_scope(FILE *f, const scope_result_t *r, int comma)
{
    fprintf(f,
            "    {\"scope\": \"%s\", \"dimensions\": \"D%u..D%u\", \"blocks\": \"%s\",\n"
            "     \"dimension_block_pairs\": %llu, \"packets\": %llu,\n"
            "     \"lanes_checked_affine_vs_oracle\": %llu, \"mismatch_affine_vs_oracle\": %llu,\n"
            "     \"affine_prepare_failures\": %llu,\n"
            "     \"lanes_checked_computed_vs_oracle\": %llu, \"mismatch_computed_vs_oracle\": %llu,\n"
            "     \"oracle_block_inverse_failures\": %llu,\n"
            "     \"first_packet_lanes\": %llu, \"first_packet_mismatch\": %llu,\n"
            "     \"last_packet_lanes\": %llu, \"last_packet_mismatch\": %llu,\n"
            "     \"donor_selection_failures\": %llu, \"duplicate_or_omitted_blocks\": %llu,\n"
            "     \"block_state_checks\": %llu, \"block_state_failures\": %llu,\n"
            "     \"asm_block_transitions\": %llu,\n"
            "     \"repeat_runs\": %llu, \"repeat_mismatch\": %llu,\n"
            "     \"alternate_register_runs\": %llu, \"alternate_register_mismatch\": %llu,\n"
            "     \"reverse_order_runs\": %llu, \"reverse_order_mismatch\": %llu,\n"
            "     \"record_model_lanes\": %llu, \"record_model_mismatch\": %llu,\n"
            "     \"computed_outputs_sha256\": \"%s\", \"affine_contexts_sha256\": \"%s\",\n"
            "     \"seconds\": %.1f}%s\n",
            r->name, r->dim_first, r->dim_last, r->blocks, (unsigned long long)r->pairs,
            (unsigned long long)r->packets, (unsigned long long)r->lanes_a,
            (unsigned long long)r->mismatch_a, (unsigned long long)r->prepare_fail_a,
            (unsigned long long)r->lanes_b, (unsigned long long)r->mismatch_b,
            (unsigned long long)r->oracle_fail, (unsigned long long)r->lanes_first_packet,
            (unsigned long long)r->mismatch_first_packet, (unsigned long long)r->lanes_last_packet,
            (unsigned long long)r->mismatch_last_packet,
            (unsigned long long)r->donor_selection_fail, (unsigned long long)r->permutation_fail,
            (unsigned long long)r->state_checks, (unsigned long long)r->state_fail,
            (unsigned long long)r->block_transitions, (unsigned long long)r->repeat_runs,
            (unsigned long long)r->repeat_mismatch, (unsigned long long)r->alt_runs,
            (unsigned long long)r->alt_mismatch, (unsigned long long)r->reverse_runs,
            (unsigned long long)r->reverse_mismatch, (unsigned long long)r->model_lanes,
            (unsigned long long)r->model_mismatch, r->outputs_sha256, r->contexts_sha256,
            r->seconds, comma ? "," : "");
}

static int scope_ok(const scope_result_t *r)
{
    return r->mismatch_a == 0 && r->mismatch_b == 0 && r->prepare_fail_a == 0 &&
           r->oracle_fail == 0 && r->mismatch_first_packet == 0 && r->mismatch_last_packet == 0 &&
           r->donor_selection_fail == 0 && r->permutation_fail == 0 && r->state_fail == 0 &&
           r->repeat_mismatch == 0 && r->alt_mismatch == 0 && r->reverse_mismatch == 0 &&
           r->model_mismatch == 0 && r->lanes_b == r->pairs * 4096u &&
           r->lanes_a == r->pairs * 4096u;
}

static int has_scope(const char *list, const char *name)
{
    const char *p = strstr(list, name);
    size_t n = strlen(name);
    while (p != NULL) {
        if ((p == list || p[-1] == ',') && (p[n] == '\0' || p[n] == ',')) {
            return 1;
        }
        p = strstr(p + 1, name);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *data = "data/joe_kuo_d1280.bin";
    const char *scopes = "contract,canonical,far,sweep";
    const char *json = NULL;
    const char *records_out = NULL;
    const char *fault = NULL;
    uint32_t ext_b0 = 0;
    uint32_t ext_b1 = 256;
    uint32_t ext_dims = 1280;
    uint32_t far_dims = 1280;
    uint32_t sweep_dims = 1280;
    uint32_t prepare_fail = 0;
    size_t rec_bytes;
    char rec_sha[65];
    scope_result_t canon;
    scope_result_t ext;
    scope_result_t far;
    sweep_result_t sweep;
    contract_result_t contract;
    int ok = 1;
    int i;
    uint32_t d;
    FILE *f;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data = argv[++i];
        } else if (strcmp(argv[i], "--scopes") == 0 && i + 1 < argc) {
            scopes = argv[++i];
        } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json = argv[++i];
        } else if (strcmp(argv[i], "--records-out") == 0 && i + 1 < argc) {
            records_out = argv[++i];
        } else if (strcmp(argv[i], "--block-begin") == 0 && i + 1 < argc) {
            ext_b0 = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--block-end") == 0 && i + 1 < argc) {
            ext_b1 = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--dims") == 0 && i + 1 < argc) {
            ext_dims = far_dims = sweep_dims = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--fault") == 0 && i + 1 < argc) {
            fault = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--data PATH] [--scopes LIST] [--json OUT] "
                            "[--records-out PATH] [--dims N] [--block-begin B] [--block-end B] "
                            "[--fault record_daddr|record_dlane|record_half|record_ctl|"
                            "record_cstep|donor_block|skip_advance]\n",
                    argv[0]);
            return 2;
        }
    }
    if (fault != NULL) {
        g_fault_donor_block = strcmp(fault, "donor_block") == 0;
        g_fault_skip_advance = strcmp(fault, "skip_advance") == 0;
    }

    g_dims_loaded = 1280;
    g_v = xalloc(64, g_dims_loaded * sizeof(*g_v));
    if (cpp_load_directions(data, g_dims_loaded, g_v) != 0) {
        fprintf(stderr, "cannot load %s\n", data);
        return 2;
    }
    rec_bytes = ((g_dims_loaded * sizeof(cpp_dim_record_t)) + 4095u) & ~(size_t)4095u;
    g_rec = xalloc(4096, rec_bytes);
    for (d = 0; d < g_dims_loaded; ++d) {
        prepare_fail += (cpp_prepare_dimension(g_v[d], d + 1u, &g_rec[d]) != 0);
    }
    if (fault != NULL) {
        if (strcmp(fault, "record_daddr") == 0) {
            g_rec[1].daddr[3] ^= 64u;
        } else if (strcmp(fault, "record_dlane") == 0) {
            g_rec[2].dlane[0] ^= 1u;
        } else if (strcmp(fault, "record_half") == 0) {
            g_rec[4].half ^= 2u;
        } else if (strcmp(fault, "record_ctl") == 0) {
            g_rec[7].ctl[9] ^= 4u;
        } else if (strcmp(fault, "record_cstep") == 0) {
            g_rec[10].cstep[0] ^= 0xFu;
        } else if (!g_fault_donor_block && !g_fault_skip_advance) {
            fprintf(stderr, "unknown fault %s\n", fault);
            return 2;
        }
    }
    {
        sha256_ctx h;
        sha256_init(&h);
        sha256_update(&h, g_rec, g_dims_loaded * sizeof(cpp_dim_record_t));
        sha256_hex(&h, rec_sha);
    }
    if (records_out != NULL) {
        FILE *rf = fopen(records_out, "wb");
        if (rf == NULL || fwrite(g_rec, sizeof(cpp_dim_record_t), g_dims_loaded, rf) !=
                              g_dims_loaded) {
            fprintf(stderr, "cannot write %s\n", records_out);
            return 2;
        }
        fclose(rf);
    }
    if (mprotect(g_rec, rec_bytes, PROT_READ) != 0) {
        fprintf(stderr, "mprotect failed\n");
        return 2;
    }
    if (oracle_block_inverse(g_v[0], 0, g_inv0) != 0) {
        fprintf(stderr, "D1 block 0 is not one point per cell\n");
        return 2;
    }

    g_donor = xalloc(64, 16384);
    g_out_a = xalloc(64, 16384);
    g_out_b = xalloc(64, 16384);
    g_out_t = xalloc(64, 16384);
    g_expected = xalloc(64, 16384);
    g_ctx = xalloc(64, sizeof(*g_ctx));
    memset(&canon, 0, sizeof(canon));
    memset(&ext, 0, sizeof(ext));
    memset(&far, 0, sizeof(far));
    memset(&sweep, 0, sizeof(sweep));
    memset(&contract, 0, sizeof(contract));

    if (has_scope(scopes, "contract")) {
        scope_contract(&contract);
        ok &= contract.unexpected_change == 0 && contract.wrong_output == 0;
        printf("contract probes=%llu registers=%llu unexpected_change=%llu wrong_output=%llu %s\n",
               (unsigned long long)contract.probes, (unsigned long long)contract.registers_checked,
               (unsigned long long)contract.unexpected_change,
               (unsigned long long)contract.wrong_output, contract.failures);
    }
    if (has_scope(scopes, "canonical")) {
        scope_canonical(&canon, 256, 16);
        ok &= scope_ok(&canon);
        printf("canonical pairs=%llu mismatch_affine=%llu mismatch_computed=%llu ok=%d (%.1fs)\n",
               (unsigned long long)canon.pairs, (unsigned long long)canon.mismatch_a,
               (unsigned long long)canon.mismatch_b, scope_ok(&canon), canon.seconds);
    }
    if (has_scope(scopes, "extended")) {
        scope_extended(&ext, ext_dims, ext_b0, ext_b1);
        ok &= scope_ok(&ext);
        printf("extended blocks=%u..%u pairs=%llu mismatch_affine=%llu mismatch_computed=%llu "
               "ok=%d (%.1fs)\n",
               ext_b0, ext_b1 - 1u, (unsigned long long)ext.pairs,
               (unsigned long long)ext.mismatch_a, (unsigned long long)ext.mismatch_b,
               scope_ok(&ext), ext.seconds);
    }
    if (has_scope(scopes, "far")) {
        scope_far(&far, far_dims);
        ok &= scope_ok(&far);
        printf("far pairs=%llu mismatch_affine=%llu mismatch_computed=%llu ok=%d (%.1fs)\n",
               (unsigned long long)far.pairs, (unsigned long long)far.mismatch_a,
               (unsigned long long)far.mismatch_b, scope_ok(&far), far.seconds);
    }
    if (has_scope(scopes, "sweep")) {
        scope_sweep(&sweep, sweep_dims);
        ok &= sweep.fail_vs_oracle == 0 && sweep.fail_vs_seek == 0;
        printf("sweep dims=%u blocks=%u checks=%llu fail_oracle=%llu fail_seek=%llu (%.1fs)\n",
               sweep.dims, sweep.nblocks, (unsigned long long)sweep.checks,
               (unsigned long long)sweep.fail_vs_oracle, (unsigned long long)sweep.fail_vs_seek,
               sweep.seconds);
    }
    ok &= prepare_fail == 0;

    if (json != NULL) {
        int first = 1;
        f = fopen(json, "w");
        if (f == NULL) {
            fprintf(stderr, "cannot write %s\n", json);
            return 2;
        }
        fprintf(f, "{\n  \"pass\": %s,\n", ok ? "true" : "false");
        fprintf(f, "  \"records\": {\"dimensions\": %u, \"prepare_failures\": %u, "
                   "\"bytes\": %zu, \"sha256\": \"%s\", \"mprotect_read_only_during_tests\": true},\n",
                g_dims_loaded, prepare_fail, g_dims_loaded * sizeof(cpp_dim_record_t), rec_sha);
        if (contract.ran) {
            fprintf(f, "  \"contract\": {\"probes\": %llu, \"registers_checked\": %llu, "
                       "\"unexpected_change\": %llu, \"wrong_output\": %llu, "
                       "\"register_maps\": [\"main\", \"alt1\", \"alt2\"], \"failures\": \"%s\"},\n",
                    (unsigned long long)contract.probes,
                    (unsigned long long)contract.registers_checked,
                    (unsigned long long)contract.unexpected_change,
                    (unsigned long long)contract.wrong_output, contract.failures);
        }
        if (sweep.ran) {
            fprintf(f, "  \"block_state_sweep\": {\"dimensions\": \"D1..D%u\", \"blocks\": \"0..%u\", "
                       "\"checks_vs_oracle\": %llu, \"failures_vs_oracle\": %llu, "
                       "\"checks_vs_seek\": %llu, \"failures_vs_seek\": %llu, \"seconds\": %.1f},\n",
                    sweep.dims, sweep.nblocks - 1u, (unsigned long long)sweep.checks,
                    (unsigned long long)sweep.fail_vs_oracle,
                    (unsigned long long)sweep.seek_checks,
                    (unsigned long long)sweep.fail_vs_seek, sweep.seconds);
        }
        fprintf(f, "  \"scopes\": [\n");
        {
            const scope_result_t *all[3] = {&canon, &ext, &far};
            int n = 0;
            int j;
            for (j = 0; j < 3; ++j) {
                n += all[j]->ran;
            }
            for (j = 0; j < 3; ++j) {
                if (all[j]->ran) {
                    json_scope(f, all[j], --n > 0);
                    first = 0;
                }
            }
        }
        (void)first;
        fprintf(f, "  ]\n}\n");
        fclose(f);
    }
    printf("records prepare_failures=%u sha256=%s\n", prepare_fail, rec_sha);
    printf("RESULT %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
