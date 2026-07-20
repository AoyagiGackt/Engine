#include "../object3d/Object3d.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material
{
    float4 color;
    int enableLighting;
    int shadingType;
    int useCubemap;
    int useTexture;
    float4x4 uvTransform;
};
ConstantBuffer<Material> gMaterial : register(b0);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float2 uv = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform).xy;
    float4 textureColor = gMaterial.useTexture != 0
        ? gTexture.Sample(gSampler, uv)
        : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 color = gMaterial.color * textureColor;
    if (color.a <= 0.0f) {
        discard;
    }
    return color;
}
