#define main mvn_rejected_phi46_probe_main
#include "tests/probe_autocall_two_asset_three_date_mvn_phi46.cpp"
#undef main

#ifdef MVN_COEFFSCHED_BENCHMARK
#include <array>
#include <atomic>
#include <cerrno>
#include <cpuid.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
#endif

extern "C" void mvn_phi4_batch8_tensor40_coeffsched_asm(
    const mvn_f64_math::Phi46Batch8Input *,double *);
extern "C" void mvn_phi6_batch8_bridge40_coeffsched_asm(
    const mvn_f64_math::Phi46Batch8Input *,double *);

namespace {

static bool run_coeffsched_dimension(unsigned dimension) {
    Phi46Batch8Input input{};
    if (!prepare_batch(dimension,input)) return false;
    alignas(64) double rejected[8]{};
    alignas(64) double output[8]{};
    if (dimension==4u) {
        mvn_f64_math::mvn_phi4_batch8_tensor40_candidate(&input,rejected);
        mvn_phi4_batch8_tensor40_coeffsched_asm(&input,output);
    } else {
        mvn_f64_math::mvn_phi6_batch8_bridge40_candidate(&input,rejected);
        mvn_phi6_batch8_bridge40_coeffsched_asm(&input,output);
    }
    size_t count=0u;
    const mvn_component_case_t *cases=mvn_selection_component_cases(
        dimension,&count);
    unsigned scalar_mismatch=0u;
    unsigned rejected_mismatch=0u;
    long double max_reference_error=0.0L;
    for (unsigned lane=0;lane<8u;++lane) {
        const double scalar=tensor_scalar(input,lane,dimension==6u);
        scalar_mismatch+=std::bit_cast<std::uint64_t>(scalar)!=
                         std::bit_cast<std::uint64_t>(output[lane]);
        rejected_mismatch+=std::bit_cast<std::uint64_t>(rejected[lane])!=
                           std::bit_cast<std::uint64_t>(output[lane]);
        mvn_reference_math::Rectangle rectangle{};
        rectangle.dimension=dimension;
        for (unsigned i=0;i<6u;++i) {
            rectangle.lower[i]=cases[lane].lower[i];
            rectangle.upper[i]=cases[lane].upper[i];
        }
        for (unsigned i=0;i<36u;++i)
            rectangle.covariance[i]=cases[lane].correlation[i];
        const long double truth=mvn_reference_math::evaluate_fixed(
            rectangle,48u,false);
        max_reference_error=std::max(max_reference_error,
            std::fabs(static_cast<long double>(output[lane])-truth));
    }
    const long double ceiling=dimension==4u ?
        5.58770695e-17L : 2.21408314e-15L;
    const bool pass=scalar_mismatch==0u && rejected_mismatch==0u &&
                    max_reference_error<=ceiling;
    std::printf("COEFFICIENT_SCHEDULING_IDENTITY dimension=%u cases=8 "
                "order=40 scalar_native_bit_mismatches=%u "
                "rejected_native_bit_mismatches=%u "
                "max_reference_error=%.17Lg ceiling=%.17Lg status=%s\n",
                dimension,scalar_mismatch,rejected_mismatch,
                max_reference_error,ceiling,pass?"PASS":"FAIL");
    return pass;
}

} // namespace

#ifndef MVN_COEFFSCHED_BENCHMARK
int main() {
    const bool phi4=run_coeffsched_dimension(4u);
    const bool phi6=run_coeffsched_dimension(6u);
    std::printf("COEFFICIENT_SCHEDULING_NUMERIC phi4=%s phi6=%s status=%s\n",
                phi4?"PASS":"FAIL",phi6?"PASS":"FAIL",
                phi4&&phi6?"PASS":"FAIL");
    return phi4&&phi6 ? 0 : 1;
}
#else
namespace {

constexpr unsigned WARMUP_GROUPS=16u;
constexpr unsigned MEASURED_GROUPS=101u;
constexpr unsigned LANES=8u;
constexpr unsigned MAX_BATCHES=17u;

using Kernel=void (*)(const Phi46Batch8Input *,double *);

struct Variant {
    Kernel phi4;
    Kernel phi6;
};

struct Workload {
    const char *name;
    unsigned phi4_batches;
    unsigned phi6_batches;
};

struct Observation {
    std::uint64_t wall_ns;
    std::uint64_t tsc_ticks;
    alignas(64) std::array<double,MAX_BATCHES*LANES> output;
};

struct Series {
    std::array<std::uint64_t,MEASURED_GROUPS> wall{};
    std::array<std::uint64_t,MEASURED_GROUPS> tsc{};
    std::uint64_t checksum=UINT64_C(1469598103934665603);
};

struct Distribution {
    std::uint64_t median;
    std::uint64_t p90;
    std::uint64_t p99;
    std::uint64_t worst;
};

struct Counters {
    bool available=false;
    int error=0;
    std::uint64_t instructions=0;
    std::uint64_t l1d_reads=0;
    std::uint64_t checksum=0;
};

static std::atomic<std::uint64_t> warm_sink{0};

static std::uint64_t timespec_ns(const timespec &value) {
    return static_cast<std::uint64_t>(value.tv_sec)*UINT64_C(1000000000)+
           static_cast<std::uint64_t>(value.tv_nsec);
}

static bool is_sapphire_rapids_model_143() {
    unsigned maximum=__get_cpuid_max(0u,nullptr);
    if (maximum<1u) return false;
    unsigned eax=0u,ebx=0u,ecx=0u,edx=0u;
    __cpuid(0u,eax,ebx,ecx,edx);
    char vendor[13]{};
    std::memcpy(vendor+0,&ebx,sizeof(ebx));
    std::memcpy(vendor+4,&edx,sizeof(edx));
    std::memcpy(vendor+8,&ecx,sizeof(ecx));
    if (std::strcmp(vendor,"GenuineIntel")!=0) return false;
    __cpuid(1u,eax,ebx,ecx,edx);
    unsigned family=(eax>>8u)&0xfu;
    unsigned model=(eax>>4u)&0xfu;
    const unsigned extended_family=(eax>>20u)&0xffu;
    const unsigned extended_model=(eax>>16u)&0xfu;
    if (family==0xfu) family+=extended_family;
    if (family==0x6u || family==0xfu) model|=extended_model<<4u;
    return family==6u && model==143u;
}

static bool pin_cpu_zero(unsigned cpu) {
    if (cpu!=0u) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu,&set);
    return sched_setaffinity(0,sizeof(set),&set)==0;
}

static unsigned parse_cpu(int argc,char **argv,bool &valid) {
    valid=argc==3 && std::strcmp(argv[1],"--cpu")==0;
    if (!valid) return 0u;
    char *end=nullptr;
    errno=0;
    const unsigned long value=std::strtoul(argv[2],&end,10);
    valid=errno==0 && end!=argv[2] && *end=='\0' && value<=UINT32_MAX;
    return valid ? static_cast<unsigned>(value) : 0u;
}

static void touch_line(const double *line,std::uint64_t &state) {
    const volatile double *source=line;
    state^=std::bit_cast<std::uint64_t>(*source)+
           UINT64_C(0x9e3779b97f4a7c15)+(state<<6u)+(state>>2u);
}

static void warm_input(const Phi46Batch8Input &input,unsigned targets,
                       double *output,unsigned batches) {
    std::uint64_t state=warm_sink.load(std::memory_order_relaxed);
    touch_line(input.outer_lower0,state);
    touch_line(input.outer_upper0,state);
    touch_line(input.outer_lower1,state);
    touch_line(input.outer_upper1,state);
    touch_line(input.outer_rho,state);
    touch_line(input.outer_residual,state);
    touch_line(input.outer_inverse_residual,state);
    for (unsigned target=0;target<targets;++target) {
        const auto &t=input.target[target];
        touch_line(t.lower0,state); touch_line(t.upper0,state);
        touch_line(t.lower1,state); touch_line(t.upper1,state);
        touch_line(t.gain00,state); touch_line(t.gain01,state);
        touch_line(t.gain10,state); touch_line(t.gain11,state);
        touch_line(t.inverse_sd0,state); touch_line(t.inverse_sd1,state);
        touch_line(t.rho,state); touch_line(t.inverse_residual,state);
    }
    for (unsigned item=0;item<40u;item+=LANES) {
        touch_line(input.node+item,state);
        touch_line(input.weight+item,state);
    }
    for (unsigned batch=0;batch<batches;++batch)
        touch_line(output+batch*LANES,state);
    warm_sink.store(state,std::memory_order_relaxed);
}

static void evaluate(const Variant &variant,const Workload &workload,
                     const Phi46Batch8Input &phi4,
                     const Phi46Batch8Input &phi6,double *output) {
    unsigned slot=0u;
    for (unsigned batch=0;batch<workload.phi4_batches;++batch,++slot)
        variant.phi4(&phi4,output+slot*LANES);
    for (unsigned batch=0;batch<workload.phi6_batches;++batch,++slot)
        variant.phi6(&phi6,output+slot*LANES);
}

static std::uint64_t consume(const double *output,unsigned count,
                             std::uint64_t state) {
    for (unsigned item=0;item<count;++item) {
        state^=std::bit_cast<std::uint64_t>(output[item])+
               UINT64_C(0x9e3779b97f4a7c15)+(state<<6u)+(state>>2u);
        state=(state<<11u)|(state>>(64u-11u));
    }
    return state;
}

static Observation observe(const Variant &variant,const Workload &workload,
                           const Phi46Batch8Input &phi4,
                           const Phi46Batch8Input &phi6) {
    Observation observation{};
    const unsigned batches=workload.phi4_batches+workload.phi6_batches;
    if (workload.phi4_batches)
        warm_input(phi4,1u,observation.output.data(),batches);
    if (workload.phi6_batches)
        warm_input(phi6,2u,observation.output.data(),batches);
    timespec begin{},end{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW,&begin)!=0) std::abort();
    _mm_lfence();
    const std::uint64_t t0=__rdtsc();
    evaluate(variant,workload,phi4,phi6,observation.output.data());
    unsigned auxiliary=0u;
    const std::uint64_t t1=__rdtscp(&auxiliary);
    _mm_lfence();
    if (clock_gettime(CLOCK_MONOTONIC_RAW,&end)!=0) std::abort();
    observation.wall_ns=timespec_ns(end)-timespec_ns(begin);
    observation.tsc_ticks=t1-t0;
    return observation;
}

static void add_observation(Series &series,const Observation &observation,
                            unsigned values,bool measured) {
    series.checksum=consume(observation.output.data(),values,series.checksum);
    if (!measured) warm_sink.store(series.checksum,std::memory_order_relaxed);
}

static void run_group(const Variant &old_variant,const Variant &new_variant,
                      const Workload &workload,
                      const Phi46Batch8Input &phi4,
                      const Phi46Batch8Input &phi6,bool reverse,
                      Series &old_series,Series &new_series,
                      unsigned measured_index,bool measured) {
    std::array<Observation,4> observation{};
    const Variant *order[4]={&old_variant,&new_variant,&new_variant,&old_variant};
    if (reverse) {
        order[0]=&new_variant; order[1]=&old_variant;
        order[2]=&old_variant; order[3]=&new_variant;
    }
    for (unsigned item=0;item<4u;++item)
        observation[item]=observe(*order[item],workload,phi4,phi6);
    std::uint64_t old_wall=0u,old_tsc=0u,new_wall=0u,new_tsc=0u;
    unsigned old_count=0u,new_count=0u;
    for (unsigned item=0;item<4u;++item) {
        const bool old=order[item]==&old_variant;
        Series &series=old ? old_series : new_series;
        add_observation(series,observation[item],
                        (workload.phi4_batches+workload.phi6_batches)*LANES,
                        measured);
        if (old) {
            old_wall+=observation[item].wall_ns;
            old_tsc+=observation[item].tsc_ticks;
            ++old_count;
        } else {
            new_wall+=observation[item].wall_ns;
            new_tsc+=observation[item].tsc_ticks;
            ++new_count;
        }
    }
    if (measured) {
        old_series.wall[measured_index]=old_wall/old_count;
        old_series.tsc[measured_index]=old_tsc/old_count;
        new_series.wall[measured_index]=new_wall/new_count;
        new_series.tsc[measured_index]=new_tsc/new_count;
    }
}

static Distribution summarize(std::array<std::uint64_t,MEASURED_GROUPS> values) {
    std::sort(values.begin(),values.end());
    return {values[50],values[90],values[99],values[100]};
}

static int open_counter(std::uint32_t type,std::uint64_t config,int group) {
    perf_event_attr attribute{};
    attribute.type=type;
    attribute.size=sizeof(attribute);
    attribute.config=config;
    attribute.disabled=1u;
    attribute.exclude_kernel=1u;
    attribute.exclude_hv=1u;
    attribute.read_format=PERF_FORMAT_GROUP;
    return static_cast<int>(syscall(SYS_perf_event_open,&attribute,0,-1,group,0));
}

static Counters measure_counters(const Variant &variant,const Workload &workload,
                                 const Phi46Batch8Input &phi4,
                                 const Phi46Batch8Input &phi6) {
    Counters result{};
    const std::uint64_t l1d_config=PERF_COUNT_HW_CACHE_L1D |
        (static_cast<std::uint64_t>(PERF_COUNT_HW_CACHE_OP_READ)<<8u) |
        (static_cast<std::uint64_t>(PERF_COUNT_HW_CACHE_RESULT_ACCESS)<<16u);
    const int instructions=open_counter(PERF_TYPE_HARDWARE,
                                        PERF_COUNT_HW_INSTRUCTIONS,-1);
    if (instructions<0) { result.error=errno; return result; }
    const int l1d=open_counter(PERF_TYPE_HW_CACHE,l1d_config,instructions);
    if (l1d<0) {
        result.error=errno;
        close(instructions);
        return result;
    }
    Observation observation{};
    const unsigned batches=workload.phi4_batches+workload.phi6_batches;
    if (workload.phi4_batches)
        warm_input(phi4,1u,observation.output.data(),batches);
    if (workload.phi6_batches)
        warm_input(phi6,2u,observation.output.data(),batches);
    ioctl(instructions,PERF_EVENT_IOC_RESET,PERF_IOC_FLAG_GROUP);
    ioctl(instructions,PERF_EVENT_IOC_ENABLE,PERF_IOC_FLAG_GROUP);
    evaluate(variant,workload,phi4,phi6,observation.output.data());
    ioctl(instructions,PERF_EVENT_IOC_DISABLE,PERF_IOC_FLAG_GROUP);
    struct {
        std::uint64_t count;
        std::uint64_t value[2];
    } readings{};
    const bool read_ok=read(instructions,&readings,sizeof(readings))==
                       static_cast<ssize_t>(sizeof(readings)) &&
                       readings.count==2u;
    if (read_ok) {
        result.instructions=readings.value[0];
        result.l1d_reads=readings.value[1];
    }
    result.error=read_ok ? 0 : errno;
    result.available=read_ok;
    result.checksum=consume(observation.output.data(),batches*LANES,
                            UINT64_C(1469598103934665603));
    close(l1d);
    close(instructions);
    return result;
}

static void print_counters(const char *workload,const char *variant,
                           const Counters &counters) {
    if (!counters.available) {
        std::printf("COUNTERS workload=%s variant=%s status=UNAVAILABLE errno=%d\n",
                    workload,variant,counters.error);
        return;
    }
    std::printf("COUNTERS workload=%s variant=%s instructions=%llu "
                "l1d_read_accesses=%llu checksum=%016llx status=AVAILABLE\n",
                workload,variant,
                static_cast<unsigned long long>(counters.instructions),
                static_cast<unsigned long long>(counters.l1d_reads),
                static_cast<unsigned long long>(counters.checksum));
}

static void benchmark_workload(const Variant &old_variant,
                               const Variant &new_variant,
                               const Workload &workload,
                               const Phi46Batch8Input &phi4,
                               const Phi46Batch8Input &phi6) {
    Series old_series{},new_series{};
    for (unsigned group=0;group<WARMUP_GROUPS;++group)
        run_group(old_variant,new_variant,workload,phi4,phi6,(group&1u)!=0u,
                  old_series,new_series,0u,false);
    old_series.checksum=UINT64_C(1469598103934665603);
    new_series.checksum=UINT64_C(1469598103934665603);
    for (unsigned group=0;group<MEASURED_GROUPS;++group)
        run_group(old_variant,new_variant,workload,phi4,phi6,(group&1u)!=0u,
                  old_series,new_series,group,true);
    const Distribution old_wall=summarize(old_series.wall);
    const Distribution new_wall=summarize(new_series.wall);
    const Distribution old_tsc=summarize(old_series.tsc);
    const Distribution new_tsc=summarize(new_series.tsc);
    const unsigned batches=workload.phi4_batches+workload.phi6_batches;
    const double wall_speedup=static_cast<double>(old_wall.median)/new_wall.median;
    const double tsc_speedup=static_cast<double>(old_tsc.median)/new_tsc.median;
    const bool wall_new_faster=new_wall.median<old_wall.median;
    const bool tsc_new_faster=new_tsc.median<old_tsc.median;
    const bool ranking_agrees=wall_new_faster==tsc_new_faster;
    const std::uint64_t checksum=old_series.checksum^std::rotl(
        new_series.checksum,17);
    std::printf(
        "BENCH workload=%s batches=%u useful_probabilities=%u "
        "old_wall_ns_median=%llu old_wall_ns_p90=%llu old_wall_ns_p99=%llu "
        "old_wall_ns_worst=%llu new_wall_ns_median=%llu new_wall_ns_p90=%llu "
        "new_wall_ns_p99=%llu new_wall_ns_worst=%llu "
        "old_tsc_median=%llu old_tsc_p90=%llu old_tsc_p99=%llu "
        "old_tsc_worst=%llu new_tsc_median=%llu new_tsc_p90=%llu "
        "new_tsc_p99=%llu new_tsc_worst=%llu "
        "new_vs_old_speedup_wall=%.6f new_vs_old_speedup_tsc=%.6f "
        "old_cycles_per_batch=%.3f new_cycles_per_batch=%.3f "
        "old_cycles_per_probability=%.3f new_cycles_per_probability=%.3f "
        "wall_tsc_ranking_agreement=%s checksum=%016llx\n",
        workload.name,batches,batches*LANES,
        static_cast<unsigned long long>(old_wall.median),
        static_cast<unsigned long long>(old_wall.p90),
        static_cast<unsigned long long>(old_wall.p99),
        static_cast<unsigned long long>(old_wall.worst),
        static_cast<unsigned long long>(new_wall.median),
        static_cast<unsigned long long>(new_wall.p90),
        static_cast<unsigned long long>(new_wall.p99),
        static_cast<unsigned long long>(new_wall.worst),
        static_cast<unsigned long long>(old_tsc.median),
        static_cast<unsigned long long>(old_tsc.p90),
        static_cast<unsigned long long>(old_tsc.p99),
        static_cast<unsigned long long>(old_tsc.worst),
        static_cast<unsigned long long>(new_tsc.median),
        static_cast<unsigned long long>(new_tsc.p90),
        static_cast<unsigned long long>(new_tsc.p99),
        static_cast<unsigned long long>(new_tsc.worst),
        wall_speedup,tsc_speedup,
        static_cast<double>(old_tsc.median)/batches,
        static_cast<double>(new_tsc.median)/batches,
        static_cast<double>(old_tsc.median)/(batches*LANES),
        static_cast<double>(new_tsc.median)/(batches*LANES),
        ranking_agrees?"YES":"NO",static_cast<unsigned long long>(checksum));
    print_counters(workload.name,"OLD",measure_counters(
        old_variant,workload,phi4,phi6));
    print_counters(workload.name,"NEW",measure_counters(
        new_variant,workload,phi4,phi6));
}

static bool preflight_identity(const Phi46Batch8Input &phi4,
                               const Phi46Batch8Input &phi6) {
    alignas(64) double old_output[8]{};
    alignas(64) double new_output[8]{};
    mvn_f64_math::mvn_phi4_batch8_tensor40_candidate(&phi4,old_output);
    mvn_phi4_batch8_tensor40_coeffsched_asm(&phi4,new_output);
    const bool phi4_equal=std::memcmp(old_output,new_output,sizeof(old_output))==0;
    std::uint64_t checksum=consume(old_output,LANES,UINT64_C(1469598103934665603));
    checksum=consume(new_output,LANES,checksum);
    mvn_f64_math::mvn_phi6_batch8_bridge40_candidate(&phi6,old_output);
    mvn_phi6_batch8_bridge40_coeffsched_asm(&phi6,new_output);
    const bool phi6_equal=std::memcmp(old_output,new_output,sizeof(old_output))==0;
    checksum=consume(old_output,LANES,checksum);
    checksum=consume(new_output,LANES,checksum);
    std::printf("BENCH_PREFLIGHT component_corpus_cases=8 phi4_byte_identity=%s "
                "phi6_byte_identity=%s checksum=%016llx status=%s\n",
                phi4_equal?"PASS":"FAIL",phi6_equal?"PASS":"FAIL",
                static_cast<unsigned long long>(checksum),
                phi4_equal&&phi6_equal?"PASS":"FAIL");
    return phi4_equal&&phi6_equal;
}

} // namespace

int main(int argc,char **argv) {
    bool valid_cpu=false;
    const unsigned cpu=parse_cpu(argc,argv,valid_cpu);
    if (!valid_cpu || cpu!=0u) {
        std::printf("MVN_COEFFICIENT_SCHEDULING_NATIVE_REFUSED reason=CPU_0_REQUIRED\n");
        return 2;
    }
    if (!is_sapphire_rapids_model_143()) {
        std::printf("MVN_COEFFICIENT_SCHEDULING_NATIVE_REFUSED "
                    "reason=SPR_FAMILY_6_MODEL_143_REQUIRED\n");
        return 2;
    }
    if (!pin_cpu_zero(cpu)) {
        std::printf("MVN_COEFFICIENT_SCHEDULING_NATIVE_REFUSED "
                    "reason=CPU_0_PIN_FAILED\n");
        return 2;
    }
    Phi46Batch8Input phi4{},phi6{};
    if (!prepare_batch(4u,phi4) || !prepare_batch(6u,phi6)) {
        std::printf("MVN_COEFFICIENT_SCHEDULING_BENCHMARK_PREPARE_FAILED\n");
        return 1;
    }
    if (!preflight_identity(phi4,phi6) ||
        !run_coeffsched_dimension(4u) || !run_coeffsched_dimension(6u)) return 1;
    const Variant old_variant{
        mvn_f64_math::mvn_phi4_batch8_tensor40_candidate,
        mvn_f64_math::mvn_phi6_batch8_bridge40_candidate};
    const Variant new_variant{mvn_phi4_batch8_tensor40_coeffsched_asm,
                              mvn_phi6_batch8_bridge40_coeffsched_asm};
    const Workload workloads[]={
        {"ISOLATED_PHI4",1u,0u},
        {"ISOLATED_PHI6",0u,1u},
        {"WORK_ONLY_12PHI4_5PHI6_NOT_A_26_TERM_PRICE",12u,5u},
    };
    std::printf(
        "BENCH_SYMBOLS phi4_old=mvn_phi4_batch8_tensor40_candidate "
        "phi4_new=mvn_phi4_batch8_tensor40_coeffsched_asm "
        "phi6_old=mvn_phi6_batch8_bridge40_candidate "
        "phi6_new=mvn_phi6_batch8_bridge40_coeffsched_asm\n");
    std::printf("BENCH_PROTOCOL cpu=0 warmup_groups=%u measured_groups=%u "
                "ordering=ALTERNATING_ABBA_BAAB cache=CANDIDATE_WARM "
                "clock=CLOCK_MONOTONIC_RAW tsc=LFENCE_RDTSC_RDTSCP_LFENCE\n",
                WARMUP_GROUPS,MEASURED_GROUPS);
    for (const auto &workload:workloads)
        benchmark_workload(old_variant,new_variant,workload,phi4,phi6);
    std::printf("MVN_COEFFICIENT_SCHEDULING_PERFORMANCE_MEASURED\n");
    return 0;
}
#endif
