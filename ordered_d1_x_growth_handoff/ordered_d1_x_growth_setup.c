#include "private/ordered_d1_x_growth_diag.h"
#include "private/ordered_d1_x_growth_diag_coeffs.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static const float exp8[9] = {
    1.00000000361f, 0.999999559932f, 0.499999873009f,
    0.166670788605f, 0.0416673696717f, 0.00832308095835f,
    0.0013875434074f, 0.0002077216867f, 2.58406812172e-05f,
};

static uint32_t sobol_word(uint64_t index)
{
    uint64_t gray = index ^ (index >> 1);
    uint32_t value = 0;
    unsigned bit = 0;
    while (gray != 0) {
        if ((gray & 1u) != 0u) {
            value ^= UINT32_C(1) << (31u - bit);
        }
        gray >>= 1;
        ++bit;
    }
    return value;
}

static float exp8f(float x)
{
    float value = exp8[8];
    for (int coefficient = 7; coefficient >= 0; --coefficient) {
        value = fmaf(value, x, exp8[coefficient]);
    }
    return value;
}

static void growth_cubic_taylor(size_t slot, float alpha, float scale, float out[4])
{
    const double c0 = ordered_d1_diag_z3[0][slot];
    const double c1 = ordered_d1_diag_z3[1][slot];
    const double c2 = ordered_d1_diag_z3[2][slot];
    const double c3 = ordered_d1_diag_z3[3][slot];
    const double m = 0.5;
    const double z0 = c0 + m * c1 + m * m * c2 + m * m * m * c3;
    const double z1 = c1 + 2.0 * m * c2 + 3.0 * m * m * c3;
    const double z2 = 2.0 * c2 + 6.0 * m * c3;
    const double z3 = 6.0 * c3;
    const double a = alpha;
    const double e0 = (double)scale * (double)exp8f((float)(a * z0));
    const double az1 = a * z1;
    const double az2 = a * z2;
    const double az3 = a * z3;
    const double b0 = e0;
    const double b1 = e0 * az1;
    const double b2 = 0.5 * e0 * (az1 * az1 + az2);
    const double b3 = e0 * (az1 * az1 * az1 + 3.0 * az1 * az2 + az3) / 6.0;
    out[0] = (float)(b0 - m * b1 + m * m * b2 - m * m * m * b3);
    out[1] = (float)(b1 - 2.0 * m * b2 + 3.0 * m * m * b3);
    out[2] = (float)(b2 - 3.0 * m * b3);
    out[3] = (float)b3;
}

static void growth_cubic_interpolate(size_t slot, float alpha, float scale, float out[4])
{
    static const double nodes[4] = {
        0.03806023374435663, 0.3086582838174551,
        0.6913417161825449, 0.9619397662556434,
    };
    double y[4];
    for (size_t node = 0; node < 4; ++node) {
        const double t = nodes[node];
        const double z = ((double)ordered_d1_diag_z3[3][slot] * t
            + (double)ordered_d1_diag_z3[2][slot]) * t * t
            + (double)ordered_d1_diag_z3[1][slot] * t
            + (double)ordered_d1_diag_z3[0][slot];
        y[node] = (double)scale * (double)exp8f((float)((double)alpha * z));
    }
    double divided[4];
    memcpy(divided, y, sizeof(divided));
    for (size_t order = 1; order < 4; ++order) {
        for (int index = 3; index >= (int)order; --index) {
            divided[index] = (divided[index] - divided[index - 1]) /
                (nodes[index] - nodes[index - order]);
        }
    }
    double polynomial[4] = {divided[3], 0.0, 0.0, 0.0};
    size_t degree = 0;
    for (int index = 2; index >= 0; --index) {
        double next[4] = {0.0, 0.0, 0.0, 0.0};
        for (size_t power = 0; power <= degree; ++power) {
            next[power] -= nodes[index] * polynomial[power];
            next[power + 1] += polynomial[power];
        }
        next[0] += divided[index];
        memcpy(polynomial, next, sizeof(polynomial));
        ++degree;
    }
    for (size_t power = 0; power < 4; ++power) {
        out[power] = (float)polynomial[power];
    }
}

int ordered_d1_diag_prepare(
    ordered_d1_diag_context_t *ctx,
    float drift,
    float diffusion,
    uint64_t max_points,
    uint32_t flags,
    uint32_t fixing_count)
{
    if (ctx == NULL || !isfinite(drift) || !isfinite(diffusion) ||
        drift < ORDERED_D1_DIAG_MIN_DRIFT ||
        drift > ORDERED_D1_DIAG_MAX_DRIFT ||
        diffusion < 0.0f || diffusion > ORDERED_D1_DIAG_MAX_ALPHA ||
        max_points < 32u || max_points > ORDERED_D1_DIAG_MAX_POINTS ||
        (max_points & 31u) != 0u ||
        (fixing_count != 16u && fixing_count != 32u &&
         fixing_count != 64u && fixing_count != 128u && fixing_count != 256u)) {
        return -1;
    }
    const double scale_double = exp((double)drift);
    if (!isfinite(scale_double) || scale_double < FLT_MIN || scale_double > FLT_MAX) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    for (size_t lane = 0; lane < 32; ++lane) {
        ctx->initial[lane] = sobol_word(8192u + lane);
    }
    for (size_t index = 0; index < 27; ++index) {
        ctx->jumps[index] = UINT32_C(0x08000000) ^
            (UINT32_C(1) << (26u - (unsigned)index));
    }
    ctx->drift = drift;
    ctx->diffusion = diffusion;
    ctx->exp_drift = (float)scale_double;
    ctx->max_packets = (uint32_t)(max_points / 32u);
    ctx->flags = flags;
    for (uint32_t step = 0; step < fixing_count; ++step) {
        ctx->weights[step] = (float)(fixing_count - step) / (float)fixing_count;
    }

    for (size_t slot = 0; slot < ORDERED_D1_DIAG_CLASSES; ++slot) {
        if ((flags & ORDERED_D1_DIAG_PREPARE_X2) != 0u) {
            ctx->x2[0][slot] = fmaf(diffusion, ordered_d1_diag_z2[0][slot], drift);
            ctx->x2[1][slot] = diffusion * ordered_d1_diag_z2[1][slot];
            ctx->x2[2][slot] = diffusion * ordered_d1_diag_z2[2][slot];
        }
        if ((flags & ORDERED_D1_DIAG_PREPARE_X3) != 0u) {
            ctx->x3[0][slot] = fmaf(diffusion, ordered_d1_diag_z3[0][slot], drift);
            ctx->x3[1][slot] = diffusion * ordered_d1_diag_z3[1][slot];
            ctx->x3[2][slot] = diffusion * ordered_d1_diag_z3[2][slot];
            ctx->x3[3][slot] = diffusion * ordered_d1_diag_z3[3][slot];
        }
        if ((flags & (ORDERED_D1_DIAG_PREPARE_GROWTH2 |
                      ORDERED_D1_DIAG_PREPARE_GROWTH3 |
                      ORDERED_D1_DIAG_PREPARE_GROWTH3_FULL)) != 0u) {
            float coefficient[4];
            if (ordered_d1_diag_growth_interp_mask[slot] != 0u) {
                growth_cubic_interpolate(slot, diffusion, ctx->exp_drift, coefficient);
            } else {
                growth_cubic_taylor(slot, diffusion, ctx->exp_drift, coefficient);
            }
            for (size_t power = 0; power < 4; ++power) {
                ctx->growth3[power][slot] = coefficient[power];
            }
        }
    }
    if ((flags & ORDERED_D1_DIAG_PREPARE_GROWTH3_FULL) != 0u) {
        for (size_t row = 0; row < 128; ++row) {
            for (size_t lane = 0; lane < 16; ++lane) {
                const size_t a = row * 16 + lane;
                const size_t b = 2048 + row * 16 + lane;
                const size_t mirrored = (127 - row) * 16 + (15 - lane);
                for (size_t power = 0; power < 4; ++power) {
                    ctx->growth3_full[power][a] = ctx->growth3[power][a];
                    ctx->growth3_full[power][b] = ctx->growth3[power][mirrored];
                }
            }
        }
    }
    for (size_t slot = 0; slot < ORDERED_D1_DIAG_CLASSES; ++slot) {
        if ((flags & ORDERED_D1_DIAG_PREPARE_GROWTH2) != 0u) {
            ctx->growth2[0][slot] = ctx->growth3[0][slot];
            ctx->growth2[1][slot] = ctx->growth3[1][slot];
            ctx->growth2[2][slot] = ctx->growth3[2][slot];
        }
    }
    return 0;
}

_Static_assert(offsetof(ordered_d1_diag_context_t, initial) == 0, "initial ABI");
_Static_assert(offsetof(ordered_d1_diag_context_t, jumps) == 128, "jumps ABI");
_Static_assert(offsetof(ordered_d1_diag_context_t, drift) == 256, "drift ABI");
_Static_assert(offsetof(ordered_d1_diag_context_t, x2) == 320, "x2 ABI");
_Static_assert(offsetof(ordered_d1_diag_context_t, x3) == 24896, "x3 ABI");
_Static_assert(offsetof(ordered_d1_diag_context_t, growth3) == 57664, "growth3 ABI");
_Static_assert(offsetof(ordered_d1_diag_context_t, growth3_full) == 90432, "growth3 full ABI");
_Static_assert(offsetof(ordered_d1_diag_context_t, growth2) == 155968, "growth2 ABI");
_Static_assert(offsetof(ordered_d1_diag_context_t, weights) == 180544, "weights ABI");
