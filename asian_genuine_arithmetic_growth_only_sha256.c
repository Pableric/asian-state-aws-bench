#define _POSIX_C_SOURCE 200809L
#include "private/asian_genuine_arithmetic_growth_only_sha256.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint32_t state[8];uint64_t bits;unsigned char block[64];size_t used;
} ago_sha256_t;

static uint32_t rotate(uint32_t value,unsigned bits)
{ return (value>>bits)|(value<<(32u-bits)); }

static void transform(ago_sha256_t *hash,const unsigned char input[64])
{
    static const uint32_t k[64]={
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    uint32_t w[64];for(uint32_t i=0;i<16u;++i)w[i]=(uint32_t)input[4u*i]<<24|(uint32_t)input[4u*i+1u]<<16|(uint32_t)input[4u*i+2u]<<8|input[4u*i+3u];
    for(uint32_t i=16u;i<64u;++i){uint32_t a=rotate(w[i-15u],7)^rotate(w[i-15u],18)^(w[i-15u]>>3);uint32_t b=rotate(w[i-2u],17)^rotate(w[i-2u],19)^(w[i-2u]>>10);w[i]=w[i-16u]+a+w[i-7u]+b;}
    uint32_t a=hash->state[0],b=hash->state[1],c=hash->state[2],d=hash->state[3],e=hash->state[4],f=hash->state[5],g=hash->state[6],h=hash->state[7];
    for(uint32_t i=0;i<64u;++i){uint32_t s1=rotate(e,6)^rotate(e,11)^rotate(e,25),ch=(e&f)^(~e&g),t1=h+s1+ch+k[i]+w[i],s0=rotate(a,2)^rotate(a,13)^rotate(a,22),maj=(a&b)^(a&c)^(b&c),t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
    hash->state[0]+=a;hash->state[1]+=b;hash->state[2]+=c;hash->state[3]+=d;hash->state[4]+=e;hash->state[5]+=f;hash->state[6]+=g;hash->state[7]+=h;
}

static void initialize(ago_sha256_t *hash)
{
    static const uint32_t state[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memset(hash,0,sizeof(*hash));memcpy(hash->state,state,sizeof(state));
}

static void add(ago_sha256_t *hash,const void *data,size_t bytes)
{
    const unsigned char *input=data;hash->bits+=(uint64_t)bytes*8u;
    while(bytes){size_t take=64u-hash->used;if(take>bytes)take=bytes;memcpy(hash->block+hash->used,input,take);hash->used+=take;input+=take;bytes-=take;if(hash->used==64u){transform(hash,hash->block);hash->used=0;}}
}

static void finish(ago_sha256_t *hash,char output[65])
{
    hash->block[hash->used++]=0x80u;if(hash->used>56u){memset(hash->block+hash->used,0,64u-hash->used);transform(hash,hash->block);hash->used=0;}
    memset(hash->block+hash->used,0,56u-hash->used);for(uint32_t i=0;i<8u;++i)hash->block[63u-i]=(unsigned char)(hash->bits>>(8u*i));transform(hash,hash->block);
    for(uint32_t i=0;i<8u;++i)sprintf(output+8u*i,"%08"PRIx32,hash->state[i]);
    output[64]=0;
}

int asian_genuine_arithmetic_growth_only_file_sha256(const char *path,char output[65])
{
    FILE *file=fopen(path,"rb");if(file==NULL)return-1;ago_sha256_t hash;initialize(&hash);unsigned char buffer[16384];size_t got;
    while((got=fread(buffer,1,sizeof(buffer),file))!=0u)add(&hash,buffer,got);
    int failed=ferror(file);fclose(file);if(failed)return-1;finish(&hash,output);return 0;
}

int asian_genuine_arithmetic_growth_only_memory_sha256(const void *data,
                                                       size_t bytes,
                                                       char output[65])
{
    if(data==NULL||output==NULL)return-1;
    ago_sha256_t hash;initialize(&hash);add(&hash,data,bytes);finish(&hash,output);
    return 0;
}

int asian_genuine_arithmetic_growth_only_self_sha256(char output[65])
{
    char path[4096];ssize_t bytes=readlink("/proc/self/exe",path,sizeof(path)-1u);if(bytes<0)return-1;path[bytes]=0;
    return asian_genuine_arithmetic_growth_only_file_sha256(path,output);
}
