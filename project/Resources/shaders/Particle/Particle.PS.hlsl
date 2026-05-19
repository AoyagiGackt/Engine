#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t1);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV 中心 (0.5, 0.5) からの距離で円形マスク
    float2 uv = input.texcoord - float2(0.5f, 0.5f);
    float dist = length(uv) * 2.0f; // 0=中心, 1=エッジ
    if (dist > 1.0f) discard;

    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    output.color = input.color * textureColor;

    // エッジをソフトにフェード
    output.color.a *= 1.0f - smoothstep(0.6f, 1.0f, dist);

    if (output.color.a == 0.0f) discard;

    return output;
}