#include "cnn_sha256.h"
#include <string.h>

typedef struct {
    u32 state[8];
    u64 bits;
    u8 block[64];
    u32 used;
} sha256_ctx_t;

static const u32 k256[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};

#define ROR(x,n) (((x) >> (n)) | ((x) << (32U-(n))))

static void sha256_transform(sha256_ctx_t *ctx, const u8 *p)
{
    u32 w[64];
    u32 a,b,c,d,e,f,g,h,t1,t2;
    unsigned int i;

    for (i=0;i<16U;++i)
        w[i]=((u32)p[i*4U]<<24)|((u32)p[i*4U+1U]<<16)|((u32)p[i*4U+2U]<<8)|p[i*4U+3U];
    for (i=16U;i<64U;++i) {
        u32 s0=ROR(w[i-15U],7)^ROR(w[i-15U],18)^(w[i-15U]>>3);
        u32 s1=ROR(w[i-2U],17)^ROR(w[i-2U],19)^(w[i-2U]>>10);
        w[i]=w[i-16U]+s0+w[i-7U]+s1;
    }
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (i=0;i<64U;++i) {
        u32 s1=ROR(e,6)^ROR(e,11)^ROR(e,25);
        u32 ch=(e&f)^((~e)&g);
        u32 s0=ROR(a,2)^ROR(a,13)^ROR(a,22);
        u32 maj=(a&b)^(a&c)^(b&c);
        t1=h+s1+ch+k256[i]+w[i];
        t2=s0+maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void sha256_init(sha256_ctx_t *ctx)
{
    static const u32 initial[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                 0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    memcpy(ctx->state, initial, sizeof(initial));
    ctx->bits=0; ctx->used=0;
}

static void sha256_update(sha256_ctx_t *ctx, const u8 *data, u32 length)
{
    while (length != 0U) {
        u32 room=64U-ctx->used;
        u32 take=(length<room)?length:room;
        memcpy(ctx->block+ctx->used,data,take);
        ctx->used+=take; data+=take; length-=take; ctx->bits+=(u64)take*8U;
        if (ctx->used==64U) { sha256_transform(ctx,ctx->block); ctx->used=0; }
    }
}

static void sha256_final(sha256_ctx_t *ctx, u8 digest[32])
{
    u64 bits=ctx->bits;
    unsigned int i;
    ctx->block[ctx->used++]=0x80U;
    if (ctx->used>56U) {
        while(ctx->used<64U) ctx->block[ctx->used++]=0;
        sha256_transform(ctx,ctx->block); ctx->used=0;
    }
    while(ctx->used<56U) ctx->block[ctx->used++]=0;
    for(i=0;i<8U;++i) ctx->block[63U-i]=(u8)(bits>>(i*8U));
    sha256_transform(ctx,ctx->block);
    for(i=0;i<8U;++i) {
        digest[i*4U]=(u8)(ctx->state[i]>>24);
        digest[i*4U+1U]=(u8)(ctx->state[i]>>16);
        digest[i*4U+2U]=(u8)(ctx->state[i]>>8);
        digest[i*4U+3U]=(u8)ctx->state[i];
    }
}

void cnn_sha256(const void *data, u32 length, u8 digest[32])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx,(const u8 *)data,length);
    sha256_final(&ctx,digest);
}

int cnn_sha256_equal(const u8 left[32], const u8 right[32])
{
    u8 different=0;
    unsigned int i;
    for(i=0;i<32U;++i) different|=(u8)(left[i]^right[i]);
    return different==0U;
}
