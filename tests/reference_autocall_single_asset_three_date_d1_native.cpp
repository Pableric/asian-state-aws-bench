#define AUTOCALL_GREEK_REFERENCE_ENTRY autocall_parent_greek_reference_main
#include "tests/reference_autocall_single_asset_three_date_greeks.cpp"
#undef AUTOCALL_GREEK_REFERENCE_ENTRY

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace d1_native_reference {
using namespace greek_reference;
constexpr uint64_t SELECTION_SEED=UINT64_C(0x4155544f43414c4c);
constexpr uint64_t HOLDOUT_SEED=UINT64_C(0x44314e4154495645);
constexpr std::array<uint32_t,7> COUNTS{{64,128,256,512,1024,2048,4096}};

std::array<std::array<uint32_t,3>,REPLICATIONS> shift_bank(uint64_t seed)
{
    std::array<std::array<uint32_t,3>,REPLICATIONS> out{};
    for(unsigned r=0;r<REPLICATIONS;++r)for(unsigned d=0;d<3;++d) {
        const uint64_t ordinal=3u*r+d+1u;
        out[r][d]=static_cast<uint32_t>(splitmix64(seed+
            UINT64_C(0x9e3779b97f4a7c15)*ordinal)>>32);
    }
    return out;
}

double conditional_price_count(const autocall_frozen_case_t &c,
    const std::array<uint32_t,3> &shift,bool order21,uint32_t paths)
{
    uint32_t directions[2][32];direction_row(0,directions[0]);
    direction_row(1,directions[1]);
    const double dt=c.maturity/3.0;
    const double mu=(c.rate-c.dividend-.5*c.sigma*c.sigma)*dt;
    const double a=c.sigma*std::sqrt(dt);double total=0.0;
    for(uint32_t path=0;path<paths;++path) {
        double z[2];
        for(unsigned d=0;d<2;++d) {
            const uint32_t word=sobol_word(8192u+path,directions[d])^shift[d];
            z[d]=inverse_normal_acklam((static_cast<double>(word)+.5)/4294967296.0);
        }
        const double z2=z[order21?1:0],z3=z[order21?0:1];
        const double cumulative[3]={0.0,z2,z2+z3};
        double prior=std::numeric_limits<double>::infinity(),pv=0.0;
        for(unsigned d=0;d<3;++d) {
            const double call=(std::log(c.call_barrier[d]/c.s0)-
                (d+1.0)*mu)/a-cumulative[d];
            const double coupon=(std::log(c.coupon_barrier[d]/c.s0)-
                (d+1.0)*mu)/a-cumulative[d];
            const double payment=c.maturity*(d+1.0)/3.0+
                c.payment_lag_fraction*c.maturity;
            const double coupon_probability=std::max(0.0,cdf(prior)-cdf(coupon));
            const double call_probability=std::max(0.0,cdf(prior)-cdf(call));
            pv+=std::exp(-c.rate*payment)*(c.coupon[d]*coupon_probability+
                c.redemption[d]*call_probability);prior=std::min(prior,call);
        }
        const double protection=(std::log(c.protection/c.s0)-3.0*mu)/a-
            cumulative[2];
        const double protected_probability=std::max(0.0,cdf(prior)-cdf(protection));
        const double upper=std::min(prior,protection);
        const double downside=c.notional*std::exp(3.0*mu+a*cumulative[2]+.5*a*a)*
            cdf(upper-a);
        const double payment=c.maturity*(1.0+c.payment_lag_fraction);
        pv+=std::exp(-c.rate*payment)*(c.notional*protected_probability+downside);
        total+=pv;
    }
    return total/static_cast<double>(paths);
}

GreekValues conditional_greeks_count(const autocall_frozen_case_t &base,
    const Legs &spot,const Legs &vol,const std::array<uint32_t,3> &shift,
    bool order21,uint32_t paths)
{
    autocall_frozen_case_t c=base;
    c.s0=spot.zero;c.sigma=vol.zero;
    const double zero=conditional_price_count(c,shift,order21,paths);
    c.s0=spot.minus;const double sm=conditional_price_count(c,shift,order21,paths);
    c.s0=spot.plus;const double sp=conditional_price_count(c,shift,order21,paths);
    c.s0=spot.zero;c.sigma=vol.minus;
    const double vm=conditional_price_count(c,shift,order21,paths);
    c.sigma=vol.plus;const double vp=conditional_price_count(c,shift,order21,paths);
    return {zero,combine(spot.first,sm,zero,sp),
        combine(spot.second,sm,zero,sp),combine(vol.first,vm,zero,vp)};
}

int count_index(uint32_t value)
{
    for(unsigned i=0;i<COUNTS.size();++i)if(COUNTS[i]==value)return (int)i;
    return -1;
}

struct SweepResult {
    std::array<bool,7> self{};
    std::array<bool,7> global{};
    std::array<double,7> aggregate_rmse{};
    std::array<std::vector<double>,7> rmse_bp;
    std::vector<double> raw_rmse_bp;
};

SweepResult sweep(uint64_t seed,const std::vector<unsigned> &indices,
                  const char *panel,bool verbose=true)
{
    const auto bank=shift_bank(seed);SweepResult result;
    result.self.fill(true);std::array<double,7> rmse_ss{};
    std::array<unsigned,7> rmse_n{};
    if(verbose) {
        std::printf("SHIFT_MANIFEST panel=%s seed=0x%016llx replications=%u\n",
            panel,(unsigned long long)seed,REPLICATIONS);
        for(unsigned r=0;r<REPLICATIONS;++r)
            std::printf("SHIFT panel=%s r=%u d1=0x%08x d2=0x%08x d3=0x%08x\n",
                panel,r,bank[r][0],bank[r][1],bank[r][2]);
    }
    for(unsigned ci=0;ci<AUTOCALL_GREEK_CASES;++ci) {
        const autocall_frozen_case_t c=autocall_greek_case(ci);
        const char *kind=ci<AUTOCALL_GREEK_CORE_CASES?"CORE":"STRESS";
        std::array<std::array<std::array<std::array<double,REPLICATIONS>,3>,3>,7>
            values{};
        std::array<std::array<std::array<double,3>,3>,7> refs{};
        std::array<std::array<std::array<double,3>,3>,7> rmses{};
        for(unsigned bump=0;bump<3;++bump) {
            const Legs spot=make_legs(c.s0,SPOT_BUMPS[bump],true);
            const Legs vol=make_legs(c.sigma,VOL_BUMPS[bump],false);
            double dd,dg,dv,reference_error;
            const GreekValues reference=oracle_greeks(c,spot,vol,&dd,&dg,&dv,
                                                       &reference_error);
            const double reference_value[3]={reference.delta,reference.gamma,
                                              reference.vega};
            if(bump==1u) {
                std::array<std::array<double,REPLICATIONS>,3> raw{};
                for(unsigned r=0;r<REPLICATIONS;++r) {
                    const GreekValues q=qmc_greeks(c,spot,vol,bank[r],
                                                    Mode::Binary32);
                    raw[0][r]=q.delta;raw[1][r]=q.gamma;raw[2][r]=q.vega;
                }
                for(unsigned g=0;g<3;++g)
                    result.raw_rmse_bp.push_back(stats(raw[g],reference_value[g]).rmse*
                                                 shock_scale(g,c));
            }
            for(unsigned selected:indices) {
                const uint32_t paths=COUNTS[selected];
                for(unsigned r=0;r<REPLICATIONS;++r) {
                    const GreekValues q=conditional_greeks_count(c,spot,vol,
                        bank[r],true,paths);
                    values[selected][bump][0][r]=q.delta;
                    values[selected][bump][1][r]=q.gamma;
                    values[selected][bump][2][r]=q.vega;
                }
                const std::array<uint32_t,3> no_shift{{0,0,0}};
                const GreekValues fixed=conditional_greeks_count(c,spot,vol,
                    no_shift,true,paths);
                const GreekValues control=conditional_greeks_count(c,spot,vol,
                    no_shift,false,paths);
                const double fixed_value[3]={fixed.delta,fixed.gamma,fixed.vega};
                const double control_value[3]={control.delta,control.gamma,control.vega};
                for(unsigned g=0;g<3;++g) {
                    std::array<double,REPLICATIONS> sample{};
                    for(unsigned r=0;r<REPLICATIONS;++r)
                        sample[r]=values[selected][bump][g][r];
                    const Stats st=stats(sample,reference_value[g]);
                    const double scale=shock_scale(g,c),bias=std::fabs(st.bias)*scale;
                    const double half=st.half*scale,rmse=st.rmse*scale;
                    const double fixed_error=std::fabs(fixed_value[g]-reference_value[g])*scale;
                    const double cap=ci<AUTOCALL_GREEK_CORE_CASES?1.0:3.0;
                    refs[selected][bump][g]=reference_value[g];
                    rmses[selected][bump][g]=rmse;
                    if(bump==1u&&(bias>cap||
                        (ci<AUTOCALL_GREEK_CORE_CASES&&half>.5)))
                        result.self[selected]=false;
                    if(fixed_error>cap||fixed_error>3.0*rmse)
                        result.self[selected]=false;
                    if(bump==1u){rmse_ss[selected]+=rmse*rmse;++rmse_n[selected];}
                    if(bump==1u) result.rmse_bp[selected].push_back(rmse);
                    if(verbose) std::printf("PATH_ACCURACY panel=%s kind=%s case=%s paths=%u "
                        "bump=%u greek=%s reference=%.17g mean=%.17g bias_bp=%.9g "
                        "rmse_bp=%.9g half_bp=%.9g median=%.17g worst=%.17g "
                        "fixed=%.17g fixed_error_bp=%.9g order12_fixed=%.17g "
                        "reference_error_bp=%.9g decision=%s\n",panel,kind,c.name,
                        paths,bump,(const char*[]){"DELTA","GAMMA","VEGA"}[g],
                        reference_value[g],st.mean,bias,rmse,half,st.median,st.worst,
                        fixed_value[g],fixed_error,control_value[g],reference_error,
                        result.self[selected]?"PASS":"FAIL");
                }
            }
        }
        for(unsigned selected:indices)for(unsigned g=0;g<3;++g)
          for(unsigned secondary: {0u,2u}) {
            std::array<double,REPLICATIONS> difference{};
            for(unsigned r=0;r<REPLICATIONS;++r)
                difference[r]=(values[selected][secondary][g][r]-
                    refs[selected][secondary][g])-
                    (values[selected][1][g][r]-refs[selected][1][g]);
            const Stats paired=stats(difference,0.0);
            const double scale=shock_scale(g,c);
            const double limit=ci<AUTOCALL_GREEK_CORE_CASES?1.0:3.0;
            const double three_se=3.0*paired.half/T31_975;
            const bool unstable=std::fabs(paired.mean)*scale>
                std::max(limit,three_se*scale)||
                (rmses[selected][secondary][g]>2.0*rmses[selected][1][g]&&
                 rmses[selected][secondary][g]-rmses[selected][1][g]>.25);
            if(unstable)result.self[selected]=false;
            if(verbose) std::printf("PATH_BUMP_STABILITY panel=%s case=%s paths=%u greek=%u "
                "secondary=%u paired_change_bp=%.9g three_se_bp=%.9g decision=%s\n",
                panel,c.name,COUNTS[selected],g,secondary,
                std::fabs(paired.mean)*scale,three_se*scale,unstable?"FAIL":"PASS");
          }
    }
    for(unsigned selected:indices)result.aggregate_rmse[selected]=
        std::sqrt(rmse_ss[selected]/std::max(1u,rmse_n[selected]));
    for(unsigned selected:indices)if(selected<6u&&result.self[selected]&&
        result.self[selected+1]&&result.aggregate_rmse[selected+1]<=
        1.10*result.aggregate_rmse[selected])result.global[selected]=true;
    for(unsigned selected:indices) {
        std::printf("PATH_COUNT_STATUS panel=%s paths=%u self=%s global=%s "
            "aggregate_rmse_bp=%.9g\n",panel,COUNTS[selected],
            result.self[selected]?"PASS":"FAIL",
            result.global[selected]?"YES":"NO",result.aggregate_rmse[selected]);
    }
    return result;
}
} // namespace d1_native_reference

#ifndef AUTOCALL_D1_NATIVE_REFERENCE_ENTRY
#define AUTOCALL_D1_NATIVE_REFERENCE_ENTRY main
#endif
int AUTOCALL_D1_NATIVE_REFERENCE_ENTRY(int argc,char **argv)
{
    using namespace d1_native_reference;
    if(argc==2&&!std::strcmp(argv[1],"--selection")) {
        const std::vector<unsigned> all{0,1,2,3,4,5,6};
        (void)sweep(SELECTION_SEED,all,"SELECTION");return 0;
    }
    if(argc==4&&!std::strcmp(argv[1],"--holdout")) {
        const int a=count_index((uint32_t)std::strtoul(argv[2],nullptr,10));
        const int b=count_index((uint32_t)std::strtoul(argv[3],nullptr,10));
        if(a<0||b!=a+1)return 2;
        const std::vector<unsigned> two{(unsigned)a,(unsigned)b};
        const SweepResult r=sweep(HOLDOUT_SEED,two,"HOLDOUT");
        return r.self[a]&&r.self[b]?0:3;
    }
    if(argc==2&&!std::strcmp(argv[1],"--manifest")) {
        const auto a=shift_bank(SELECTION_SEED),b=shift_bank(HOLDOUT_SEED);
        std::printf("SHIFT_BANKS selection=0x%016llx holdout=0x%016llx "
            "distinct=%s first_selection=0x%08x first_holdout=0x%08x\n",
            (unsigned long long)SELECTION_SEED,(unsigned long long)HOLDOUT_SEED,
            a!=b?"YES":"NO",a[0][0],b[0][0]);return a!=b?0:1;
    }
    std::fprintf(stderr,"usage: %s --selection | --holdout P NEXT | --manifest\n",argv[0]);
    return 2;
}
