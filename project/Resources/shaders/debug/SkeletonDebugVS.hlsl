cbuffer DebugCB : register(b0)
{
    float4x4 WVP;
    float4   color;
};

struct VSIn
{
    float4 pos : POSITION;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

VSOut main(VSIn v)
{
    VSOut o;
    o.pos = mul(v.pos, WVP);
    o.col = color;
    return o;
}
