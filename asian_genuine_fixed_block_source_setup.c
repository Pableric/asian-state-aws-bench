#include "private/asian_genuine_fixed_block_source_diag.h"
#include "private/asian_genuine_multistrike_full_risk_diag.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
} sha256_t;

static uint32_t ror(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void sha256_transform(sha256_t *s, const unsigned char block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
        0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
        0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
        0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
        0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
        0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
        0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
        0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
        0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
    };
    uint32_t w[64];
    for (unsigned i = 0; i < 16u; ++i)
        w[i] = (uint32_t)block[4u*i] << 24 |
               (uint32_t)block[4u*i+1u] << 16 |
               (uint32_t)block[4u*i+2u] << 8 |
               (uint32_t)block[4u*i+3u];
    for (unsigned i = 16u; i < 64u; ++i) {
        const uint32_t a = ror(w[i-15u],7)^ror(w[i-15u],18)^(w[i-15u]>>3);
        const uint32_t b = ror(w[i-2u],17)^ror(w[i-2u],19)^(w[i-2u]>>10);
        w[i] = w[i-16u] + a + w[i-7u] + b;
    }
    uint32_t a=s->state[0],b=s->state[1],c=s->state[2],d=s->state[3];
    uint32_t e=s->state[4],f=s->state[5],g=s->state[6],h=s->state[7];
    for (unsigned i = 0; i < 64u; ++i) {
        const uint32_t s1=ror(e,6)^ror(e,11)^ror(e,25);
        const uint32_t ch=(e&f)^(~e&g);
        const uint32_t t1=h+s1+ch+k[i]+w[i];
        const uint32_t s0=ror(a,2)^ror(a,13)^ror(a,22);
        const uint32_t maj=(a&b)^(a&c)^(b&c);
        const uint32_t t2=s0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    s->state[0]+=a;s->state[1]+=b;s->state[2]+=c;s->state[3]+=d;
    s->state[4]+=e;s->state[5]+=f;s->state[6]+=g;s->state[7]+=h;
}

static void sha256(const void *data, size_t bytes, unsigned char out[32])
{
    sha256_t s = {{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                   0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u},0,{0},0};
    const unsigned char *p = data;
    s.bits = (uint64_t)bytes * 8u;
    while (bytes != 0u) {
        size_t take = 64u - s.used;
        if (take > bytes) take = bytes;
        memcpy(s.block+s.used,p,take);s.used+=take;p+=take;bytes-=take;
        if (s.used == 64u) { sha256_transform(&s,s.block);s.used=0u; }
    }
    s.block[s.used++] = 0x80u;
    if (s.used > 56u) {
        memset(s.block+s.used,0,64u-s.used);sha256_transform(&s,s.block);s.used=0u;
    }
    memset(s.block+s.used,0,56u-s.used);
    for (unsigned i=0;i<8u;++i)s.block[63u-i]=(unsigned char)(s.bits>>(8u*i));
    sha256_transform(&s,s.block);
    for (unsigned i=0;i<8u;++i) {
        out[4u*i]=(unsigned char)(s.state[i]>>24);
        out[4u*i+1u]=(unsigned char)(s.state[i]>>16);
        out[4u*i+2u]=(unsigned char)(s.state[i]>>8);
        out[4u*i+3u]=(unsigned char)s.state[i];
    }
}

static int fixed_request(const asian_genuine_fixed_block_source_request_t *r)
{
    return r->target_start_index == ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX &&
           r->path_count == ASIAN_GENUINE_FIXED_BLOCK_PATHS &&
           r->block_count == 1u && r->block_ordinal == 0u;
}

int asian_genuine_fixed_block_source_prepare(
    asian_genuine_fixed_block_source_context_t *out,
    const asian_genuine_fixed_block_source_request_t *r)
{
    if (out == NULL || r == NULL || ((uintptr_t)out & 63u) != 0u)
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_INVALID;
    memset(out,0,sizeof(*out));
    if (!fixed_request(r))
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BLOCK_UNSUPPORTED;
    if (r->flags != 0u || r->digital_shift != 0u)
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FEATURE_UNSUPPORTED;
    if (r->fixing_count < 2u || r->fixing_count > 256u ||
        asian_genuine_msfr_producer_fixing_count(r->fixing_count) == 0u)
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FIXINGS_UNSUPPORTED;
    if (r->sigma == 0.0)
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_SIGMA_ZERO_UNSUPPORTED;
    if (!isfinite(r->s0) || !isfinite(r->rate) ||
        !isfinite(r->dividend_yield) || !isfinite(r->sigma) ||
        !isfinite(r->maturity) || !(r->sigma > 0.0))
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_INVALID;

    asian_genuine_msfr_basis_controls_t qualified;
    const int qualified_status = asian_genuine_msfr_prepare_basis_controls(
        &qualified,r->s0,r->rate,r->dividend_yield,r->sigma,r->maturity,
        r->fixing_count);
    if (qualified_status != ASIAN_GENUINE_MSFR_OK)
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_QUALIFIED_DOMAIN;

    if (r->signed_z == NULL || r->signed_z_bytes !=
          ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES ||
        ((uintptr_t)r->signed_z & 63u) != 0u)
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_TABLE_INVALID;
    unsigned char digest[32];
    sha256(r->signed_z,r->signed_z_bytes,digest);
    if (memcmp(digest,asian_genuine_fixed_block_signed_z_sha256,32u) != 0)
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_TABLE_INVALID;

    const double dt = r->maturity/(double)r->fixing_count;
    const float drift = (float)((r->rate-r->dividend_yield-
                                0.5*r->sigma*r->sigma)*dt);
    const float diffusion = (float)(r->sigma*sqrt(dt));
    if (!isfinite(drift) || !isfinite(diffusion) || !(diffusion > 0.0f))
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_INVALID;
    out->signed_z=r->signed_z;out->drift=drift;out->diffusion=diffusion;
    out->abi_version=ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION;
    out->magic=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_MAGIC;
    return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK;
}

int asian_genuine_fixed_block_exact_x_prepare(
    asian_genuine_fixed_block_exact_x_context_t *out,
    const asian_genuine_fixed_block_source_context_t *source,
    float *prepared_x, size_t prepared_x_bytes)
{
    if (out == NULL || source == NULL || prepared_x == NULL ||
        ((uintptr_t)out & 63u) != 0u || ((uintptr_t)prepared_x & 63u) != 0u ||
        prepared_x_bytes != ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES ||
        source->magic != ASIAN_GENUINE_FIXED_BLOCK_SOURCE_MAGIC ||
        source->abi_version != ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION)
        return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_INVALID;
    memset(out,0,sizeof(*out));
    for (size_t i=0;i<ASIAN_GENUINE_FIXED_BLOCK_SOURCE_VALUES;++i)
        prepared_x[i]=fmaf(source->diffusion,source->signed_z[i],source->drift);
    out->prepared_x=prepared_x;out->magic=ASIAN_GENUINE_FIXED_BLOCK_EXACT_MAGIC;
    out->abi_version=ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION;
    return ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK;
}
