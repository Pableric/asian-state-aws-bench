#include "private/asian_genuine_permute.h"
#include <stdlib.h>
#include <string.h>
typedef struct{uint32_t word,index;} pair_t;
static int pc(const void*a,const void*b){uint32_t x=((const pair_t*)a)->word,y=((const pair_t*)b)->word;return(x>y)-(x<y);}
static int build(const uint16_t src[4096],fragment_map_t*m){
 memset(m,0,sizeof(*m)); unsigned np=0;
 for(unsigned p=0;p<128;p++)for(unsigned h=0;h<2;h++){
  unsigned b=p*32+h*16,line=src[b]/16,ctl[16];
  for(unsigned l=0;l<16;l++){if(src[b+l]/16!=line)return-1;ctl[l]=src[b+l]&15;}
  unsigned q;for(q=0;q<np;q++)if(!memcmp(m->patterns[q],ctl,64))break;
  if(q==np){if(np==16)return-1;memcpy(m->patterns[np++],ctl,64);}
  m->select[p][h]=(uint8_t)line;m->select[p][2+h]=(uint8_t)q;
 }
 m->pattern_count=np;return 0;
}
int asian_genuine_prepare_route(const uint32_t*const*sw,uint32_t nb,
 const float*const*xb,const float*const*gb,const uint32_t target[4096],
 uint32_t k,uint32_t n,fragment_map_t*m,asian_genuine_route_t*r){
 if(!sw||!xb||!gb||!target||!m||!r||!nb||!n||k>=n||((uintptr_t)m&63))return-1;
 uint16_t idx[4096];unsigned matches=0,chosen=0;
 for(unsigned b=0;b<nb;b++){uint16_t rev[4096];unsigned char seen[4096]={0};pair_t tab[4096];int ok=1;
  for(unsigned j=0;j<4096;j++){tab[j].word=sw[b][j];tab[j].index=j;}qsort(tab,4096,sizeof*tab,pc);
  for(unsigned i=1;i<4096;i++)if(tab[i-1].word==tab[i].word)ok=0;
  for(unsigned i=0;ok&&i<4096;i++){pair_t key={target[i],0},*p=bsearch(&key,tab,4096,sizeof*tab,pc);if(!p||seen[p->index]){ok=0;break;}seen[p->index]=1;rev[i]=(uint16_t)p->index;}
  if(ok){matches++;chosen=b;memcpy(idx,rev,sizeof idx);}
 }
 if(matches!=1||build(idx,m))return-1;
 m->dimension=k+1;
 r->x_base=xb[chosen];r->growth_base=gb[chosen];r->map=m;
 float w=(float)(n-k)/(float)n;memcpy(&r->weight_bits,&w,4);r->fixing_index=k;return 0;
}
