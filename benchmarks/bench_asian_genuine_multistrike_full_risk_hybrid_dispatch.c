#define main asian_genuine_msfr_qualified_benchmark_main
#include "bench_asian_genuine_multistrike_full_risk.c"
#undef main

#include "private/asian_genuine_multistrike_full_risk_hybrid_dispatch_diag.h"

enum followup_candidate {
    FOLLOWUP_PHASE1 = 0,
    FOLLOWUP_TILE2 = 1,
    FOLLOWUP_TILE4 = 2,
    FOLLOWUP_HYBRID = 3,
    FOLLOWUP_CANDIDATES = 4,
};

static const char *const followup_names[FOLLOWUP_CANDIDATES] = {
    "existing_phase1_direct_full_risk",
    "qualified_uniform_tile2",
    "qualified_uniform_tile4",
    "hybrid_dispatch",
};

static int followup_prepare_fixture(uint32_t n, uint32_t k)
{
    static const float full_grid[32] = {
        70.0f, 72.0f, 74.0f, 76.0f, 78.0f, 80.0f, 82.0f, 84.0f,
        86.0f, 88.0f, 90.0f, 92.0f, 94.0f, 96.0f, 98.0f, 100.0f,
        100.5f, 102.0f, 104.0f, 106.0f, 108.0f, 110.0f, 112.0f,
        114.0f, 116.0f, 118.0f, 120.0f, 122.0f, 124.0f, 126.0f,
        128.0f, 130.0f,
    };
    /* Reuse the frozen qualified fixture without teaching it new K values. */
    if (prepare_fixture(n, 32u) != 0) return -1;
    b.k = k;
    if (asian_genuine_msfr_prepare_strikes(
          b.strike_controls, 100.0, 0.03, 0.0, 0.20, 1.0, n,
          full_grid, k) != ASIAN_GENUINE_MSFR_OK ||
        asian_genuine_msfr_prepare_consumer_context(
          b.consumer_context, b.strike_controls) != ASIAN_GENUINE_MSFR_OK) {
        release_fixture();
        return -1;
    }
    for (uint32_t strike = 0; strike < k; ++strike)
        if (asian_genuine_aad_phase1_prepare_controls(
              &b.phase_controls[strike], 100.0, full_grid[strike], 0.03,
              0.0, 0.20, 1.0, n) != ASIAN_GENUINE_AAD_PHASE1_OK ||
            asian_genuine_aad_phase1_prepare_context(
              &b.phase_contexts[strike], routes(0), b.tape,
              &b.phase_controls[strike], 100.0, full_grid[strike], 0.03,
              0.0, 0.20, 1.0, n) != ASIAN_GENUINE_AAD_PHASE1_OK) {
            release_fixture();
            return -1;
        }
    return 0;
}

static double run_followup(uint32_t candidate, int cv)
{
    produce(0);
    if (candidate == FOLLOWUP_PHASE1) {
        phase_leaf(0u, cv, &b.phase_values[0]);
        phase_finalize(0u, &b.phase_values[0]);
    } else if (candidate == FOLLOWUP_TILE2 ||
               candidate == FOLLOWUP_TILE4) {
        asian_genuine_msfr_basis_forward_diag(b.basis_context, b.basis);
        consume_basis(cv, candidate == FOLLOWUP_TILE2 ? 2u : 4u);
    } else if (candidate == FOLLOWUP_HYBRID) {
        if (asian_genuine_msfr_accumulator_init(
              b.accumulator, b.consumer_context, cv) != ASIAN_GENUINE_MSFR_OK)
            abort();
        if (b.k == 1u) {
            if (asian_genuine_msfr_hybrid_consume_block_diag(
                  NULL, b.consumer_context, cv, &b.phase_contexts[0],
                  b.accumulator) != ASIAN_GENUINE_MSFR_OK)
                abort();
        } else {
            asian_genuine_msfr_basis_forward_diag(b.basis_context, b.basis);
            if (asian_genuine_msfr_hybrid_consume_block_diag(
                  b.basis, b.consumer_context, cv, NULL, b.accumulator) !=
                  ASIAN_GENUINE_MSFR_OK)
                abort();
        }
        if (asian_genuine_msfr_finalize(
              b.consumer_context, b.accumulator, b.output) !=
              ASIAN_GENUINE_MSFR_OK)
            abort();
    } else {
        abort();
    }
    return output_sum(4u);
}

static void reset_followup(void)
{
    reset_candidate(BASIS_TILE2);
}

static uint32_t active_candidates(uint32_t k, uint32_t active[3])
{
    if (k == 1u) {
        active[0] = FOLLOWUP_PHASE1;
        active[1] = FOLLOWUP_HYBRID;
        return 2u;
    }
    active[0] = FOLLOWUP_TILE2;
    active[1] = FOLLOWUP_TILE4;
    active[2] = FOLLOWUP_HYBRID;
    return 3u;
}

static int followup_preflight_case(uint32_t n, uint32_t k, int cv)
{
    if (followup_prepare_fixture(n, k) != 0) return -1;
    uint32_t active[3];
    const uint32_t count = active_candidates(k, active);
    asian_genuine_msfr_output_t *reference = a64(sizeof(*reference));
    if (reference == NULL) {
        release_fixture();
        return -1;
    }
    for (uint32_t i = 0; i < count; ++i) {
        reset_followup();
        run_followup(active[i], cv);
        if (i == 0u)
            memcpy(reference, b.output, sizeof(*reference));
        else if (memcmp(reference, b.output, sizeof(*reference)) != 0) {
            fprintf(stderr,
                    "hybrid benchmark preflight mismatch N=%u K=%u "
                    "estimator=%d candidate=%s\n",
                    n, k, cv, followup_names[active[i]]);
            free(reference);
            release_fixture();
            return -1;
        }
    }
    asian_genuine_msfr_hybrid_plan_t plan;
    const int status = asian_genuine_msfr_hybrid_plan_diag(k, &plan);
    printf("hybrid_preflight N=%u K=%u estimator=%s phase1_calls=%u "
           "tile2_calls=%u tile4_calls=%u padded_outputs=%u status=%s\n",
           n, k, cv ? "geometric_cv" : "arithmetic", plan.phase1_calls,
           plan.tile2_calls, plan.tile4_calls, plan.padded_outputs,
           status == ASIAN_GENUINE_MSFR_OK ? "PASS" : "FAIL");
    free(reference);
    release_fixture();
    return status == ASIAN_GENUINE_MSFR_OK ? 0 : -1;
}

static int followup_preflight(void)
{
    static const uint32_t representative_k[] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 31u, 32u,
    };
    for (uint32_t i = 0; i < sizeof(representative_k) /
                                  sizeof(representative_k[0]); ++i)
        for (int cv = 0; cv < 2; ++cv)
            if (followup_preflight_case(16u, representative_k[i], cv) != 0)
                return -1;
    if (followup_preflight_case(2u, 1u, 0) != 0 ||
        followup_preflight_case(256u, 1u, 1) != 0)
        return -1;
    puts("asian_genuine_multistrike_full_risk_hybrid benchmark_preflight=PASS "
         "representative_K=1,2,3,4,5,6,7,8,31,32 K1_N=2,16,256 "
         "estimators=2 bit_identity=yes");
    return 0;
}

static void shuffle_followup(uint32_t *values, uint32_t count, uint64_t *seed)
{
    for (uint32_t i = count - 1u; i != 0u; --i) {
        const uint32_t j = (uint32_t)(rng_step(seed) % (i + 1u));
        const uint32_t swap = values[i];
        values[i] = values[j];
        values[j] = swap;
    }
}

static int benchmark_one(FILE *json, int *comma, uint32_t n, uint32_t k,
                         int cv, int cache_mode, uint64_t *seed)
{
    if (followup_prepare_fixture(n, k) != 0) return -1;
    uint32_t active[3];
    const uint32_t count = active_candidates(k, active);
    uint64_t ticks[FOLLOWUP_CANDIDATES][SAMPLES] = {{0}};
    uint64_t wall[FOLLOWUP_CANDIDATES][SAMPLES] = {{0}};
    double numerical[FOLLOWUP_CANDIDATES] = {0};
    double max_error[FOLLOWUP_CANDIDATES] = {0};
    asian_genuine_msfr_output_t *reference = a64(sizeof(*reference));
    if (reference == NULL) {
        release_fixture();
        return -1;
    }

    for (int warmup = 0; warmup < WARMUPS; ++warmup)
        for (uint32_t at = 0; at < count; ++at) {
            reset_followup();
            condition(BASIS_TILE2, cache_mode);
            checksum(run_followup(active[at], cv));
        }
    for (int sample = 0; sample < SAMPLES; ++sample) {
        uint32_t order[3];
        memcpy(order, active, count * sizeof(*order));
        shuffle_followup(order, count, seed);
        for (uint32_t at = 0; at < count; ++at) {
            const uint32_t candidate = order[at];
            reset_followup();
            condition(BASIS_TILE2, cache_mode);
            const uint64_t wall_start = wall_ns();
            const uint64_t tsc_start = tsc0();
            const double value = run_followup(candidate, cv);
            const uint64_t tsc_end = tsc1();
            const uint64_t wall_end = wall_ns();
            ticks[candidate][sample] = tsc_end - tsc_start;
            wall[candidate][sample] = wall_end - wall_start;
            checksum(value);
        }
    }
    for (uint32_t at = 0; at < count; ++at) {
        const uint32_t candidate = active[at];
        reset_followup();
        numerical[candidate] = run_followup(candidate, cv);
        if (at == 0u)
            memcpy(reference, b.output, sizeof(*reference));
        else
            max_error[candidate] = max_difference(reference, b.output, 4u);
    }
    for (uint32_t at = 0; at < count; ++at) {
        const uint32_t candidate = active[at];
        const uint64_t tsc_median = quantile(ticks[candidate], 25u);
        const uint64_t wall_median = quantile(wall[candidate], 25u);
        fprintf(json,
                "%s{\"N\":%u,\"K\":%u,\"estimator\":\"%s\","
                "\"cache_mode\":\"%s\",\"candidate\":\"%s\","
                "\"classification\":\"complete_two_sided_full_risk\","
                "\"tsc_p10\":%" PRIu64 ",\"tsc_median\":%" PRIu64
                ",\"tsc_p90\":%" PRIu64 ",\"wall_ns_p10\":%" PRIu64
                ",\"wall_ns_median\":%" PRIu64 ",\"wall_ns_p90\":%" PRIu64
                ",\"numerical_checksum\":%.17g,"
                "\"max_abs_error_vs_first_candidate\":%.9g",
                *comma ? "," : "", n, k,
                cv ? "geometric_cv" : "arithmetic",
                cache_mode ? "historical_32KiB_rmw" :
                             "warm_candidate_specific",
                followup_names[candidate], quantile(ticks[candidate], 5u),
                tsc_median, quantile(ticks[candidate], 45u),
                quantile(wall[candidate], 5u), wall_median,
                quantile(wall[candidate], 45u), numerical[candidate],
                max_error[candidate]);
        if (candidate == FOLLOWUP_HYBRID && k > 1u) {
            fprintf(json,
                    ",\"paired_hybrid_over_tile2_tsc_ratio\":%.9g,"
                    "\"paired_hybrid_over_tile2_wall_ratio\":%.9g,"
                    "\"paired_hybrid_over_tile4_tsc_ratio\":%.9g,"
                    "\"paired_hybrid_over_tile4_wall_ratio\":%.9g",
                    paired_ratio(ticks[FOLLOWUP_HYBRID],
                                 ticks[FOLLOWUP_TILE2]),
                    paired_ratio(wall[FOLLOWUP_HYBRID],
                                 wall[FOLLOWUP_TILE2]),
                    paired_ratio(ticks[FOLLOWUP_HYBRID],
                                 ticks[FOLLOWUP_TILE4]),
                    paired_ratio(wall[FOLLOWUP_HYBRID],
                                 wall[FOLLOWUP_TILE4]));
        }
        fputs(",\"raw_tsc\":[", json);
        for (int sample = 0; sample < SAMPLES; ++sample)
            fprintf(json, "%s%" PRIu64, sample ? "," : "",
                    ticks[candidate][sample]);
        fputs("],\"raw_wall_ns\":[", json);
        for (int sample = 0; sample < SAMPLES; ++sample)
            fprintf(json, "%s%" PRIu64, sample ? "," : "",
                    wall[candidate][sample]);
        fputs("]}", json);
        *comma = 1;
    }
    free(reference);
    release_fixture();
    return 0;
}

int main(int argc, char **argv)
{
    const char *output_path =
        "results/asian_genuine_multistrike_full_risk_hybrid_dispatch/native.json";
    int check_only = 0;
    uint32_t selected_n = 0u, selected_k = 0u;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check-only") == 0)
            check_only = 1;
        else if (strcmp(argv[i], "--json") == 0 && ++i < argc)
            output_path = argv[i];
        else if (strcmp(argv[i], "--N") == 0 && ++i < argc)
            selected_n = (uint32_t)strtoul(argv[i], NULL, 10);
        else if (strcmp(argv[i], "--K") == 0 && ++i < argc)
            selected_k = (uint32_t)strtoul(argv[i], NULL, 10);
        else
            return 2;
    }
    char binary[65] = "unavailable";
    binary_sha(binary);
    if (first_cpu() < 0) return 2;
    if (check_only) return followup_preflight() == 0 ? 0 : 2;
    if ((selected_n != 0u &&
         (selected_n < 16u || selected_n > 256u ||
          (selected_n & (selected_n - 1u)) != 0u)) ||
        selected_k > 32u)
        return 2;
    if (access(output_path, F_OK) == 0) {
        write_failure(output_path, "success_output_exists", selected_n,
                      selected_k, "hybrid_dispatch", "REFUSED", binary);
        fprintf(stderr, "refusing to replace existing success JSON: %s\n",
                output_path);
        return 2;
    }
    atomic_json_t atomic;
    if (json_open(&atomic, output_path) != 0) return 2;
    FILE *json = atomic.f;
    fprintf(json,
            "{\"status\":\"PASS\",\"benchmark_provenance\":{"
            "\"qualified_commit\":\"538840542de2380aa0423684aa89da5ff0d748d8\","
            "\"followup_commit\":\"%s\","
            "\"branch\":\"research/asian-multistrike-full-risk-hybrid-dispatch\","
            "\"binary_sha256\":\"%s\","
            "\"native_evidence_basis\":\"user_supplied_Sapphire_Rapids_20_K32_cells\"},"
            "\"selection_policy\":{"
            "\"large_strip_default\":\"tile4_on_Sapphire_Rapids\","
            "\"tile4_K32_wins\":\"14_of_20_including_all_N256\","
            "\"tile2_retained\":\"K2_and_final_remainders_1_or_2\","
            "\"cache_mode_selection\":false,"
            "\"universal_tile4_claim\":false,"
            "\"arbitrary_K_performance_claim\":\"requires_this_native_benchmark\"},"
            "\"contract\":{\"S0\":100,\"r\":0.03,\"q\":0,"
            "\"sigma\":0.20,\"T\":1,\"paths\":4096,"
            "\"native_N\":[16,32,64,128,256],"
            "\"native_K\":\"1_through_32\","
            "\"strike_grid_usage\":\"first_K_entries_of_qualified_32_grid\"},"
            "\"warmups\":16,\"samples\":51,"
            "\"timer\":\"fenced_TSC_and_CLOCK_MONOTONIC_RAW\","
            "\"tsc_units\":\"not_CPU_cycles\",\"results\":[",
            MSFR_GIT_COMMIT, binary);
    static const uint32_t native_n[] = {16u, 32u, 64u, 128u, 256u};
    uint64_t seed = UINT64_C(0x4859425249444d53);
    int comma = 0;
    for (uint32_t ni = 0; ni < sizeof(native_n) / sizeof(native_n[0]); ++ni) {
        const uint32_t n = native_n[ni];
        if (selected_n != 0u && selected_n != n) continue;
        for (uint32_t k = 1u; k <= 32u; ++k) {
            if (selected_k != 0u && selected_k != k) continue;
            for (int cv = 0; cv < 2; ++cv)
                for (int cache_mode = 0; cache_mode < 2; ++cache_mode)
                    if (benchmark_one(json, &comma, n, k, cv, cache_mode,
                                      &seed) != 0) {
                        json_abort(&atomic);
                        write_failure(output_path, "native_benchmark", n, k,
                                      "hybrid_dispatch", "FAIL", binary);
                        return 2;
                    }
        }
    }
    fputs("]}\n", json);
    if (json_commit(&atomic, output_path) != 0) return 2;
    return sink == 0.0;
}
