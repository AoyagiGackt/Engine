/**
 * @file EnemyEntity.h
 * @brief ローグライトの戦闘で使用する汎用敵エンティティを定義するファイル
 */
#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <memory>
namespace engine::game {
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;

/**
 * @brief ローグライト戦闘シーンで使用する汎用の敵エンティティクラス
 * @note HP 管理・打ち上げ物理・撃破判定を提供する。
 * SetMaxHp() で種別（Normal/Elite/Boss）ごとの HP を設定してから使用すること
 */
class EnemyEntity {
public:
    /**
     * @brief 初期化。モデル生成とワールド座標を設定する
     * @param modelCommon モデル共通設定
     * @param startPos    初期ワールド座標
     */
    void Initialize(ModelCommon* modelCommon, const Vector3& startPos);

    /** @brief 物理・アニメーションを毎フレーム更新する */
    void Update();

    /** @brief モデルを描画する */
    void Draw();

    /**
     * @brief 上方向の初速を与えて打ち上げる
     * @param velY 上方向速度（正の値）
     */
    void Launch(float velY);

    /**
     * @brief ダメージを与える。HP が 0 以下になると撃破状態になる
     * @param dmg 与えるダメージ量（デフォルト 1）
     */
    void TakeDamage(int dmg = 1) {
        if (defeated_) { return; }
        hp_ -= dmg;
        if (hp_ <= 0) { hp_ = 0; defeated_ = true; }
    }

    /**
     * @brief 最大 HP を設定し、現在 HP をリセットする
     * @param v 設定する最大 HP 値
     */
    void SetMaxHp(int v)   { maxHp_ = v; hp_ = v; defeated_ = false; }

    /** @brief 撃破済みかどうかを返す */
    bool IsDefeated() const { return defeated_; }
    /** @brief 現在の HP を返す */
    int  GetHp()      const { return hp_; }
    /** @brief 最大 HP を返す */
    int  GetMaxHp()   const { return maxHp_; }

    /**
     * @brief 本体モデルの表示/非表示を切り替える（切断演出中に非表示にする用）
     * @param visible 表示するなら true
     */
    void SetVisible(bool visible) { visible_ = visible; }
    /** @brief 本体モデルが表示中かどうか */
    bool IsVisible() const { return visible_; }

    /** @brief 切断演出などが参照するモデルを返す */
    Model* GetModel() const { return model_.get(); }

    /** @brief このフレームに着地したか */
    bool           JustLanded()  const { return justLanded_; }
    /** @brief 打ち上げ中かどうか */
    bool           IsLaunched()  const { return isLaunched_; }
    /** @brief 現在のワールド座標を返す */
    const Vector3& GetPosition() const { return pos_; }

private:
    static constexpr float kGroundY_  = 0.4f;
    static constexpr float kCeilingY_ = 12.5f;
    static constexpr float kGravity_  = 0.015f;

    std::unique_ptr<Model>    model_;
    std::unique_ptr<Object3d> object_;

    int     maxHp_      = 20;
    int     hp_         = 20;
    bool    defeated_   = false;
    bool    visible_    = true;

    Vector3 pos_        = {};
    float   velY_       = 0.0f;
    bool    isLaunched_ = false;
    bool    justLanded_ = false;
};

} // namespace engine::game
