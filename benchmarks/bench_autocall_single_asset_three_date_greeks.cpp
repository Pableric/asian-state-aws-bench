#define AUTOCALL_GREEK_REFERENCE_ENTRY autocall_greek_reference_embedded_main
#include "tests/reference_autocall_single_asset_three_date_greeks.cpp"
#undef AUTOCALL_GREEK_REFERENCE_ENTRY

#include <cpuid.h>
#include <sched.h>
#include <time.h>

namespace {

constexpr unsigned OBSERVATIONS=101,WARMUPS=16;
volatile double benchmark_sink;

struct Sample { uint64_t wall,tsc; };

uint64_t now_ns()
{
    timespec ts{};clock_gettime(CLOCK_MONOTONIC_RAW,&ts);
    return static_cast<uint64_t>(ts.tv_sec)*UINT64_C(1000000000)+ts.tv_nsec;
}

uint64_t tsc_begin()
{
    unsigned lo,hi;
    asm volatile("lfence\n\trdtsc":"=a"(lo),"=d"(hi)::"memory");
    return (static_cast<uint64_t>(hi)<<32)|lo;
}

uint64_t tsc_end()
{
    unsigned lo,hi,aux;
    asm volatile("rdtscp\n\tlfence":"=a"(lo),"=d"(hi),"=c"(aux)::"memory");
    return (static_cast<uint64_t>(hi)<<32)|lo;
}

bool sapphire_rapids()
{
    unsigned a,b,c,d;if(!__get_cpuid(1,&a,&b,&c,&d))return false;
    const unsigned family=((a>>8)&15u)+((((a>>8)&15u)==15u)?((a>>20)&255u):0u);
    const unsigned model=((a>>4)&15u)|(((a>>16)&15u)<<4);
    return family==6u&&model==143u;
}

bool pin_zero()
{
    cpu_set_t set;CPU_ZERO(&set);CPU_SET(0,&set);
    return sched_setaffinity(0,sizeof(set),&set)==0;
}

uint64_t median(std::array<uint64_t,OBSERVATIONS> values)
{
    std::sort(values.begin(),values.end());return values[OBSERVATIONS/2];
}

enum class Candidate { Raw,Conditional12,Conditional21 };

greek_reference::GreekValues operation(const autocall_frozen_case_t &c,
    const greek_reference::Legs &spot,const greek_reference::Legs &vol,
    const std::array<uint32_t,3> &shift,Candidate candidate)
{
    using namespace greek_reference;
    if(candidate==Candidate::Raw)return qmc_greeks(c,spot,vol,shift,Mode::Binary32);
    return conditional_greeks(c,spot,vol,shift,candidate==Candidate::Conditional21);
}

Sample observe(const autocall_frozen_case_t &c,const greek_reference::Legs &spot,
    const greek_reference::Legs &vol,const std::array<uint32_t,3> &shift,
    Candidate candidate,unsigned char *pressure,bool pressured)
{
    if(pressured)for(unsigned i=0;i<32768u;i+=64u)pressure[i]++;
    const uint64_t w0=now_ns(),t0=tsc_begin();
    const auto out=operation(c,spot,vol,shift,candidate);
    const uint64_t t1=tsc_end(),w1=now_ns();
    benchmark_sink+=out.price+out.delta+out.gamma+out.vega;
    return {w1-w0,t1-t0};
}

struct Timing { uint64_t wall,tsc; };

Timing paired(const autocall_frozen_case_t &c,const greek_reference::Legs &spot,
    const greek_reference::Legs &vol,Candidate a,Candidate b,
    unsigned char *pressure,bool pressured,bool return_a)
{
    const auto shifts=greek_reference::shifts();
    for(unsigned warm=0;warm<WARMUPS;++warm) {
        const unsigned r=warm%greek_reference::REPLICATIONS;
        (void)observe(c,spot,vol,shifts[r],a,pressure,pressured);
        (void)observe(c,spot,vol,shifts[r],b,pressure,pressured);
        (void)observe(c,spot,vol,shifts[r],b,pressure,pressured);
        (void)observe(c,spot,vol,shifts[r],a,pressure,pressured);
    }
    std::array<uint64_t,OBSERVATIONS> aw{},at{},bw{},bt{};
    for(unsigned i=0;i<OBSERVATIONS;++i) {
        const unsigned r=i%greek_reference::REPLICATIONS;
        Sample s[4];
        if((i&1u)==0u) {
            s[0]=observe(c,spot,vol,shifts[r],a,pressure,pressured);
            s[1]=observe(c,spot,vol,shifts[r],b,pressure,pressured);
            s[2]=observe(c,spot,vol,shifts[r],b,pressure,pressured);
            s[3]=observe(c,spot,vol,shifts[r],a,pressure,pressured);
            aw[i]=(s[0].wall+s[3].wall)/2;at[i]=(s[0].tsc+s[3].tsc)/2;
            bw[i]=(s[1].wall+s[2].wall)/2;bt[i]=(s[1].tsc+s[2].tsc)/2;
        } else {
            s[0]=observe(c,spot,vol,shifts[r],b,pressure,pressured);
            s[1]=observe(c,spot,vol,shifts[r],a,pressure,pressured);
            s[2]=observe(c,spot,vol,shifts[r],a,pressure,pressured);
            s[3]=observe(c,spot,vol,shifts[r],b,pressure,pressured);
            bw[i]=(s[0].wall+s[3].wall)/2;bt[i]=(s[0].tsc+s[3].tsc)/2;
            aw[i]=(s[1].wall+s[2].wall)/2;at[i]=(s[1].tsc+s[2].tsc)/2;
        }
    }
    return return_a?Timing{median(aw),median(at)}:Timing{median(bw),median(bt)};
}

double variance(const std::array<double,greek_reference::REPLICATIONS> &v)
{
    const double mean=std::accumulate(v.begin(),v.end(),0.0)/v.size();
    double total=0;for(double x:v)total+=(x-mean)*(x-mean);
    return total/(v.size()-1u);
}

} // namespace

int main(int argc,char **argv)
{
    if(argc!=3||std::strcmp(argv[1],"--cpu")||std::strcmp(argv[2],"0")||
       !sapphire_rapids()||!pin_zero()) {
        std::fprintf(stderr,"Sapphire Rapids model 143 CPU 0 required\n");return 2;
    }
    using namespace greek_reference;
    unsigned char *pressure=nullptr;
    if(posix_memalign(reinterpret_cast<void **>(&pressure),64,32768u)!=0)return 1;
    std::memset(pressure,1,32768u);
    const unsigned cases[4]={1u,4u,AUTOCALL_GREEK_CORE_CASES+2u,
                             AUTOCALL_GREEK_CORE_CASES+12u};
    const auto shiftbank=shifts();
    std::array<double,24> vr12{},vr21{};unsigned cell=0;
    std::array<double,4> time_ratio12{},time_ratio21{};
    bool accuracy=true;
    std::puts("fallback_case cache raw_wall_ns raw_tsc conditional12_wall_ns "
              "conditional12_tsc conditional21_wall_ns conditional21_tsc");
    for(unsigned case_ordinal=0;case_ordinal<4u;++case_ordinal) {
        const unsigned case_index=cases[case_ordinal];
        const autocall_frozen_case_t c=autocall_greek_case(case_index);
        const Legs spot=make_legs(c.s0,.01,true),vol=make_legs(c.sigma,.01,false);
        double dd,dg,dv,error_bp;
        const GreekValues ref=oracle_greeks(c,spot,vol,&dd,&dg,&dv,&error_bp);
        std::array<std::array<double,REPLICATIONS>,3> raw{},c12{},c21{};
        for(unsigned r=0;r<REPLICATIONS;++r) {
            const auto a=qmc_greeks(c,spot,vol,shiftbank[r],Mode::Binary32);
            const auto b=conditional_greeks(c,spot,vol,shiftbank[r],false);
            const auto d=conditional_greeks(c,spot,vol,shiftbank[r],true);
            const double av[3]={a.delta,a.gamma,a.vega};
            const double bv[3]={b.delta,b.gamma,b.vega};
            const double dvv[3]={d.delta,d.gamma,d.vega};
            for(unsigned g=0;g<3;++g){raw[g][r]=av[g];c12[g][r]=bv[g];c21[g][r]=dvv[g];}
        }
        const double refs[3]={ref.delta,ref.gamma,ref.vega};
        for(unsigned g=0;g<3;++g) {
            const Stats a=stats(c12[g],refs[g]),b=stats(c21[g],refs[g]);
            const double limit=case_index<AUTOCALL_GREEK_CORE_CASES?1.0:3.0;
            if(std::min(std::fabs(a.bias),std::fabs(b.bias))*shock_scale(g,c)>limit)
                accuracy=false;
            vr12[cell]=variance(c12[g])/variance(raw[g]);
            vr21[cell]=variance(c21[g])/variance(raw[g]);
            ++cell;
        }
        for(unsigned mode=0;mode<2;++mode) {
            const bool pressured=mode!=0;
            const Timing raw12=paired(c,spot,vol,Candidate::Raw,
                Candidate::Conditional12,pressure,pressured,true);
            const Timing time12=paired(c,spot,vol,Candidate::Raw,
                Candidate::Conditional12,pressure,pressured,false);
            const Timing raw21=paired(c,spot,vol,Candidate::Raw,
                Candidate::Conditional21,pressure,pressured,true);
            const Timing time21=paired(c,spot,vol,Candidate::Raw,
                Candidate::Conditional21,pressure,pressured,false);
            std::printf("%s %s %llu %llu %llu %llu %llu %llu\n",c.name,
                pressured?"pressure_32KiB":"candidate_warm",
                (unsigned long long)((raw12.wall+raw21.wall)/2),
                (unsigned long long)((raw12.tsc+raw21.tsc)/2),
                (unsigned long long)time12.wall,(unsigned long long)time12.tsc,
                (unsigned long long)time21.wall,(unsigned long long)time21.tsc);
            if(!pressured) {
                time_ratio12[case_ordinal]=std::max(
                    (double)time12.wall/(double)raw12.wall,
                    (double)time12.tsc/(double)raw12.tsc);
                time_ratio21[case_ordinal]=std::max(
                    (double)time21.wall/(double)raw21.wall,
                    (double)time21.tsc/(double)raw21.tsc);
            }
        }
    }
    std::array<double,24> vt12{},vt21{};
    for(unsigned i=0;i<cell;++i) {
        vt12[i]=vr12[i]*time_ratio12[i/3u];
        vt21[i]=vr21[i]*time_ratio21[i/3u];
    }
    std::sort(vr12.begin(),vr12.begin()+cell);std::sort(vr21.begin(),vr21.begin()+cell);
    std::sort(vt12.begin(),vt12.begin()+cell);std::sort(vt21.begin(),vt21.begin()+cell);
    const double median_var12=vr12[cell/2],median_var21=vr21[cell/2];
    const double p90_var12=vr12[(9u*cell)/10u],p90_var21=vr21[(9u*cell)/10u];
    const double median_vt12=vt12[cell/2],median_vt21=vt21[cell/2];
    const double p90_vt12=vt12[(9u*cell)/10u],p90_vt21=vt21[(9u*cell)/10u];
    std::printf("fallback_variance order12_median=%.9g order12_p90=%.9g "
                "order21_median=%.9g order21_p90=%.9g accuracy=%s\n",
                median_var12,p90_var12,median_var21,p90_var21,
                accuracy?"PASS":"FAIL");
    std::printf("fallback_variance_time order12_median=%.9g order12_p90=%.9g "
                "order21_median=%.9g order21_p90=%.9g\n",
                median_vt12,p90_vt12,median_vt21,p90_vt21);
    const bool order12=median_vt12<=0.50&&p90_vt12<=1.25;
    const bool order21=median_vt21<=0.50&&p90_vt21<=1.25;
    std::puts(accuracy&&(order12||order21)?"D1_PREINTEGRATION_NEXT":
              "AUTOCALL_GREEK_ESTIMATOR_RESEARCH_REQUIRED");
    free(pressure);return accuracy?0:1;
}
