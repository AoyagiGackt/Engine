Texture2D<float4> gColor  : register(t0);
Texture2D<float>  gDepth  : register(t1);
SamplerState      gSampler: register(s0);

cbuffer MotionBlurParams : register(b0) {
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj;
    float    gStrength;
    int      gNumSamples;
    float2   _pad;
}

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };

float4 main(VSOut i) : SV_TARGET
{
    float depth = gDepth.Sample(gSampler, i.uv).r;

    float4 ndc   = float4(i.uv.x * 2.0f - 1.0f, (1.0f - i.uv.y) * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(ndc, gInvViewProj);
    world /= world.w;

    float4 prevNdc = mul(world, gPrevViewProj);
    prevNdc /= prevNdc.w;
    float2 prevUV = float2(prevNdc.x * 0.5f + 0.5f, 1.0f - (prevNdc.y * 0.5f + 0.5f));

    float2 velocity = (i.uv - prevUV) * gStrength;
    float2 step     = velocity / float(gNumSamples);

    float4 result = float4(0, 0, 0, 0);
    float2 uv     = i.uv;

    [unroll(8)]
    for (int s = 0; s < gNumSamples; ++s) {
        result += gColor.Sample(gSampler, saturate(uv));
        uv -= step;
    }
    return result / float(gNumSamples);
}
