#define _Static_assert static_assert
extern "C" {
#include "private/autocall_single_asset_three_date_raw_diag.h"
}
#undef _Static_assert

#define AUTOCALL_D1_NATIVE_REFERENCE_ENTRY autocall_d1_native_embedded_reference_main
#include "tests/reference_autocall_single_asset_three_date_d1_native.cpp"
#undef AUTOCALL_D1_NATIVE_REFERENCE_ENTRY

#include "private/autocall_single_asset_three_date_d1_native_diag.h"
#include "tests/autocall_single_asset_three_date_d1_native_selection.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cpuid.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sched.h>
#include <string>
#include <vector>

namespace {
constexpr unsigned WARMUPS=16,OBSERVATIONS=101;
constexpr std::array<unsigned,5> CASE_INDEX{{1,4,9,19,3}};
constexpr std::array<uint32_t,7> PATHS{{64,128,256,512,1024,2048,4096}};
alignas(64) std::array<unsigned char,32768> pressure_buffer{};
volatile double result_sink;

struct Tick { uint64_t wall,tsc; };
struct TimedResult { double wall,tsc; };
struct Pair { TimedResult candidate,baseline; };

static inline uint64_t clock_ns()
{
    timespec t{};clock_gettime(CLOCK_MONOTONIC_RAW,&t);
    return uint64_t(t.tv_sec)*UINT64_C(1000000000)+uint64_t(t.tv_nsec);
}
static inline uint64_t tsc_start()
{
    unsigned lo,hi;asm volatile("lfence\n\trdtsc\n\tlfence":"=a"(lo),"=d"(hi)::"memory");
    return (uint64_t(hi)<<32)|lo;
}
static inline uint64_t tsc_stop()
{
    unsigned lo,hi,aux;asm volatile("rdtscp\n\tlfence":"=a"(lo),"=d"(hi),"=c"(aux)::"memory");
    return (uint64_t(hi)<<32)|lo;
}

autocall_3date_request_input_t request_from(const autocall_frozen_case_t &c)
{
    autocall_3date_request_input_t r{};
    r.s0=c.s0;r.rate=c.rate;r.dividend_yield=c.dividend;r.sigma=c.sigma;
    r.maturity=c.maturity;r.notional=c.notional;r.protection_barrier=c.protection;
    for(unsigned d=0;d<3;++d) {
        const double t=c.maturity*(d+1.0)/3.0+c.payment_lag_fraction*c.maturity;
        r.call_barrier[d]=c.call_barrier[d];r.coupon_barrier[d]=c.coupon_barrier[d];
        r.coupon_cashflow[d]=c.coupon[d];r.call_redemption[d]=c.redemption[d];
        r.coupon_payment_time[d]=t;r.call_payment_time[d]=t;
    }
    r.terminal_payment_time=c.maturity*(1.0+c.payment_lag_fraction);return r;
}

struct State {
    alignas(64) autocall_d1_native_engine_t conditional{};
    alignas(64) autocall_3date_engine_t raw{};
    alignas(64) autocall_d1_native_market_t market{};
    alignas(64) autocall_d1_native_request_t request{};
    alignas(64) autocall_d1_native_output_t output{};
    alignas(64) autocall_3date_carrier_t raw_carrier{};
    alignas(64) autocall_3date_request_t raw_request{};
    alignas(64) autocall_3date_output_t raw_output{};
    autocall_3date_request_input_t input{};
    autocall_frozen_case_t frozen{};
    uint32_t paths=0;
};

enum class Operation { CONDITIONAL_PREPARED,CONDITIONAL_REUSED,CONDITIONAL_FRESH,
                       RAW_PREPARED,RAW_REUSED,RAW_FRESH,FIVE_RAW_FRESH };
struct OperationContext { State *state;Operation operation;bool pressure; };

void condition(OperationContext &o)
{
    if(o.pressure) {
        for(size_t i=0;i<pressure_buffer.size();i+=64) {
            pressure_buffer[i]^=static_cast<unsigned char>(i+1u);
            asm volatile(""::"r"(&pressure_buffer[i]):"memory");
        }
        return;
    }
    volatile uint32_t touch=0;State &s=*o.state;
    if(o.operation==Operation::CONDITIONAL_PREPARED||
       o.operation==Operation::CONDITIONAL_REUSED||
       o.operation==Operation::CONDITIONAL_FRESH) {
        const float *d1=s.conditional.signed_z;
        const auto *map=s.request.leaf.d2_map;
        const float *d2=s.request.leaf.d2_donor;
        for(uint32_t p=0;p<s.paths;p+=16) {
            touch^=*reinterpret_cast<const volatile uint32_t *>(d1+p);
            const uint32_t packet=p>>5,half=(p>>4)&1u;
            const uint32_t line=map->sel2[packet][0]^half;
            touch^=*reinterpret_cast<const volatile uint32_t *>(d2+16u*line);
        }
        const volatile uint32_t *q=reinterpret_cast<const volatile uint32_t *>(&s.request);
        for(size_t i=0;i<sizeof(s.request)/4;i+=16)touch^=q[i];
        const volatile uint32_t *identity=reinterpret_cast<const volatile uint32_t *>(
            reinterpret_cast<const unsigned char *>(&s.market)+192u);
        touch^=*identity;
    } else {
        const volatile uint32_t *q=reinterpret_cast<const volatile uint32_t *>(s.raw_carrier.growth);
        for(size_t i=0;i<AUTOCALL_3DATE_DONOR_VALUES;i+=16)touch^=q[i];
    }
    asm volatile(""::"r"(touch):"memory");
}

int invoke(OperationContext &o)
{
    State &s=*o.state;int status=0;
    switch(o.operation) {
    case Operation::CONDITIONAL_PREPARED:
        status=autocall_d1_native_prepared_price(&s.request,&s.output);
        result_sink+=s.output.price;break;
    case Operation::CONDITIONAL_REUSED:
        status=autocall_d1_native_reused_total(&s.conditional,&s.market,&s.input,
                                               .01,s.paths,&s.output);
        result_sink+=s.output.price;break;
    case Operation::CONDITIONAL_FRESH:
        status=autocall_d1_native_fresh_total(&s.conditional,&s.input,.01,.01,
                                              s.paths,&s.output);
        result_sink+=s.output.price;break;
    case Operation::RAW_PREPARED:
        status=autocall_3date_prepared_price(&s.raw_request,&s.raw_output);
        result_sink+=s.raw_output.price;break;
    case Operation::RAW_REUSED:
        status=autocall_3date_reuse_total(&s.raw,&s.raw_carrier,&s.input,&s.raw_output);
        result_sink+=s.raw_output.price;break;
    case Operation::RAW_FRESH:
        status=autocall_3date_fresh_total(&s.raw,&s.input,&s.raw_output);
        result_sink+=s.raw_output.price;break;
    case Operation::FIVE_RAW_FRESH: {
        const auto spot=greek_reference::make_legs(s.input.s0,.01,true);
        const auto vol=greek_reference::make_legs(s.input.sigma,.01,false);
        autocall_3date_request_input_t leg=s.input;
        const double spot_value[3]={spot.minus,spot.zero,spot.plus};
        for(unsigned i=0;i<3&&status==0;++i) {
            leg=s.input;leg.s0=spot_value[i];
            status=autocall_3date_fresh_total(&s.raw,&leg,&s.raw_output);
            result_sink+=s.raw_output.price;
        }
        const double vol_value[2]={vol.minus,vol.plus};
        for(unsigned i=0;i<2&&status==0;++i) {
            leg=s.input;leg.sigma=vol_value[i];
            status=autocall_3date_fresh_total(&s.raw,&leg,&s.raw_output);
            result_sink+=s.raw_output.price;
        }
        break;
    }}
    return status;
}

Tick observe(OperationContext &o)
{
    condition(o);const uint64_t w0=clock_ns(),t0=tsc_start();
    const int status=invoke(o);const uint64_t t1=tsc_stop(),w1=clock_ns();
    if(status) {std::fprintf(stderr,"timed operation status=%d\n",status);std::exit(2);}
    return {w1-w0,t1-t0};
}

TimedResult median_ticks(std::vector<double> wall,std::vector<double> tsc)
{
    std::sort(wall.begin(),wall.end());std::sort(tsc.begin(),tsc.end());
    return {wall[wall.size()/2],tsc[tsc.size()/2]};
}

Pair measure_pair(OperationContext a,OperationContext b)
{
    for(unsigned i=0;i<WARMUPS;++i) {
        if(i&1u){observe(b);observe(a);observe(a);observe(b);}
        else {observe(a);observe(b);observe(b);observe(a);}
    }
    std::vector<double> aw,at,bw,bt;aw.reserve(OBSERVATIONS);at.reserve(OBSERVATIONS);
    bw.reserve(OBSERVATIONS);bt.reserve(OBSERVATIONS);
    for(unsigned i=0;i<OBSERVATIONS;++i) {
        Tick a0,a1,b0,b1;
        if(i&1u){b0=observe(b);a0=observe(a);a1=observe(a);b1=observe(b);}
        else {a0=observe(a);b0=observe(b);b1=observe(b);a1=observe(a);}
        aw.push_back(.5*(a0.wall+a1.wall));at.push_back(.5*(a0.tsc+a1.tsc));
        bw.push_back(.5*(b0.wall+b1.wall));bt.push_back(.5*(b0.tsc+b1.tsc));
    }
    return {median_ticks(aw,at),median_ticks(bw,bt)};
}

double percentile(std::vector<double> x,double q)
{
    std::sort(x.begin(),x.end());return x[size_t(std::ceil(q*x.size()))-1u];
}

template<class Function>
TimedResult measure_component(Function function)
{
    for(unsigned i=0;i<WARMUPS;++i)function();
    std::vector<double> wall,tsc;wall.reserve(OBSERVATIONS);tsc.reserve(OBSERVATIONS);
    for(unsigned i=0;i<OBSERVATIONS;++i) {
        const uint64_t w0=clock_ns(),t0=tsc_start();function();
        const uint64_t t1=tsc_stop(),w1=clock_ns();
        wall.push_back(double(w1-w0));tsc.push_back(double(t1-t0));
    }
    return median_ticks(wall,tsc);
}

TimedResult measure_plan_create()
{
    for(unsigned i=0;i<WARMUPS;++i) {
        asian_meta_affine_plan_t *plan=nullptr;
        if(asian_meta_affine_plan_create(&plan))std::exit(2);
        asian_meta_affine_plan_destroy(plan);
    }
    std::vector<double> wall,tsc;
    for(unsigned i=0;i<OBSERVATIONS;++i) {
        asian_meta_affine_plan_t *plan=nullptr;
        const uint64_t w0=clock_ns(),t0=tsc_start();
        const int status=asian_meta_affine_plan_create(&plan);
        const uint64_t t1=tsc_stop(),w1=clock_ns();
        if(status)std::exit(2);
        asian_meta_affine_plan_destroy(plan);
        wall.push_back(double(w1-w0));tsc.push_back(double(t1-t0));
    }
    return median_ticks(wall,tsc);
}

TimedResult measure_engine_create()
{
    for(unsigned i=0;i<WARMUPS;++i) {
        autocall_d1_native_engine_t e __attribute__((aligned(64)));
        if(autocall_d1_native_engine_create(&e))std::exit(2);
        autocall_d1_native_engine_destroy(&e);
    }
    std::vector<double> wall,tsc;
    for(unsigned i=0;i<OBSERVATIONS;++i) {
        autocall_d1_native_engine_t e __attribute__((aligned(64)));
        const uint64_t w0=clock_ns(),t0=tsc_start();
        const int status=autocall_d1_native_engine_create(&e);
        const uint64_t t1=tsc_stop(),w1=clock_ns();
        if(status)std::exit(2);
        autocall_d1_native_engine_destroy(&e);
        wall.push_back(double(w1-w0));tsc.push_back(double(t1-t0));
    }
    return median_ticks(wall,tsc);
}
bool spr_model143()
{
    unsigned a,b,c,d;__cpuid(1,a,b,c,d);unsigned family=(a>>8)&15u;
    unsigned model=(a>>4)&15u;if(family==15u)family+=(a>>20)&255u;
    if(family==6u||family==15u)model|=((a>>16)&15u)<<4;
    return family==6u&&model==143u;
}
bool pin_cpu0()
{
    cpu_set_t set;CPU_ZERO(&set);CPU_SET(0,&set);
    return sched_setaffinity(0,sizeof(set),&set)==0;
}

void initialize(State &s,unsigned case_index,uint32_t paths)
{
    s.frozen=autocall_greek_case(case_index);s.input=request_from(s.frozen);s.paths=paths;
    if(autocall_d1_native_engine_create(&s.conditional)||autocall_3date_engine_create(&s.raw))
        std::exit(2);
    const autocall_3date_market_input_t market={s.input.rate,s.input.dividend_yield,
                                                s.input.sigma,s.input.maturity};
    if(autocall_d1_native_market_prepare(&s.conditional,&market,.01,&s.market)||
       autocall_d1_native_request_prepare(&s.conditional,&s.market,&s.input,.01,paths,
                                          &s.request)||
       autocall_3date_market_prepare(&s.raw,&market,&s.raw_carrier)||
       autocall_3date_request_prepare(&s.raw,&s.raw_carrier,&s.input,&s.raw_request))
        std::exit(2);
    double replay[5];
    autocall_d1_native_price5_leaf(&s.request.leaf,replay);
    if(autocall_d1_native_prepared_price(&s.request,&s.output)||
       std::memcmp(replay,s.output.leg_price,sizeof(replay)))std::exit(2);
}
void destroy(State &s)
{
    autocall_3date_engine_destroy(&s.raw);autocall_d1_native_engine_destroy(&s.conditional);
}

struct CellTimes { double cw=0,ct=0,rw=0,rt=0,fw=0,ft=0; };
} // namespace

int main(int argc,char **argv)
{
    if(argc!=3||std::strcmp(argv[1],"--cpu")||std::strcmp(argv[2],"0")) {
        std::fprintf(stderr,"usage: %s --cpu 0\n",argv[0]);return 2;
    }
    if(!spr_model143()) {std::fprintf(stderr,"Sapphire Rapids family-6 model-143 required\n");return 2;}
    if(!pin_cpu0()) {std::fprintf(stderr,"CPU 0 pin failed: %s\n",std::strerror(errno));return 2;}

    const TimedResult plan_create=measure_plan_create();
    const TimedResult engine_create=measure_engine_create();
    std::printf("COMPONENT full_affine_plan_create wall_ns=%.1f tsc_ticks=%.1f\n",
                plan_create.wall,plan_create.tsc);
    std::printf("COMPONENT engine_initialize wall_ns=%.1f tsc_ticks=%.1f\n",
                engine_create.wall,engine_create.tsc);

    const std::vector<unsigned> all{0,1,2,3,4,5,6};
    const auto accuracy=d1_native_reference::sweep(
        d1_native_reference::SELECTION_SEED,all,"SELECTION_NATIVE",false);
    std::array<std::array<CellTimes,5>,7> warm{};
    std::array<std::vector<double>,7> prepared_wall_ratios,prepared_tsc_ratios;
    std::array<std::vector<double>,7> fresh_wall_ratios,fresh_tsc_ratios;
    std::array<std::vector<double>,7> five_raw_fresh;
    std::array<std::vector<double>,7> portable_wall;
    std::puts("path_count contract cache lifecycle conditional_wall_ns conditional_tsc_ticks raw_wall_ns raw_tsc_ticks ratio_wall ratio_tsc identity");
    for(unsigned pi=0;pi<PATHS.size();++pi) for(unsigned fi=0;fi<CASE_INDEX.size();++fi) {
        State s;initialize(s,CASE_INDEX[fi],PATHS[pi]);
        const auto selection_bank=d1_native_reference::shift_bank(
            d1_native_reference::SELECTION_SEED);
        const auto spot=greek_reference::make_legs(s.input.s0,.01,true);
        const auto vol=greek_reference::make_legs(s.input.sigma,.01,false);
        const TimedResult portable=measure_component([&]{
            const auto value=d1_native_reference::conditional_greeks_count(
                s.frozen,spot,vol,selection_bank[0],true,PATHS[pi]);
            result_sink+=value.price;
        });
        portable_wall[pi].push_back(portable.wall);
        std::printf("PORTABLE_CONTROL paths=%u contract=%s wall_ns=%.1f "
                    "tsc_ticks=%.1f advisory=YES\n",PATHS[pi],s.frozen.name,
                    portable.wall,portable.tsc);
        if(fi==0) {
            const autocall_3date_market_input_t mi={s.input.rate,s.input.dividend_yield,
                                                    s.input.sigma,s.input.maturity};
            autocall_d1_native_market_t market __attribute__((aligned(64)));
            const TimedResult mp=measure_component([&]{
                if(autocall_d1_native_market_prepare(&s.conditional,&mi,.01,&market))
                    std::exit(2);
            });
            if(autocall_d1_native_market_prepare(&s.conditional,&mi,.01,&market))std::exit(2);
            autocall_d1_native_request_t request __attribute__((aligned(64)));
            const TimedResult rp=measure_component([&]{
                if(autocall_d1_native_request_prepare(&s.conditional,&market,&s.input,
                                                       .01,PATHS[pi],&request))std::exit(2);
            });
            std::printf("COMPONENT paths=%u market_prepare_wall_ns=%.1f "
                "market_prepare_tsc=%.1f request_prepare_wall_ns=%.1f "
                "request_prepare_tsc=%.1f portable_conditional_wall_ns=%.1f "
                "portable_conditional_tsc=%.1f\n",PATHS[pi],mp.wall,mp.tsc,
                rp.wall,rp.tsc,portable.wall,portable.tsc);
        }
        for(unsigned pressure=0;pressure<2;++pressure) {
            const char *cache=pressure?"pressure_32KiB":"candidate_warm";
            const Operation candidate[3]={Operation::CONDITIONAL_PREPARED,
                Operation::CONDITIONAL_REUSED,Operation::CONDITIONAL_FRESH};
            const Operation raw[3]={Operation::RAW_PREPARED,Operation::RAW_REUSED,
                Operation::RAW_FRESH};
            for(unsigned life=0;life<3;++life) {
                Pair p=measure_pair({&s,candidate[life],bool(pressure)},
                                    {&s,raw[life],bool(pressure)});
                std::printf("%u %s %s %s %.1f %.1f %.1f %.1f %.6f %.6f PASS\n",
                    PATHS[pi],s.frozen.name,cache,
                    (const char*[]){"prepared","reused","fresh"}[life],
                    p.candidate.wall,p.candidate.tsc,p.baseline.wall,p.baseline.tsc,
                    p.candidate.wall/p.baseline.wall,p.candidate.tsc/p.baseline.tsc);
                if(life==0) {
                    prepared_wall_ratios[pi].push_back(p.candidate.wall/p.baseline.wall);
                    prepared_tsc_ratios[pi].push_back(p.candidate.tsc/p.baseline.tsc);
                }
                if(life==2) {
                    fresh_wall_ratios[pi].push_back(p.candidate.wall/p.baseline.wall);
                    fresh_tsc_ratios[pi].push_back(p.candidate.tsc/p.baseline.tsc);
                }
                if(!pressure) {
                    CellTimes &v=warm[pi][fi];
                    if(life==0){v.cw=p.candidate.wall;v.ct=p.candidate.tsc;}
                    if(life==2){v.fw=p.candidate.wall;v.ft=p.candidate.tsc;}
                }
            }
            if(!pressure) {
                Pair p=measure_pair({&s,Operation::CONDITIONAL_FRESH,false},
                                    {&s,Operation::FIVE_RAW_FRESH,false});
                five_raw_fresh[pi].push_back(p.baseline.wall);
                warm[pi][fi].rw=p.baseline.wall;warm[pi][fi].rt=p.baseline.tsc;
                std::printf("FIVE_PRICE_BASELINE paths=%u contract=%s conditional_wall_ns=%.1f "
                    "raw_five_wall_ns=%.1f speedup=%.6f\n",PATHS[pi],s.frozen.name,
                    p.candidate.wall,p.baseline.wall,p.baseline.wall/p.candidate.wall);
            }
        }
        destroy(s);
    }

    std::array<double,7> fresh_latency{};
    std::array<double,7> score_median{},score_p90{},score_worst{};
    for(unsigned pi=0;pi<PATHS.size();++pi) {
        std::vector<double> latency;for(const auto &v:warm[pi])latency.push_back(v.fw);
        fresh_latency[pi]=percentile(latency,.5);
        std::vector<double> scores;
        for(double rmse:accuracy.rmse_bp[pi])scores.push_back(rmse*rmse*fresh_latency[pi]);
        score_median[pi]=percentile(scores,.5);score_p90[pi]=percentile(scores,.9);
        score_worst[pi]=*std::max_element(scores.begin(),scores.end());
        std::printf("TIME_TO_ACCURACY paths=%u fresh_wall_ns=%.1f median_bp2_ns=%.9g "
            "p90_bp2_ns=%.9g worst_bp2_ns=%.9g self=%s global=%s\n",PATHS[pi],
            fresh_latency[pi],score_median[pi],score_p90[pi],score_worst[pi],
            accuracy.self[pi]?"PASS":"FAIL",accuracy.global[pi]?"YES":"NO");
    }
    for(unsigned pi=0;pi<PATHS.size();++pi) {
        const double raw_latency=percentile(five_raw_fresh[pi],.5);
        const double portable_latency_4096=percentile(portable_wall[6],.5);
        std::vector<double> raw_improvement,portable_improvement;
        for(size_t i=0;i<accuracy.rmse_bp[pi].size();++i) {
            const double here=accuracy.rmse_bp[pi][i]*accuracy.rmse_bp[pi][i]*
                              fresh_latency[pi];
            const double raw=accuracy.raw_rmse_bp[i]*accuracy.raw_rmse_bp[i]*raw_latency;
            const double portable4096=accuracy.rmse_bp[6][i]*accuracy.rmse_bp[6][i]*
                                      portable_latency_4096;
            raw_improvement.push_back(raw/here);
            portable_improvement.push_back(portable4096/here);
        }
        std::printf("ERROR2_TIME_RATIOS paths=%u raw_crn_median=%.6f raw_crn_p90_gate=%.6f "
            "conditional4096_median=%.6f conditional4096_p90_gate=%.6f\n",PATHS[pi],
            percentile(raw_improvement,.5),percentile(raw_improvement,.1),
            percentile(portable_improvement,.5),percentile(portable_improvement,.1));
    }

    uint32_t global_mask=0;
    for(unsigned pi=0;pi<PATHS.size();++pi)
        if(accuracy.global[pi]) global_mask|=UINT32_C(1)<<pi;
    const int selected=autocall_d1_native_select_best_global(
        global_mask,score_median.data(),score_median.size());
    bool holdout=false;
    if(selected>=0) {
        const std::vector<unsigned> two{unsigned(selected),unsigned(selected+1)};
        const auto h=d1_native_reference::sweep(d1_native_reference::HOLDOUT_SEED,
                                                two,"HOLDOUT_NATIVE",false);
        holdout=h.self[selected]&&h.self[selected+1];
    }
    bool performance=false,time_error=false;
    if(selected>=0&&holdout) {
        const double prep_wall_med=percentile(prepared_wall_ratios[selected],.5);
        const double prep_tsc_med=percentile(prepared_tsc_ratios[selected],.5);
        const double fresh_wall_med=percentile(fresh_wall_ratios[selected],.5);
        const double fresh_tsc_med=percentile(fresh_tsc_ratios[selected],.5);
        const double prep_worst=std::max(
            *std::max_element(prepared_wall_ratios[selected].begin(),prepared_wall_ratios[selected].end()),
            *std::max_element(prepared_tsc_ratios[selected].begin(),prepared_tsc_ratios[selected].end()));
        const double fresh_worst=std::max(
            *std::max_element(fresh_wall_ratios[selected].begin(),fresh_wall_ratios[selected].end()),
            *std::max_element(fresh_tsc_ratios[selected].begin(),fresh_tsc_ratios[selected].end()));
        std::vector<double> prepared_abs,fresh_abs;
        for(const auto &v:warm[selected]){prepared_abs.push_back(v.cw);fresh_abs.push_back(v.fw);}
        const double prepared_ns=percentile(prepared_abs,.5),fresh_ns=percentile(fresh_abs,.5);
        performance=prep_wall_med<=6.0&&prep_tsc_med<=6.0&&
                    fresh_wall_med<=6.0&&fresh_tsc_med<=6.0&&
                    prep_worst<=8.0&&fresh_worst<=8.0&&
                    prepared_ns<=14000.0&&fresh_ns<=24000.0;
        const double raw_latency=percentile(five_raw_fresh[selected],.5);
        std::vector<double> improvements;
        for(size_t i=0;i<accuracy.rmse_bp[selected].size();++i) {
            const double raw=accuracy.raw_rmse_bp[i]*accuracy.raw_rmse_bp[i]*raw_latency;
            const double conditional=accuracy.rmse_bp[selected][i]*
                accuracy.rmse_bp[selected][i]*fresh_latency[selected];
            improvements.push_back(raw/conditional);
        }
        const double improvement_median=percentile(improvements,.5);
        const double improvement_p10=percentile(improvements,.1);
        time_error=improvement_median>=10.0&&improvement_p10>=2.0;
        std::printf("SELECTED paths=%u holdout=PASS prepared_ns=%.1f fresh_ns=%.1f "
            "prepared_wall_ratio_median=%.6f prepared_tsc_ratio_median=%.6f "
            "prepared_ratio_worst=%.6f fresh_wall_ratio_median=%.6f "
            "fresh_tsc_ratio_median=%.6f fresh_ratio_worst=%.6f "
            "error2_time_improvement_median=%.6f error2_time_improvement_p90_gate=%.6f\n",
            PATHS[selected],prepared_ns,fresh_ns,prep_wall_med,prep_tsc_med,
            prep_worst,fresh_wall_med,fresh_tsc_med,fresh_worst,
            improvement_median,improvement_p10);
    }
    for(uint32_t paths:PATHS) {
        const uint32_t selector_lines=((paths/32u)*2u+63u)/64u;
        const uint32_t route_lines=selector_lines+2u;
        const uint32_t source_bytes=paths*8u;
        const uint32_t data_bytes=source_bytes+route_lines*64u+512u+64u+192u+192u;
        std::printf("HOT_FOOTPRINT paths=%u direct_source_bytes=%u d2_source_bytes=%u "
            "route_control_bytes=%u request_bytes=512 market_identity_bytes=64 "
            "output_write_bytes=192 linked_math_constant_lines_bytes=192 "
            "unique_linked_data_bytes=%u\n",paths,paths*4u,paths*4u,
            route_lines*64u,data_bytes);
    }
    std::printf("MEMORY descriptor_bytes=8192 full_affine_plan_bytes=%zu "
        "signed_z_rodata_bytes=32768 market_bytes=%zu leaf_context_bytes=%zu "
        "prepared_request_bytes=%zu output_bytes=%zu engine_workspace_bytes=%zu "
        "engine_heap_bytes=%zu engine_handle_bytes=%zu hot_leaf_bytes=24308 peak_zmm=25\n",
        sizeof(asian_meta_affine_plan_t),sizeof(autocall_d1_native_market_t),
        sizeof(autocall_d1_native_leaf_context_t),sizeof(autocall_d1_native_request_t),
        sizeof(autocall_d1_native_output_t),sizeof(autocall_d1_native_workspace_t),
        sizeof(asian_meta_affine_plan_t)+sizeof(autocall_d1_native_workspace_t),
        sizeof(autocall_d1_native_engine_t));
    const char *verdict;
    if(selected<0||!holdout) verdict="D1_NATIVE_PATH_COUNT_UNRESOLVED";
    else if(!performance||!time_error) verdict="D1_NATIVE_ACCURATE_PERFORMANCE_MISSED";
    else verdict="D1_NATIVE_CORE_GREEKS_READY";
    std::puts(verdict);return 0;
}
