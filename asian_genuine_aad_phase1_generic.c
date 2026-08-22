#include "private/asian_genuine_aad_phase1_diag.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t route_source(const asian_genuine_route_t *route, uint32_t path)
{
    const fragment_map_t *map = route->map;
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    const uint32_t line = map->select[packet][half];
    const uint32_t pattern = map->select[packet][2u + half];
    return line * 16u + map->patterns[pattern][lane];
}

static void trace_packet(const asian_genuine_aad_phase1_context_t *ctx,
                         uint32_t packet,
                         asian_genuine_aad_phase1_packet_trace_t *trace,
                         int suffix_mode)
{
    const uint32_t n = ctx->fixing_count;
    float tape[ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS][32];
    float xs[ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS][32];
    memset(trace, 0, sizeof(*trace));
    for (uint32_t lane = 0; lane < 32u; ++lane) {
        const uint32_t path = packet * 32u + lane;
        float s = ctx->s0, q = 0.0f, l = 0.0f;
        float cumulative_x = 0.0f, rho_weighted = 0.0f;
        float x_weighted = 0.0f;
        for (uint32_t k = 0; k < n; ++k) {
            const asian_genuine_route_t *route = &ctx->routes[k];
            const uint32_t source = route_source(route, path);
            const float x = route->x_base[source];
            const float growth = route->growth_base[source];
            float weight;
            memcpy(&weight, &route->weight_bits, sizeof(weight));
            s *= growth;
            q += s;
            l = fmaf(weight, x, l);
            tape[k][lane] = s;
            xs[k][lane] = x;
            if (!suffix_mode) {
                cumulative_x += x;
                rho_weighted = fmaf((float)(k + 1u), s, rho_weighted);
                x_weighted = fmaf(s, cumulative_x, x_weighted);
            }
        }
        const float a = q * ctx->inv_n;
        float a_rho, a_vega;
        if (suffix_mode) {
            float suffix = 0.0f, rho_sum = 0.0f, x_dot = 0.0f;
            for (uint32_t k = n; k-- > 0u;) {
                suffix += tape[k][lane];
                rho_sum += suffix;
                x_dot = fmaf(suffix, xs[k][lane], x_dot);
            }
            a_rho = rho_sum * ctx->dt_over_n;
            x_dot *= ctx->inv_n;
            a_vega = fmaf(-ctx->c, a_rho, x_dot) * ctx->inv_sigma;
        } else {
            a_rho = rho_weighted * ctx->dt_over_n;
            x_weighted *= ctx->inv_n;
            a_vega = fmaf(-ctx->c, a_rho, x_weighted) * ctx->inv_sigma;
        }
        const float g = expf(ctx->controls->log_s0 + l);
        const float g_vega = g * ((l - ctx->c * ctx->controls->geometric_b) *
                                  ctx->inv_sigma);
        trace->final_s[lane] = s;
        trace->q[lane] = q;
        trace->l[lane] = l;
        trace->basis[ASIAN_GENUINE_AAD_PHASE1_A][lane] = a;
        trace->basis[ASIAN_GENUINE_AAD_PHASE1_A_DELTA][lane] =
            a * ctx->inv_s0;
        trace->basis[ASIAN_GENUINE_AAD_PHASE1_A_VEGA][lane] = a_vega;
        trace->basis[ASIAN_GENUINE_AAD_PHASE1_A_RHO][lane] = a_rho;
        trace->basis[ASIAN_GENUINE_AAD_PHASE1_G][lane] = g;
        trace->basis[ASIAN_GENUINE_AAD_PHASE1_G_DELTA][lane] =
            g * ctx->inv_s0;
        trace->basis[ASIAN_GENUINE_AAD_PHASE1_G_VEGA][lane] = g_vega;
        trace->basis[ASIAN_GENUINE_AAD_PHASE1_G_RHO][lane] =
            g * ctx->controls->geometric_b;
    }
}

void asian_genuine_aad_phase1_forward_probe_diag(
    const asian_genuine_aad_phase1_context_t *ctx, uint32_t packet,
    asian_genuine_aad_phase1_packet_trace_t *trace)
{
    trace_packet(ctx, packet, trace, 0);
}

void asian_genuine_aad_phase1_suffix_probe_diag(
    const asian_genuine_aad_phase1_context_t *ctx, uint32_t packet,
    asian_genuine_aad_phase1_packet_trace_t *trace)
{
    trace_packet(ctx, packet, trace, 1);
}

/* One deliberately unranked, generic reverse recurrence.  It retains the
 * generic adjoint vocabulary as a correctness comparator only. */
void asian_genuine_aad_phase1_generic_basis_diag(
    const asian_genuine_aad_phase1_context_t *ctx,
    float basis[ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS]
               [ASIAN_GENUINE_AAD_PHASE1_PATHS])
{
    const uint32_t n = ctx->fixing_count;
    float *s_tape = malloc((size_t)n * ASIAN_GENUINE_AAD_PHASE1_PATHS *
                           sizeof(*s_tape));
    float *x_tape = malloc((size_t)n * ASIAN_GENUINE_AAD_PHASE1_PATHS *
                           sizeof(*x_tape));
    float *growth_tape = malloc((size_t)n * ASIAN_GENUINE_AAD_PHASE1_PATHS *
                                sizeof(*growth_tape));
    if (!s_tape || !x_tape || !growth_tape) {
        free(growth_tape); free(x_tape); free(s_tape);
        memset(basis, 0, ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS *
               ASIAN_GENUINE_AAD_PHASE1_PATHS * sizeof(float));
        return;
    }
    for (uint32_t path = 0; path < ASIAN_GENUINE_AAD_PHASE1_PATHS; ++path) {
        float s = ctx->s0, q = 0.0f, l = 0.0f;
        for (uint32_t k = 0; k < n; ++k) {
            const asian_genuine_route_t *route = &ctx->routes[k];
            const uint32_t source = route_source(route, path);
            const size_t at = (size_t)k * ASIAN_GENUINE_AAD_PHASE1_PATHS + path;
            float weight;
            memcpy(&weight, &route->weight_bits, sizeof(weight));
            x_tape[at] = route->x_base[source];
            growth_tape[at] = route->growth_base[source];
            s *= growth_tape[at];
            s_tape[at] = s;
            q += s;
            l = fmaf(weight, x_tape[at], l);
        }
        float adj_s = 0.0f, rho = 0.0f, vega = 0.0f;
        for (uint32_t k = n; k-- > 0u;) {
            const size_t at = (size_t)k * ASIAN_GENUINE_AAD_PHASE1_PATHS + path;
            adj_s += ctx->inv_n;
            const float adj_growth = adj_s *
                (k == 0u ? ctx->s0 : s_tape[at-ASIAN_GENUINE_AAD_PHASE1_PATHS]);
            const float adj_x = adj_growth * growth_tape[at];
            rho += adj_x * (ctx->dt_over_n * (float)n);
            vega += adj_x * ((x_tape[at] - ctx->c *
                     (ctx->dt_over_n * (float)n)) * ctx->inv_sigma);
            adj_s *= growth_tape[at];
        }
        const float a = q * ctx->inv_n;
        const float g = expf(ctx->controls->log_s0 + l);
        basis[ASIAN_GENUINE_AAD_PHASE1_A][path] = a;
        basis[ASIAN_GENUINE_AAD_PHASE1_A_DELTA][path] = a*ctx->inv_s0;
        basis[ASIAN_GENUINE_AAD_PHASE1_A_VEGA][path] = vega;
        basis[ASIAN_GENUINE_AAD_PHASE1_A_RHO][path] = rho;
        basis[ASIAN_GENUINE_AAD_PHASE1_G][path] = g;
        basis[ASIAN_GENUINE_AAD_PHASE1_G_DELTA][path] = g*ctx->inv_s0;
        basis[ASIAN_GENUINE_AAD_PHASE1_G_VEGA][path] =
            g*(l-ctx->c*ctx->controls->geometric_b)*ctx->inv_sigma;
        basis[ASIAN_GENUINE_AAD_PHASE1_G_RHO][path] =
            g*ctx->controls->geometric_b;
    }
    free(growth_tape); free(x_tape); free(s_tape);
}

void asian_genuine_aad_phase1_consume_basis_diag(
    const asian_genuine_aad_phase1_context_t *ctx,
    const float basis[ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS]
                     [ASIAN_GENUINE_AAD_PHASE1_PATHS],
    enum asian_genuine_aad_phase1_side side, int cv,
    asian_genuine_aad_phase1_value_t *out)
{
    double totals[4] = {0.0, 0.0, 0.0, 0.0};
    const float sign = side == ASIAN_GENUINE_AAD_PHASE1_CALL ? 1.0f : -1.0f;
    for (uint32_t path = 0; path < ASIAN_GENUINE_AAD_PHASE1_PATHS; ++path) {
        const float a = basis[ASIAN_GENUINE_AAD_PHASE1_A][path];
        const float g = basis[ASIAN_GENUINE_AAD_PHASE1_G][path];
        const float ap = fmaxf(sign*(a-ctx->strike), 0.0f);
        const float gp = cv ? fmaxf(sign*(g-ctx->strike), 0.0f) : 0.0f;
        const float ai = sign*(a-ctx->strike) > 0.0f ? sign : 0.0f;
        const float gi = cv && sign*(g-ctx->strike) > 0.0f ? sign : 0.0f;
        const float pd = ap-gp;
        totals[0] += ctx->discount*pd;
        totals[1] += ctx->discount*(ai*basis[ASIAN_GENUINE_AAD_PHASE1_A_DELTA][path] -
            gi*basis[ASIAN_GENUINE_AAD_PHASE1_G_DELTA][path]);
        totals[2] += ctx->discount*(ai*basis[ASIAN_GENUINE_AAD_PHASE1_A_VEGA][path] -
            gi*basis[ASIAN_GENUINE_AAD_PHASE1_G_VEGA][path]);
        totals[3] += ctx->discount*(ai*basis[ASIAN_GENUINE_AAD_PHASE1_A_RHO][path] -
            gi*basis[ASIAN_GENUINE_AAD_PHASE1_G_RHO][path]) -
            ctx->controls->maturity*ctx->discount*pd;
    }
    out->price=totals[0]/ASIAN_GENUINE_AAD_PHASE1_PATHS;
    out->delta=totals[1]/ASIAN_GENUINE_AAD_PHASE1_PATHS;
    out->vega=totals[2]/ASIAN_GENUINE_AAD_PHASE1_PATHS;
    out->rho=totals[3]/ASIAN_GENUINE_AAD_PHASE1_PATHS;
    if (cv) {
        const asian_genuine_aad_phase1_value_t *exact =
            side == ASIAN_GENUINE_AAD_PHASE1_CALL ?
            &ctx->controls->geometric_call : &ctx->controls->geometric_put;
        out->price += exact->price; out->delta += exact->delta;
        out->vega += exact->vega; out->rho += exact->rho;
    }
}
