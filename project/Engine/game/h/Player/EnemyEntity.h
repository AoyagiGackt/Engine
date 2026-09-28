/**
 * @file EnemyEntity.h
 * @brief ローグライトの戦闘で使用する汎用敵エンティティを定義するファイル
 */
#pragma once
#include "Animation.h"
#include "EnemyTuning.h"
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

    void SetArchetype(const std::string& archetype)
    {
        archetype_ = archetype;
        const Vector4 color = archetype_ == "flying" ? Vector4 { 0.55f, 0.8f, 1.0f, 1.0f }
            : archetype_ == "healer" ? Vector4 { 0.45f, 1.0f, 0.55f, 1.0f }
                                      : Vector4 { 1.0f, 1.0f, 1.0f, 1.0f };
        SetColor(color);
    }
    /**
     * @brief 近接で殴ってくる敵か（槍とボールは投擲＝遠隔扱い）
     * @note 近接敵は間合いに入るまで攻撃を始めず、発生時は自分の前方だけに判定を出す（GamePlayScene側）
     */
    bool IsMeleeAttacker() const { return weaponType_ != WeaponType::Spear && weaponType_ != WeaponType::Ball; }
    bool IsHealer() const { return archetype_ == "healer"; }
    bool IsFlying() const { return archetype_ == "flying"; }
    /** @brief flying/healerなど、武器色ではなく種別色で見分けさせるアーキタイプか */
    bool HasArchetypeColor() const { return IsHealer() || IsFlying(); }

    /** @brief モデルを描画する */
    void Draw();

    /** @brief 所持武器を識別するための表示色を設定する（被弾フラッシュが明けた後に戻る基準色になる） */
    void SetColor(const Vector4& color)
    {
        baseColor_ = color;
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
    /** @brief 予備動作に入った瞬間のフレームだけ true（シーン側が警告演出を出し、回避のタイミングを読めるようにする） */
    bool JustStartedTelegraph() const { return justStartedTelegraph_; }
    /** @brief 予備動作中か（本体を警告色に寄せる等、攻撃が来ることを見た目で伝えるために使う） */
    bool IsTelegraphing() const { return attackState_ == AttackState::Telegraph && !defeated_ && !isLaunched_; }

    /** @brief 攻撃がヒットした際に与えるダメージ量を返す */
    int GetAttackDamage() const
    {
        const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
        return (weaponType_ == WeaponType::Hammer || weaponType_ == WeaponType::Axe)
            ? tuning.heavyAttackDamage
            : tuning.attackDamage;
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
        hitFlashTimer_ = kHitFlashDuration_; // 白く光って一瞬膨らむ（当たった手応えを見た目でも返す）
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
    // 攻撃間隔・索敵距離・重力などのゲームプレイ調整値はResources/Config/enemy_params.json（EnemyTuning::Basic()）で持つ。
    // ここに残す定数は見た目（装備の位置・傾き・被弾リアクションの倍率）だけ

    // 装備ビジュアル（武器の見た目スケール・本体からのオフセット・傾き）
    static constexpr Vector3 kSpearWeaponScale_ = { 0.11f, 0.11f, 0.22f };
    static constexpr Vector3 kHeavyWeaponScale_ = { 0.16f, 0.16f, 0.16f }; // Hammer/Axe
    static constexpr float kWeaponOffsetX_ = 0.35f; // 本体から向いている方向へのオフセット
    static constexpr float kWeaponOffsetY_ = 0.75f;
    static constexpr float kWeaponOffsetZ_ = 0.15f;
    static constexpr float kWeaponRestTilt_ = 0.4f; // Idle中の武器の傾き

    // 被弾リアクション（白フラッシュと一瞬のスケール膨張）
    static constexpr float kHitFlashDuration_ = 0.12f;
    static constexpr float kHitScalePunch_ = 0.22f; // フラッシュ開始時に本体スケールへ足す割合
    static constexpr float kBodyScale_ = 0.2f; // 本体モデルの基準スケール

    // モーション演出（攻撃ステート毎の体/武器の傾き）
    static constexpr float kTelegraphBodyLean_ = -0.10f;
    static constexpr float kTelegraphWeaponSwing_ = 1.15f;
    static constexpr float kActiveBodyLean_ = 0.14f;
    static constexpr float kActiveWeaponSwing_ = -1.0f;

    // 予備動作中の警告色（本体色をこの色へ寄せ、攻撃が来ることを傾きだけでなく色でも伝える）
    static constexpr Vector4 kTelegraphTint_ = { 1.0f, 0.3f, 0.2f, 1.0f };
    static constexpr float kTelegraphTintStrength_ = 0.55f; // 基準色から警告色へ寄せる割合（0〜1）

    // 攻撃ステートマシン（Idle→Telegraph→Active→Idle を固定時間で巡回する）
    // Telegraph→Active の切り替わり瞬間が弾の発射トリガー実際の弾はGamePlayScene側が撃ち出して追跡する
    // 各時間はEnemyTuning::Basic()から読む

    // コンボ被弾リアクション
    static constexpr float kSwitchPullStrength_ = 0.18f; // 武器切替吸い寄せの引き込み強さ
    static constexpr float kSwitchPullClamp_ = 0.32f; // 引き込み速度の上限
    static constexpr float kSwitchPullAirComboBonus_ = 0.18f; // 吸い寄せ時に伸びる空中コンボ猶予
    static constexpr float kKnockDirXScale_ = 0.055f; // 通常ノックバックの反映倍率
    static constexpr float kLaunchThreshold_ = 0.08f; // これを超えるknockYで打ち上げが発生する

    /** @brief 攻撃ステートマシンを毎フレーム進める（Update() から呼ぶ）
     *  @param playerX プレイヤーのワールドX座標（持ち場基準の索敵判定に使う） */
    void UpdateAttack(float playerX);

    /** @brief 表示アニメーションの種類（攻撃ステートと歩行状態の組み合わせから決まる） */
    enum class VisualAnim {
        Idle, ///< 立ち姿勢
        Run, ///< 接近歩行中の走り
        Attack, ///< 予備動作〜攻撃中
    };

    std::unique_ptr<Model> model_;
    std::unique_ptr<SkinCommon> skinCommon_;
    std::unique_ptr<SkinnedModel> animatedModel_;
    std::unique_ptr<SkinnedObject3d> object_;
    Animation idleAnimation_;
    Animation runAnimation_;
    Animation attackAnimation_;
    VisualAnim animationState_ = VisualAnim::Attack;
    std::unique_ptr<Model> weaponModel_;
    std::unique_ptr<Object3d> weaponObject_;
    Vector3 weaponScale_ { 0.14f, 0.14f, 0.14f };

    int maxHp_ = 20;
    int hp_ = 20;
    bool defeated_ = false;
    bool visible_ = true;
    Vector4 baseColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float hitFlashTimer_ = 0.0f;
    std::string id_; // EnemyRegistry登録名（未登録なら空）

    Vector3 pos_ = { };
    float spawnX_ = 0.0f; ///< 配置時のワールドX（接近AIの持ち場判定の基準。Initialize()で記録する）
    float facingSign_ = -1.0f; ///< 現在向いている方向(+1=+X向き/-1=-X向き)。Update()毎にプレイヤー位置から再計算する
    float velY_ = 0.0f;
    bool isLaunched_ = false;
    bool justLanded_ = false;
    float launchOriginY_ = 0.0f; ///< 打ち上げ直前のpos_.y（着地時にここへ戻す。Launch()の最初の呼び出しでだけ更新する）
    WeaponType weaponType_ = WeaponType::Sword;
    float spawnY_ = 0.0f;
    float archetypeTimer_ = 0.0f;
    std::string archetype_ = "basic";
    float knockVelX_ = 0.0f;
    float slowTimer_ = 0.0f;
    float airComboTimer_ = 0.0f;

    AttackState attackState_ = AttackState::Idle;
    float attackTimer_ = 0.0f;
    bool justFiredAttack_ = false;
    bool justStartedTelegraph_ = false;
};

} // namespace engine::game
