Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PieceParams : register(b0)
{
    float4x4 WVP;
    float4x4 World;
    float4 color; // rgb=ティント a=不透明度
    float4 glow; // rgb=断面色 w=発光強度
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float cap : TEXCOORD1;
};

struct PSOutput
{
    float4 color : SV_TARGET0;
};

PSOutput main(PSInput input)
{
    PSOutput output;

    float4 tex = gTexture.Sample(gSampler, input.texcoord);

    // 固定方向の簡易ライティングで破片に立体感を付ける
    float ndl = saturate(dot(normalize(input.normal), normalize(float3(-0.4f, 0.85f, -0.5f))));
    float3 body = tex.rgb * color.rgb * (0.45f + 0.55f * ndl);

    // 切断面は発光色で塗りつぶす
    float3 capColor = glow.rgb * (0.35f + glow.w);
    float3 result = lerp(body, capColor, saturate(input.cap));

    output.color = float4(result, color.a);
    if (output.color.a <= 0.001f)
    {
        discard;
    }
    return output;
}
