/**
 * @file OutlineVS.hlsl
 * @brief 2パスアウトライン描画の第2パス用 頂点シェーダー
 *
 * 【2パスアウトライン手法の流れ】
 *   Pass1（通常描画）: 普通に Draw() する
 *   Pass2（このシェーダー）:
 *     - 前面カリング（CULL_FRONT）で描画する
 *     - 頂点を法線方向にクリップ空間で押し出す
 *     - 結果としてオブジェクトの外側だけがアウトライン色で塗られる
 *
 * 【クリップ空間での法線押し出し】
 *   ビュー空間ではなくクリップ空間（WVP適用後）で法線を押し出すことで、
 *   遠近感に関わらず画面上で一定幅のアウトラインを描ける
 *   ただし pos.w を乗算して透視除算の影響を補正する必要がある
 */

// =====================================================
// 定数バッファ
// =====================================================
// アウトラインのパラメータ（PS と共有、register b0）
struct OutlineParams {
    float4 color; ///< アウトラインの色（RGBA）
    float  width; ///< 画面上でのアウトライン幅（推奨 0.01〜0.05）
    float3 pad;   ///< 16 バイトアライン用パディング
};
ConstantBuffer<OutlineParams> gOutline : register(b0);

// ---- 変換行列 (b1) ----
// ワールド・ビュー・プロジェクション行列（register b1）
// Object3d の TransformationMatrix と同じレイアウト
struct TransformationMatrix {
    float4x4 WVP;                   ///< ワールド × ビュー × プロジェクション
    float4x4 World;                 ///< ワールド行列（このシェーダーでは未使用）
    float4x4 WorldInverseTranspose; ///< 法線変換用逆転置行列（このシェーダーでは未使用）
    float4x4 LightVP;               ///< ライト空間行列（このシェーダーでは未使用）
};
ConstantBuffer<TransformationMatrix> gTransform : register(b1);

// =====================================================
// 頂点入力構造体
// =====================================================
// 頂点入力（Model::VertexData と一致させること）
struct VertexData {
    float4 position : POSITION; ///< ローカル空間の頂点座標（W=1.0）
    float2 texcoord : TEXCOORD; ///< UV 座標（このシェーダーでは未使用）
    float3 normal   : NORMAL;   ///< ローカル空間の法線ベクトル
};

// =====================================================
// 頂点シェーダー
// =====================================================
float4 main(VertexData input) : SV_POSITION
{
    // 頂点をクリップ空間へ変換
    float4 pos = mul(input.position, gTransform.WVP);

    // 法線をクリップ空間へ変換（方向ベクトルなので w=0 で変換）
    float4 normalClip = mul(float4(input.normal, 0.0f), gTransform.WVP);

    // クリップ空間での法線方向を XY 平面に正規化（2D 方向ベクトル）
    float2 dir = normalize(normalClip.xy);

    // 頂点を外側へ押し出す
    // pos.w を掛けることで透視除算後も一定ピクセル幅のアウトラインを維持する
    pos.xy += dir * gOutline.width * pos.w;

    return pos;
}
