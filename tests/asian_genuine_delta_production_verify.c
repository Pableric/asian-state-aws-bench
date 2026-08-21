#define _POSIX_C_SOURCE 200112L
#include "private/asian_genuine_price_delta_strip_diag.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATHS=4096,STRIKE_COUNT_COUNT=5,ESTIMATOR_COUNT=2,SIDE_COUNT=2 };
static const uint32_t strike_counts[STRIKE_COUNT_COUNT]={1,4,8,16,32};

typedef struct {uint32_t magic,version,fixture_count,paths;} corpus_header_t;
typedef struct {uint32_t n,replication,strike_count_count,reserved;} fixture_header_t;

static void *aligned_zero(size_t bytes)
{
    void*p=NULL;if(posix_memalign(&p,64,bytes))return NULL;
    memset(p,0,bytes);return p;
}

static int read_one(FILE*input,void*value,size_t bytes)
{
    return fread(value,bytes,1,input)==1?0:-1;
}

int main(int argc,char**argv)
{
    if(argc!=5||strcmp(argv[1],"--corpus")||strcmp(argv[3],"--json"))return 2;
    FILE*input=fopen(argv[2],"rb");if(!input)return 2;
    corpus_header_t corpus;if(read_one(input,&corpus,sizeof corpus)||
        corpus.magic!=UINT32_C(0x51445341)||corpus.version!=1||corpus.paths!=PATHS)return 2;
    float*q=aligned_zero(PATHS*sizeof *q),*g=aligned_zero(PATHS*sizeof *g);
    asian_genuine_strip_context_t*contexts[STRIKE_COUNT_COUNT];
    for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)contexts[ci]=aligned_zero(sizeof*contexts[ci]);
    if(!q||!g)return 2;
    uint64_t price_mismatch=0,tile_delta_mismatch=0,model_price_mismatch=0,model_delta_mismatch=0;
    uint64_t observations=0,smooth_positive=0,smooth_negative=0;
    long double same_sum=0.0L,smooth_sum=0.0L,max_same=0.0L,max_smooth=0.0L;
    uint32_t worst_n=0,worst_rep=0,worst_count=0,worst_position=0,worst_estimator=0,worst_side=0;
    long double worst_unadjusted=0.0L,worst_flip=0.0L,worst_residual=0.0L;
    for(uint32_t fixture=0;fixture<corpus.fixture_count;++fixture){
        fixture_header_t header;if(read_one(input,&header,sizeof header)||
            header.strike_count_count!=STRIKE_COUNT_COUNT||
            fread(q,sizeof *q,PATHS,input)!=PATHS||fread(g,sizeof *g,PATHS,input)!=PATHS)return 2;
        for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)
            if(read_one(input,contexts[ci],sizeof*contexts[ci]))return 2;
        for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)for(uint32_t estimator=0;
            estimator<ESTIMATOR_COUNT;++estimator){
            asian_genuine_strip_output_t expected_model,same_reference,independent_reference;
            double flips[32][SIDE_COUNT];
            if(read_one(input,&expected_model,sizeof expected_model)||
               read_one(input,&same_reference,sizeof same_reference)||
               read_one(input,&independent_reference,sizeof independent_reference)||
               read_one(input,flips,sizeof flips))return 2;
            asian_genuine_strip_output_t*p4=aligned_zero(sizeof*p4),*p8=aligned_zero(sizeof*p8);
            asian_genuine_strip_output_t*d4=aligned_zero(sizeof*d4),*d8=aligned_zero(sizeof*d8);
            if(!p4||!p8||!d4||!d8)return 2;
            if(asian_genuine_strip_price_diag(q,g,contexts[ci],
                    (enum asian_genuine_strip_estimator)estimator,4,p4)||
               asian_genuine_strip_price_diag(q,g,contexts[ci],
                    (enum asian_genuine_strip_estimator)estimator,8,p8)||
               asian_genuine_strip_price_delta_diag(q,g,contexts[ci],
                    (enum asian_genuine_strip_estimator)estimator,4,d4)||
               asian_genuine_strip_price_delta_diag(q,g,contexts[ci],
                    (enum asian_genuine_strip_estimator)estimator,8,d8))return 2;
            for(uint32_t k=0;k<strike_counts[ci];++k){
                const asian_genuine_strip_value_t*a=&p4->values[k],*b=&p8->values[k];
                const asian_genuine_strip_value_t*c=&d4->values[k],*d=&d8->values[k];
                const asian_genuine_strip_value_t*m=&expected_model.values[k];
                const asian_genuine_strip_value_t*s=&same_reference.values[k];
                const asian_genuine_strip_value_t*r=&independent_reference.values[k];
                if(a->call_price!=b->call_price||a->put_price!=b->put_price||
                   a->call_price!=c->call_price||a->put_price!=c->put_price||
                   c->call_price!=d->call_price||c->put_price!=d->put_price)++price_mismatch;
                if(c->call_delta!=d->call_delta||c->put_delta!=d->put_delta)++tile_delta_mismatch;
                if(c->call_price!=m->call_price||c->put_price!=m->put_price)++model_price_mismatch;
                if(c->call_delta!=m->call_delta||c->put_delta!=m->put_delta)++model_delta_mismatch;
                const double actual[SIDE_COUNT]={c->call_delta,c->put_delta};
                const double same[SIDE_COUNT]={s->call_delta,s->put_delta};
                const double independent[SIDE_COUNT]={r->call_delta,r->put_delta};
                for(uint32_t side=0;side<SIDE_COUNT;++side){
                    const long double same_error=(long double)actual[side]-same[side];
                    const long double unadjusted=(long double)actual[side]-independent[side];
                    const long double residual=unadjusted-flips[k][side];
                    same_sum+=same_error;smooth_sum+=residual;++observations;
                    if(fabsl(same_error)>max_same)max_same=fabsl(same_error);
                    if(fabsl(residual)>1e-8L){if(residual>0)++smooth_positive;else++smooth_negative;}
                    if(fabsl(residual)>max_smooth){max_smooth=fabsl(residual);
                        worst_n=header.n;worst_rep=header.replication;worst_count=strike_counts[ci];
                        worst_position=k;worst_estimator=estimator;worst_side=side;
                        worst_unadjusted=unadjusted;worst_flip=flips[k][side];worst_residual=residual;}
                }
            }
            free(d8);free(d4);free(p8);free(p4);
        }
    }
    if(fgetc(input)!=EOF)return 2;
    fclose(input);
    const long double same_mean=same_sum/observations,smooth_mean=smooth_sum/observations;
    const uint64_t smooth_nonzero=smooth_positive+smooth_negative;
    const int gate_price=price_mismatch==0&&tile_delta_mismatch==0;
    const int gate_same=max_same<=1e-6L&&fabsl(same_mean)<=1e-7L;
    const int gate_smooth=max_smooth<=1e-4L&&fabsl(smooth_mean)<=1e-6L&&
        (smooth_nonzero<20||(smooth_positive*100<95*smooth_nonzero&&
                            smooth_negative*100<95*smooth_nonzero));
    FILE*out=fopen(argv[4],"w");if(!out)return 2;
    fprintf(out,"{\n\"schema\":1,\n\"fixtures\":%u,\n\"observations\":%"PRIu64",\n"
        "\"price_bit_mismatches\":%"PRIu64",\n\"tile_delta_mismatches\":%"PRIu64",\n"
        "\"model_price_bit_mismatches\":%"PRIu64",\n\"model_delta_bit_mismatches\":%"PRIu64",\n"
        "\"max_same_state_error\":%.17g,\n\"signed_mean_same_state_error\":%.17g,\n"
        "\"max_smooth_residual\":%.17g,\n\"signed_mean_smooth_residual\":%.17g,\n"
        "\"smooth_positive\":%"PRIu64",\n\"smooth_negative\":%"PRIu64",\n"
        "\"worst_smooth\":{\"N\":%u,\"replication\":%u,\"strike_count\":%u,"
        "\"strike_position\":%u,\"estimator\":\"%s\",\"side\":\"%s\","
        "\"unadjusted\":%.17g,\"indicator_flip\":%.17g,\"smooth_residual\":%.17g},\n"
        "\"gates\":{\"price_and_tile_bits\":%s,\"same_state\":%s,"
        "\"smooth_residual\":%s}\n}\n",corpus.fixture_count,observations,
        price_mismatch,tile_delta_mismatch,model_price_mismatch,model_delta_mismatch,
        (double)max_same,(double)same_mean,(double)max_smooth,(double)smooth_mean,
        smooth_positive,smooth_negative,worst_n,worst_rep,worst_count,worst_position,
        worst_estimator?"geometric_cv":"arithmetic",worst_side?"put":"call",
        (double)worst_unadjusted,(double)worst_flip,(double)worst_residual,
        gate_price?"true":"false",gate_same?"true":"false",gate_smooth?"true":"false");
    fclose(out);
    fprintf(stderr,"production_verify price=%s same_state=%s smooth=%s\n",
        gate_price?"PASS":"FAIL",gate_same?"PASS":"FAIL",gate_smooth?"PASS":"FAIL");
    for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)free(contexts[ci]);
    free(g);free(q);
    return 0;
}
