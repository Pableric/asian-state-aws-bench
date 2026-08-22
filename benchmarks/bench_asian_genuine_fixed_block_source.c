#define main asian_genuine_msfr_qualified_benchmark_main
#include "bench_asian_genuine_multistrike_full_risk.c"
#undef main

#include "private/asian_genuine_fixed_block_source_diag.h"

enum source_candidate { SRC_X3,SRC_FIXED,SRC_EXACT,SRC_X3_EXP,SRC_FIXED_EXP,
                        SRC_COLD_VALIDATE,SRC_COLD_EXACT,SRC_CANDIDATES };
enum complete_candidate { COMPLETE_X3,COMPLETE_FIXED,PHASE1_X3,PHASE1_FIXED,
                          PRICE_DELTA_X3,PRICE_DELTA_FIXED,COMPLETE_CANDIDATES };

static const char *const source_names[SRC_CANDIDATES]={
 "qualified_x3_x_only_source","prepared_fixed_block_source_consumption",
 "prepared_exact_x_lookup_ceiling","qualified_x3_plus_vector_exp",
 "prepared_fixed_block_source_consumption_plus_vector_exp",
 "cold_fixed_block_table_validation","cold_prepared_exact_x_setup"};
static const char *const complete_names[COMPLETE_CANDIDATES]={
 "qualified_x3_complete_full_risk_strip","prepared_fixed_block_complete_full_risk_strip",
 "qualified_x3_complete_phase1_direct_full_risk","prepared_fixed_block_complete_phase1_direct_full_risk",
 "qualified_x3_complete_price_delta_strip","prepared_fixed_block_complete_price_delta_strip"};

static asian_genuine_fixed_block_source_context_t *fixed_context;
static asian_genuine_fixed_block_exact_x_context_t *exact_context;
static float *exact_table;

static asian_genuine_fixed_block_source_request_t benchmark_request(uint32_t n)
{
    asian_genuine_fixed_block_source_request_t r;memset(&r,0,sizeof(r));
    r.target_start_index=8192u;r.path_count=PATHS;r.block_count=1u;
    r.fixing_count=n;r.s0=100;r.rate=.03;r.dividend_yield=0;r.sigma=.20;
    r.maturity=1;r.signed_z=asian_genuine_fixed_block_signed_z;
    r.signed_z_bytes=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES;return r;
}

static int prepare_fixed(uint32_t n)
{
    fixed_context=a64(sizeof(*fixed_context));exact_context=a64(sizeof(*exact_context));
    exact_table=a64(ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    if(!fixed_context||!exact_context||!exact_table)return -1;
    asian_genuine_fixed_block_source_request_t r=benchmark_request(n);
    return asian_genuine_fixed_block_source_prepare(fixed_context,&r)||
      asian_genuine_fixed_block_exact_x_prepare(exact_context,fixed_context,
        exact_table,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES)?-1:0;
}

static void release_fixed(void)
{
    free(exact_table);free(exact_context);free(fixed_context);
    exact_table=NULL;exact_context=NULL;fixed_context=NULL;
}

static void source_only(uint32_t candidate)
{
    if(candidate==SRC_X3||candidate==SRC_X3_EXP)
        ordered_d1_x_only_diag(256u,b.producers[0],source_x(0));
    else if(candidate==SRC_FIXED||candidate==SRC_FIXED_EXP)
        asian_genuine_fixed_block_signed_z_one_fma_source_diag(fixed_context,source_x(0));
    else if(candidate==SRC_EXACT)
        asian_genuine_fixed_block_prepared_exact_x_lookup_diag(exact_context,source_x(0));
    if(candidate==SRC_X3_EXP||candidate==SRC_FIXED_EXP){
        asian_vector_exp_range_reduced_array_diag(source_x(0),source_growth(0));
        asian_vector_exp_range_reduced_array_diag(source_x(0)+PATHS,source_growth(0)+PATHS);
    }
}

static double run_source(uint32_t candidate)
{
    if(candidate==SRC_COLD_VALIDATE){
        asian_genuine_fixed_block_source_request_t r=benchmark_request(b.n);
        asian_genuine_fixed_block_source_context_t temporary;
        if(asian_genuine_fixed_block_source_prepare(&temporary,&r))abort();
        return temporary.diffusion;
    }
    if(candidate==SRC_COLD_EXACT){
        asian_genuine_fixed_block_exact_x_context_t temporary;
        if(asian_genuine_fixed_block_exact_x_prepare(&temporary,fixed_context,
             exact_table,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES))abort();
        return exact_table[17];
    }
    source_only(candidate);return source_x(0)[17];
}

static void source_with_exp(int fixed)
{
    source_only(fixed?SRC_FIXED_EXP:SRC_X3_EXP);
}

static double run_complete(uint32_t candidate,int cv)
{
    const int fixed=candidate==COMPLETE_FIXED||candidate==PHASE1_FIXED||
                    candidate==PRICE_DELTA_FIXED;
    reset_candidate(BASIS_TILE4);source_with_exp(fixed);
    if(candidate==COMPLETE_X3||candidate==COMPLETE_FIXED){
        asian_genuine_msfr_basis_forward_diag(b.basis_context,b.basis);
        consume_basis(cv,4u);
        return output_sum(4u);
    }
    if(candidate==PHASE1_X3||candidate==PHASE1_FIXED){
        phase_leaf(0u,cv,&b.phase_values[0]);phase_finalize(0u,&b.phase_values[0]);
        return output_sum(4u);
    }
    init_state(&b.states[0],100.0f);
    asian_genuine_sql_dual_control_diag(routes(0),b.n,&b.states[0]);
    if(cv)asian_genuine_strip_l_to_g_diag(b.states[0].l,&b.strip_contexts[0],b.g);
    if(asian_genuine_strip_price_delta_diag(b.states[0].q,b.g,
         &b.strip_contexts[0],cv,4u,&b.strip_outputs[0]))abort();
    for(uint32_t i=0;i<b.k;++i){
      b.output->values[i].call.price=b.strip_outputs[0].values[i].call_price;
      b.output->values[i].put.price=b.strip_outputs[0].values[i].put_price;
      b.output->values[i].call.delta=b.strip_outputs[0].values[i].call_delta;
      b.output->values[i].put.delta=b.strip_outputs[0].values[i].put_delta;
    }
    return output_sum(2u);
}

static int fixed_preflight_case(uint32_t n,uint32_t k,int cv)
{
    if(prepare_fixture(n,k)||prepare_fixed(n))return -1;
    source_only(SRC_FIXED);float *copy=a64(ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    if(!copy)return -1;
    memcpy(copy,source_x(0),ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    source_only(SRC_EXACT);if(memcmp(copy,source_x(0),ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES))return -1;
    asian_genuine_msfr_output_t *reference=a64(sizeof(*reference));if(!reference)return -1;
    run_complete(COMPLETE_X3,cv);memcpy(reference,b.output,sizeof(*reference));
    run_complete(COMPLETE_FIXED,cv);const double full=max_difference(reference,b.output,4u);
    run_complete(PRICE_DELTA_X3,cv);memcpy(reference,b.output,sizeof(*reference));
    run_complete(PRICE_DELTA_FIXED,cv);const double pd=max_difference(reference,b.output,2u);
    double phase=0;if(k==1u){run_complete(PHASE1_X3,cv);memcpy(reference,b.output,sizeof(*reference));
      run_complete(PHASE1_FIXED,cv);phase=max_difference(reference,b.output,4u);}
    printf("fixed_source_preflight N=%u K=%u estimator=%s full_raw=%g "
      "price_delta_raw=%g phase1_raw=%g status=%s\n",n,k,cv?"geometric_cv":"arithmetic",
      full,pd,phase,full<=1e-3&&pd<=1e-3&&phase<=1e-3?"PASS":"FAIL");
    free(reference);free(copy);release_fixed();release_fixture();
    return full<=1e-3&&pd<=1e-3&&phase<=1e-3?0:-1;
}

static int fixed_preflight(void)
{
    const uint32_t pn[]={2u,16u,256u},pk[]={1u,32u};
    for(unsigned ni=0;ni<3u;++ni)for(unsigned ki=0;ki<2u;++ki)
      for(int cv=0;cv<2;++cv)if(fixed_preflight_case(pn[ni],pk[ki],cv))return -1;
    puts("asian_genuine_fixed_block_source benchmark_preflight=PASS N=2,16,256 "
      "K=1,32 calls_puts=yes estimators=2 table_hash=yes exact_x_bits=yes");return 0;
}

static void emit_samples(FILE *json,const uint64_t values[SAMPLES])
{
    for(int sample=0;sample<SAMPLES;++sample)
        fprintf(json,"%s%"PRIu64,sample?",":"",values[sample]);
}

static void emit_row(FILE *json,int *comma,uint32_t n,uint32_t k,int cv,int mode,
  const char *name,const char *classification,const uint64_t ticks[SAMPLES],
  const uint64_t wall[SAMPLES],double checksum_value,double error,
  const uint64_t baseline_ticks[SAMPLES],const uint64_t baseline_wall[SAMPLES])
{
    const uint64_t tm=quantile(ticks,25u),wm=quantile(wall,25u);
    fprintf(json,"%s{\"N\":%u,\"K\":%u,\"estimator\":\"%s\","
      "\"cache_mode\":\"%s\",\"candidate\":\"%s\","
      "\"classification\":\"%s\",\"tsc_p10\":%"PRIu64
      ",\"tsc_median\":%"PRIu64",\"tsc_p90\":%"PRIu64
      ",\"wall_ns_p10\":%"PRIu64",\"wall_ns_median\":%"PRIu64
      ",\"wall_ns_p90\":%"PRIu64",\"tsc_units_per_produced_value\":%.9g,"
      "\"wall_ns_per_produced_value\":%.9g,\"numerical_checksum\":%.17g,"
      "\"max_abs_raw_difference_vs_x3\":%.9g,",
      (*comma)++?",":"",n,k,cv<0?"not_applicable":cv?"geometric_cv":"arithmetic",
      mode?"historical_32KiB_rmw":"warm_candidate_specific",name,classification,
      quantile(ticks,5u),tm,quantile(ticks,45u),quantile(wall,5u),wm,
      quantile(wall,45u),(double)tm/8192.0,(double)wm/8192.0,
      checksum_value,error);
    if(baseline_ticks&&baseline_wall)
      fprintf(json,"\"paired_speedup_vs_x3_tsc\":%.9g,"
        "\"paired_speedup_vs_x3_wall\":%.9g,",
        paired_ratio(baseline_ticks,ticks),paired_ratio(baseline_wall,wall));
    fputs("\"raw_tsc\":[",json);
    emit_samples(json,ticks);fputs("],\"raw_wall_ns\":[",json);
    emit_samples(json,wall);fputs("]}",json);
}

static void shuffle_list(uint32_t *list,uint32_t count,uint64_t *seed)
{
    for(uint32_t i=0;i<count;++i)list[i]=i;
    for(uint32_t i=count-1u;i;--i){uint32_t j=(uint32_t)(rng_step(seed)%(i+1u));
      uint32_t t=list[i];list[i]=list[j];list[j]=t;}
}

static int audit_fixed(const char *which)
{
    audit_only=1;if(prepare_fixture(16u,1u)||prepare_fixed(16u))return -1;
    if(!strcmp(which,"x3"))for(int i=0;i<64;++i)source_only(SRC_X3);
    else if(!strcmp(which,"fixed"))for(int i=0;i<64;++i)source_only(SRC_FIXED);
    else if(!strcmp(which,"exact"))for(int i=0;i<64;++i)source_only(SRC_EXACT);
    else return -1;
    printf("audit_leaf=%s checksum=%a\n",which,source_x(0)[17]);
    release_fixed();release_fixture();return 0;
}

int main(int argc,char **argv)
{
    const char *output="results/asian_genuine_fixed_block_source/aws.json",*audit=NULL;
    int check=0;for(int i=1;i<argc;++i)if(!strcmp(argv[i],"--check-only"))check=1;
      else if(!strcmp(argv[i],"--json")&&++i<argc)output=argv[i];
      else if(!strcmp(argv[i],"--audit-leaf")&&++i<argc)audit=argv[i];else return 2;
    char binary[65]="unavailable";binary_sha(binary);
    if(first_cpu()<0){write_failure(output,"cpu_affinity",0,0,NULL,"FAIL",binary);return 2;}
    if(check)return fixed_preflight()?2:0;
    if(audit)return audit_fixed(audit)?2:0;
    if(access(output,F_OK)==0){write_failure(output,"success_output_exists",0,0,NULL,
      "REFUSED",binary);fprintf(stderr,"refusing to replace existing success JSON: %s\n",output);return 2;}
    atomic_json_t atomic;if(json_open(&atomic,output)){write_failure(output,
      "success_temp_open",0,0,NULL,"FAIL",binary);return 2;}FILE *json=atomic.f;
    fprintf(json,"{\"status\":\"PASS\",\"benchmark_provenance\":{"
      "\"git_commit\":\"%s\",\"branch\":\"research/asian-fixed-block-one-fma-source\","
      "\"binary_sha256\":\"%s\",\"base_commit\":"
      "\"9c6146bb4e38d0aa5ebcf2ca0b026734e772804a\","
      "\"joe_kuo_sha256\":\"fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199\","
      "\"signed_z_table_sha256\":\"ecf3bb854e98bedcf724d0743438457ccf8b600e1264cb537741ce0b9d90d98d\"},"
      "\"contract\":{\"S0\":100,\"strike\":100,\"r\":0.03,\"q\":0,"
      "\"sigma\":0.20,\"T\":1,\"paths\":4096,\"target_indices\":"
      "\"8192-12287\",\"donor_indices\":\"8192-16383\","
      "\"fixing_domain\":[2,256],\"native_N\":[16,32,64,128,256],"
      "\"native_K\":[1,4,8,16,32]},\"terminology\":{"
      "\"fixed_candidate\":\"prepared fixed-block source consumption\","
      "\"x3\":\"general generated-source baseline\","
      "\"exact_ceiling\":\"unranked diagnostic only\"},"
      "\"warmups\":16,\"samples\":51,\"timer\":"
      "\"fenced_TSC_and_CLOCK_MONOTONIC_RAW\",\"tsc_units\":"
      "\"not_CPU_cycles\",\"working_sets\":{\"signed_z_table_bytes\":32768,"
      "\"x_payload_bytes\":32768,\"growth_payload_bytes\":32768},"
      "\"results\":[",MSFR_GIT_COMMIT,binary);
    int comma=0;uint64_t seed=UINT64_C(0x46495845445a464d);
    int source_gate=1,n16_gate=1,n32_gate=1,large_gate=1,exact_under_five=1;
    for(unsigned ni=0;ni<5u;++ni){const uint32_t n=ns[ni];
      if(prepare_fixture(n,32u)||prepare_fixed(n))goto fail;
      for(int mode=0;mode<2;++mode){
        uint64_t st[SRC_CANDIDATES][SAMPLES]={{0}},sw[SRC_CANDIDATES][SAMPLES]={{0}};
        for(int w=0;w<WARMUPS;++w)for(uint32_t c=0;c<SRC_CANDIDATES;++c){
          condition(BASIS_TILE4,mode);checksum(run_source(c));}
        for(int s=0;s<SAMPLES;++s){uint32_t order[SRC_CANDIDATES];shuffle_list(order,SRC_CANDIDATES,&seed);
          for(uint32_t at=0;at<SRC_CANDIDATES;++at){uint32_t c=order[at];condition(BASIS_TILE4,mode);
            uint64_t w0=wall_ns(),t0=tsc0();double v=run_source(c);uint64_t t1=tsc1(),w1=wall_ns();
            st[c][s]=t1-t0;sw[c][s]=w1-w0;checksum(v);}}
        for(uint32_t c=0;c<SRC_CANDIDATES;++c){
          const uint32_t base=c==SRC_FIXED||c==SRC_EXACT?SRC_X3:
                              c==SRC_FIXED_EXP?SRC_X3_EXP:SRC_CANDIDATES;
          emit_row(json,&comma,n,0u,-1,mode,
          source_names[c],c==SRC_EXACT?"unranked_ceiling":c>=SRC_COLD_VALIDATE?
          "cold_preparation":"source_component",st[c],sw[c],source_x(0)[17],0.0,
          base<SRC_CANDIDATES?st[base]:NULL,base<SRC_CANDIDATES?sw[base]:NULL);
        }
        if(paired_ratio(st[SRC_X3],st[SRC_FIXED])<1.5||
           paired_ratio(sw[SRC_X3],sw[SRC_FIXED])<1.5)source_gate=0;
        if(paired_ratio(st[SRC_X3],st[SRC_EXACT])>=1.05||
           paired_ratio(sw[SRC_X3],sw[SRC_EXACT])>=1.05)exact_under_five=0;
      }
      release_fixed();release_fixture();
    }
    for(unsigned ni=0;ni<5u;++ni)for(unsigned ki=0;ki<5u;++ki)for(int cv=0;cv<2;++cv){
      const uint32_t n=ns[ni],k=ks[ki];if(prepare_fixture(n,k)||prepare_fixed(n))goto fail;
      for(int mode=0;mode<2;++mode){uint32_t count=k==1u?COMPLETE_CANDIDATES:4u;
        uint32_t active[COMPLETE_CANDIDATES]={COMPLETE_X3,COMPLETE_FIXED,PRICE_DELTA_X3,PRICE_DELTA_FIXED,PHASE1_X3,PHASE1_FIXED};
        if(k==1u){active[2]=PHASE1_X3;active[3]=PHASE1_FIXED;active[4]=PRICE_DELTA_X3;active[5]=PRICE_DELTA_FIXED;}
        uint64_t ticks[COMPLETE_CANDIDATES][SAMPLES]={{0}},wall[COMPLETE_CANDIDATES][SAMPLES]={{0}};
        for(int w=0;w<WARMUPS;++w)for(uint32_t at=0;at<count;++at){condition(BASIS_TILE4,mode);checksum(run_complete(active[at],cv));}
        for(int s=0;s<SAMPLES;++s){uint32_t order[COMPLETE_CANDIDATES];shuffle_list(order,count,&seed);
          for(uint32_t pos=0;pos<count;++pos){uint32_t c=active[order[pos]];condition(BASIS_TILE4,mode);
            uint64_t w0=wall_ns(),t0=tsc0();double v=run_complete(c,cv);uint64_t t1=tsc1(),w1=wall_ns();
            ticks[c][s]=t1-t0;wall[c][s]=w1-w0;checksum(v);}}
        asian_genuine_msfr_output_t *reference=a64(sizeof(*reference));if(!reference)goto fail;
        run_complete(COMPLETE_X3,cv);memcpy(reference,b.output,sizeof(*reference));
        double errors[COMPLETE_CANDIDATES]={0},values[COMPLETE_CANDIDATES]={0};
        for(uint32_t at=0;at<count;++at){uint32_t c=active[at];values[c]=run_complete(c,cv);
          uint32_t fields=c==PRICE_DELTA_X3||c==PRICE_DELTA_FIXED?2u:4u;
          errors[c]=max_difference(reference,b.output,fields);emit_row(json,&comma,n,k,cv,mode,
            complete_names[c],fields==2u?"complete_two_sided_price_delta":"complete_two_sided_full_risk",
            ticks[c],wall[c],values[c],errors[c],c&1u?ticks[c-1u]:NULL,
            c&1u?wall[c-1u]:NULL);}
        if(k==1u){const double tr=paired_ratio(ticks[PHASE1_X3],ticks[PHASE1_FIXED]);
          const double wr=paired_ratio(wall[PHASE1_X3],wall[PHASE1_FIXED]);
          if(n==16u&&(tr<1.08||wr<1.08))n16_gate=0;
          if(n==32u&&(tr<1.05||wr<1.05))n32_gate=0;}
        if(n>=64u){for(uint32_t pair=0;pair<count;pair+=2u){uint32_t x=active[pair],f=active[pair+1u];
          if(paired_ratio(ticks[x],ticks[f])<1.0/1.01||
             paired_ratio(wall[x],wall[f])<1.0/1.01)large_gate=0;}}
        free(reference);
      }
      release_fixed();release_fixture();
    }
    fprintf(json,"],\"promotion_evaluation\":{\"source_speedup_at_least_1_50x\":%s,"
      "\"N16_complete_improvement_at_least_8_percent\":%s,"
      "\"N32_complete_improvement_at_least_5_percent\":%s,"
      "\"N64_128_256_no_more_than_1_percent_regression\":%s,"
      "\"exact_lookup_less_than_5_percent_faster_than_x3\":%s,"
      "\"source_arithmetic_conclusion\":\"%s\",\"promotion_eligible\":%s},"
      "\"metric_definitions\":{"
      "\"prepared_fixed_block_source_consumption\":\"loads a frozen canonical signed-Gaussian payload and applies the prepared affine FMA\","
      "\"qualified_x3_x_only_source\":\"general generated-source baseline\","
      "\"prepared_exact_x_lookup_ceiling\":\"unranked copy ceiling with cold affine preparation excluded\"}}\n",
      source_gate?"true":"false",n16_gate?"true":"false",n32_gate?"true":"false",
      large_gate?"true":"false",exact_under_five?"true":"false",
      exact_under_five?"source arithmetic is not worth further optimization":
        "exact lookup retains material transform headroom",
      source_gate&&n16_gate&&n32_gate&&large_gate?"true":"false");
    if(json_commit(&atomic,output)){write_failure(output,"success_commit",0,0,NULL,"FAIL",binary);return 2;}
    return sink==0;
fail:{uint32_t fn=b.n,fk=b.k;release_fixed();release_fixture();json_abort(&atomic);
    write_failure(output,"timed_candidate",fn,fk,"fixed_block_source","FAIL",binary);return 2;}
}
