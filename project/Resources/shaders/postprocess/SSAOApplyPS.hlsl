// SSAO Apply PS: AO テクスチャを乗算ブレンドでシーンに適用する
// ブレンド設定: SrcBlend=DEST_COLOR, DestBlend=ZERO (multiply)
Texture2D<float4> gAO : register(t0);
SamplerState gSampler : register(s0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 main(VSOut input) : SV_TARGET
{
    float ao = gAO.Sample(gSampler, input.texcoord).r;
    return float4(ao, ao, ao, 1.0f);
}
