cbuffer TransformMatrix : register(b0)
{
    float4x4 WVP;
    float4 color;
};

struct VSInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(input.position, WVP);
    output.texcoord = input.texcoord;
    output.color = color;
    return output;
}
