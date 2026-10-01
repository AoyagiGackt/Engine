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
        return { { pos_.x - kHitBoxHalfWidth, pos_.y - kHitBoxBelow, -kHitBoxHalfDepth },
            { pos_.x + kHitBoxHalfWidth, pos_.y + kHitBoxAbove, kHitBoxHalfDepth } };
    }

private:
    enum class State { Idle,
        Telegraph,
        Dash,
        Recover,
        Defeated,
        Absorbing,
        Consumed };

    // AI State パターン
    // 生存中の行動フェーズ（Idle/Telegraph/Dash/Recover）ごとに毎フレームの処理と遷移条件を切り替える
    /** @brief 生存中の行動フェーズ固有処理を抽象化する状態 */
    class IAIState {
    public:
        virtual ~IAIState() = default;
        virtual void Update(KnightEnemy& knight, ParticleManager* pm, const Vector3& playerPos) const = 0;
    };
    /** @brief 次の突進までの待機状態 */
    class IdleAIState;
    /** @brief 突進前に剣を引く予備動作状態 */
    class TelegraphAIState;
    /** @brief プレイヤーへ向けて突進する状態 */
    class DashAIState;
    /** @brief 突進後の硬直状態 */
    class RecoverAIState;
    /** @brief 生存中の状態に対応するAI状態を返す（生存中以外はnullptr） */
    static const IAIState* GetAIState(State state);
    /** @brief 状態を切り替えて経過時間をリセットする */
    void ChangeState(State next);

    void UpdateAI(ParticleManager* pm, const Vector3& playerPos);
    void UpdateAbsorb(ParticleManager* pm, const Vector3& playerPos);
    void ApplyTransforms();

    int maxHp_ = 0; ///< Initialize()でEnemyTuning::Knight().maxHpから設定する

    State state_ = State::Idle;
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
    bool justAbsorbed_ = false;

    // 本体・剣のモデル実体はModelManagerが所有・共有する（同種の敵なら読み込みは1回だけで済む）
    std::unique_ptr<SkinCommon> skinCommon_;
    SkinnedModel* model_ = nullptr;
    std::unique_ptr<SkinnedObject3d> object_;
    Animation idleAnimation_;
    Animation attackAnimation_;
    State animationState_ = State::Defeated;
    Model* swordModel_ = nullptr;
    std::unique_ptr<Object3d> swordObject_;
};

} // namespace engine::game
