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
    static constexpr float kDefaultKnockY = 0.09f; // 打ち上げ量を指定しない被弾時の垂直ノックバック
    // 当たり判定（足元基準の箱。上方向は頭まで含めて高めに取る）
    static constexpr float kHitBoxHalfWidth = 0.5f;
    static constexpr float kHitBoxBelow = 0.5f;
    static constexpr float kHitBoxAbove = 1.0f;
    static constexpr float kHitBoxHalfDepth = 0.5f;

    KnightEnemy();
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
    void TakeDamage(int damage, float knockDirX = 0.0f, float knockY = kDefaultKnockY);

    /** @brief 通常行動中（攻撃で倒せる状態）か */
    bool IsAlive() const;
    /** @brief 現在のHPを返す */
    int GetHp() const override { return hp_; }
    /** @brief 最大HPを返す */
    int GetMaxHp() const override { return maxHp_; }
    /** @brief 撃破後、武器を奪われるのを待っている（灰色で静止）状態か */
    bool IsAwaitingSteal() const;

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
        return { { pos_.x - kHitBoxHalfWidth, pos_.y - kHitBoxBelow, -kHitBoxHalfDepth },
            { pos_.x + kHitBoxHalfWidth, pos_.y + kHitBoxAbove, kHitBoxHalfDepth } };
    }

private:
    // State パターン
    // 行動フェーズ（待機/予備動作/突進/硬直）と撃破後（凍結/吸収/消滅）を状態クラスに分け、
    // 毎フレームの処理・見た目の更新・外から見た性質を状態ごとに切り替える
    /** @brief ナイトの状態を抽象化する基底（状態はステートレスで、全ナイトが同じインスタンスを共有する） */
    class IState {
    public:
        virtual ~IState() = default;
        /** @brief 状態固有の1フレーム分の処理と遷移判定 */
        virtual void Update(KnightEnemy& knight, ParticleManager* pm, const Vector3& playerPos) const = 0;
        /** @brief 攻撃で倒せる生存中の状態か */
        virtual bool IsAlive() const { return true; }
        /** @brief 攻撃モーションを再生する状態か（それ以外は待機モーション） */
        virtual bool PlaysAttackAnimation() const { return false; }
        /** @brief 撃破後、武器を奪われるのを待っている状態か */
        virtual bool IsAwaitingSteal() const { return false; }
        /** @brief 本体と剣を立ち位置へ配置する（吸収演出中のように独自の軌道で動かす状態は何もしない） */
        virtual void ApplyTransforms(KnightEnemy& knight) const { knight.ApplyStandingTransforms(false); }
        /** @brief 描画するか */
        virtual bool IsVisible() const { return true; }
    };
    class IdleState;
    class TelegraphState;
    class DashState;
    class RecoverState;
    class DefeatedState;
    class AbsorbingState;
    class ConsumedState;
    /** @brief 状態の共有インスタンスを返す */
    template <class T>
    static const IState& StateOf();
    /** @brief 状態を切り替えて経過時間をリセットする */
    void ChangeState(const IState& next);

    void UpdateAbsorb(ParticleManager* pm, const Vector3& playerPos);
    void ApplyStandingTransforms(bool defeatedPose);

    int maxHp_ = 0; ///< Initialize()でEnemyTuning::Knight().maxHpから設定する

    const IState* state_ = nullptr;
    float stateTimer_ = 0.0f;
    int hp_ = 0;

    Vector3 pos_ = { };
    float yaw_ = 0.0f;
    Vector3 dashStart_ = { };
    Vector3 dashTarget_ = { };

    float swordSwing_ = 0.0f; ///< 剣の振り角（ラジアン、状態に応じてlerpで追従）
    float hitFlash_ = 0.0f; ///< 被弾時に白く光らせる残り秒数
    float knockVelX_ = 0.0f; ///< 被弾ノックバックの水平速度（毎フレーム減衰）
    float knockVelY_ = 0.0f; ///< 被弾ノックバックの垂直速度（毎フレーム重力減衰）

    // 本体・剣のモデル実体はModelManagerが所有・共有する（同種の敵なら読み込みは1回だけで済む）
    std::unique_ptr<SkinCommon> skinCommon_;
    SkinnedModel* model_ = nullptr;
    std::unique_ptr<SkinnedObject3d> object_;
    Animation idleAnimation_;
    Animation attackAnimation_;
    bool playingAttackAnimation_ = false; ///< いま攻撃モーションを再生中か（切り替わった時だけSetAnimationし直す）
    Model* swordModel_ = nullptr;
    std::unique_ptr<Object3d> swordObject_;
};

} // namespace engine::game
