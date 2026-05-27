/**
 * @file BloomBrightPS.hlsl
 * @brief Bloom 第1パス: 輝度が threshold を超えたピクセルのみ抽出する
 *
 * 入力: シーンテクスチャ (t0)
 * 出力: 明るい部分だけのテクスチャ（暗い部分は黒）
 */
struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

cbuffer BloomParams : register(b0)
{
    float threshold; ///< 抽出する輝度のしきい値（0.0〜1.0、デフォルト 0.7）
    float intensity; ///< Bloom の強度倍率（デフォルト 1.0）
    float2 pad;
};

Texture2D<float4> gScene  : register(t0);
SamplerState      gSampler : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    float4 col = gScene.Sample(gSampler, input.uv);

    // BT.709 輝度係数で明るさを計算
    float lum = dot(col.rgb, float3(0.2126f, 0.7152f, 0.0722f));

    // threshold を超えた分だけ bloom に寄与させる（ソフトクリップ）
    float bloom = saturate((lum - threshold) / max(1.0f - threshold, 0.001f));

    return float4(col.rgb * bloom * intensity, 1.0f);
}
