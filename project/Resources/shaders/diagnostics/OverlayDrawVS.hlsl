struct VSIn  { float3 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };
cbuffer VP : register(b0) { float4x4 gViewProjection; }
VSOut main(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.pos, 1.0f), gViewProjection);
    o.col = v.col;
    return o;
}
