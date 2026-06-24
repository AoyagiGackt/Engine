// SSAO Blur PS: 4x4 ボックスブラー
Texture2D<float4> gSSAO    : register(t0);
SamplerState      gSampler : register(s0);

cbuffer BlurParams : register(b0)
{
    float gTexW;
    float gTexH;
    float gPad[2];
}

struct VSOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(VSOut input) : SV_TARGET
{
    float2 texel = float2(1.0f / gTexW, 1.0f / gTexH);
    float ao = 0.0f;
    [unroll]
    for (int x = -1; x <= 2; ++x)
    {
        [unroll]
        for (int y = -1; y <= 2; ++y)
        {
            ao += gSSAO.Sample(gSampler, input.texcoord + float2(x, y) * texel).r;
        }
    }
    ao /= 16.0f;
    return float4(ao, ao, ao, 1.0f);
}
