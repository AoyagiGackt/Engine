Texture2D<float4> gCurrent : register(t0);
Texture2D<float4> gHistory : register(t1);
SamplerState      gSampler : register(s0);

cbuffer TAAParams : register(b0) {
    float2 gJitter;
    float  gBlendAlpha;
    float  _pad;
}

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };

float4 main(VSOut i) : SV_TARGET
{
    float2 uv  = i.uv;
    float4 cur = gCurrent.Sample(gSampler, uv);
    float4 his = gHistory.Sample(gSampler, uv);

    float4 minC = cur, maxC = cur;
    float2 ts = float2(1.0f / 1280.0f, 1.0f / 720.0f);
    [unroll] for (int dx = -1; dx <= 1; dx++) {
        [unroll] for (int dy = -1; dy <= 1; dy++) {
            float4 s = gCurrent.Sample(gSampler, uv + float2(dx, dy) * ts);
            minC = min(minC, s);
            maxC = max(maxC, s);
        }
    }
    his = clamp(his, minC, maxC);
    return lerp(his, cur, gBlendAlpha);
}
