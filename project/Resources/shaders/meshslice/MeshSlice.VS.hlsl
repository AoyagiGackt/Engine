cbuffer PieceParams : register(b0)
{
    float4x4 WVP;
    float4x4 World;
    float4 color; // rgb=ティント a=不透明度
    float4 glow; // rgb=断面色 w=発光強度
};

struct VSInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float cap : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float cap : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(input.position, WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) World));
    output.cap = input.cap;
    return output;
}
