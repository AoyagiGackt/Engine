/**
 * @file BloomCombinePS.hlsl
 * @brief Bloom 第4パス: シーン + Bloom テクスチャを加算合成してバックバッファへ出力
 *
 * gScene (t0): 元のシーンテクスチャ
 * gBloom (t1): ブラーされた Bloom テクスチャ
 */
struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

Texture2D<float4> gScene  : register(t0);
Texture2D<float4> gBloom  : register(t1);
SamplerState      gSampler : register(s0);

float4 main(PSInput input) : SV_TARGET
{
    float4 scene = gScene.Sample(gSampler, input.uv);
    float4 bloom = gBloom.Sample(gSampler, input.uv);

    // 加算合成（Bloom は発光エネルギーの追加分として扱う）
    return float4(saturate(scene.rgb + bloom.rgb), scene.a);
}
