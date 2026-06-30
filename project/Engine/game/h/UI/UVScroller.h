/**
 * @file UVScroller.h
 * @brief テクスチャ UV スクロールヘルパー（ヘッダーオンリー）
 *
 * 【概要】
 *   Object3d::SetUVTransform() に渡すオフセット行列を毎フレーム更新する軽量なヘルパー。
 *   水の流れ、炎のゆらぎ、コンベアベルト、背景のパノラマ移動などに使える。
 *   新しいシェーダーは不要（Object3d の uvTransform をそのまま利用する）。
 *
 * 【使い方】
 *   // 宣言（メンバ変数として持つ）
 *   UVScroller waterScroller_;
 *
 *   // 初期化
 *   waterScroller_.speed = { 0.2f, 0.05f }; // 横に 0.2、縦に 0.05 UV/秒でスクロール
 *
 *   // Update() 内
 *   waterScroller_.Update(dt);
 *   waterObject_.SetUVTransform(waterScroller_.GetUVTransform());
 *
 *   // リセット
 *   waterScroller_.Reset(); // offset をゼロに戻す
 */
#pragma once
#include "MakeAffine.h"
#include <cmath>
namespace engine::game {
struct UVScroller {
    // ---- メンバ変数 ----
    Vector2 speed  = {};  ///< スクロール速度（UV 座標単位 / 秒）
    Vector2 offset = {};  ///< 現在の累積オフセット（[0, 1) 内に正規化される）

    // ---- メンバ関数 ----
    /**
     * @brief 毎フレーム呼び出してオフセットを更新する
     * @param dt デルタタイム（秒）
     */
    void Update(float dt)
    {
        offset.x += speed.x * dt;
        offset.y += speed.y * dt;

        // [0, 1) の範囲に収める（テクスチャのタイリングに対応）
        // floorf を使って負のスクロールにも対応
        offset.x -= std::floorf(offset.x);
        offset.y -= std::floorf(offset.y);
    }

    /// @brief 累積オフセットをゼロにリセットする
    void Reset() { offset = {}; }

    /**
     * @brief Object3d::SetUVTransform() に渡す UV 変換行列を返す
     * @return 現在のオフセットを平行移動成分に持つ4×4単位行列
     * @note UVTransform は行列の [3][0]、[3][1] が U、V オフセットに対応する
     */
    Matrix4x4 GetUVTransform() const
    {
        Matrix4x4 mat = MakeIdentity4x4();
        mat.m[3][0]   = offset.x; // U オフセット
        mat.m[3][1]   = offset.y; // V オフセット
        return mat;
    }
};

} // namespace engine::game
