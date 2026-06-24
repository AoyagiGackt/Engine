// ACES フィルミックトーンマッピング + ガンマ補正

Texture2D<float4> gHDRTexture : register(t0);
SamplerState      gSampler     : register(s0);

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

cbuffer TonemapParams : register(b0) {
    float gExposure; // 露出倍率（デフォルト 1.0）
    float gGamma;    // ガンマ値（デフォルト 2.2）
    float2 gPad;
}

// ACES フィルミック近似 (Stephen Hill)
float3 ACESFilm(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(VSOutput input) : SV_TARGET
{
    float3 hdr = gHDRTexture.Sample(gSampler, input.uv).rgb;
    hdr *= gExposure;
    float3 ldr = ACESFilm(hdr);
    ldr = pow(saturate(ldr), 1.0f / gGamma);
    return float4(ldr, 1.0f);
}
