#define main asian_genuine_msfr_qualified_vector_main
#include "test_asian_genuine_multistrike_full_risk_vector.c"
#undef main

#include "private/asian_genuine_multistrike_full_risk_hybrid_dispatch_diag.h"

static int plan_matches(uint32_t k)
{
    asian_genuine_msfr_hybrid_plan_t got;
    if (asian_genuine_msfr_hybrid_plan_diag(k, &got) !=
        ASIAN_GENUINE_MSFR_OK)
        return -1;
    asian_genuine_msfr_hybrid_plan_t expected = {0};
    if (k == 1u) {
        expected.phase1_calls = 1u;
    } else if (k == 2u) {
        expected.tile2_calls = 1u;
    } else if (k <= 4u) {
        expected.tile4_calls = 1u;
        expected.padded_outputs = (uint8_t)(4u - k);
    } else {
        expected.tile4_calls = (uint8_t)(k / 4u);
        const uint32_t remainder = k & 3u;
        if (remainder == 1u) {
            expected.tile2_calls = 1u;
            expected.padded_outputs = 1u;
        } else if (remainder == 2u) {
            expected.tile2_calls = 1u;
        } else if (remainder == 3u) {
            ++expected.tile4_calls;
            expected.padded_outputs = 1u;
        }
    }
    return memcmp(&got, &expected, sizeof(got)) == 0 ? 0 : -1;
}

static void arbitrary_strikes(uint32_t k, float strikes[32])
{
    static const float values[32] = {
        100.0f, 70.0f, 130.0f, 98.0f, 102.0f, 72.0f, 128.0f, 96.0f,
        104.0f, 74.0f, 126.0f, 94.0f, 106.0f, 76.0f, 124.0f, 92.0f,
        100.5f, 108.0f, 78.0f, 122.0f, 90.0f, 110.0f, 80.0f, 120.0f,
        88.0f, 112.0f, 82.0f, 118.0f, 86.0f, 114.0f, 84.0f, 116.0f,
    };
    memcpy(strikes, values, k * sizeof(*strikes));
}

static int prepare_phase1_for_strike(
    const fixture_t *fixture, float strike,
    asian_genuine_aad_phase1_controls_t *controls,
    asian_genuine_aad_phase1_context_t *context)
{
    if (asian_genuine_aad_phase1_prepare_controls(
          controls, 100.0, strike, 0.03, 0.0, 0.20, 1.0, fixture->n) !=
          ASIAN_GENUINE_AAD_PHASE1_OK)
        return -1;
    return asian_genuine_aad_phase1_prepare_context(
          context, fixture->routes, fixture->tape, controls, 100.0, strike,
          0.03, 0.0, 0.20, 1.0, fixture->n) ==
          ASIAN_GENUINE_AAD_PHASE1_OK ? 0 : -1;
}

static int check_hybrid_case(const fixture_t *fixture, const float *strikes,
                             uint32_t k)
{
    asian_genuine_msfr_strike_controls_t *controls = a64(sizeof(*controls));
    asian_genuine_msfr_consumer_context_t *context = a64(sizeof(*context));
    asian_genuine_msfr_accumulator_t *tile2 = a64(sizeof(*tile2));
    asian_genuine_msfr_accumulator_t *tile4 = a64(sizeof(*tile4));
    asian_genuine_msfr_accumulator_t *hybrid = a64(sizeof(*hybrid));
    asian_genuine_msfr_output_t *out2 = a64(sizeof(*out2));
    asian_genuine_msfr_output_t *out4 = a64(sizeof(*out4));
    asian_genuine_msfr_output_t *outh = a64(sizeof(*outh));
    asian_genuine_aad_phase1_controls_t *phase_controls =
        a64(sizeof(*phase_controls));
    asian_genuine_aad_phase1_context_t *phase_context =
        a64(sizeof(*phase_context));
    if (!controls || !context || !tile2 || !tile4 || !hybrid || !out2 ||
        !out4 || !outh || !phase_controls || !phase_context)
        return -1;
    if (asian_genuine_msfr_prepare_strikes(
          controls, 100.0, 0.03, 0.0, 0.20, 1.0, fixture->n, strikes, k) !=
          ASIAN_GENUINE_MSFR_OK ||
        asian_genuine_msfr_prepare_consumer_context(context, controls) !=
          ASIAN_GENUINE_MSFR_OK ||
        (k == 1u && prepare_phase1_for_strike(
          fixture, strikes[0], phase_controls, phase_context) != 0))
        return -1;

    const uint64_t controls_hash = hash_bytes(
        UINT64_C(1469598103934665603), controls, sizeof(*controls));
    const uint64_t basis_hash = hash_bytes(
        UINT64_C(1469598103934665603), fixture->basis,
        sizeof(*fixture->basis));
    for (int estimator = 0; estimator < 2; ++estimator) {
        if (asian_genuine_msfr_accumulator_init(tile2, context, estimator) ||
            asian_genuine_msfr_accumulator_init(tile4, context, estimator) ||
            asian_genuine_msfr_accumulator_init(hybrid, context, estimator) ||
            asian_genuine_msfr_consume_block(
                fixture->basis, context, estimator, 2u, tile2) ||
            asian_genuine_msfr_consume_block(
                fixture->basis, context, estimator, 4u, tile4) ||
            asian_genuine_msfr_hybrid_consume_block_diag(
                fixture->basis, context, estimator,
                k == 1u ? phase_context : NULL, hybrid) ||
            asian_genuine_msfr_finalize(context, tile2, out2) ||
            asian_genuine_msfr_finalize(context, tile4, out4) ||
            asian_genuine_msfr_finalize(context, hybrid, outh))
            return -1;
        if (memcmp(out2, out4, sizeof(*out2)) != 0 ||
            memcmp(out2, outh, sizeof(*out2)) != 0) {
            fprintf(stderr, "hybrid output mismatch N=%u K=%u estimator=%d\n",
                    fixture->n, k, estimator);
            return -1;
        }
        if (k > 1u)
            for (uint32_t strike = 0; strike < k; ++strike)
                if (memcmp(tile2->direct_sums[strike],
                           hybrid->direct_sums[strike],
                           sizeof(tile2->direct_sums[strike])) != 0 ||
                    memcmp(tile4->direct_sums[strike],
                           hybrid->direct_sums[strike],
                           sizeof(tile4->direct_sums[strike])) != 0) {
                    fprintf(stderr,
                            "hybrid raw-sum mismatch N=%u K=%u estimator=%d "
                            "strike=%u\n", fixture->n, k, estimator, strike);
                    return -1;
                }
        if (hybrid->completed_path_count != PATHS ||
            hybrid->completed_block_count != 1u)
            return -1;
        if (asian_genuine_msfr_hybrid_consume_block_diag(
                fixture->basis, context, estimator,
                k == 1u ? phase_context : NULL, hybrid) ||
            asian_genuine_msfr_finalize(context, hybrid, outh) ||
            hybrid->completed_path_count != 2u * PATHS ||
            hybrid->completed_block_count != 2u ||
            memcmp(out2, outh, sizeof(*out2)) != 0)
            return -1;
    }
    const int immutable = controls_hash == hash_bytes(
          UINT64_C(1469598103934665603), controls, sizeof(*controls)) &&
        basis_hash == hash_bytes(UINT64_C(1469598103934665603),
                                 fixture->basis, sizeof(*fixture->basis));
    free(phase_context); free(phase_controls); free(outh); free(out4);
    free(out2); free(hybrid); free(tile4); free(tile2); free(context);
    free(controls);
    return immutable ? 0 : -1;
}

static int check_all_k(void)
{
    fixture_t fixture;
    if (prepare_fixture(&fixture, 16u) != 0) return -1;
    asian_genuine_msfr_basis_forward_diag(fixture.basis_context,
                                          fixture.basis);
    int status = 0;
    for (uint32_t k = 1u; k <= 32u && status == 0; ++k) {
        float strikes[32];
        arbitrary_strikes(k, strikes);
        if (plan_matches(k) != 0 ||
            check_hybrid_case(&fixture, strikes, k) != 0)
            status = -1;
    }
    if (asian_genuine_msfr_hybrid_plan_diag(0u, NULL) !=
            ASIAN_GENUINE_MSFR_STRIKE_COUNT_UNSUPPORTED ||
        asian_genuine_msfr_hybrid_plan_diag(33u,
            &(asian_genuine_msfr_hybrid_plan_t){0}) !=
            ASIAN_GENUINE_MSFR_STRIKE_COUNT_UNSUPPORTED)
        status = -1;
    release_fixture(&fixture);
    return status;
}

static int check_k1_fixing_boundaries(void)
{
    const uint32_t counts[] = {2u, 256u};
    const float strikes[] = {70.0f, 100.0f, 130.0f};
    for (uint32_t ni = 0; ni < 2u; ++ni) {
        fixture_t fixture;
        if (prepare_fixture(&fixture, counts[ni]) != 0) return -1;
        asian_genuine_msfr_basis_forward_diag(fixture.basis_context,
                                              fixture.basis);
        for (uint32_t si = 0; si < 3u; ++si)
            if (check_hybrid_case(&fixture, &strikes[si], 1u) != 0) {
                release_fixture(&fixture);
                return -1;
            }
        release_fixture(&fixture);
    }
    return 0;
}

int main(void)
{
    if (check_all_k() != 0 || check_k1_fixing_boundaries() != 0) return 1;
    puts("asian_genuine_multistrike_full_risk_hybrid_dispatch=PASS "
         "K=1..32 policy=phase1,tile2,tile4 streaming=two_blocks "
         "N_k1=2,16,256 arbitrary_strike_order=yes bit_identity=yes");
    return 0;
}
