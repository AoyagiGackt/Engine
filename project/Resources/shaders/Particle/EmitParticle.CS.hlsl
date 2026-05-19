// EmitParticle.CS.hlsl

struct GPUParticleState
{
    float3 position;
    float  lifeTime;
    float3 velocity;
    float  currentTime;
    float4 color;
    float3 scale;
    float  rotateZ;
    uint   alive;
    uint   curveFlag;
    float2 pad;
};

cbuffer CBEmitter : register(b0)
{
    float3 gTranslate;     //  0
    float  gRadius;        // 12 -> 16
    uint   gCount;         // 16
    float  gFrequency;     // 20
    float  gFrequencyTime; // 24
    uint   gEmit;          // 28 -> 32
    float  gLifeTime;      // 32
    uint   gSeed;          // 36
    uint   gTime;          // 40
    float  gPad;           // 44 -> 48
    float4 gColor;         // 48 -> 64
};

RWStructuredBuffer<GPUParticleState> gParticles : register(u0);

// -------------------------------------------------------
//  xoshiro128+ (32-bit output, 128-bit state)
// -------------------------------------------------------

uint rotl32(uint x, int k)
{
    return (x << k) | (x >> (32 - k));
}

// splitmix32 ハッシュで各スレッド独立の初期状態を作る
uint4 xoshiroSeed(uint seed, uint time, uint idx)
{
    uint a = seed ^ (idx * 2654435761u);
    uint b = time ^ (idx * 2246822519u);
    a ^= a >> 16; a *= 0x45d9f3bu; a ^= a >> 16;
    b ^= b >> 16; b *= 0x45d9f3bu; b ^= b >> 16;
    return uint4(a, b, a ^ 0x9e3779b9u, b ^ 0x6c62272eu);
}

uint xoshiroNext(inout uint4 s)
{
    uint result = s.x + s.w;
    uint t = s.y << 9;
    s.z ^= s.x;
    s.w ^= s.y;
    s.y ^= s.z;
    s.x ^= s.w;
    s.z ^= t;
    s.w = rotl32(s.w, 11);
    return result;
}

// [0, 1)
float rand01(inout uint4 s)
{
    return float(xoshiroNext(s) >> 8) * (1.0f / 16777216.0f);
}

// -------------------------------------------------------

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    for (uint i = 0; i < gCount; ++i)
    {
        uint4 rng = xoshiroSeed(gSeed, gTime, i);

        float angle  = rand01(rng) * 6.28318530f;
        float speed  = 0.5f + rand01(rng) * 1.5f;      // [0.5, 2.0]
        float r      = rand01(rng) * gRadius;           // [0, radius]
        float scaleV = 0.15f + rand01(rng) * 0.3f;     // [0.15, 0.45]

        float3 dir = float3(cos(angle), sin(angle), 0.0f);

        gParticles[i].position    = gTranslate + dir * r;
        gParticles[i].velocity    = dir * speed;
        gParticles[i].scale       = float3(scaleV, scaleV, scaleV);
        gParticles[i].color       = gColor;
        gParticles[i].lifeTime    = gLifeTime;
        gParticles[i].currentTime = 0.0f;
        gParticles[i].rotateZ     = 0.0f;
        gParticles[i].alive       = 1;
        gParticles[i].curveFlag   = 0;
        gParticles[i].pad         = float2(0.0f, 0.0f);
    }
}
