#define _POSIX_C_SOURCE 200112L
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_genuine_aad_phase1_diag.h"
#include "private/asian_genuine_multistrike_full_risk_diag.h"
#include "private/asian_genuine_price_delta_strip_diag.h"
#include "private/asian_geometric_cv_diag.h"
#include "asian_genuine_multistrike_full_risk_reference.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATHS = ASIAN_GENUINE_MSFR_PATHS };

typedef struct {
    uint32_t n;
    uint32_t directions[256][32];
    uint32_t *words[2];
    float *x;
    float *growth;
    float *tape;
    fragment_map_t *maps;
    asian_genuine_route_t *routes;
    ordered_d1_diag_context_t *producer;
    asian_genuine_msfr_basis_controls_t *basis_controls;
    asian_genuine_msfr_basis_context_t *basis_context;
    unsigned char *basis_storage;
    asian_genuine_msfr_basis_t *basis;
    asian_genuine_msfr_basis_t *basis_copy;
    uint64_t immutable_hash;
} fixture_t;

static void *a64(size_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes) != 0) return NULL;
    memset(p, 0, bytes);
    return p;
}

static uint32_t sobol(uint32_t index, const uint32_t *directions)
{
    uint32_t gray = index ^ (index >> 1), word = 0;
    for (uint32_t bit = 0; gray; ++bit, gray >>= 1)
        if (gray & 1u) word ^= directions[bit];
    return word;
}

static uint64_t hash_bytes(uint64_t h, const void *data, size_t bytes)
{
    const unsigned char *p = data;
    while (bytes--) { h ^= *p++; h *= UINT64_C(1099511628211); }
    return h;
}

static uint64_t fixture_hash(const fixture_t *f)
{
    uint64_t h = UINT64_C(1469598103934665603);
    h = hash_bytes(h, f->directions, sizeof(f->directions));
    h = hash_bytes(h, f->words[0], PATHS * sizeof(uint32_t));
    h = hash_bytes(h, f->words[1], PATHS * sizeof(uint32_t));
    h = hash_bytes(h, f->x, 2u * PATHS * sizeof(float));
    h = hash_bytes(h, f->growth, 2u * PATHS * sizeof(float));
    h = hash_bytes(h, f->maps, f->n * sizeof(*f->maps));
    h = hash_bytes(h, f->routes, f->n * sizeof(*f->routes));
    h = hash_bytes(h, f->basis_controls, sizeof(*f->basis_controls));
    h = hash_bytes(h, f->basis_context, sizeof(*f->basis_context));
    return h;
}

static int load_directions(fixture_t *f)
{
    FILE *in = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
    if (in == NULL) return -1;
    for (uint32_t d = 0; d < 256u; ++d) {
        uint32_t length;
        if (fread(&length, 4, 1, in) != 1 || length != 32u ||
            fread(f->directions[d], 4, 32, in) != 32u) {
            fclose(in);
            return -1;
        }
    }
    fclose(in);
    return 0;
}

static void release_fixture(fixture_t *f)
{
    free(f->basis_copy); free(f->basis_storage); free(f->basis_context);
    free(f->basis_controls); free(f->producer); free(f->routes); free(f->maps);
    free(f->tape); free(f->growth); free(f->x);
    free(f->words[1]); free(f->words[0]);
    memset(f, 0, sizeof(*f));
}

static uint32_t route_source(const asian_genuine_route_t *route, uint32_t path)
{
    const fragment_map_t *map = route->map;
    const uint32_t packet = path >> 5, half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    const uint32_t line = map->select[packet][half];
    const uint32_t pattern = map->select[packet][2u + half];
    return line * 16u + map->patterns[pattern][lane];
}

static int verify_routes(const fixture_t *f)
{
    for (uint32_t k = 0; k < f->n; ++k) {
        const asian_genuine_route_t *route = &f->routes[k];
        for (uint32_t path = 0; path < PATHS; ++path) {
            const uint32_t source = route_source(route, path);
            const uint32_t block = route->x_base == f->x ? 0u : 1u;
            if (source >= PATHS || f->words[block][source] !=
                    sobol(8192u + path, f->directions[k]))
                return -1;
        }
    }
    return 0;
}

static int prepare_fixture(fixture_t *f, uint32_t n)
{
    const double s0 = 100.0, rate = 0.03, q = 0.0, sigma = 0.20, t = 1.0;
    memset(f, 0, sizeof(*f));
    f->n = n;
    if (load_directions(f) != 0) return -1;
    f->words[0] = a64(PATHS * sizeof(uint32_t));
    f->words[1] = a64(PATHS * sizeof(uint32_t));
    f->x = a64(2u * PATHS * sizeof(float));
    f->growth = a64(2u * PATHS * sizeof(float));
    f->tape = a64(ASIAN_GENUINE_AAD_PHASE1_TAPE_BYTES);
    f->maps = a64((size_t)n * sizeof(*f->maps));
    f->routes = a64((size_t)n * sizeof(*f->routes));
    f->producer = a64(sizeof(*f->producer));
    f->basis_controls = a64(sizeof(*f->basis_controls));
    f->basis_context = a64(sizeof(*f->basis_context));
    f->basis_storage = a64(sizeof(*f->basis) + 128u);
    f->basis_copy = a64(sizeof(*f->basis_copy));
    if (!f->words[0] || !f->words[1] || !f->x || !f->growth || !f->tape ||
        !f->maps || !f->routes || !f->producer || !f->basis_controls ||
        !f->basis_context || !f->basis_storage || !f->basis_copy)
        return -1;
    f->basis = (asian_genuine_msfr_basis_t *)(f->basis_storage + 64u);
    memset(f->basis_storage, 0xa5, sizeof(*f->basis) + 128u);
    memset(f->basis, 0, sizeof(*f->basis));

    for (uint32_t path = 0; path < PATHS; ++path) {
        f->words[0][path] = sobol(8192u + path, f->directions[0]);
        f->words[1][path] = sobol(12288u + path, f->directions[0]);
    }
    const double dt = t / n;
    const float drift = (float)((rate - q - 0.5 * sigma * sigma) * dt);
    const float diffusion = (float)(sigma * sqrt(dt));
    const uint32_t producer_n = asian_genuine_msfr_producer_fixing_count(n);
    if (producer_n == 0u || ordered_d1_diag_prepare(f->producer, drift,
          diffusion, 8192u, ORDERED_D1_DIAG_PREPARE_X3, producer_n) != 0)
        return -1;
    ordered_d1_x_only_diag(256u, f->producer, f->x);
    asian_vector_exp_range_reduced_array_diag(f->x, f->growth);
    asian_vector_exp_range_reduced_array_diag(f->x + PATHS, f->growth + PATHS);

    const uint32_t *words[2] = {f->words[0], f->words[1]};
    const float *xb[2] = {f->x, f->x + PATHS};
    const float *gb[2] = {f->growth, f->growth + PATHS};
    uint32_t *target = a64(PATHS * sizeof(uint32_t));
    if (target == NULL) return -1;
    for (uint32_t k = 0; k < n; ++k) {
        for (uint32_t path = 0; path < PATHS; ++path)
            target[path] = sobol(8192u + path, f->directions[k]);
        if (asian_genuine_prepare_route(words, 2u, xb, gb, target, k, n,
              &f->maps[k], &f->routes[k]) != 0) {
            free(target);
            return -1;
        }
    }
    free(target);
    if (verify_routes(f) != 0 ||
        asian_genuine_msfr_prepare_basis_controls(f->basis_controls, s0, rate,
          q, sigma, t, n) != ASIAN_GENUINE_MSFR_OK ||
        asian_genuine_msfr_prepare_basis_context(f->basis_context, f->routes,
          f->basis_controls, s0, rate, q, sigma, t, n) !=
          ASIAN_GENUINE_MSFR_OK || f->basis_context->route_count != n - 1u ||
        f->basis_context->route_count == 0u)
        return -1;
    f->immutable_hash = fixture_hash(f);
    return 0;
}

static int guard_ok(const fixture_t *f)
{
    for (uint32_t i = 0; i < 64u; ++i)
        if (f->basis_storage[i] != 0xa5 ||
            f->basis_storage[64u + sizeof(*f->basis) + i] != 0xa5)
            return 0;
    return 1;
}

static void make_strikes(uint32_t k, float strikes[32]);

static int phase1_basis_probe_compatibility(const fixture_t *f,
                                            uint32_t packet)
{
    asian_genuine_aad_phase1_controls_t *controls = a64(sizeof(*controls));
    asian_genuine_aad_phase1_context_t *context = a64(sizeof(*context));
    asian_genuine_aad_phase1_packet_trace_t *phase = a64(sizeof(*phase));
    asian_genuine_msfr_packet_trace_t *strip = a64(sizeof(*strip));
    if (controls == NULL || context == NULL || phase == NULL || strip == NULL)
        return -1;
    int status = asian_genuine_aad_phase1_prepare_controls(
          controls, 100.0, 100.0, 0.03, 0.0, 0.20, 1.0, f->n);
    if (status == ASIAN_GENUINE_AAD_PHASE1_OK)
        status = asian_genuine_aad_phase1_prepare_context(
            context, f->routes, f->tape, controls, 100.0, 100.0,
            0.03, 0.0, 0.20, 1.0, f->n);
    if (status == ASIAN_GENUINE_AAD_PHASE1_OK) {
        asian_genuine_aad_phase1_forward_probe_diag(context, packet, phase);
        asian_genuine_msfr_forward_probe_diag(f->basis_context, packet, strip);
        status = memcmp(phase->final_s, strip->final_s,
                        sizeof(strip->final_s)) == 0 &&
                 memcmp(phase->q, strip->q, sizeof(strip->q)) == 0 &&
                 memcmp(phase->l, strip->l, sizeof(strip->l)) == 0 &&
                 memcmp(phase->basis, strip->basis,
                        4u * 32u * sizeof(float)) == 0 ? 0 : -1;
        for (uint32_t field = 4u; status == 0 && field < 8u; ++field)
            for (uint32_t lane = 0; lane < 32u; ++lane)
                if (fabsf(phase->basis[field][lane] -
                          strip->basis[field][lane]) > 2e-4f) {
                    status = -1;
                    break;
                }
    }
    free(strip); free(phase); free(context); free(controls);
    return status;
}

static int compare_basis(fixture_t *f)
{
    asian_genuine_msfr_basis_forward_diag(f->basis_context, f->basis);
    if (!guard_ok(f) || fixture_hash(f) != f->immutable_hash) return -1;
    if (phase1_basis_probe_compatibility(f, 0u) != 0 ||
        phase1_basis_probe_compatibility(f, PATHS / 32u - 1u) != 0)
        return -1;
    for (uint32_t packet = 0; packet < PATHS / 32u; ++packet) {
        asian_genuine_msfr_packet_trace_t trace;
        asian_genuine_msfr_forward_probe_diag(f->basis_context, packet, &trace);
        for (uint32_t field = 0; field < 8u; ++field)
            for (uint32_t lane = 0; lane < 32u; ++lane) {
                const uint32_t path = packet * 32u + lane;
                if (memcmp(&trace.basis[field][lane],
                           &f->basis->values[field][path], sizeof(float)) != 0) {
                    fprintf(stderr, "basis mismatch N=%u packet=%u lane=%u field=%u "
                            "got=%a expected=%a\n", f->n, packet, lane, field,
                            f->basis->values[field][path],
                            trace.basis[field][lane]);
                    return -1;
                }
            }
    }
    memcpy(f->basis_copy, f->basis, sizeof(*f->basis));
    asian_genuine_msfr_basis_forward_diag(f->basis_context, f->basis);
    if (memcmp(f->basis_copy, f->basis, sizeof(*f->basis)) != 0 ||
        !guard_ok(f) || fixture_hash(f) != f->immutable_hash) return -1;

    float *dm_x = a64((size_t)f->n * PATHS * sizeof(float));
    float *dm_growth = a64((size_t)f->n * PATHS * sizeof(float));
    if (dm_x == NULL || dm_growth == NULL) {
        free(dm_growth); free(dm_x); return -1;
    }
    for (uint32_t k = 0; k < f->n; ++k)
        for (uint32_t path = 0; path < PATHS; ++path) {
            const uint32_t source = route_source(&f->routes[k], path);
            dm_x[(size_t)k * PATHS + path] = f->routes[k].x_base[source];
            dm_growth[(size_t)k * PATHS + path] =
                f->routes[k].growth_base[source];
        }
    asian_genuine_msfr_dimension_major_basis_diag(dm_x, dm_growth,
                                                  f->basis_context,
                                                  f->basis_copy);
    const int same = memcmp(f->basis_copy, f->basis, sizeof(*f->basis)) == 0;
    if (!same)
        for (uint32_t field = 0; field < 8u; ++field)
            for (uint32_t path = 0; path < PATHS; ++path)
                if (memcmp(&f->basis_copy->values[field][path],
                           &f->basis->values[field][path], 4) != 0) {
                    fprintf(stderr, "dimension-major mismatch N=%u field=%u "
                            "path=%u got=%a expected=%a\n", f->n, field, path,
                            f->basis_copy->values[field][path],
                            f->basis->values[field][path]);
                    field = 8u; break;
                }
    free(dm_growth); free(dm_x);
    return same ? 0 : -1;
}

static double inverse_normal(double p)
{
    static const double a[] = {-39.69683028665376, 220.9460984245205,
      -275.9285104469687, 138.3577518672690, -30.66479806614716,
      2.506628277459239};
    static const double c[] = {-0.007784894002430293, -0.3223964580411365,
      -2.400758277161838, -2.549732539343734, 4.374664141464968,
      2.938163982698783};
    static const double d[] = {0.007784695709041462, 0.3224671290700398,
      2.445134137142996, 3.754408661907416};
    static const double den[] = {-54.47609879822406, 161.5858368580409,
      -155.6989798598866, 66.80131188771972, -13.28068155288572};
    if (p < .02425) {
        const double q = sqrt(-2.0 * log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    if (p > .97575) {
        const double q = sqrt(-2.0 * log(1.0-p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    const double q = p-.5, r = q*q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
           (((((den[0]*r+den[1])*r+den[2])*r+den[3])*r+den[4])*r+1.0);
}

static void double_sample(const asian_msfr_ref_basis_t *basis, double strike,
                          int put, int cv, int force, int ai, int gi,
                          double out[4])
{
    const double sign = put ? -1.0 : 1.0, discount = exp(-.03);
    const double am = sign * (basis->a-strike);
    const double gm = sign * (basis->g-strike);
    if (!force) { ai = am > 0.0; gi = gm > 0.0; }
    const double ap = ai ? am : 0.0, gp = cv && gi ? gm : 0.0;
    out[0] = discount * (ap-gp);
    out[1] = discount * ((ai ? sign*basis->a_delta : 0.0) -
                         (cv && gi ? sign*basis->g_delta : 0.0));
    out[2] = discount * ((ai ? sign*basis->a_vega : 0.0) -
                         (cv && gi ? sign*basis->g_vega : 0.0));
    out[3] = discount * ((ai ? sign*basis->a_rho : 0.0) -
                         (cv && gi ? sign*basis->g_rho : 0.0)) - out[0];
}

static int float64_validation(const fixture_t *f)
{
    float strikes[32];
    make_strikes(32u, strikes);
    asian_msfr_ref_basis_t *db = calloc(PATHS, sizeof(*db));
    asian_genuine_msfr_strike_controls_t *sc = a64(sizeof(*sc));
    asian_genuine_msfr_consumer_context_t *cc = a64(sizeof(*cc));
    asian_genuine_msfr_accumulator_t *acc = a64(sizeof(*acc));
    asian_genuine_msfr_accumulator_t *same_acc = a64(sizeof(*same_acc));
    asian_genuine_msfr_output_t *got = a64(sizeof(*got));
    asian_genuine_msfr_output_t *same_output = a64(sizeof(*same_output));
    if (!db || !sc || !cc || !acc || !same_acc || !got || !same_output)
        return -1;
    const double dt = 1.0/f->n;
    const double drift = (.03-.5*.20*.20)*dt;
    const double diffusion = .20*sqrt(dt);
    for (uint32_t path = 0; path < PATHS; ++path) {
        double x[256];
        for (uint32_t k = 0; k < f->n; ++k) {
            const uint32_t word = sobol(8192u+path, f->directions[k]);
            const double u = ((double)word+.5)*0x1p-32;
            x[k] = drift + diffusion*inverse_normal(u);
        }
        asian_msfr_ref_targeted(x, f->n, 100.0, .03, 0.0, .20, 1.0,
                                &db[path]);
    }
    if (asian_genuine_msfr_prepare_strikes(sc,100,.03,0,.20,1,f->n,
          strikes,32) || asian_genuine_msfr_prepare_consumer_context(cc,sc))
        return -1;
    double max_adjusted=0.0,max_same_state=0.0,smooth_sum=0.0;
    uint64_t smooth_count=0;
    for (int cv=0; cv<2; ++cv) {
        if (asian_genuine_msfr_accumulator_init(acc,cc,cv) ||
            asian_genuine_msfr_consume_block(f->basis,cc,cv,2,acc) ||
            asian_genuine_msfr_finalize(cc,acc,got) ||
            asian_genuine_msfr_accumulator_init(same_acc,cc,cv) ||
            asian_genuine_msfr_scalar_consume_block(f->basis,cc,cv,same_acc) ||
            asian_genuine_msfr_finalize(cc,same_acc,same_output) ||
            memcmp(got,same_output,sizeof(*got)) != 0) return -1;
        for (uint32_t i=0; i<32u; ++i) {
            const int direct_put =
                !(sc->strikes[i].flags&ASIAN_GENUINE_MSFR_DIRECT_CALL);
            double independent[4]={0},hybrid[4]={0},opposite[4]={0};
            uint32_t call_af=0,put_af=0,call_gf=0,put_gf=0;
            for (uint32_t path=0; path<PATHS; ++path) {
                const float fa=f->basis->values[ASIAN_GENUINE_MSFR_A][path];
                const float fg=f->basis->values[ASIAN_GENUINE_MSFR_G][path];
                const int fai=direct_put ? fa<strikes[i] : fa>strikes[i];
                const int fgi=direct_put ? fg<strikes[i] : fg>strikes[i];
                const int dai=direct_put ? db[path].a<strikes[i] :
                                           db[path].a>strikes[i];
                const int dgi=direct_put ? db[path].g<strikes[i] :
                                           db[path].g>strikes[i];
                const int ca_f=fa>strikes[i],ca_d=db[path].a>strikes[i];
                const int cg_f=fg>strikes[i],cg_d=db[path].g>strikes[i];
                call_af += ca_f!=ca_d; put_af += (fa<strikes[i])!=(db[path].a<strikes[i]);
                call_gf += cg_f!=cg_d; put_gf += (fg<strikes[i])!=(db[path].g<strikes[i]);
                double one[4],forced[4],opp[4];
                double_sample(&db[path],strikes[i],direct_put,cv,0,0,0,one);
                double_sample(&db[path],strikes[i],direct_put,cv,1,fai,fgi,forced);
                double_sample(&db[path],strikes[i],!direct_put,cv,0,0,0,opp);
                (void)dai; (void)dgi;
                for(uint32_t field=0;field<4u;++field){
                    independent[field]+=one[field];hybrid[field]+=forced[field];
                    opposite[field]+=opp[field];
                }
            }
            asian_msfr_ref_value_t gc,gp;
            asian_msfr_ref_geometric_exact(100,strikes[i],.03,0,.20,1,f->n,
                                           &gc,&gp);
            const double *exact=direct_put?(const double*)&gp:(const double*)&gc;
            const asian_genuine_msfr_value_t *direct=direct_put?
                &got->values[i].put:&got->values[i].call;
            const asian_genuine_msfr_value_t *other=direct_put?
                &got->values[i].call:&got->values[i].put;
            const double *dv=(const double*)direct,*ov=(const double*)other;
            for(uint32_t field=0;field<4u;++field){
                independent[field]/=PATHS;hybrid[field]/=PATHS;
                opposite[field]/=PATHS;
                if(cv){independent[field]+=exact[field];hybrid[field]+=exact[field];
                    opposite[field]+=direct_put?((const double*)&gc)[field]:
                                                   ((const double*)&gp)[field];}
                const double raw=dv[field]-independent[field];
                const double flip=hybrid[field]-independent[field];
                const double smooth=raw-flip;
                const double same_error=dv[field]-
                    (direct_put?(const double*)&same_output->values[i].put:
                                (const double*)&same_output->values[i].call)[field];
                const double parity=sc->strikes[i].call_adjust[field]-
                                    sc->strikes[i].put_adjust[field];
                const double independent_other_from_parity=
                    direct_put?independent[field]+parity:independent[field]-parity;
                const double other_raw=ov[field]-independent_other_from_parity;
                const double independently_evaluated_opposite_gap=
                    ov[field]-opposite[field];
                if(fabs(smooth)>max_adjusted)max_adjusted=fabs(smooth);
                if(field&&fabs(same_error)>max_same_state)
                    max_same_state=fabs(same_error);
                smooth_sum+=smooth;++smooth_count;
                printf("float64 N=%u strike=%.9g direct_side=%s estimator=%s "
                  "field=%s raw_signed_error=%.9g indicator_flip_contribution=%.9g "
                  "smooth_residual=%.9g same_state_error=%.9g "
                  "opposite_parity_raw_error=%.9g "
                  "independent_opposite_estimator_gap=%.9g call_arithmetic_flips=%u "
                  "put_arithmetic_flips=%u call_geometric_flips=%u put_geometric_flips=%u\n",
                  f->n,strikes[i],direct_put?"put":"call",cv?"cv":"arithmetic",
                  (const char*[]){"price","delta","vega","rho"}[field],raw,flip,
                  smooth,same_error,other_raw,independently_evaluated_opposite_gap,
                  call_af,put_af,call_gf,put_gf);
            }
        }
    }
    const double mean=smooth_sum/smooth_count;
    printf("float64_summary N=%u max_kink_adjusted_residual=%.9g "
      "max_same_state_greek_error=%.9g signed_mean_smooth_residual=%.9g\n",
      f->n,max_adjusted,max_same_state,mean);
    free(same_output);free(got);free(same_acc);free(acc);free(cc);free(sc);free(db);
    return max_adjusted<=1e-4&&max_same_state<=1e-6&&fabs(mean)<=1e-6?0:-1;
}

static void make_strikes(uint32_t k, float strikes[32])
{
    if (k == 1u || k == 4u || k == 8u || k == 16u || k == 32u) {
        if (asian_genuine_strip_fixed_strikes(k, strikes) != 0) abort();
        return;
    }
    for (uint32_t i = 0; i < k; ++i) strikes[i] = 70.0f + 1.75f * i;
}

static int phase1_compatibility(const fixture_t *f,
                                const asian_genuine_msfr_strike_controls_t *sc,
                                enum asian_genuine_msfr_estimator estimator,
                                const asian_genuine_msfr_output_t *output)
{
    asian_genuine_aad_phase1_controls_t *pc = a64(sizeof(*pc));
    asian_genuine_aad_phase1_context_t *ctx = a64(sizeof(*ctx));
    if (pc == NULL || ctx == NULL) return -1;
    const float strike = sc->strikes[0].strike;
    if (asian_genuine_aad_phase1_prepare_controls(pc, 100.0, strike, 0.03, 0.0,
          0.20, 1.0, f->n) != ASIAN_GENUINE_AAD_PHASE1_OK ||
        asian_genuine_aad_phase1_prepare_context(ctx, f->routes, f->tape, pc,
          100.0, strike, 0.03, 0.0, 0.20, 1.0, f->n) !=
          ASIAN_GENUINE_AAD_PHASE1_OK) {
        free(ctx); free(pc); return -1;
    }
    const int direct_call =
        (sc->strikes[0].flags & ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0;
    typedef void (*leaf_t)(const asian_genuine_aad_phase1_context_t *,
                           asian_genuine_aad_phase1_value_t *);
    leaf_t leaf;
    if (estimator == ASIAN_GENUINE_MSFR_ARITHMETIC)
        leaf = direct_call ? asian_genuine_aad_phase1_forward_arithmetic_call_diag
                           : asian_genuine_aad_phase1_forward_arithmetic_put_diag;
    else
        leaf = direct_call ? asian_genuine_aad_phase1_forward_cv_call_diag
                           : asian_genuine_aad_phase1_forward_cv_put_diag;
    asian_genuine_aad_phase1_value_t expected;
    leaf(ctx, &expected);
    const asian_genuine_msfr_value_t *got =
        direct_call ? &output->values[0].call : &output->values[0].put;
    const int ok = memcmp(&expected, got, sizeof(expected)) == 0;
    if (!ok) {
        const double *ev = (const double *)&expected;
        const double *gv = (const double *)got;
        fprintf(stderr, "Phase1 compatibility mismatch N=%u estimator=%u "
                "price=%a/%a delta=%a/%a vega=%a/%a rho=%a/%a\n",
                f->n, estimator, gv[0], ev[0], gv[1], ev[1], gv[2], ev[2],
                gv[3], ev[3]);
    }
    free(ctx); free(pc);
    return ok ? 0 : -1;
}

static int validate_strike_array(const fixture_t *f, const float *strikes,
                                 uint32_t k)
{
    asian_genuine_msfr_strike_controls_t *sc = a64(sizeof(*sc));
    asian_genuine_msfr_consumer_context_t *cc = a64(sizeof(*cc));
    asian_genuine_msfr_accumulator_t *a2 = a64(sizeof(*a2));
    asian_genuine_msfr_accumulator_t *a4 = a64(sizeof(*a4));
    asian_genuine_msfr_accumulator_t *as = a64(sizeof(*as));
    asian_genuine_msfr_output_t *o2 = a64(sizeof(*o2));
    asian_genuine_msfr_output_t *o4 = a64(sizeof(*o4));
    asian_genuine_msfr_output_t *os = a64(sizeof(*os));
    if (!sc || !cc || !a2 || !a4 || !as || !o2 || !o4 || !os) return -1;
    if (asian_genuine_msfr_prepare_strikes(sc, 100.0, 0.03, 0.0, 0.20,
          1.0, f->n, strikes, k) != ASIAN_GENUINE_MSFR_OK ||
        asian_genuine_msfr_prepare_consumer_context(cc, sc) !=
          ASIAN_GENUINE_MSFR_OK) return -1;
    const uint64_t controls_hash =
        hash_bytes(UINT64_C(1469598103934665603), sc, sizeof(*sc));
    const uint64_t basis_hash =
        hash_bytes(UINT64_C(1469598103934665603), f->basis, sizeof(*f->basis));

    for (int estimator = 0; estimator < 2; ++estimator) {
        if (asian_genuine_msfr_accumulator_init(a2, cc, estimator) ||
            asian_genuine_msfr_accumulator_init(a4, cc, estimator) ||
            asian_genuine_msfr_accumulator_init(as, cc, estimator) ||
            asian_genuine_msfr_consume_block(f->basis, cc, estimator, 2u, a2) ||
            asian_genuine_msfr_consume_block(f->basis, cc, estimator, 4u, a4) ||
            asian_genuine_msfr_scalar_consume_block(f->basis, cc, estimator, as) ||
            asian_genuine_msfr_finalize(cc, a2, o2) ||
            asian_genuine_msfr_finalize(cc, a4, o4) ||
            asian_genuine_msfr_finalize(cc, as, os)) return -1;
        for (uint32_t i = 0; i < k; ++i) {
            if (memcmp(a2->direct_sums[i], a4->direct_sums[i], 32u) != 0 ||
                memcmp(a2->direct_sums[i], as->direct_sums[i], 32u) != 0 ||
                memcmp(&o2->values[i], &o4->values[i],
                       sizeof(o2->values[i])) != 0 ||
                memcmp(&o2->values[i], &os->values[i],
                       sizeof(o2->values[i])) != 0) {
                fprintf(stderr, "consumer mismatch N=%u K=%u estimator=%d strike=%u\n",
                        f->n, k, estimator, i);
                return -1;
            }
            const double *call = (const double *)&o2->values[i].call;
            const double *put = (const double *)&o2->values[i].put;
            for (uint32_t field = 0; field < 4u; ++field) {
                const double parity = sc->strikes[i].call_adjust[field] -
                                      sc->strikes[i].put_adjust[field];
                if (fabs((call[field] - put[field]) - parity) > 2e-14)
                    return -1;
            }
        }
        if (k == 1u && phase1_compatibility(f, sc, estimator, o2) != 0)
            return -1;
        if (a2->completed_path_count != PATHS ||
            a2->completed_block_count != 1u) return -1;
        if (asian_genuine_msfr_accumulator_init(as, cc, estimator) ||
            asian_genuine_msfr_consume_block(f->basis, cc, estimator, 2u, as) ||
            asian_genuine_msfr_consume_block(f->basis, cc, estimator, 2u, as) ||
            asian_genuine_msfr_finalize(cc, as, os) ||
            as->completed_path_count != 2u * PATHS ||
            as->completed_block_count != 2u ||
            memcmp(o2, os, k * sizeof(o2->values[0])) != 0)
            return -1;
    }
    const int immutable = controls_hash ==
            hash_bytes(UINT64_C(1469598103934665603), sc, sizeof(*sc)) &&
        basis_hash == hash_bytes(UINT64_C(1469598103934665603),
                                 f->basis, sizeof(*f->basis));
    free(os); free(o4); free(o2); free(as); free(a4); free(a2); free(cc); free(sc);
    return immutable ? 0 : -1;
}

static int validate_consumers(const fixture_t *f, uint32_t k)
{
    float strikes[32];
    make_strikes(k, strikes);
    return validate_strike_array(f, strikes, k);
}

static int validate_kink_strikes(const fixture_t *f)
{
    const float arithmetic = f->basis->values[ASIAN_GENUINE_MSFR_A][0];
    const float geometric = f->basis->values[ASIAN_GENUINE_MSFR_G][17];
    const float strikes[] = {
        nextafterf(arithmetic, 0.0f), arithmetic,
        nextafterf(arithmetic, INFINITY),
        nextafterf(geometric, 0.0f), geometric,
        nextafterf(geometric, INFINITY),
    };
    if (!(arithmetic > strikes[0]) || arithmetic > strikes[1] ||
        arithmetic < strikes[1] || !(arithmetic < strikes[2]) ||
        !(geometric > strikes[3]) || geometric > strikes[4] ||
        geometric < strikes[4] || !(geometric < strikes[5]))
        return -1;
    return validate_strike_array(f, strikes,
                                 sizeof(strikes) / sizeof(strikes[0]));
}

static int run_n(uint32_t n)
{
    fixture_t fixture;
    if (prepare_fixture(&fixture, n) != 0) {
        fprintf(stderr, "preparation failure N=%u\n", n);
        return -1;
    }
    int status = compare_basis(&fixture);
    if (status == 0) status = float64_validation(&fixture);
    const uint32_t ks[] = {1, 3, 4, 8, 16, 31, 32};
    for (uint32_t i = 0; status == 0 && i < sizeof(ks) / sizeof(ks[0]); ++i)
        status = validate_consumers(&fixture, ks[i]);
    if (status == 0) status = validate_kink_strikes(&fixture);
    if (status == 0 && fixture_hash(&fixture) != fixture.immutable_hash)
        status = -1;
    printf("vector N=%u status=%s basis_bytes=%zu route_count=%u\n", n,
           status == 0 ? "PASS" : "FAIL", sizeof(*fixture.basis), n - 1u);
    release_fixture(&fixture);
    return status;
}

static int sweep_domain(void)
{
    uint64_t digest = UINT64_C(1469598103934665603);
    for (uint32_t n = 2u; n <= 256u; ++n) {
        fixture_t fixture;
        if (prepare_fixture(&fixture, n) != 0) {
            fprintf(stderr, "domain preparation failure N=%u\n", n);
            return -1;
        }
        asian_genuine_msfr_basis_forward_diag(fixture.basis_context,
                                              fixture.basis);
        int status = guard_ok(&fixture) &&
                     fixture_hash(&fixture) == fixture.immutable_hash ? 0 : -1;
        for (uint32_t packet = 0; status == 0 && packet < PATHS / 32u;
             ++packet) {
            asian_genuine_msfr_packet_trace_t trace;
            asian_genuine_msfr_forward_probe_diag(fixture.basis_context,
                                                   packet, &trace);
            for (uint32_t field = 0; status == 0 && field < 8u; ++field)
                if (memcmp(trace.basis[field],
                           &fixture.basis->values[field][packet * 32u],
                           32u * sizeof(float)) != 0)
                    status = -1;
        }
        const uint32_t phase_packet = (n * 73u) & (PATHS / 32u - 1u);
        if (status == 0 &&
            phase1_basis_probe_compatibility(&fixture, phase_packet) != 0)
            status = -1;
        const uint64_t first = hash_bytes(UINT64_C(1469598103934665603),
                                          fixture.basis,
                                          sizeof(*fixture.basis));
        asian_genuine_msfr_basis_forward_diag(fixture.basis_context,
                                              fixture.basis_copy);
        const uint64_t second = hash_bytes(UINT64_C(1469598103934665603),
                                           fixture.basis_copy,
                                           sizeof(*fixture.basis_copy));
        if (first != second || fixture_hash(&fixture) != fixture.immutable_hash)
            status = -1;
        digest ^= first + n;
        digest *= UINT64_C(1099511628211);
        release_fixture(&fixture);
        if (status != 0) {
            fprintf(stderr, "domain basis identity failure N=%u\n", n);
            return -1;
        }
    }
    printf("domain_sweep=PASS N_min=2 N_max=256 counts_checked=255 "
           "basis_fields=8 paths_per_count=4096 deterministic=yes "
           "digest=%016" PRIx64 "\n", digest);
    return 0;
}

static int validate_edge_contracts(void)
{
    fixture_t fixture;
    if (prepare_fixture(&fixture, 17u) != 0) return -1;
    const double rates[] = {-0.02, 0.0, 0.03};
    const double sigmas[] = {0.05, 0.50, 0.20};
    const float strikes[] = {73.25f, 99.75f, 100.0f, 100.5f, 137.0f};
    asian_genuine_msfr_strike_controls_t *strike_controls =
        a64(sizeof(*strike_controls));
    if (strike_controls == NULL) return -1;
    int status = 0;
    for (uint32_t contract = 0; contract < 3u && status == 0; ++contract) {
        const double dt = 1.0 / fixture.n;
        const float drift = (float)((rates[contract] -
              0.5 * sigmas[contract] * sigmas[contract]) * dt);
        const float diffusion = (float)(sigmas[contract] * sqrt(dt));
        if (ordered_d1_diag_prepare(fixture.producer, drift, diffusion, 8192u,
              ORDERED_D1_DIAG_PREPARE_X3,
              asian_genuine_msfr_producer_fixing_count(fixture.n)) != 0) {
            status = -1;
            break;
        }
        ordered_d1_x_only_diag(256u, fixture.producer, fixture.x);
        asian_vector_exp_range_reduced_array_diag(fixture.x, fixture.growth);
        asian_vector_exp_range_reduced_array_diag(fixture.x + PATHS,
                                                   fixture.growth + PATHS);
        if (asian_genuine_msfr_prepare_basis_controls(
              fixture.basis_controls, 100.0, rates[contract], 0.0,
              sigmas[contract], 1.0, fixture.n) != ASIAN_GENUINE_MSFR_OK ||
            asian_genuine_msfr_prepare_basis_context(
              fixture.basis_context, fixture.routes, fixture.basis_controls,
              100.0, rates[contract], 0.0, sigmas[contract], 1.0,
              fixture.n) != ASIAN_GENUINE_MSFR_OK ||
            asian_genuine_msfr_prepare_strikes(
              strike_controls, 100.0, rates[contract], 0.0,
              sigmas[contract], 1.0, fixture.n, strikes,
              sizeof(strikes) / sizeof(strikes[0])) != ASIAN_GENUINE_MSFR_OK) {
            status = -1;
            break;
        }
        fixture.immutable_hash = fixture_hash(&fixture);
        asian_genuine_msfr_basis_forward_diag(fixture.basis_context,
                                              fixture.basis);
        const uint32_t packets[] = {0u, 63u, 127u};
        for (uint32_t pi = 0; pi < 3u && status == 0; ++pi) {
            asian_genuine_msfr_packet_trace_t trace;
            asian_genuine_msfr_forward_probe_diag(fixture.basis_context,
                                                   packets[pi], &trace);
            for (uint32_t field = 0; field < 8u; ++field)
                if (memcmp(trace.basis[field],
                           &fixture.basis->values[field][packets[pi] * 32u],
                           32u * sizeof(float)) != 0) {
                    status = -1;
                    break;
                }
        }
        if (!guard_ok(&fixture) ||
            fixture_hash(&fixture) != fixture.immutable_hash)
            status = -1;
    }
    free(strike_controls);
    release_fixture(&fixture);
    printf("edge_contracts status=%s N=17 rates=-0.02,0,0.03 "
           "sigmas=0.05,0.50,0.20 arbitrary_strikes=5\n",
           status == 0 ? "PASS" : "FAIL");
    return status;
}

int main(int argc, char **argv)
{
    uint32_t selected_n = 0u;
    const char *report = NULL;
    int sweep = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--N") == 0 && ++i < argc)
            selected_n = (uint32_t)strtoul(argv[i], NULL, 10);
        else if (strcmp(argv[i], "--report") == 0 && ++i < argc)
            report = argv[i];
        else if (strcmp(argv[i], "--sweep-domain") == 0)
            sweep = 1;
        else return 2;
    }
    if (report != NULL && freopen(report, "w", stdout) == NULL) return 2;
    if (sweep) return selected_n == 0u && sweep_domain() == 0 ? 0 : 1;
    if (selected_n != 0u)
        return selected_n >= 2u && selected_n <= 256u &&
               run_n(selected_n) == 0 ? 0 : 1;
    const uint32_t ns[] = {2, 16, 256};
    for (uint32_t i = 0; i < sizeof(ns) / sizeof(ns[0]); ++i)
        if (run_n(ns[i]) != 0) return 1;
    if (validate_edge_contracts() != 0) return 1;
    puts("asian_genuine_multistrike_full_risk_vector=PASS "
         "check_N=2,16,256 arbitrary_K=1..32 tiles=2,4");
    return 0;
}
