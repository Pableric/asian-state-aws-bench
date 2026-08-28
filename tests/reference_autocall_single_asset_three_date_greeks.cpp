#define main autocall_raw_reference_embedded_main
#include "tests/reference_autocall_single_asset_three_date_raw.cpp"
#undef main

#include "tests/autocall_single_asset_three_date_greek_cases.h"
#include "private/autocall_single_asset_three_date_greeks_diag.h"

#include <array>
#include <cfenv>
#include <numeric>

namespace greek_reference {

constexpr unsigned REPLICATIONS = 32;
constexpr uint64_t SHIFT_SEED = UINT64_C(0x4155544f43414c4c);
constexpr double T31_975 = 2.0395134463964077;
constexpr std::array<double,3> SPOT_BUMPS{{0.005,0.010,0.020}};
constexpr std::array<double,3> VOL_BUMPS{{0.005,0.010,0.020}};

using Coefficients = autocall_3date_greek_coefficients_t;
using Legs = autocall_3date_greek_legs_t;

struct GreekValues {
    double price, delta, gamma, vega;
};

struct OracleLeg {
    std::array<double,3> call;
    std::array<double,3> coupon;
    double survival,protected_probability,unprotected_contribution,price;
    double direct, error, mass;
};

uint64_t splitmix64(uint64_t value)
{
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

std::array<std::array<uint32_t,3>,REPLICATIONS> shifts()
{
    std::array<std::array<uint32_t,3>,REPLICATIONS> out{};
    for (unsigned r=0;r<REPLICATIONS;++r)
        for (unsigned d=0;d<3;++d) {
            const uint64_t ordinal=3u*r+d+1u;
            out[r][d]=static_cast<uint32_t>(splitmix64(
                SHIFT_SEED+UINT64_C(0x9e3779b97f4a7c15)*ordinal)>>32);
        }
    return out;
}

Legs make_legs(double center, double relative_or_absolute, bool relative)
{
    Legs out{};
    const int status=relative ? autocall_3date_prepare_spot_legs(
        center,relative_or_absolute,&out) :
        autocall_3date_prepare_volatility_legs(center,relative_or_absolute,&out);
    if (status!=AUTOCALL_3DATE_GREEK_OK) std::abort();
    return out;
}

double combine(const Coefficients &c,double minus,double zero,double plus)
{
    return c.minus*minus+c.zero*zero+c.plus*plus;
}

OracleLeg oracle_leg(const autocall_frozen_case_t &c)
{
    const GaussianOracle primary(c);
    const Decomposition decomposition=primary.evaluate();
    const ValueError independent=DirectIntegrator(c).evaluate();
    const double mass=std::fabs(decomposition.call[0]+decomposition.call[1]+
                                decomposition.call[2]+decomposition.survival-1.0);
    if (mass>1e-11 || !std::isfinite(decomposition.price) ||
        !std::isfinite(independent.value)) std::abort();
    return {decomposition.call,decomposition.coupon,decomposition.survival,
            decomposition.protected_probability,
            decomposition.unprotected_contribution,decomposition.price,
            independent.value,
            decomposition.estimated_error+std::fabs(decomposition.price-
                                                     independent.value),mass};
}

GreekValues oracle_greeks(const autocall_frozen_case_t &base,
                          const Legs &spot,const Legs &vol,
                          double *direct_delta,double *direct_gamma,
                          double *direct_vega,double *error_bp)
{
    autocall_frozen_case_t c=base;
    c.s0=spot.zero; c.sigma=vol.zero;
    const OracleLeg zero=oracle_leg(c);
    c.s0=spot.minus; const OracleLeg sm=oracle_leg(c);
    c.s0=spot.plus; const OracleLeg sp=oracle_leg(c);
    c.s0=spot.zero; c.sigma=vol.minus; const OracleLeg vm=oracle_leg(c);
    c.sigma=vol.plus; const OracleLeg vp=oracle_leg(c);
    const double delta=combine(spot.first,sm.price,zero.price,sp.price);
    const double gamma=combine(spot.second,sm.price,zero.price,sp.price);
    const double vega=combine(vol.first,vm.price,zero.price,vp.price);
    *direct_delta=combine(spot.first,sm.direct,zero.direct,sp.direct);
    *direct_gamma=combine(spot.second,sm.direct,zero.direct,sp.direct);
    *direct_vega=combine(vol.first,vm.direct,zero.direct,vp.direct);
    const double d_bound=std::fabs(spot.first.minus)*sm.error+
        std::fabs(spot.first.zero)*zero.error+std::fabs(spot.first.plus)*sp.error;
    const double g_bound=std::fabs(spot.second.minus)*sm.error+
        std::fabs(spot.second.zero)*zero.error+std::fabs(spot.second.plus)*sp.error;
    const double v_bound=std::fabs(vol.first.minus)*vm.error+
        std::fabs(vol.first.zero)*zero.error+std::fabs(vol.first.plus)*vp.error;
    const double d_error=std::max(std::fabs(delta-*direct_delta),d_bound)*
        (.01*spot.zero);
    const double g_error=.5*std::max(std::fabs(gamma-*direct_gamma),g_bound)*
        (.01*spot.zero)*(.01*spot.zero);
    const double v_error=std::max(std::fabs(vega-*direct_vega),v_bound)*.01;
    *error_bp=1e4*std::max({d_error,g_error,v_error})/base.notional;
    return {zero.price,delta,gamma,vega};
}

float from_bits(uint32_t bits)
{
    float out; std::memcpy(&out,&bits,sizeof(out)); return out;
}

float qualified_exp(float x)
{
    const float log2e=from_bits(UINT32_C(0x3fb8aa3b));
    const float ln2hi=from_bits(UINT32_C(0x3f318000));
    const float ln2lo=from_bits(UINT32_C(0xb95e8083));
    static constexpr uint32_t bits[9]={
        0x3f800000,0x3f7ffff9,0x3efffffc,0x3e2aabbf,0x3d2aab67,
        0x3c085d88,0x3ab5de3b,0x3959cfde,0x37d8c471};
    const float exponent=std::nearbyintf(x*log2e);
    float reduced=std::fma(-ln2hi,exponent,x);
    reduced=std::fma(-ln2lo,exponent,reduced);
    float y=from_bits(bits[8]);
    for (int i=7;i>=0;--i) y=std::fma(reduced,y,from_bits(bits[i]));
    return std::scalbnf(y,static_cast<int>(exponent));
}

enum class Mode { Mathematical, Binary32, Replay };

double qmc_price(const autocall_frozen_case_t &c,
                 const std::array<uint32_t,3> &shift,Mode mode)
{
    uint32_t directions[3][32];
    for (unsigned d=0;d<3;++d) direction_row(d,directions[d]);
    const double dt=c.maturity/3.0;
    const double drift=(c.rate-c.dividend-0.5*c.sigma*c.sigma)*dt;
    const double diffusion=c.sigma*std::sqrt(dt);
    const float fdrift=static_cast<float>(drift);
    const float fdiffusion=static_cast<float>(diffusion);
    std::array<float,16> low{},high{};
    double total=0.0;
    for (uint32_t path=0;path<4096u;++path) {
        double ds=c.s0,dpv=0.0;
        float fs=static_cast<float>(c.s0),fpv=0.0f;
        bool alive=true;
        for (unsigned d=0;d<3;++d) {
            const uint32_t word=sobol_word(8192u+path,directions[d])^shift[d];
            const double u=(static_cast<double>(word)+0.5)/4294967296.0;
            const double z=inverse_normal_acklam(u);
            const float x=std::fma(fdiffusion,static_cast<float>(z),fdrift);
            const float growth=qualified_exp(x);
            const double observation=c.maturity*(d+1.0)/3.0;
            const double payment=observation+c.payment_lag_fraction*c.maturity;
            if (mode==Mode::Mathematical) {
                ds*=std::exp(drift+diffusion*z);
                if (alive && ds>=c.coupon_barrier[d])
                    dpv+=c.coupon[d]*std::exp(-c.rate*payment);
                if (alive && ds>=c.call_barrier[d]) {
                    dpv+=c.redemption[d]*std::exp(-c.rate*payment); alive=false;
                }
            } else if (mode==Mode::Replay) {
                ds*=static_cast<double>(growth);
                const float cb=static_cast<float>(c.coupon_barrier[d]);
                const float ab=static_cast<float>(c.call_barrier[d]);
                if (alive && ds>=static_cast<double>(cb))
                    dpv+=static_cast<double>(static_cast<float>(c.coupon[d]*
                        std::exp(-c.rate*payment)));
                if (alive && ds>=static_cast<double>(ab)) {
                    dpv+=static_cast<double>(static_cast<float>(c.redemption[d]*
                        std::exp(-c.rate*payment))); alive=false;
                }
            } else {
                fs=fs*growth;
                if (alive && fs>=static_cast<float>(c.coupon_barrier[d]))
                    fpv=fpv+static_cast<float>(c.coupon[d]*
                        std::exp(-c.rate*payment));
                if (alive && fs>=static_cast<float>(c.call_barrier[d])) {
                    fpv=fpv+static_cast<float>(c.redemption[d]*
                        std::exp(-c.rate*payment)); alive=false;
                }
            }
        }
        const double payment=c.maturity*(1.0+c.payment_lag_fraction);
        if (alive) {
            if (mode==Mode::Mathematical)
                dpv+=c.notional*std::exp(-c.rate*payment)*
                    (ds>=c.protection ? 1.0 : ds/c.s0);
            else if (mode==Mode::Replay) {
                const float terminal=static_cast<float>(c.notional*
                    std::exp(-c.rate*payment));
                const float inv=1.0f/static_cast<float>(c.s0);
                dpv+=static_cast<double>(terminal)*(ds>=static_cast<float>(c.protection) ?
                    1.0 : ds*static_cast<double>(inv));
            } else {
                const float terminal=static_cast<float>(c.notional*
                    std::exp(-c.rate*payment));
                fpv=fpv+(fs>=static_cast<float>(c.protection) ? terminal :
                    (terminal*(fs*(1.0f/static_cast<float>(c.s0)))));
            }
        }
        if (mode==Mode::Binary32) {
            std::array<float,16> &acc=(path&16u)?high:low;
            acc[path&15u]=acc[path&15u]+fpv;
        } else total+=dpv;
    }
    if (mode!=Mode::Binary32) return total/4096.0;
    float lane[16];
    for (unsigned i=0;i<16;++i) lane[i]=low[i]+high[i];
    float a[4],b[4];
    for (unsigned i=0;i<4;++i) {
        a[i]=lane[i]+lane[i+4]; b[i]=lane[i+8]+lane[i+12]; a[i]=a[i]+b[i];
    }
    a[0]=a[0]+a[2]; a[1]=a[1]+a[3]; a[0]=a[0]+a[1];
    return static_cast<double>(a[0])/4096.0;
}

GreekValues qmc_greeks(const autocall_frozen_case_t &base,
                       const Legs &spot,const Legs &vol,
                       const std::array<uint32_t,3> &shift,Mode mode)
{
    autocall_frozen_case_t c=base;
    c.s0=spot.zero; c.sigma=vol.zero; const double zero=qmc_price(c,shift,mode);
    c.s0=spot.minus; const double sm=qmc_price(c,shift,mode);
    c.s0=spot.plus; const double sp=qmc_price(c,shift,mode);
    c.s0=spot.zero; c.sigma=vol.minus; const double vm=qmc_price(c,shift,mode);
    c.sigma=vol.plus; const double vp=qmc_price(c,shift,mode);
    return {zero,combine(spot.first,sm,zero,sp),
            combine(spot.second,sm,zero,sp),combine(vol.first,vm,zero,vp)};
}

double conditional_price(const autocall_frozen_case_t &c,
                         const std::array<uint32_t,3> &shift,bool swap)
{
    uint32_t directions[2][32]; direction_row(0,directions[0]);
    direction_row(1,directions[1]);
    const double dt=c.maturity/3.0;
    const double mu=(c.rate-c.dividend-0.5*c.sigma*c.sigma)*dt;
    const double a=c.sigma*std::sqrt(dt);
    double total=0.0;
    for (uint32_t path=0;path<4096u;++path) {
        double z[2];
        for (unsigned d=0;d<2;++d) {
            const uint32_t word=sobol_word(8192u+path,directions[d])^shift[d];
            z[d]=inverse_normal_acklam((static_cast<double>(word)+0.5)/4294967296.0);
        }
        const double z2=z[swap?1:0],z3=z[swap?0:1];
        const double cumulative[3]={0.0,z2,z2+z3};
        double prior=std::numeric_limits<double>::infinity(),pv=0.0;
        for (unsigned d=0;d<3;++d) {
            const double call=(std::log(c.call_barrier[d]/c.s0)-
                (d+1.0)*mu)/a-cumulative[d];
            const double coupon=(std::log(c.coupon_barrier[d]/c.s0)-
                (d+1.0)*mu)/a-cumulative[d];
            const double payment=c.maturity*(d+1.0)/3.0+
                c.payment_lag_fraction*c.maturity;
            const double coupon_probability=std::max(0.0,cdf(prior)-cdf(coupon));
            const double call_probability=std::max(0.0,cdf(prior)-cdf(call));
            pv+=std::exp(-c.rate*payment)*(c.coupon[d]*coupon_probability+
                                          c.redemption[d]*call_probability);
            prior=std::min(prior,call);
        }
        const double protection=(std::log(c.protection/c.s0)-3.0*mu)/a-
            cumulative[2];
        const double protected_probability=std::max(0.0,cdf(prior)-cdf(protection));
        const double upper=std::min(prior,protection);
        const double downside=c.notional*std::exp(3.0*mu+a*cumulative[2]+
            0.5*a*a)*cdf(upper-a);
        const double payment=c.maturity*(1.0+c.payment_lag_fraction);
        pv+=std::exp(-c.rate*payment)*(c.notional*protected_probability+downside);
        total+=pv;
    }
    return total/4096.0;
}

GreekValues conditional_greeks(const autocall_frozen_case_t &base,
                               const Legs &spot,const Legs &vol,
                               const std::array<uint32_t,3> &shift,bool swap)
{
    autocall_frozen_case_t c=base;
    c.s0=spot.zero;c.sigma=vol.zero;const double zero=conditional_price(c,shift,swap);
    c.s0=spot.minus;const double sm=conditional_price(c,shift,swap);
    c.s0=spot.plus;const double sp=conditional_price(c,shift,swap);
    c.s0=spot.zero;c.sigma=vol.minus;const double vm=conditional_price(c,shift,swap);
    c.sigma=vol.plus;const double vp=conditional_price(c,shift,swap);
    return {zero,combine(spot.first,sm,zero,sp),combine(spot.second,sm,zero,sp),
            combine(vol.first,vm,zero,vp)};
}

struct Stats { double mean,rmse,half,median,worst,bias; };

Stats stats(std::array<double,REPLICATIONS> values,double reference)
{
    double sum=std::accumulate(values.begin(),values.end(),0.0);
    const double mean=sum/REPLICATIONS;
    double ss=0.0,se=0.0;
    for (double value:values) { ss+=(value-mean)*(value-mean);
                               se+=(value-reference)*(value-reference); }
    std::sort(values.begin(),values.end());
    const double half=T31_975*std::sqrt(ss/(REPLICATIONS-1))/std::sqrt(REPLICATIONS);
    const auto worst=std::max_element(values.begin(),values.end(),[&](double x,double y){
        return std::fabs(x-reference)<std::fabs(y-reference);});
    return {mean,std::sqrt(se/REPLICATIONS),half,
            .5*(values[15]+values[16]),*worst,mean-reference};
}

double shock_scale(unsigned greek,const autocall_frozen_case_t &c)
{
    if (greek==0) return .01*c.s0/c.notional*1e4;
    if (greek==1) return .5*(.01*c.s0)*(.01*c.s0)/c.notional*1e4;
    return .01/c.notional*1e4;
}

} // namespace greek_reference

#ifndef AUTOCALL_GREEK_REFERENCE_ENTRY
#define AUTOCALL_GREEK_REFERENCE_ENTRY main
#endif

int AUTOCALL_GREEK_REFERENCE_ENTRY()
{
    using namespace greek_reference;
    std::fesetround(FE_TONEAREST);
    const auto manifest=shifts();
    std::printf("SHIFT_MANIFEST seed=0x%016llx replications=%u\n",
        static_cast<unsigned long long>(SHIFT_SEED),REPLICATIONS);
    for (unsigned r=0;r<REPLICATIONS;++r)
        std::printf("SHIFT r=%u d1=0x%08x d2=0x%08x d3=0x%08x\n",r,
                    manifest[r][0],manifest[r][1],manifest[r][2]);

    bool raw_pass=true,conditional12_pass=true,conditional21_pass=true;
    std::puts("GREEK_REFERENCE convergence=paired_GL64_128 error_limit_bp=0.05");
    for (unsigned ci=0;ci<AUTOCALL_GREEK_CASES;++ci) {
        const autocall_frozen_case_t c=autocall_greek_case(ci);
        const char *panel=ci<AUTOCALL_GREEK_CORE_CASES?"CORE":"STRESS";
        std::array<std::array<std::array<double,REPLICATIONS>,3>,3> all_raw{};
        std::array<std::array<double,3>,3> all_reference{};
        std::array<std::array<Stats,3>,3> all_raw_stats{};
        for (unsigned d=0;d<3;++d) {
            const double z=(std::log(autocall_greek_inactive_call(&c,d)/
                autocall_greek_forward(&c,d)))/(c.sigma*std::sqrt(c.maturity*(d+1.0)/3.0));
            const double probability=1.0-cdf(z);
            if (ci>=AUTOCALL_GREEK_CORE_CASES &&
                c.call_barrier[d]==autocall_greek_inactive_call(&c,d)) {
                std::printf("INACTIVE_CALL case=%s date=%u crossing_probability=%.17g "
                            "limit=1e-14\n",c.name,d+1u,probability);
                if (probability>=1e-14) raw_pass=false;
            }
        }
        for (unsigned bump=0;bump<3;++bump) {
            const Legs spot=make_legs(c.s0,SPOT_BUMPS[bump],true);
            const Legs vol=make_legs(c.sigma,VOL_BUMPS[bump],false);
            double dd,dg,dv,ref_error_bp;
            const GreekValues reference=oracle_greeks(c,spot,vol,&dd,&dg,&dv,
                                                       &ref_error_bp);
            if (ref_error_bp>0.05) {
                raw_pass=false;conditional12_pass=false;conditional21_pass=false;
            }
            const std::array<uint32_t,3> no_shift{{0u,0u,0u}};
            const GreekValues mathematical=qmc_greeks(
                c,spot,vol,no_shift,Mode::Mathematical);
            std::printf("FIXED_REFERENCE panel=%s case=%s bump=%u price=%.17g "
                "delta=%.17g gamma=%.17g vega_per_unit_sigma=%.17g "
                "vega_per_vol_point=%.17g mathematical_price=%.17g "
                "mathematical_delta=%.17g mathematical_gamma=%.17g "
                "mathematical_vega=%.17g dm=%.17g dp=%.17g vm=%.17g vp=%.17g "
                "reference_error_bp=%.9g\n",panel,c.name,bump,reference.price,
                reference.delta,reference.gamma,reference.vega,.01*reference.vega,
                mathematical.price,mathematical.delta,mathematical.gamma,
                mathematical.vega,spot.dm,spot.dp,vol.dm,vol.dp,ref_error_bp);
            std::array<std::array<double,REPLICATIONS>,3> raw{},replay{},cond0{},cond1{};
            for (unsigned r=0;r<REPLICATIONS;++r) {
                const GreekValues a=qmc_greeks(c,spot,vol,manifest[r],Mode::Binary32);
                const GreekValues b=qmc_greeks(c,spot,vol,manifest[r],Mode::Replay);
                const GreekValues p=conditional_greeks(c,spot,vol,manifest[r],false);
                const GreekValues q=conditional_greeks(c,spot,vol,manifest[r],true);
                raw[0][r]=a.delta;raw[1][r]=a.gamma;raw[2][r]=a.vega;
                replay[0][r]=b.delta;replay[1][r]=b.gamma;replay[2][r]=b.vega;
                cond0[0][r]=p.delta;cond0[1][r]=p.gamma;cond0[2][r]=p.vega;
                cond1[0][r]=q.delta;cond1[1][r]=q.gamma;cond1[2][r]=q.vega;
            }
            const double refs[3]={reference.delta,reference.gamma,reference.vega};
            const char *names[3]={"DELTA","GAMMA","VEGA"};
            for (unsigned g=0;g<3;++g) {
                const Stats rs=stats(raw[g],refs[g]);
                const Stats cs0=stats(cond0[g],refs[g]);
                const Stats cs1=stats(cond1[g],refs[g]);
                std::array<double,REPLICATIONS> amplification{};
                for (unsigned r=0;r<REPLICATIONS;++r)
                    amplification[r]=raw[g][r]-replay[g][r];
                const Stats amp=stats(amplification,0.0);
                const double scale=shock_scale(g,c);
                const double bias_bp=std::fabs(rs.bias)*scale;
                const double half_bp=rs.half*scale;
                const double amp_bp=std::fabs(amp.mean)*scale;
                if (bump==1u && (bias_bp>(ci<AUTOCALL_GREEK_CORE_CASES?1.0:3.0) ||
                    (ci<AUTOCALL_GREEK_CORE_CASES && half_bp>0.5) || amp_bp>0.25))
                    raw_pass=false;
                const double limit=ci<AUTOCALL_GREEK_CORE_CASES?1.0:3.0;
                if (bump==1u && (std::fabs(cs0.bias)*scale>limit ||
                    (ci<AUTOCALL_GREEK_CORE_CASES&&cs0.half*scale>0.5)))
                    conditional12_pass=false;
                if (bump==1u && (std::fabs(cs1.bias)*scale>limit ||
                    (ci<AUTOCALL_GREEK_CORE_CASES&&cs1.half*scale>0.5)))
                    conditional21_pass=false;
                all_reference[bump][g]=refs[g];
                all_raw_stats[bump][g]=rs;
                all_raw[bump][g]=raw[g];
                std::printf("RANDOM panel=%s case=%s bump=%u greek=%s reference=%.17g "
                    "raw_mean=%.17g raw_rmse=%.17g raw_half=%.17g raw_median=%.17g "
                    "raw_worst=%.17g raw_bias_bp=%.9g f32_amplification_bp=%.9g "
                    "conditional_12_rmse_bp=%.9g conditional_21_rmse_bp=%.9g "
                    "reference_error_bp=%.9g\n",panel,c.name,bump,names[g],refs[g],
                    rs.mean,rs.rmse,rs.half,rs.median,rs.worst,bias_bp,amp_bp,
                    cs0.rmse*scale,cs1.rmse*scale,ref_error_bp);
            }
        }
        for (unsigned g=0;g<3;++g) for (unsigned secondary: {0u,2u}) {
            std::array<double,REPLICATIONS> difference{};
            for (unsigned r=0;r<REPLICATIONS;++r)
                difference[r]=(all_raw[secondary][g][r]-all_reference[secondary][g])-
                    (all_raw[1][g][r]-all_reference[1][g]);
            const Stats paired=stats(difference,0.0);
            const double scale=shock_scale(g,c);
            const double limit=ci<AUTOCALL_GREEK_CORE_CASES?1.0:3.0;
            const double three_se=3.0*paired.half/T31_975;
            const bool unstable=std::fabs(paired.mean)*scale>
                    std::max(limit,three_se*scale) ||
                (all_raw_stats[secondary][g].rmse>
                    2.0*all_raw_stats[1][g].rmse &&
                 (all_raw_stats[secondary][g].rmse-
                    all_raw_stats[1][g].rmse)*scale>0.25);
            std::printf("BUMP_STABILITY panel=%s case=%s greek=%u secondary=%u "
                "paired_change_bp=%.9g three_se_bp=%.9g decision=%s\n",panel,
                c.name,g,secondary,std::fabs(paired.mean)*scale,
                three_se*scale,unstable?"FAIL":"PASS");
            if (unstable) raw_pass=false;
        }
    }
    std::puts(raw_pass?"RAW_CRN_GREEKS_QUALIFIED":"RAW_CRN_GREEKS_NOT_QUALIFIED");
    const bool conditional_pass=conditional12_pass||conditional21_pass;
    std::printf("D1_PREINTEGRATION_ORDER_ACCURACY order12=%s order21=%s\n",
        conditional12_pass?"PASS":"FAIL",conditional21_pass?"PASS":"FAIL");
    std::puts(conditional_pass?"D1_PREINTEGRATION_ACCURACY_PASS":
                               "D1_PREINTEGRATION_ACCURACY_FAIL");
    return raw_pass ? 0 : 3;
}
