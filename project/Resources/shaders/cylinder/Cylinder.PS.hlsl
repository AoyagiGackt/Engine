Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer TransformMatrix : register(b0)
{
    float4x4 WVP;
    float4 color;
    float alphaReference;
    float3 _pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

struct PSOutput
{
    float4 color : SV_TARGET0;
};

PSOutput main(PSInput input)
{
    PSOutput output;
    float2 uv = input.texcoord;
    uv.y = 1.0f - uv.y; // flip V
    float4 tex = gTexture.Sample(gSampler, uv);
    output.color = input.color * tex;
    if (output.color.a <= alphaReference)
        discard;
    return output;
}
