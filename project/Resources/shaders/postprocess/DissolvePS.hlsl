cbuffer DissolveParams : register(b0)
{
    float  threshold;  // 溶解進行度（0=なし, 1=完全消滅）
    float  edgeWidth;  // エッジ帯の幅
    float2 pad0;
    float4 edgeColor;  // エッジハイライト色
};

Texture2D<float4> gTexture : register(t0); // シーンカラー
Texture2D<float4> gMask    : register(t1); // ノイズマスク

SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target
{
    float mask = gMask.Sample(gSampler, input.uv).r;

    // threshold を下回るピクセルは消滅
    clip(mask - threshold);

    float4 scene = gTexture.Sample(gSampler, input.uv);

    // エッジ帯: threshold ～ threshold+edgeWidth の範囲をエッジ色でブレンド
    float edgeFactor = saturate((mask - threshold) / max(edgeWidth, 0.0001f));
    return lerp(edgeColor, scene, edgeFactor);
}
