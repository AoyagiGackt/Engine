/**
 * @file ChromaticAberrationPS.hlsl
 * @brief 色収差（クロマティックアベレーション）ポストエフェクト ピクセルシェーダー
 *
 * 【原理】
 *   実際のレンズは波長ごとに屈折率が異なるため、赤・緑・青がわずかにズレて見える。
 *   これをシミュレートするため、R チャンネルは画面中心から外側へ、
 *   B チャンネルは逆方向（内側）へズラしてサンプリングする。
 *   G チャンネルはズレなし（基準）。
 *
 * 【使い方】
 *   strength = 0.0 で効果なし、0.01〜0.05 程度で自然なズレ感になる。
 *   派手にするなら 0.1 くらいまで上げてもよい。
 */

// =====================================================
// 入力構造体
// =====================================================
// FullscreenVS.hlsl から渡される入力構造体
struct PSInput {
    float4 position : SV_POSITION; ///< クリップ座標（PS では不使用）
    float2 uv       : TEXCOORD;    ///< [0,1] 範囲の UV 座標
};

// ---- 定数バッファ (b0) ----
// 定数バッファ（b0）: 色収差の強さ
cbuffer ChromaticParams : register(b0) {
    float  strength; ///< ズレの強さ（0=なし, 推奨 0.01〜0.05）
    float3 pad;      ///< 16 バイトアライン用パディング
};

// ---- テクスチャ / サンプラー ----
Texture2D<float4> gTexture : register(t0); ///< オフスクリーンに描いたシーンのテクスチャ
SamplerState      gSampler : register(s0); ///< クランプサンプラー

// =====================================================
// ピクセルシェーダー
// =====================================================
float4 main(PSInput input) : SV_TARGET
{
    float2 uv = input.uv;

    // 画面中心 (0.5, 0.5) からこのピクセルへの方向ベクトル
    // 中心に近いほど dir の長さが小さくなり、ズレ量も小さくなる（自然なグラデーション）
    float2 dir = uv - float2(0.5f, 0.5f);

    // R: 中心から外へズラす（赤が外側に滲む）
    float r = gTexture.Sample(gSampler, uv + dir * strength).r;

    // G: ズレなし（基準チャンネル）
    float g = gTexture.Sample(gSampler, uv).g;

    // B: 中心方向へズラす（青が内側に滲む）
    float b = gTexture.Sample(gSampler, uv - dir * strength).b;

    // アルファは元のテクスチャを使う
    float a = gTexture.Sample(gSampler, uv).a;

    return float4(r, g, b, a);
}
