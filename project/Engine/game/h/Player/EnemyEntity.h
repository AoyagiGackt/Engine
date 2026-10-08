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
    EnemyEntity();
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

    /**
     * @brief 敵の性格（"basic" "flying" "healer"）を設定し、性格ごとの色に塗る
     * @note 名前を見るのはここだけで、以降の振る舞いの違いは性格クラスに任せる（未知の名前はbasic扱い）
     */
    void SetArchetype(const std::string& archetype);
    /**
     * @brief 近接で殴ってくる敵か（槍とボールは投擲＝遠隔扱い）
     * @note 近接敵は間合いに入るまで攻撃を始めず、発生時は自分の前方だけに判定を出す（GamePlayScene側）
     */
    bool IsMeleeAttacker() const { return weaponType_ != WeaponType::Spear && weaponType_ != WeaponType::Ball; }
    /** @brief 周りの味方を回復する回復役か */
    bool IsHealer() const { return archetype_->HealsAllies(); }
    /** @brief 持ち場の高さで浮遊し続ける飛行型か */
    bool IsFlying() const { return archetype_->Hovers(); }
    /** @brief flying/healerやモンスターなど、武器色で塗り替えず自前の色で見分けさせる敵か */
    bool HasArchetypeColor() const { return archetype_->HasOwnColor() || IsMonster(); }

    /**
     * @brief 見た目を騎士からモンスターのモデルに差し替える（動き・攻撃・被弾の挙動は騎士と同じものを使う）
     * @param modelCommon モデル共通設定
     * @param kind        "Slime" "Bat" "Dragon" "Skeleton" のいずれか（未知の名前なら騎士のまま）
     * @note 武器は持たないので手元の武器モデルは描画しない
     */
    void SetMonsterVisual(ModelCommon* modelCommon, const std::string& kind);
    /** @brief モンスターの見た目か */
    bool IsMonster() const { return monsterObject_ != nullptr; }

    /** @brief モデルを描画する */
    void Draw();

    /** @brief 所持武器を識別するための表示色を設定する（被弾フラッシュが明けた後に戻る基準色になる） */
    void SetColor(const Vector4& color)
    {
        baseColor_ = color;
        SetBodyColor(color);
        if (weaponObject_) {
            weaponObject_->SetColor(color);
        }
    }

    /**
     * @brief 上方向の初速を与えて打ち上げる
     * @param velY 上方向速度（正の値）
     */
    void Launch(float velY);
    /**
     * @brief 近接コンボのヒットに対する吹き飛び・打ち上げを与える
     * @param knockDirX  水平ノックバック（向き×強さ）
     * @param knockY     打ち上げ量（閾値を超えると打ち上げ）
     * @param switchPull 武器切替ヒットの吸い寄せか
     * @param playerX    プレイヤーのワールドX座標
     * @param finisher   コンボの締めか（途中の段は間合いに留め、締めだけ大きく吹き飛ばす）
     */
    void ApplyComboReaction(float knockDirX, float knockY, bool switchPull,
        float playerX, bool finisher = false);
    /**
     * @brief 被弾硬直を与える（硬直中は歩かず攻撃も進めず、のけぞった姿勢になる）
     * @param seconds         硬直時間（既に長い硬直が残っていれば延長しない）
     * @param interruptAttack 予備動作・攻撃中の動作を潰して待機へ戻すか（銃の連射などは潰さない）
     */
    void ApplyHitstun(float seconds, bool interruptAttack);
    /** @brief 被弾硬直中か */
    bool IsInHitstun() const { return hitstunTimer_ > 0.0f; }

    /**
     * @brief 大技の締めで斜め上へ大きく打ち飛ばし、回転させながら飛ばす（撃破済みでも飛ばす）
     * @param dirX 打ち飛ばす向き（+1/-1）
     * @note 持っていた武器は打たれたその場に落とす
     */
    void ApplyHomeRun(float dirX);
    /** @brief 持っている武器をその場に落とす（以後は本体から離れて地面に転がる） */
    void DropWeapon();
    /** @brief 武器を落としているか */
    bool HasDroppedWeapon() const { return weaponDropped_; }
    /** @brief 武器を奪いに行く位置（落とした武器があればそこ、無ければ本体） */
    Vector3 GetWeaponPickupPosition() const { return weaponDropped_ ? droppedWeaponPos_ : pos_; }
    /**
     * @brief 落とした武器を指定位置へ寄せる（武器回収の吸い込み演出用）
     * @param target 寄せる先
     * @param rate   1フレームで寄せる割合（0〜1）
     */
    void PullDroppedWeaponToward(const Vector3& target, float rate);
    void ApplySlow(float seconds) { slowTimer_ = (std::max)(slowTimer_, seconds); }

    /** @brief 予備動作が明けて弾を撃ち出す瞬間のフレームだけ true（弾の発射トリガー用） */
    bool JustFiredAttack() const { return justFiredAttack_; }
    /** @brief 予備動作に入った瞬間のフレームだけ true（シーン側が警告演出を出し、回避のタイミングを読めるようにする） */
    bool JustStartedTelegraph() const { return justStartedTelegraph_; }
    /** @brief 予備動作中か（本体を警告色に寄せる等、攻撃が来ることを見た目で伝えるために使う） */
    bool IsTelegraphing() const { return attackState_->IsTelegraph() && !defeated_ && !isLaunched_; }

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
            // 撃破後もUpdate自体は落下・吸収演出のため続くが、歩行アニメだけはその場で完全停止する。
            if (object_) {
                object_->SetAnimSpeed(0.0f);
            }
        }
    }

    /**
     * @brief 端数を含むダメージを与える（1未満の端数は次のヒットへ持ち越す）
     * @param amount 与えるダメージ量（疲労で減った近接ダメージなど）
     * @note 端数しか溜まっていないヒットでも被弾フラッシュは出す（当たったこと自体は伝える）
     */
    void TakeDamageScaled(float amount)
    {
        if (defeated_) {
            return;
        }
        damageCarry_ += (std::max)(amount, 0.0f);
        const int whole = static_cast<int>(damageCarry_);
        damageCarry_ -= static_cast<float>(whole);
        if (whole > 0) {
            TakeDamage(whole);
        } else {
            hitFlashTimer_ = kHitFlashDuration_;
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
        damageCarry_ = 0.0f;
        weaponDropped_ = false;
        homeRunSpinTimer_ = 0.0f;
        defeated_ = false;
        if (object_) {
            object_->SetAnimSpeed(1.0f);
        }
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
        SyncMonsterVisual(false);
    }

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
    Model* GetModel() const { return model_; }

    /** @brief このフレームに着地したか */
    bool JustLanded() const { return justLanded_; }
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
    static constexpr Vector4 kHitFlashColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; // 被弾フラッシュが最も強い瞬間の色
    static constexpr float kBodyScale_ = 0.2f; // 本体モデルの基準スケール

    // モーション演出（攻撃ステート毎の体/武器の傾き）
    static constexpr float kTelegraphBodyLean_ = -0.10f;
    static constexpr float kTelegraphWeaponSwing_ = 1.15f;
    static constexpr float kActiveBodyLean_ = 0.14f;
    static constexpr float kActiveWeaponSwing_ = -1.0f;

    // 予備動作中の警告色（本体色をこの色へ寄せ、攻撃が来ることを傾きだけでなく色でも伝える）
    static constexpr Vector4 kTelegraphTint_ = { 1.0f, 0.3f, 0.2f, 1.0f };
    static constexpr float kTelegraphTintStrength_ = 0.55f; // 基準色から警告色へ寄せる割合（0〜1）

    // 撃破後の色（生存中の武器色と紛れないよう固定の灰色にする。KnightEnemyの撃破色と同じ配色）
    static constexpr Vector4 kDefeatedColor_ = { 0.4f, 0.4f, 0.4f, 1.0f };
    static constexpr Vector4 kFlyingColor_ = { 0.55f, 0.8f, 1.0f, 1.0f };
    static constexpr Vector4 kHealerColor_ = { 0.45f, 1.0f, 0.55f, 1.0f };
    static constexpr Vector4 kDefaultArchetypeColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    static constexpr float kDefeatedBodyLean_ = 1.35f; // 撃破後に倒れ込む本体の傾き
    static constexpr float kDefeatedWeaponTilt_ = 1.15f; // 撃破後に落とした武器の傾き
    static constexpr float kKnockbackStopSpeed_ = 0.001f; // これ未満の水平ノックバックは止まっているとみなす
    static constexpr float kCeilingBounceFactor_ = -0.1f; // 天井に当たった時の跳ね返り（負で下向きに返す）

    // 攻撃ステートマシン（Idle→Telegraph→Active→Idle を固定時間で巡回する）
    // Telegraph→Active の切り替わり瞬間が弾の発射トリガー実際の弾はGamePlayScene側が撃ち出して追跡する
    // 各時間はEnemyTuning::Basic()から読む

    // コンボ被弾リアクション
    static constexpr float kSwitchPullStrength_ = 0.18f; // 武器切替吸い寄せの引き込み強さ
    static constexpr float kSwitchPullClamp_ = 0.32f; // 引き込み速度の上限
    static constexpr float kSwitchPullAirComboBonus_ = 0.18f; // 吸い寄せ時に伸びる空中コンボ猶予
    static constexpr float kKnockDirXScale_ = 0.055f; // 通常ノックバックの反映倍率
    static constexpr float kFinisherKnockDirXScale_ = 0.3f; // コンボの締めで吹き飛ばす時の反映倍率

    // 被弾硬直（のけぞり姿勢と、硬直明けに即反撃しないための猶予）
    static constexpr float kHitstunBodyLean_ = -0.38f; // のけぞりの最大傾き（負で後ろへ反る）
    static constexpr float kHitstunWeaponSwing_ = 0.9f; // のけぞり中に武器が振り上がる角度
    static constexpr float kHitstunLeanFullSeconds_ = 0.25f; // この秒数以上の残り硬直では最大まで反らせる
    static constexpr float kPostHitstunAttackDelay_ = 0.35f; // 攻撃を潰された後、次の予備動作に入るまでの最短時間

    // ヒットストップ中の震え（止まっている間に効いていることを伝える）
    static constexpr float kHitStopShakeAmplitude_ = 0.06f;

    // 大技の締めで打ち飛ばされる時の飛び方
    static constexpr float kHomeRunSpeedX_ = 0.75f; // 水平初速（1フレームあたり、knockbackDecayで減衰する）
    static constexpr float kHomeRunLaunchY_ = 0.55f; // 打ち上げ初速
    static constexpr float kHomeRunSpinSeconds_ = 1.2f; // 回転しながら飛ぶ時間
    static constexpr float kHomeRunSpinSpeed_ = 0.6f; // 1フレームあたりの回転量（ラジアン）

    // 落とした武器の転がり方
    static constexpr float kDroppedWeaponPopY_ = 0.12f; // 手を離れた瞬間に少し跳ね上がる初速
    static constexpr float kDroppedWeaponGravity_ = 0.012f; // 1フレームあたりの落下加速
    static constexpr float kDroppedWeaponRestHeight_ = 0.1f; // 地面から少し浮かせて置く高さ
    static constexpr float kDroppedWeaponLieTilt_ = 1.5708f; // 地面に寝かせる傾き（90度）
    static constexpr float kDroppedWeaponSpinPerFrame_ = 0.35f; // 落ちている間に回る量（ラジアン）
    static constexpr float kLaunchThreshold_ = 0.08f; // これを超えるknockYで打ち上げが発生する

    /** @brief 攻撃ステートマシンを毎フレーム進める（Update() から呼ぶ）
     *  @param playerX プレイヤーのワールドX座標（持ち場基準の索敵判定に使う） */
    void UpdateAttack(float playerX);

    // Attack State パターン
    // 攻撃の進行フェーズ（クールダウン/予備動作/攻撃判定）ごとに次フェーズへの遷移内容と構えの姿勢を切り替える
    /** @brief 攻撃フェーズ固有処理を抽象化する状態 */
    class IAttackState {
    public:
        virtual ~IAttackState() = default;
        /** @brief フェーズのタイマーが尽きた時に次のフェーズへ進める */
        virtual void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const = 0;
        /** @brief フェーズに応じた本体の傾きと武器の振り角を書き込む */
        virtual void ApplyPose(float& bodyLean, float& weaponSwing) const = 0;
        /** @brief 次の攻撃までのクールダウン中か（歩いて間合いを詰めてよいのはこの間だけ） */
        virtual bool IsIdle() const { return false; }
        /** @brief 予備動作中か（攻撃判定はまだ発生しない） */
        virtual bool IsTelegraph() const { return false; }
    };
    /** @brief 次の攻撃までのクールダウン状態 */
    class IdleAttackState;
    /** @brief 攻撃前の予備動作状態 */
    class TelegraphAttackState;
    /** @brief 攻撃判定が発生している状態 */
    class ActiveAttackState;
    /** @brief 攻撃フェーズの共有インスタンスを返す（フェーズはステートレスで全員が共有する） */
    template <class T>
    static const IAttackState& AttackStateOf();
    /** @brief クールダウン状態へ戻す（被弾・打ち飛ばし・回復役など、攻撃を中断する時に使う） */
    void ResetAttackState();

    /** @brief 表示アニメーションの種類（攻撃ステートと歩行状態の組み合わせから決まる） */
    enum class VisualAnim {
        Idle, ///< 立ち姿勢
        Run, ///< 接近歩行中の走り
        Attack, ///< 予備動作〜攻撃中
    };

    // 本体・武器のモデル実体はModelManagerが所有・共有する（同種の敵/同じ武器なら読み込みは1回だけで済む）
    Model* model_ = nullptr;
    std::unique_ptr<SkinCommon> skinCommon_;
    SkinnedModel* animatedModel_ = nullptr;
    std::unique_ptr<SkinnedObject3d> object_;
    Animation idleAnimation_;
    Animation runAnimation_;
    Animation attackAnimation_;
    VisualAnim animationState_ = VisualAnim::Attack;
    Model* weaponModel_ = nullptr;
    std::unique_ptr<Object3d> weaponObject_;
    Vector3 weaponScale_ { 0.14f, 0.14f, 0.14f };

    int maxHp_ = 20;
    int hp_ = 20;
    bool defeated_ = false;
    bool visible_ = true;
    Vector4 baseColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float hitFlashTimer_ = 0.0f;
    float damageCarry_ = 0.0f; ///< TakeDamageScaled()で1に満たなかったダメージの持ち越し

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
    /**
     * @brief 敵の性格ごとの違い（色・攻撃するか・浮遊するか・回復役か）を抽象化する
     * @note 性格はステートレスで、同じ性格の敵は同じインスタンスを共有する
     */
    class IArchetype {
    public:
        virtual ~IArchetype() = default;
        /** @brief 性格を見分けるための本体色 */
        virtual Vector4 Color() const = 0;
        /** @brief 武器色で塗り替えず自前の色を使うか */
        virtual bool HasOwnColor() const { return true; }
        /** @brief 自分から攻撃を仕掛けるか */
        virtual bool Attacks() const { return true; }
        /** @brief 持ち場の高さで浮遊するか */
        virtual bool Hovers() const { return false; }
        /** @brief 周りの味方を回復するか */
        virtual bool HealsAllies() const { return false; }
    };
    class BasicArchetype;
    class FlyingArchetype;
    class HealerArchetype;
    /** @brief 名前に対応する性格を返す（未知の名前はbasic） */
    static const IArchetype& FindArchetype(const std::string& name);
    const IArchetype* archetype_ = nullptr; ///< コンストラクタでbasicを入れる
    float knockVelX_ = 0.0f;
    float slowTimer_ = 0.0f;
    float airComboTimer_ = 0.0f;
    float hitstunTimer_ = 0.0f; ///< 被弾硬直の残り秒数
    float homeRunSpinTimer_ = 0.0f; ///< 打ち飛ばされて回転している残り秒数
    float homeRunSpinAngle_ = 0.0f;
    bool weaponDropped_ = false;
    Vector3 droppedWeaponPos_ { };
    float droppedWeaponVelY_ = 0.0f;
    float droppedWeaponGroundY_ = 0.0f;
    float droppedWeaponSpin_ = 0.0f;
    bool droppedWeaponLanded_ = false;

    // モンスターの見た目（設定されていれば騎士の代わりにこれを描画する）
    std::unique_ptr<Object3d> monsterObject_;
    float monsterScale_ = 1.0f;
    float monsterYawOffset_ = 0.0f; ///< モデルの正面を+Zへ揃える向き補正（ラジアン）
    float monsterBobTimer_ = 0.0f;
    Vector4 bodyColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 本体に今かけている色（モンスターの見た目へ写す）
    static constexpr float kMonsterBobSpeed_ = 6.0f; // その場で弾む速さ（生きている感じを出す、ラジアン毎秒）
    static constexpr float kMonsterBobAmplitude_ = 0.05f;

    /** @brief 本体の色を設定する（騎士とモンスターの見た目の両方に効かせる） */
    void SetBodyColor(const Vector4& color)
    {
        bodyColor_ = color;
        object_->SetColor(color);
    }
    /**
     * @brief 騎士の本体に計算した位置・向き・大きさ・色を、モンスターの見た目へ写す
     * @param advanceBob 弾む動きを進めるか（ヒットストップ中や外部からの位置合わせでは止める）
     */
    void SyncMonsterVisual(bool advanceBob);

    /** @brief 落とした武器を地面まで落とし、寝かせた姿勢で置く */
    void UpdateDroppedWeapon();
    /** @brief 武器の位置を本体の手元（落としていれば落とした場所）へ合わせる */
    void PlaceWeapon(const Vector3& bodyPos);
    int hitStopShakeFrame_ = 0; ///< ヒットストップ中の震えの左右を交互にするカウンタ

    /** @brief ヒットストップ中の1フレーム（物理・AIを止め、被弾直後なら本体を左右に震わせて描画行列だけ更新する） */
    void UpdateDuringHitStop();

    const IAttackState* attackState_ = nullptr; ///< Initialize()/ResetAttackState()でクールダウン状態を入れる
    float attackTimer_ = 0.0f;
    bool justFiredAttack_ = false;
    bool justStartedTelegraph_ = false;
};

} // namespace engine::game
