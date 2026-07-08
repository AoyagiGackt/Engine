/**
 * @file KnightEnemy.h
 * @brief 剣を持つナイト型の敵撃破後は灰色に凍結し、専用キー入力で武器を吸収できる
 */
#pragma once
#include "CollisionConfig.h"
#include "MakeAffine.h"
#include "Model.h"
#include "Object3d.h"
#include "ParticleManager.h"
#include <memory>
namespace engine::graphics { class ModelCommon; }

namespace engine::game {
using engine::AABB;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::ParticleManager;

/**
 * @brief 剣を持つナイト型の敵
 * @note ボーンなしの静的メッシュ（剣も剛体アタッチ）。KnightCharacter.fbx はボーン付きだが
 *       エンジンのAssimpビルドがFBXインポーターを含まないため、色付けのみ済んだOBJ版を使う。
 *       派手なテレポート斬りのバージルではなく、抑制的で静かな立ち回りのDMC5 Vを意識した動き。
 */
class KnightEnemy {
public:
    void Initialize(ModelCommon* modelCommon, const Vector3& spawnPos);

    /** @brief AI（Idle/Telegraph/Dash/Recover）または吸収演出を1フレーム進める */
    void Update(ParticleManager* pm, const Vector3& playerPos);
    void Draw();

    /** @brief ダメージを与える生存中のみ有効撃破すると凍結状態(Defeated)へ遷移する */
    void TakeDamage(int damage);

    /** @brief 通常行動中（攻撃で倒せる状態）か */
    bool IsAlive() const;
    /** @brief 撃破後、武器を奪われるのを待っている（灰色で静止）状態か */
    bool IsAwaitingSteal() const { return state_ == State::Defeated; }
    /** @brief 吸収演出が完全に終わり消滅したか */
    bool IsConsumed() const { return state_ == State::Consumed; }
    /** @brief 吸収が完了した瞬間のフレームだけ true（武器付与などのフックに使う） */
    bool JustAbsorbed() const { return justAbsorbed_; }

    /**
     * @brief 武器の吸収を開始するIsAwaitingSteal() が true の時だけ受理する
     * @return 受理して開始したら true（呼び出し側はここでプレイヤーの刺突モーション等を再生する）
     */
    bool TryBeginAbsorb();

    Vector3 GetPosition() const { return pos_; }
    AABB    GetAABB()     const {
        return { { pos_.x - 0.5f, pos_.y - 0.5f, -0.5f },
                 { pos_.x + 0.5f, pos_.y + 1.0f,  0.5f } };
    }

private:
    enum class State { Idle, Telegraph, Dash, Recover, Defeated, Absorbing, Consumed };

    void UpdateAI(ParticleManager* pm, const Vector3& playerPos);
    void UpdateAbsorb(ParticleManager* pm, const Vector3& playerPos);
    void ApplyTransforms();

    static constexpr int kMaxHp = 3;

    State   state_      = State::Idle;
    float   stateTimer_ = 0.0f;
    int     hp_         = kMaxHp;

    Vector3 pos_        = {};
    float   yaw_        = 0.0f;
    Vector3 dashStart_  = {};
    Vector3 dashTarget_ = {};

    float   swordSwing_       = 0.0f; ///< 剣の振り角（ラジアン、状態に応じてlerpで追従）
    float   hitFlash_         = 0.0f; ///< 被弾時に白く光らせる残り秒数
    bool    justAbsorbed_     = false;

    std::unique_ptr<Model>    model_;
    std::unique_ptr<Object3d> object_;
    std::unique_ptr<Model>    swordModel_;
    std::unique_ptr<Object3d> swordObject_;
};

} // namespace engine::game
