cbuffer SceneParams : register(b0)
{
    float4x4 viewProj;
};

struct VSInput
{
    float4 position : POSITION0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(input.position, viewProj);
    output.color = input.color;
    return output;
}
