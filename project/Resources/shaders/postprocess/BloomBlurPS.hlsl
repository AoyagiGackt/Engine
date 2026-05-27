/**
 * @file BloomBlurPS.hlsl
 * @brief Bloom 第2・3パス: セパラブル 9-tap ガウシアンブラー
 *
 * direction = (1,0) → 水平パス
 * direction = (0,1) → 垂直パス
 */
struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

cbuffer BlurParams : register(b0)
{
    float2 direction; ///< (1,0) = 水平, (0,1) = 垂直
    float2 texelSize; ///< (1/width, 1/height)
};

Texture2D<float4> gSrc    : register(t0);
SamplerState      gSampler : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    // sigma ≒ 2.0 の 9-tap Gaussian 重み（正規化済み）
    static const float w[5] = {
        0.2270270f,
        0.1945946f,
        0.1216216f,
        0.0540541f,
        0.0162162f
    };

    float4 result = gSrc.Sample(gSampler, input.uv) * w[0];

    [unroll]
    for (int i = 1; i < 5; i++)
    {
        float2 off = direction * texelSize * (float) i;
        result += gSrc.Sample(gSampler, input.uv + off) * w[i];
        result += gSrc.Sample(gSampler, input.uv - off) * w[i];
    }

    return result;
}
