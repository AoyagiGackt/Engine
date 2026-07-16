/**
 * @file AfterImageRenderer.h
 * @brief プレイヤーの移動・攻撃時に残像を生成・描画するクラス
 */
#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <array>
#include <memory>
namespace engine::game {
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;

/**
 * @brief プレイヤーの残像エフェクトを管理するクラス
 * @note 一定間隔で過去のポジション・姿勢をリングバッファに記録し、
 *       アルファ値を減衰させながら同一モデルで重ね描画する
 */
class AfterImageRenderer {
public:
    /**
     * @brief 初期化描画に使うモデルを設定する
     * @param modelCommon 描画共通設定
     * @param model       残像として描画するモデル（プレイヤーと同一のものを渡す）
     * @param scale       描画スケール（プレイヤー本体と同じ値を渡す）
     */
    void Initialize(ModelCommon* modelCommon, Model* model, float scale = 1.0f);

    /**
     * @brief 残像モデルを差し替える（覚醒フォーム切り替え用）
     * @param model 新しく残像として描画するモデル
     * @param scale 新モデルの描画スケール
     * @note 旧フォームの残り残像は新モデルで描かれると不自然なため全消去する
     */
    void SetModel(Model* model, float scale);

    /**
     * @brief 毎フレーム残像を更新する
     * @param active 残像を生成するか（false のとき新規スポーンしない）
     * @param dense  乱舞中など高頻度スポーンモード（true で kFastInterval を使用）
     * @param pos    現在のワールド座標
     * @param yaw    左右回転（ラジアン）
     * @param spinZ  Z 軸スピン（ラジアン）
     */
    void Update(bool active, bool dense, const Vector3& pos, float yaw, float spinZ);

    /** @brief 全残像を描画する（半透明のため通常の Draw() の後に呼ぶ） */
    void Draw();

private:
    struct AfterImage {
        Vector3 pos;
        float yaw;
        float spinZ;
        float alpha;
    };

    static constexpr int kMaxImages = 10;
    static constexpr float kFastInterval = 0.03f;
    static constexpr float kSlowInterval = 0.05f;

    std::array<AfterImage, kMaxImages> images_ { };
    int idx_ = 0;
    float timer_ = 0.0f;

    std::unique_ptr<Object3d> object_;
};

} // namespace engine::game
