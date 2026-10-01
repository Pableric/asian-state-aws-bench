#define _GNU_SOURCE

#include "bench_ab.h"
#include "cpp_directions.h"
#include "dim_provider.h"
#include "oracle_scalar.h"

#include <cpuid.h>
#include <errno.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

typedef void (*pass_fn)(const bench_job_t *);

typedef struct {
    const char *name;
    uint32_t ndims;
    uint32_t nblocks;
    int unit_modes;
} scope_t;

static const scope_t SCOPES[] = {
    {"d256_b16", 256, 16, 1},
    {"d1280_b256", 1280, 256, 0},
};

static void *xalloc(size_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes) != 0) {
        fprintf(stderr, "alloc %zu failed\n", bytes);
        exit(2);
    }
    memset(p, 0, bytes);
    return p;
}

static int host_avx512f(char *why, size_t n)
{
    unsigned a, b, c, d;
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[4096];
    int flag = 0;
    if (f != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            if (strncmp(line, "flags", 5) == 0) {
                flag = strstr(line, " avx512f") != NULL;
                break;
            }
        }
        fclose(f);
    }
    if (!__get_cpuid_count(7, 0, &a, &b, &c, &d) || (b & (1u << 16)) == 0u) {
        snprintf(why, n, "cpuid reports no avx512f");
        return 0;
    }
    if (!flag) {
        snprintf(why, n, "/proc/cpuinfo of the host lacks avx512f (emulator suspected)");
        return 0;
    }
    snprintf(why, n, "native avx512f");
    return 1;
}

static void cpu_model(char *out, size_t n)
{
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[512];
    snprintf(out, n, "unknown");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "model name", 10) == 0) {
            char *p = strchr(line, ':');
            if (p != NULL) {
                p += 2;
                p[strcspn(p, "\n")] = '\0';
                snprintf(out, n, "%s", p);
            }
            break;
        }
    }
    fclose(f);
}

typedef struct {
    int fd_cycles;
    int fd_instr;
} perf_t;

static int perf_open(perf_t *p)
{
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_GROUP;
    p->fd_cycles = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
    if (p->fd_cycles < 0) {
        p->fd_instr = -1;
        return 0;
    }
    attr.config = PERF_COUNT_HW_INSTRUCTIONS;
    attr.disabled = 0;
    p->fd_instr = (int)syscall(SYS_perf_event_open, &attr, 0, -1, p->fd_cycles, 0);
    if (p->fd_instr < 0) {
        close(p->fd_cycles);
        p->fd_cycles = -1;
        return 0;
    }
    return 1;
}

static void perf_read(const perf_t *p, uint64_t *cycles, uint64_t *instr)
{
    uint64_t buf[3] = {0, 0, 0};
    *cycles = 0;
    *instr = 0;
    if (p->fd_cycles < 0) {
        return;
    }
    if (read(p->fd_cycles, buf, sizeof(buf)) == (ssize_t)sizeof(buf) && buf[0] == 2u) {
        *cycles = buf[1];
        *instr = buf[2];
    }
}

typedef struct {
    uint64_t tsc;
    uint64_t ns;
    uint64_t cycles;
    uint64_t instructions;
} sample_t;

static uint64_t ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void timed(const perf_t *perf, pass_fn fn, const bench_job_t *job, sample_t *s)
{
    unsigned aux;
    uint64_t c0, i0, c1, i1, t0, t1, n0, n1;
    if (perf->fd_cycles >= 0) {
        ioctl(perf->fd_cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
    perf_read(perf, &c0, &i0);
    n0 = ns_now();
    _mm_lfence();
    t0 = __rdtscp(&aux);
    _mm_lfence();
    fn(job);
    _mm_lfence();
    t1 = __rdtscp(&aux);
    _mm_lfence();
    n1 = ns_now();
    perf_read(perf, &c1, &i1);
    if (perf->fd_cycles >= 0) {
        ioctl(perf->fd_cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    }
    s->tsc = t1 - t0;
    s->ns = n1 - n0;
    s->cycles = c1 - c0;
    s->instructions = i1 - i0;
}

static volatile uint64_t g_sink;

static void stream_touch(uint8_t *buf, size_t bytes)
{
    uint64_t acc = 0;
    size_t i;
    for (i = 0; i < bytes; i += 64) {
        acc += buf[i];
        buf[i] = (uint8_t)(acc + i);
    }
    g_sink = acc;
}

typedef struct {
    const scope_t *scope;
    uint32_t (*v)[32];
    cpp_dim_record_t *records;
    uint32_t *donor_bank;
    dim_affine_ctx_t *a_ctx;
    uint32_t *block_state;
    float *consumer_state;
    uint32_t *sink;
    float *weights;
    uint16_t *inv0;
} env_t;

static void build_env(env_t *e, const scope_t *s, uint32_t (*v)[32], cpp_dim_record_t *records,
                      const uint16_t *inv0)
{
    static uint16_t inv[4096];
    static uint16_t src[4096];
    uint32_t b;
    uint32_t d;
    uint32_t o;
    e->scope = s;
    e->v = v;
    e->records = records;
    e->inv0 = (uint16_t *)inv0;
    e->donor_bank = xalloc((size_t)s->nblocks * 16384u);
    e->a_ctx = xalloc((size_t)s->nblocks * s->ndims * sizeof(dim_affine_ctx_t));
    e->block_state = xalloc((size_t)s->ndims * 4u);
    e->consumer_state = xalloc(3u * 16384u);
    e->sink = xalloc(128);
    e->weights = xalloc((size_t)s->ndims * 4u);
    for (d = 0; d < s->ndims; ++d) {
        e->weights[d] = 1.0f / (float)s->ndims;
    }
    for (b = 0; b < s->nblocks; ++b) {
        float *donor = (float *)(e->donor_bank + (size_t)b * 4096u);
        uint32_t w = oracle_sobol_word(v[0], b * 4096u);
        for (o = 0; o < 4096u; ++o) {
            if (o != 0u) {
                w ^= v[0][__builtin_ctz(o)];
            }
            donor[o] = 1.0f + 0x1p-10f * ((float)(w >> 8) * 0x1p-24f - 0.5f);
        }
        if (oracle_block_inverse(v[0], b, inv) != 0) {
            fprintf(stderr, "oracle inverse failed block %u\n", b);
            exit(2);
        }
        for (d = 0; d < s->ndims; ++d) {
            oracle_block_sources(v[d], b, inv, src);
            if (dim_provider_prepare_affine(src, &e->a_ctx[(size_t)b * s->ndims + d]) != 0) {
                fprintf(stderr, "affine prepare failed D%u block %u\n", d + 1u, b);
                exit(2);
            }
        }
    }
}

static void reset_consumer(env_t *e)
{
    uint32_t i;
    for (i = 0; i < 4096u; ++i) {
        e->consumer_state[i] = 1.0f;
        e->consumer_state[4096u + i] = 0.0f;
        e->consumer_state[8192u + i] = 0.0f;
    }
    memset(e->sink, 0, 128);
}

static void reset_block_state(env_t *e, uint32_t block_first, uint32_t d0, uint32_t nd)
{
    uint32_t d;
    for (d = 0; d < nd; ++d) {
        e->block_state[d0 + d] =
            block_first == 0u ? 0u : cpp_block_state(&e->records[d0 + d], block_first - 1u);
    }
}

static bench_job_t pass_job(env_t *e)
{
    bench_job_t j;
    j.donor_bank = e->donor_bank;
    j.a_contexts = e->a_ctx;
    j.records = e->records;
    j.block_state = e->block_state;
    j.consumer_state = e->consumer_state;
    j.sink = e->sink;
    j.block_first = 0;
    j.nblocks = e->scope->nblocks;
    j.ndims = e->scope->ndims;
    j.weights = e->weights;
    return j;
}

/* One (block b, dimension d) unit: the pass kernels with nblocks = ndims = 1. */
static bench_job_t unit_job(env_t *e, uint32_t b, uint32_t d)
{
    bench_job_t j;
    j.donor_bank = e->donor_bank + (size_t)b * 4096u;
    j.a_contexts = &e->a_ctx[(size_t)b * e->scope->ndims + d];
    j.records = &e->records[d];
    j.block_state = &e->block_state[d];
    j.consumer_state = e->consumer_state;
    j.sink = e->sink;
    j.block_first = b;
    j.nblocks = 1;
    j.ndims = 1;
    j.weights = &e->weights[d];
    return j;
}

static int verify_scope(env_t *e, FILE *log, int *first)
{
    static const struct {
        const char *kind;
        pass_fn a;
        pass_fn b;
    } kinds[2] = {{"sink", bench_a_sink, bench_b_sink}, {"fused", bench_a_fused, bench_b_fused}};
    float *save = xalloc(3u * 16384u);
    uint32_t saved_sink[32];
    int ok = 1;
    int k;
    for (k = 0; k < 2; ++k) {
        bench_job_t j = pass_job(e);
        int same;
        reset_consumer(e);
        reset_block_state(e, 0, 0, e->scope->ndims);
        kinds[k].a(&j);
        memcpy(save, e->consumer_state, 3u * 16384u);
        memcpy(saved_sink, e->sink, 128);
        reset_consumer(e);
        reset_block_state(e, 0, 0, e->scope->ndims);
        kinds[k].b(&j);
        same = memcmp(save, e->consumer_state, 3u * 16384u) == 0 &&
               memcmp(saved_sink, e->sink, 128) == 0;
        ok &= same;
        if (log != NULL) {
            fprintf(log, "%s    {\"scope\": \"%s\", \"dimensions\": %u, \"blocks\": %u, "
                         "\"consumer\": \"%s\", \"a_equals_b_bitwise\": %s, "
                         "\"sink_word0\": \"0x%08x\", \"q_path0\": %.9g}",
                    *first ? "" : ",\n", e->scope->name, e->scope->ndims, e->scope->nblocks,
                    kinds[k].kind, same ? "true" : "false", e->sink[0],
                    (double)e->consumer_state[4096]);
            *first = 0;
        }
        printf("verify scope=%s consumer=%s a_equals_b=%d\n", e->scope->name, kinds[k].kind, same);
    }
    free(save);
    return ok;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t pct(uint64_t *v, size_t n, int permille)
{
    size_t i = (size_t)(((uint64_t)permille * (n - 1u)) / 1000u);
    return v[i];
}

typedef struct {
    double median, p10, p90;
} stat_t;

static stat_t stats_of(const sample_t *s, size_t n, int field)
{
    uint64_t *v = xalloc(n * sizeof(uint64_t));
    stat_t r;
    size_t i;
    for (i = 0; i < n; ++i) {
        v[i] = field == 0 ? s[i].tsc : field == 1 ? s[i].ns : field == 2 ? s[i].cycles
                                                                         : s[i].instructions;
    }
    qsort(v, n, sizeof(uint64_t), cmp_u64);
    r.median = (double)pct(v, n, 500);
    r.p10 = (double)pct(v, n, 100);
    r.p90 = (double)pct(v, n, 900);
    free(v);
    return r;
}

static void write_raw(const char *dir, const char *tag, const sample_t *s, size_t n)
{
    char path[512];
    FILE *f;
    size_t i;
    snprintf(path, sizeof(path), "%s/raw_%s.csv", dir, tag);
    f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    fprintf(f, "sample,tsc,ns,core_cycles,instructions\n");
    for (i = 0; i < n; ++i) {
        fprintf(f, "%zu,%llu,%llu,%llu,%llu\n", i, (unsigned long long)s[i].tsc,
                (unsigned long long)s[i].ns, (unsigned long long)s[i].cycles,
                (unsigned long long)s[i].instructions);
    }
    fclose(f);
}

static void emit_summary(FILE *js, int *first, const char *scope, const char *mode,
                         const char *consumer, const char *variant, const sample_t *s, size_t n,
                         double packets_per_sample, int have_perf)
{
    stat_t t = stats_of(s, n, 0);
    stat_t ns = stats_of(s, n, 1);
    stat_t cy = stats_of(s, n, 2);
    stat_t in = stats_of(s, n, 3);
    fprintf(js, "%s    {\"scope\": \"%s\", \"mode\": \"%s\", \"consumer\": \"%s\", "
                "\"variant\": \"%s\", \"samples\": %zu, \"packets_per_sample\": %.0f,\n"
                "     \"tsc\": {\"median\": %.0f, \"p10\": %.0f, \"p90\": %.0f, "
                "\"median_per_packet\": %.4f},\n"
                "     \"ns\": {\"median\": %.0f, \"p10\": %.0f, \"p90\": %.0f, "
                "\"median_per_packet\": %.4f},\n"
                "     \"core_cycles_median\": %.0f, \"core_cycles_per_packet\": %.4f, "
                "\"instructions_per_packet\": %.4f, \"perf_counters\": %s}",
            *first ? "" : ",\n", scope, mode, consumer, variant, n, packets_per_sample, t.median,
            t.p10, t.p90, t.median / packets_per_sample, ns.median, ns.p10, ns.p90,
            ns.median / packets_per_sample, have_perf ? cy.median : 0.0,
            have_perf ? cy.median / packets_per_sample : 0.0,
            have_perf ? in.median / packets_per_sample : 0.0, have_perf ? "true" : "false");
    *first = 0;
}

int main(int argc, char **argv)
{
    const char *data = "data/joe_kuo_d1280.bin";
    const char *out_dir = "results/bench_native";
    int verify_only = 0;
    uint32_t samples = 15;
    size_t cold_bytes = (size_t)64 << 20;
    size_t pressure_bytes = 32768;
    char why[160];
    char model[256];
    char path[512];
    uint32_t (*v)[32];
    cpp_dim_record_t *records;
    uint16_t inv0[4096];
    uint8_t *cold;
    uint8_t *pressure;
    perf_t perf;
    int have_perf;
    int native;
    int ok = 1;
    int first = 1;
    FILE *js;
    size_t si;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data = argv[++i];
        } else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--verify-only") == 0) {
            verify_only = 1;
        } else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc) {
            samples = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else {
            fprintf(stderr, "usage: %s [--data PATH] [--out-dir DIR] [--verify-only] "
                            "[--samples N]\n", argv[0]);
            return 2;
        }
    }
    mkdir(out_dir, 0755);
    native = host_avx512f(why, sizeof(why));
    cpu_model(model, sizeof(model));

    if (!verify_only && !native) {
        snprintf(path, sizeof(path), "%s/status.json", out_dir);
        js = fopen(path, "w");
        if (js != NULL) {
            fprintf(js, "{\"timed\": false, \"reason\": \"%s\", \"host_cpu\": \"%s\"}\n", why,
                    model);
            fclose(js);
        }
        printf("bench_ab SKIP timed=0 reason=\"%s\" host=\"%s\"\n", why, model);
        return 0;
    }

    v = xalloc(1280u * sizeof(*v));
    if (cpp_load_directions(data, 1280u, v) != 0) {
        fprintf(stderr, "cannot load %s\n", data);
        return 2;
    }
    records = xalloc(1280u * sizeof(cpp_dim_record_t));
    for (i = 0; i < 1280; ++i) {
        if (cpp_prepare_dimension(v[i], (uint32_t)i + 1u, &records[i]) != 0) {
            fprintf(stderr, "record prepare failed D%d\n", i + 1);
            return 2;
        }
    }
    if (oracle_block_inverse(v[0], 0, inv0) != 0) {
        return 2;
    }

    if (verify_only) {
        snprintf(path, sizeof(path), "%s/verify.json", out_dir);
        js = fopen(path, "w");
        if (js == NULL) {
            return 2;
        }
        fprintf(js, "{\n  \"timed\": false,\n  \"host_check\": \"%s\",\n  \"checks\": [\n", why);
        for (si = 0; si < sizeof(SCOPES) / sizeof(SCOPES[0]); ++si) {
            env_t e;
            build_env(&e, &SCOPES[si], v, records, inv0);
            ok &= verify_scope(&e, js, &first);
        }
        fprintf(js, "\n  ],\n  \"pass\": %s\n}\n", ok ? "true" : "false");
        fclose(js);
        printf("RESULT %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    have_perf = perf_open(&perf);
    if (!have_perf) {
        perf.fd_cycles = perf.fd_instr = -1;
    }
    cold = xalloc(cold_bytes);
    pressure = xalloc(pressure_bytes);
    snprintf(path, sizeof(path), "%s/summary.json", out_dir);
    js = fopen(path, "w");
    if (js == NULL) {
        return 2;
    }
    fprintf(js, "{\n  \"timed\": true,\n  \"host_cpu\": \"%s\",\n  \"host_check\": \"%s\",\n"
                "  \"perf_counters\": %s,\n  \"cold_evict_bytes\": %zu,\n"
                "  \"l1_pressure_bytes\": %zu,\n  \"results\": [\n",
            model, why, have_perf ? "true" : "false", cold_bytes, pressure_bytes);

    for (si = 0; si < sizeof(SCOPES) / sizeof(SCOPES[0]); ++si) {
        static const struct {
            const char *kind;
            pass_fn fn[2];
        } kinds[2] = {{"sink", {bench_a_sink, bench_b_sink}},
                      {"fused", {bench_a_fused, bench_b_fused}}};
        static const char *variant[2] = {"A_affine448", "B_computed"};
        env_t e;
        int k;
        int mode;
        build_env(&e, &SCOPES[si], v, records, inv0);
        ok &= verify_scope(&e, NULL, NULL);
        for (k = 0; k < 2; ++k) {
            for (mode = 0; mode < 2; ++mode) {
                const char *mname = mode == 0 ? "pass_warm" : "pass_cold_evict";
                sample_t *s[2];
                uint32_t n;
                int var;
                char tag[160];
                double pk = (double)e.scope->nblocks * e.scope->ndims * 128.0;
                s[0] = xalloc(samples * sizeof(sample_t));
                s[1] = xalloc(samples * sizeof(sample_t));
                for (n = 0; n < samples + 2u; ++n) {
                    for (var = 0; var < 2; ++var) {
                        bench_job_t j = pass_job(&e);
                        sample_t tmp;
                        reset_consumer(&e);
                        reset_block_state(&e, 0, 0, e.scope->ndims);
                        if (mode == 1) {
                            stream_touch(cold, cold_bytes);
                        }
                        timed(&perf, kinds[k].fn[var], &j, &tmp);
                        if (n >= 2u) {
                            s[var][n - 2u] = tmp;
                        }
                    }
                }
                for (var = 0; var < 2; ++var) {
                    snprintf(tag, sizeof(tag), "%s_%s_%s_%s", e.scope->name, mname,
                             kinds[k].kind, variant[var]);
                    write_raw(out_dir, tag, s[var], samples);
                    emit_summary(js, &first, e.scope->name, mname, kinds[k].kind, variant[var],
                                 s[var], samples, pk, have_perf);
                }
                free(s[0]);
                free(s[1]);
            }
            if (e.scope->unit_modes) {
                for (mode = 0; mode < 2; ++mode) {
                    const char *mname = mode == 0 ? "unit_warm" : "unit_l1_pressure_32KiB";
                    size_t nunits = (size_t)e.scope->nblocks * e.scope->ndims;
                    sample_t *s[2];
                    size_t u;
                    int var;
                    char tag[160];
                    s[0] = xalloc(nunits * sizeof(sample_t));
                    s[1] = xalloc(nunits * sizeof(sample_t));
                    for (u = 0; u < nunits; ++u) {
                        uint32_t b = (uint32_t)(u / e.scope->ndims);
                        uint32_t d = (uint32_t)(u % e.scope->ndims);
                        for (var = 0; var < 2; ++var) {
                            bench_job_t j = unit_job(&e, b, d);
                            reset_block_state(&e, b, d, 1);
                            if (mode == 0) {
                                kinds[k].fn[var](&j);
                                reset_block_state(&e, b, d, 1);
                            } else {
                                stream_touch(pressure, pressure_bytes);
                            }
                            timed(&perf, kinds[k].fn[var], &j, &s[var][u]);
                        }
                    }
                    for (var = 0; var < 2; ++var) {
                        snprintf(tag, sizeof(tag), "%s_%s_%s_%s", e.scope->name, mname,
                                 kinds[k].kind, variant[var]);
                        write_raw(out_dir, tag, s[var], nunits);
                        emit_summary(js, &first, e.scope->name, mname, kinds[k].kind,
                                     variant[var], s[var], nunits, 128.0, have_perf);
                    }
                    free(s[0]);
                    free(s[1]);
                }
            }
        }
        free(e.donor_bank);
        free(e.a_ctx);
        free(e.block_state);
        free(e.consumer_state);
        free(e.sink);
        free(e.weights);
    }
    fprintf(js, "\n  ],\n  \"a_equals_b_before_timing\": %s\n}\n", ok ? "true" : "false");
    fclose(js);
    printf("RESULT %s (timings in %s)\n", ok ? "PASS" : "FAIL", out_dir);
    return ok ? 0 : 1;
}
