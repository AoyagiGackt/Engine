/**
 * @file EnemyEntity.h
 * @brief ローグライトの戦闘で使用する汎用敵エンティティを定義するファイル
 */
#pragma once
#include "Animation.h"
#include "IEnemyEntity.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "SkinCommon.h"
#include "SkinnedModel.h"
#include "SkinnedObject3d.h"
#include "Weapon.h"
#include <algorithm>
#include <memory>
#include <string>
namespace engine::game {
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::SkinCommon;
using engine::graphics::SkinnedModel;
using engine::graphics::SkinnedObject3d;

/**
 * @brief ローグライト戦闘シーンで使用する汎用の敵エンティティクラス
 * @note HP 管理・打ち上げ物理・撃破判定を提供する
 * SetMaxHp() で種別（Normal/Elite/Boss）ごとの HP を設定してから使用すること
 */
class EnemyEntity : public IEnemyEntity {
public:
    /**
     * @brief 初期化モデル生成とワールド座標を設定する
     * @param modelCommon モデル共通設定
     * @param startPos    初期ワールド座標
     */
    void Initialize(ModelCommon* modelCommon, const Vector3& startPos,
        WeaponType weaponType = WeaponType::Sword);

    /**
     * @brief 物理・アニメーションを毎フレーム更新する
     * @param playerX プレイヤーのワールドX座標（自分から見た左右どちらを向くか判定するために使う）
     * @note 接地中（!isLaunched_）はpos_.yに一切触れない。ステージエディタで配置した高さ・
     * StageEditorInteractionのドラッグでpos_を書き換えた高さをそのまま信用する
     */
    void Update(float playerX);

    /** @brief モデルを描画する */
    void Draw();

    /** @brief 所持武器を識別するための表示色を設定する */
    void SetColor(const Vector4& color)
    {
        object_->SetColor(color);
        if (weaponObject_) {
            weaponObject_->SetColor(color);
        }
    }

    /**
     * @brief 上方向の初速を与えて打ち上げる
     * @param velY 上方向速度（正の値）
     */
    void Launch(float velY);
    void ApplyComboReaction(float knockDirX, float knockY, bool switchPull,
        float playerX);
    void ApplySlow(float seconds) { slowTimer_ = (std::max)(slowTimer_, seconds); }

    /** @brief 攻撃の進行フェーズ */
    enum class AttackState {
        Idle, ///< 次の攻撃までのクールダウン中
        Telegraph, ///< 予備動作中（攻撃判定はまだ発生しない）
        Active, ///< 攻撃判定が発生している短い窓
    };

    /** @brief 予備動作が明けて弾を撃ち出す瞬間のフレームだけ true（弾の発射トリガー用） */
    bool JustFiredAttack() const { return justFiredAttack_; }

    /** @brief 攻撃がヒットした際に与えるダメージ量を返す */
    int GetAttackDamage() const
    {
        return (weaponType_ == WeaponType::Hammer || weaponType_ == WeaponType::Axe)
            ? kHeavyAttackDamage_
            : kAttackDamage_;
    }

    /**
     * @brief ダメージを与えるHP が 0 以下になると撃破状態になる
     * @param dmg 与えるダメージ量（デフォルト 1）
     */
    void TakeDamage(int dmg = 1)
    {
        if (defeated_) {
            return;
        }
        hp_ -= dmg;
        if (hp_ <= 0) {
            hp_ = 0;
            defeated_ = true;
        }
    }

    /**
     * @brief 最大 HP を設定し、現在 HP をリセットする
     * @param v 設定する最大 HP 値
     */
    void SetMaxHp(int v)
    {
        maxHp_ = v;
        hp_ = v;
        defeated_ = false;
    }

    /**
     * @brief HP を回復する（maxHp を超えない、撃破済みは復活しない）
     * @param amount 回復量
     */
    void Heal(int amount)
    {
        if (defeated_) {
            return;
        }
        hp_ = (std::min)(hp_ + amount, maxHp_);
    }

    /**
     * @brief 位置だけ再計算する（AI/物理は一切進めない、StageEditor等が外部からpos_を書き換えた後の追従用）
     */
    void RefreshVisualTransforms() override
    {
        object_->SetPosition(pos_);
        object_->Update();
    }

    /**
     * @brief EnemyRegistry へ登録する際のid。ノードグラフの対象敵指定に使う
     * @param id シーン内で一意な識別名（例: "enemy", "boss"）
     */
    void SetId(const std::string& id) { id_ = id; }
    /** @brief 登録id未設定なら空文字 */
    const std::string& GetId() const { return id_; }

    /** @brief 撃破済みかどうかを返す */
    bool IsDefeated() const { return defeated_; }
    /** @brief 現在の HP を返す */
    int GetHp() const override { return hp_; }
    /** @brief 最大 HP を返す */
    int GetMaxHp() const override { return maxHp_; }

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
    bool JustLanded() const { return justLanded_; }
    /** @brief 打ち上げ中かどうか */
    bool IsLaunched() const { return isLaunched_; }
    /** @brief 現在のワールド座標を返す */
    Vector3 GetPosition() const override { return pos_; }
    /** @brief StageEditorのギズモドラッグ等、外部から直接書き換えるための可変参照 */
    Vector3& GetPositionRef() override { return pos_; }

private:
    static constexpr float kCeilingY_ = 12.5f;
    static constexpr float kGravity_ = 0.015f;

    // 装備ビジュアル（武器の見た目スケール・本体からのオフセット・傾き）
    static constexpr Vector3 kSpearWeaponScale_ = { 0.11f, 0.11f, 0.22f };
    static constexpr Vector3 kHeavyWeaponScale_ = { 0.16f, 0.16f, 0.16f }; // Hammer/Axe
    static constexpr float kWeaponOffsetX_ = 0.35f; // 本体から向いている方向へのオフセット
    static constexpr float kWeaponOffsetY_ = 0.75f;
    static constexpr float kWeaponOffsetZ_ = 0.15f;
    static constexpr float kWeaponRestTilt_ = 0.4f; // Idle中の武器の傾き

    // ノックバック・打ち上げ挙動
    static constexpr float kKnockbackSlowMultiplier_ = 0.45f; // ApplySlow()中のノックバック速度倍率
    static constexpr float kKnockbackDecay_ = 0.82f; // ノックバック速度の1フレームあたりの減衰率
    static constexpr float kAirComboGravityScale_ = 0.28f; // 空中コンボ猶予中の重力倍率

    // モーション演出（攻撃ステート毎の体/武器の傾き）
    static constexpr float kTelegraphBodyLean_ = -0.10f;
    static constexpr float kTelegraphWeaponSwing_ = 1.15f;
    static constexpr float kActiveBodyLean_ = 0.14f;
    static constexpr float kActiveWeaponSwing_ = -1.0f;

    // 攻撃ステートマシン（Idle→Telegraph→Active→Idle を固定時間で巡回する）
    // Telegraph→Active の切り替わり瞬間が弾の発射トリガー実際の弾はGamePlayScene側が撃ち出して追跡する
    static constexpr float kAttackInterval_ = 2.5f; // 攻撃と攻撃の間隔（秒）
    static constexpr float kAttackTelegraph_ = 0.5f; // 予備動作の長さ（秒）
    static constexpr float kAttackActive_ = 0.18f; // 発射直後の連射防止用の短い不応期（秒）
    static constexpr int kAttackDamage_ = 2;
    static constexpr int kHeavyAttackDamage_ = 3; // Hammer/Axe

    // 武器種別ごとの予備動作/回復時間（Dagger=速攻小威力、Spear=中間、Hammer/Axe=遅いが重い）
    static constexpr float kDaggerTelegraph_ = 0.20f;
    static constexpr float kSpearTelegraph_ = 0.38f;
    static constexpr float kHeavyTelegraph_ = 0.75f;
    static constexpr float kDaggerRecovery_ = 1.25f;
    static constexpr float kSpearRecovery_ = 2.0f;
    static constexpr float kHeavyRecovery_ = 3.2f;

    // 接近AI（Idle中だけプレイヤーへ向かって歩く。予備動作/攻撃中や被弾ノックバック中は歩かせない）
    static constexpr float kApproachSpeed_ = 0.06f; // 1フレームあたりの歩行距離（プレイヤーのkSpeed_=0.15fより遅め）
    static constexpr float kEngageRange_ = 2.0f; // これより近づいたら歩みを止める（武器の間合い目安）
    static constexpr float kAggroRange_ = 6.0f; // 配置位置からこの距離にプレイヤーが来るまでは歩き出さない
    static constexpr float kLeashDistance_ = 3.0f; // 配置位置からこれ以上は離れない（持ち場を離れて全員が団子にならないように）

    // コンボ被弾リアクション
    static constexpr float kSwitchPullStrength_ = 0.18f; // 武器切替吸い寄せの引き込み強さ
    static constexpr float kSwitchPullClamp_ = 0.32f; // 引き込み速度の上限
    static constexpr float kSwitchPullAirComboBonus_ = 0.18f; // 吸い寄せ時に伸びる空中コンボ猶予
    static constexpr float kKnockDirXScale_ = 0.055f; // 通常ノックバックの反映倍率
    static constexpr float kLaunchThreshold_ = 0.08f; // これを超えるknockYで打ち上げが発生する

    /** @brief 攻撃ステートマシンを毎フレーム進める（Update() から呼ぶ） */
    void UpdateAttack();

    std::unique_ptr<Model> model_;
    std::unique_ptr<SkinCommon> skinCommon_;
    std::unique_ptr<SkinnedModel> animatedModel_;
    std::unique_ptr<SkinnedObject3d> object_;
    Animation idleAnimation_;
    Animation attackAnimation_;
    AttackState animationState_ = AttackState::Active;
    std::unique_ptr<Model> weaponModel_;
    std::unique_ptr<Object3d> weaponObject_;
    Vector3 weaponScale_ { 0.14f, 0.14f, 0.14f };

    int maxHp_ = 20;
    int hp_ = 20;
    bool defeated_ = false;
    bool visible_ = true;
    std::string id_; // EnemyRegistry登録名（未登録なら空）

    Vector3 pos_ = { };
    float spawnX_ = 0.0f; ///< 配置時のワールドX（接近AIの持ち場判定の基準。Initialize()で記録する）
    float facingSign_ = -1.0f; ///< 現在向いている方向(+1=+X向き/-1=-X向き)。Update()毎にプレイヤー位置から再計算する
    float velY_ = 0.0f;
    bool isLaunched_ = false;
    bool justLanded_ = false;
    float launchOriginY_ = 0.0f; ///< 打ち上げ直前のpos_.y（着地時にここへ戻す。Launch()の最初の呼び出しでだけ更新する）
    WeaponType weaponType_ = WeaponType::Sword;
    float knockVelX_ = 0.0f;
    float slowTimer_ = 0.0f;
    float airComboTimer_ = 0.0f;
    static constexpr float kAirComboHold_ = 0.32f;

    AttackState attackState_ = AttackState::Idle;
    float attackTimer_ = kAttackInterval_;
    bool justFiredAttack_ = false;
};

} // namespace engine::game
