Texture2D<float4> gScene : register(t0);
SamplerState gSampler : register(s0);

cbuffer WarpParams : register(b0)
{
    float2 gCenter; // 歪みの中心（UV）
    float gStrength; // 歪み強度（0〜1）
    float gTime;
    float gAspect; // 画面幅/高さ
    float gRadius; // 未使用（互換のため維持）
    float2 gPad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

float4 main(PSInput input) : SV_TARGET0
{
    if (gStrength <= 0.0005f)
    {
        discard; // 非アクティブ時は元の画面のまま
    }

    float2 uv = input.uv;

    // ---- 画面全体を中心へ吸い込む（カメラごと歪みに巻き込まれる感覚）----
    float2 toCenter = gCenter - uv;
    float2 zoomedUV = uv + toCenter * (gStrength * 0.10f);

    float2 d = zoomedUV - gCenter;
    d.x *= gAspect;
    float r = length(d) + 1.0e-4f;
    float2 dir = d / r;
    float2 perp = float2(-dir.y, dir.x);

    // プレイヤー/敵がいる中心付近は歪みを抜き、少し離れたところから渦を強くする
    // （台風の目のように、戦闘が見える安全地帯を確保する）
    float coreClear = smoothstep(0.0f, 0.24f, r); // 中心はほぼ0、離れると1
    float farFade = 1.0f - smoothstep(0.32f, 0.85f, r); // 遠方は緩やかに減衰
    float ringMask = coreClear * farFade;
    float s = gStrength * ringMask;

    float wave = sin(gTime * 22.0f + r * 26.0f) * 0.3f;
    float2 disp = dir * (-(0.55f + wave) * s) + perp * (0.55f * s);
    disp *= 0.22f;
    disp.x /= gAspect;

    float2 sampleUV = zoomedUV + disp;

    // 色収差（RGBで歪み量をずらす）
    float3 col;
    col.r = gScene.Sample(gSampler, sampleUV + disp * 0.9f).r;
    col.g = gScene.Sample(gSampler, sampleUV).g;
    col.b = gScene.Sample(gSampler, sampleUV - disp * 0.9f).b;

    // 薄暗い青紫の沈み込みも渦の輪に合わせる（中心＝プレイヤー周辺は明るいまま保つ）
    float darken = saturate(gStrength) * (0.10f + ringMask * 0.35f);
    col = lerp(col, col * float3(0.55f, 0.6f, 0.95f), darken);

    // 渦の輪に沿った薄い青のリム光（山なりに輪の中間で最大）
    float rim = ringMask * (1.0f - ringMask) * 4.0f;
    col += float3(0.3f, 0.55f, 1.0f) * rim * saturate(gStrength) * 0.6f;

    // 画面端に向かう薄い青のビネット（空間そのものが歪んでいる印象を全体に広げる）
    float2 edge = (uv - 0.5f) * 2.0f;
    float edgeDist = saturate(length(edge));
    col += float3(0.2f, 0.35f, 0.9f) * edgeDist * edgeDist * saturate(gStrength) * 0.25f;

    return float4(col, 1.0f);
}
