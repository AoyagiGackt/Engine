/**
 * @file KnightEnemy.h
 * @brief 剣を持つナイト型の敵撃破後は灰色に凍結し、専用キー入力で武器を吸収できる
 */
#pragma once
#include "Animation.h"
#include "CollisionConfig.h"
#include "IEnemyEntity.h"
#include "MakeAffine.h"
#include "Model.h"
#include "Object3d.h"
#include "ParticleManager.h"
#include "SkinCommon.h"
#include "SkinnedModel.h"
#include "SkinnedObject3d.h"
#include <memory>
#include <vector>
namespace engine::graphics {
class ModelCommon;
}

namespace engine::game {
using engine::AABB;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::ParticleManager;
using engine::graphics::SkinCommon;
using engine::graphics::SkinnedModel;
using engine::graphics::SkinnedObject3d;

/**
 * @brief 剣を持つナイト型の敵
 * @note ボーンなしの静的メッシュ（剣も剛体アタッチ）。KnightCharacter.fbx はボーン付きだが
 *       エンジンのAssimpビルドがFBXインポーターを含まないため、色付けのみ済んだOBJ版を使う。
 *       派手なテレポート斬りではなく、抑制的で静かな立ち回りを意識した動き。
 */
class KnightEnemy : public IEnemyEntity {
public:
    void Initialize(ModelCommon* modelCommon, const Vector3& spawnPos);

    /** @brief AI（Idle/Telegraph/Dash/Recover）または吸収演出を1フレーム進める */
    void Update(ParticleManager* pm, const Vector3& playerPos);
    void Draw();

    /**
     * @brief 見た目のトランスフォーム行列だけを再計算する（AIは一切進めない）
     * @note ステージエディタ中、カメラだけ動く状況で呼ばないと古いカメラ行列のまま
     * 描画されて画面に張り付いて見える（Player::RefreshVisualTransformsと同じ理由）
     */
    void RefreshVisualTransforms() override;

    /**
     * @brief ステージ上のsolidブロックとの当たり判定を解決する（Update()の後に毎フレーム呼ぶ）
     * @param blocks StageEditor::GetSolidColliders()等で得たワールドAABB一覧
     * @note ナイトはジャンプしない（常にkGroundY固定）ため、横から当たったら押し出すだけで良い
     */
    void ResolveBlockCollision(const std::vector<AABB>& blocks);

    /**
     * @brief ダメージを与える生存中のみ有効撃破すると凍結状態(Defeated)へ遷移する
     * @param knockDirX ノックバックの方向（+1/-1想定、プレイヤーの向き等）
     * @param knockY    垂直ノックバック初速（MeleeAttackDef::knockY等。打ち上げ技ほど大きい値を渡す）
     * @note 与えた分は毎フレーム重力で減衰し、地面(kGroundY)で自然に着地する（Update()参照）
     */
    void TakeDamage(int damage, float knockDirX = 0.0f, float knockY = 0.09f);

    /** @brief 通常行動中（攻撃で倒せる状態）か */
    bool IsAlive() const;
    /** @brief 現在のHPを返す */
    int GetHp() const override { return hp_; }
    /** @brief 最大HPを返す */
    int GetMaxHp() const override { return kMaxHp; }
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

    Vector3 GetPosition() const override { return pos_; }
    /** @brief StageEditorのギズモドラッグ等、外部から直接書き換えるための可変参照 */
    Vector3& GetPositionRef() override { return pos_; }
    AABB GetAABB() const
    {
        return { { pos_.x - 0.5f, pos_.y - 0.5f, -0.5f },
            { pos_.x + 0.5f, pos_.y + 1.0f, 0.5f } };
    }

private:
    enum class State { Idle,
        Telegraph,
        Dash,
        Recover,
        Defeated,
        Absorbing,
        Consumed };

    void UpdateAI(ParticleManager* pm, const Vector3& playerPos);
    void UpdateAbsorb(ParticleManager* pm, const Vector3& playerPos);
    void ApplyTransforms();

    static constexpr int kMaxHp = 3;

    State state_ = State::Idle;
    float stateTimer_ = 0.0f;
    int hp_ = kMaxHp;

    Vector3 pos_ = { };
    float yaw_ = 0.0f;
    Vector3 dashStart_ = { };
    Vector3 dashTarget_ = { };

    float swordSwing_ = 0.0f; ///< 剣の振り角（ラジアン、状態に応じてlerpで追従）
    float hitFlash_ = 0.0f; ///< 被弾時に白く光らせる残り秒数
    float knockVelX_ = 0.0f; ///< 被弾ノックバックの水平速度（毎フレーム減衰）
    float knockVelY_ = 0.0f; ///< 被弾ノックバックの垂直速度（毎フレーム重力減衰）
    bool justAbsorbed_ = false;

    std::unique_ptr<SkinCommon> skinCommon_;
    std::unique_ptr<SkinnedModel> model_;
    std::unique_ptr<SkinnedObject3d> object_;
    Animation idleAnimation_;
    Animation attackAnimation_;
    State animationState_ = State::Defeated;
    std::unique_ptr<Model> swordModel_;
    std::unique_ptr<Object3d> swordObject_;
};

} // namespace engine::game
