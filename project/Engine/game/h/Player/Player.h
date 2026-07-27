/**
 * @file Player.h
 * @brief プレイヤーキャラクターの物理・アクション・スタイルシステムを定義するファイル
 */
#pragma once
#include "AfterImageRenderer.h"
#include "Animation.h"
#include "CollisionConfig.h"
#include "GunCombo.h"
#include "MeleeCombo.h"
#include "Model.h"
#include "Object3d.h"
#include "Skeleton.h"
#include "SkinCommon.h"
#include "SkinnedModel.h"
#include "SkinnedObject3d.h"
#include <algorithm>
#include <memory>
#include <vector>
namespace engine {
class Input;
}
namespace engine::graphics {
class ModelCommon;
}

namespace engine::game {
using engine::AABB;
using engine::Collider;
using engine::Input;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Skeleton;
using engine::graphics::SkinCommon;
using engine::graphics::SkinnedModel;
using engine::graphics::SkinnedObject3d;

enum class WeaponType; // Weapon.h で定義
enum class GunType; // Weapon.h で定義

/**
 * @brief プレイヤーキャラクターを制御するクラス
 * @note スタイリッシュアクション（コンボ・ブリンク・連射・覚醒乱舞）と
 * ローグライト用のスキル補正（SkillMods）を統合管理する
 */
class Player {
public:
    // ══════════════════════════════════════════════════════
    // 公開型とライフサイクル
    // ══════════════════════════════════════════════════════

    /** @brief 覚醒乱舞の進行フェーズ */
    enum class RampagePhase { Inactive,
        Launch,
        Juggle };

    /**
     * @brief ローグライトのスキルによる各種パラメータ補正を保持する構造体
     * @note RunData のスキル一覧を ApplySkillMods() に渡して適用する
     */
    struct SkillMods {
        float blinkDistMult = 1.0f; ///< ブリンク距離の倍率
        int comboMaxBonus = 0; ///< コンボ最大数への加算
        float fireIntervalMult = 1.0f; ///< 連射間隔の倍率（<1 = 速く）
        float gaugeChargeMult = 1.0f; ///< 覚醒ゲージ蓄積量の倍率
        float speedMult = 1.0f; ///< 移動速度の倍率
        float jumpMult = 1.0f; ///< ジャンプ力の倍率
        int juggleMaxBonus = 0; ///< 乱舞スラッシュ数への加算
    };

    /**
     * @brief プレイヤーを初期化する
     * @param modelCommon モデル共通設定のポインタ
     */
    void Initialize(ModelCommon* modelCommon);

    /**
     * @brief 入力に基づいてプレイヤーの物理とアクションを毎フレーム更新する
     * @param input    入力マネージャー
     * @param enemyPos 乱舞スラッシュのターゲット座標（デフォルトは原点）
     */
    void Update(Input* input, const Vector3& enemyPos = { });

    /** @brief プレイヤーモデルを描画する */
    void Draw();

    /**
     * @brief 見た目のトランスフォーム行列だけを再計算する（ゲームロジックは一切進めない）
     * @note Object3dのUpdate()はカメラのVP行列込みで定数バッファを書くため、
     * ステージエディタ中など本体Updateを止めたままカメラだけ動く状況でこれを呼ばないと
     * 古いカメラ行列のまま描画されて、モデルが画面に張り付いて見える
     */
    void RefreshVisualTransforms();

    /**
     * @brief ステージ上のsolidブロックとの当たり判定を解決する（Update()の後に毎フレーム呼ぶ）
     * @param blocks StageEditor::GetSolidColliders()等で得たワールドAABB一覧
     * @note 上に乗れば着地・横から当たれば壁として押し出す。ブロックを動かせば次フレームから追従する
     */
    void ResolveBlockCollision(const std::vector<AABB>& blocks);

    /** @brief 乱舞フェーズを強制終了する（外部から撃破時などに呼ぶ） */
    void EndRampage()
    {
        if (rampagePhase_ == RampagePhase::Juggle) {
            rampagePhase_ = RampagePhase::Inactive;
        }
    }

    /** @brief 武器奪取の刺突モーションを頭から再生する（外部トリガー用、既存の斬撃を流用） */
    void PlayStealStab();

    /**
     * @brief ローグライトのスキル補正を適用する
     * @param mods RunData のスキル一覧から計算した補正値
     */
    void ApplySkillMods(const SkillMods& mods) { skillMods_ = mods; }

    /** @brief 現在のワールド座標を返す */
    const Vector3& GetPosition() const { return pos_; }
    /** @brief スポーン位置を上書きする（Initialize 直後に呼ぶこと） */
    void SetPosition(const Vector3& pos) { pos_ = pos; }
    void SetHorizontalBounds(float minX, float maxX)
    {
        minX_ = minX;
        maxX_ = maxX;
    }
    /** @brief StageEditorのギズモドラッグ等、外部から直接書き換えるための可変参照 */
    Vector3& GetPositionRef() { return pos_; }
    /**
     * @brief 水面のY座標を設定する（WaterPool::GetSurfaceY() の値を渡す）
     * @note 呼ばない場合は水中判定が無効のまま（水なしステージ用のデフォルト）
     */
    void SetWaterLevel(float waterLevelY) { waterLevel_ = waterLevelY; }
    /** @brief 残像・分身演出用の静的モデル（ボーンなし、現在のフォームの見た目）のポインタを返す */
    Model* GetModel() const { return rig_->staticModel.get(); }

    /**
     * @brief プレイヤーの現在位置から AABB コライダーを生成して返す
     * @return 当たり判定に使用する Collider
     */
    Collider GetCollider() const
    {
        Collider c;
        c.SetAsAABB({ { pos_.x - 0.5f, pos_.y - 0.5f, -0.5f },
            { pos_.x + 0.5f, pos_.y + 0.5f, 0.5f } });
        return c;
    }
    /**
     * @brief 被弾時の無敵時間を開始する
     * @note 敵の攻撃判定がヒットした際に GamePlayScene 側から呼ぶ
     */
    void OnHit() { invincibleTimer_ = kInvincibleDuration_; }
    /** @brief 被弾直後の無敵時間中かどうかを返す */
    bool IsInvincible() const { return invincibleTimer_ > 0.0f; }

    bool IsOnGround() const { return onGround_; } ///< 地面に接触中か
    void SetVisualPreset(int preset) { visualPreset_ = std::clamp(preset, -1, 1); }
    int GetVisualPreset() const { return visualPreset_; }
    /** @brief 差し替え用の静的モデル（とテクスチャ）を設定する。texturePathを省略すると白テクスチャになる */
    void SetStaticVisualModel(const std::string& modelPath, const std::string& texturePath = "");
    const std::string& GetStaticVisualModelPath() const { return staticOverrideModelPath_; }
    const std::string& GetStaticVisualTexturePath() const { return staticOverrideTexturePath_; }
    /** @brief 手持ち武器（近接・銃）の描画有無を切り替える（撮影用に一時的に隠したい場合など） */
    void SetWeaponsVisible(bool visible) { weaponsVisible_ = visible; }
    bool IsInWater() const { return inWater_; } ///< 水中にいるか
    bool JustJumped() const { return justJumped_; } ///< このフレームにジャンプしたか
    bool JustLanded() const { return justLanded_; } ///< このフレームに着地したか
    bool JustEnteredWater() const { return justEnteredWater_; } ///< このフレームに入水したか
    bool JustExitedWater() const { return justExitedWater_; } ///< このフレームに出水したか

    /** @brief 覚醒ゲージの現在値を返す（0.0〜1.0） */
    float GetAwakenGauge() const { return awakenGauge_; }
    /** @brief 覚醒状態かどうかを返す */
    bool IsAwakened() const { return isAwakened_; }

    /**
     * @brief 攻撃が実際に命中した際にシーン側の当たり判定から呼び、覚醒ゲージを加算する
     * @note 空振りでは溜まらないよう、振った瞬間ではなく命中確定フレームで呼ぶこと
     * @param amount 加算量（skillMods_.gaugeChargeMult 込みで計算される）
     */
    void ChargeAwakenGauge(float amount)
    {
        if (!isAwakened_) {
            awakenGauge_ = (std::min)(awakenGauge_ + amount * skillMods_.gaugeChargeMult, 1.0f);
        }
    }

    // スタイル技フラグ（その1フレームだけ true）
    bool JustComboHit() const { return justComboHit_; } ///< コンボヒット発生フレーム
    bool JustWeaponSwitchHit() const { return justWeaponSwitchHit_; }
    int GetComboStep() const { return comboStep_; } ///< 現在のコンボステップ（1始まり、打ち上げ技は0）
    bool JustFired() const { return justFired_; } ///< 射撃発生フレーム
    bool JustDaggerStingerHit() const { return justDaggerStingerHit_; } ///< ダガー: スティンガー各刺突のヒット発生フレーム
    int GetDaggerStingerHitIndex() const { return daggerStingerHitIndex_; } ///< 現在の刺突番号（0始まり、非発動中は-1）
    bool JustChargedGauge() const { return justChargedGauge_; } ///< ゲージチャージ発生フレーム
    bool JustAwakened() const { return justAwakened_; } ///< 覚醒発動フレーム
    float GetLastDirX() const { return lastDirX_; } ///< 最後に入力した横方向（+1=右, -1=左）
    /** @brief 指定位置の方向へ向きだけ強制的に振り向かせる（ロックオン中、移動入力に関係なく敵の方を向かせる用） */
    void FaceTarget(const Vector3& targetPos) { lastDirX_ = (targetPos.x >= pos_.x) ? 1.0f : -1.0f; }
    /** @brief スキル補正込みのコンボ最大数を返す（現在の武器の地上コンボ段数基準） */
    int GetComboMax() const;

    /**
     * @brief 進行中の近接攻撃の定義を返す（攻撃していなければ nullptr）
     * @note JustComboHit() のフレームにダメージ・ノックバック・打ち上げ・技IDの参照に使う
     */
    const MeleeAttackDef* GetActiveMeleeAttack() const { return meleeCombo_.GetActive(); }
    /** @brief 近接コンボのモーション中か */
    bool IsMeleeAttacking() const { return meleeCombo_.IsAttacking(); }

    /**
     * @brief 進行中の射撃コンボ段の定義を返す（撃っていなければ nullptr）
     * @note JustFired() のフレームに弾数・拡散・射程倍率・ノックバック・技IDの参照に使う
     */
    const GunShotDef* GetActiveGunShot() const { return gunCombo_.GetActive(); }
    /** @brief 射撃コンボのモーション中か */
    bool IsGunShooting() const { return gunCombo_.IsShooting(); }
    /** @brief 現在の射撃コンボの段数を返す（1始まり） */
    int GetGunComboStep() const { return gunCombo_.GetStep(); }

    // スペースキー スピン連射
    bool JustSpinShot() const { return justSpinShot_; } ///< スピン連射発生フレーム
    bool IsUpsideDown() const { return isUpsideDown_; } ///< 逆さま状態か
    float GetSpinAngle() const { return spinAngle_; } ///< スピン角度（度、0=正立, 180=逆さ）

    // 固有技（スペースキー、武器種別ごと。Dagger/Hammer/Ball は上の Just～ を使う）
    bool JustSwordDash() const { return justSwordDash_; } ///< ソード: 瞬迅斬り（ダッシュ斬り）発生フレーム
    bool JustSpearRetreat() const { return justSpearRetreat_; } ///< スピア: 間合い外し（後退突き）発生フレーム
    bool JustGreatswordSlam() const { return justGreatswordSlam_; } ///< グレートソード/ハンマー共通: 大地砕き発生フレーム
    /** @brief グレートソード: 投げ回転斬りで大剣が静止し渦を巻いている最中か（飛行中・帰還中はfalse） */
    bool IsGreatswordSpinning() const
    {
        if (!greatswordThrowActive_) {
            return false;
        }
        const float spinElapsed = greatswordThrowTimer_ - kGreatswordThrowTravelTime_;
        return spinElapsed >= 0.0f && spinElapsed < kGreatswordVortexMaxDuration_;
    }
    bool JustGreatswordSpinHit() const { return justGreatswordSpinHit_; } ///< グレートソード: 渦の多段ヒット発生フレーム
    bool JustGreatswordThrown() const { return justGreatswordThrown_; } ///< グレートソード: 投げた瞬間のフレーム（発射エフェクト用）
    const Vector3& GetGreatswordThrowPos() const { return greatswordThrowPos_; } ///< 投げた大剣が静止している位置（渦の中心）
    bool JustAxeCharge() const { return justAxeCharge_; } ///< アックス: バーサーク突進発生フレーム
    bool IsAxeEnraged() const { return axeRageTimer_ > 0.0f; } ///< アックス: 突進後の怒り強化中か
    /** @brief アックスの怒り強化中に攻撃力へ掛けるべき倍率を返す（強化中でなければ1.0） */
    float GetAxeRageMult() const { return IsAxeEnraged() ? kAxeRageDamageMult_ : 1.0f; }
    bool IsScytheHovering() const { return isScytheHovering_; } ///< シザー: 滞空ホバー中か
    bool JustScytheSpin() const { return justScytheSpin_; }

    // 覚醒乱舞（Sword + 覚醒 + L）
    bool IsRampaging() const { return rampagePhase_ != RampagePhase::Inactive; } ///< 乱舞中か
    bool JustLaunched() const { return justLaunched_; } ///< 打ち上げ発生フレーム
    bool JustRampageHit() const { return justRampageHit_; } ///< 乱舞スラッシュヒットフレーム
    bool JustRampageFinish() const { return justRampageFinish_; } ///< 乱舞終了フレーム
    int GetJuggleCount() const { return juggleSlashCount_; } ///< 現在の乱舞スラッシュ回数
    /** @brief スキル補正込みの乱舞最大スラッシュ数を返す */
    int GetJuggleMax() const { return kJuggleMaxSlashes_ + skillMods_.juggleMaxBonus; }

    // フィニッシャースラッシュ（覚醒ゲージ満タン + F）
    bool JustFinisherSlash() const { return justFinisherSlash_; } ///< フィニッシャースラッシュ発動フレーム

private:
    // ══════════════════════════════════════════════════════
    // 物理設定と状態データ
    // ══════════════════════════════════════════════════════

    // 通常物理
    static constexpr float kGroundY_ = 0.4f;
    static constexpr float kCeilingY_ = 12.0f;
    // 通常ステージの左右端はStageEditorのsolid配置物で制御する
    float minX_ = -1000.0f;
    float maxX_ = 1000.0f;
    static constexpr float kGravity_ = 0.012f;
    static constexpr float kJumpPower_ = 0.4f;
    static constexpr float kSpeed_ = 0.15f;
    // 敵とめり込まないための最小X距離。両者のAABB半幅は0.5+0.5=1.0だが、そこまで離すと
    // GamePlayScene の接触ダメージ判定（プレイヤーAABBと敵AABBの重なりで発生）が一切当たらなくなるため、
    // 完全な重なり(0)は防ぎつつ接触判定用の重なりしろは残す0.75にしている
    static constexpr float kMinEnemyDistanceX_ = 0.75f;
    // 上記のY版。ダミー/ナイトのAABBはY方向にも高さを持つため、X距離だけで判定すると
    // ジャンプで頭上を飛び越えている最中まで押し出されてしまう（縦に無限のめり込み防止扱いになる）。
    // ダミーのAABBは概ね±0.5なので、それより少し広い程度に留めてジャンプ回避を潰さない
    static constexpr float kMinEnemyDistanceY_ = 0.75f;

    // 水中物理（水なしステージでは無効化された状態のままにする）
    static constexpr float kWaterLevelDisabled_ = -1.0f;
    static constexpr float kWaterGravity_ = 0.003f; // 浮力で弱い沈下加速度
    static constexpr float kWaterSpeed_ = 0.10f; // 水中横移動速度
    static constexpr float kSwimAccel_ = 0.025f; // 長押しで上昇する加速度
    static constexpr float kSwimMaxVY_ = 0.28f; // 上昇最大速度
    static constexpr float kSinkMaxVY_ = -0.12f; // 沈下最大速度
    static constexpr float kWaterEntryImpactDamping_ = 0.4f; // 入水時に落下速度を大きく減衰させる倍率

    Vector3 pos_ = { 8.0f, 0.4f, 0.0f };
    float waterLevel_ = kWaterLevelDisabled_; // SetWaterLevel() で上書きされるまで水中判定は無効
    float velocityY_ = 0.0f;
    float groundVisualCorrection_ = 0.0f; // 実ブロック床と通常地面の高さ差を見た目だけへ反映
    bool onGround_ = true;
    int visualPreset_ = -1; // -1=自動、0=Alien、1=Mech
    bool prevOnGround_ = true;
    bool justJumped_ = false;
    bool justLanded_ = false;

    bool inWater_ = false;
    bool prevInWater_ = false;
    bool justEnteredWater_ = false;
    bool justExitedWater_ = false;

    // 被弾後の無敵時間
    float invincibleTimer_ = 0.0f;
    static constexpr float kInvincibleDuration_ = 1.0f; // 被弾後の無敵時間（秒）

    // 向き（最後に入力した横方向+1=右 -1=左）
    float lastDirX_ = 1.0f;

    // 覚醒ゲージ
    float awakenGauge_ = 0.0f;
    bool isAwakened_ = false;
    float awakenTimer_ = 0.0f;
    static constexpr float kAwakenDuration_ = 8.0f;
    static constexpr float kAwakenActivationThreshold_ = 0.3f; // このゲージ量が溜まっていれば発動可能

    // スタイル技（L / K キー）
    bool justComboHit_ = false;
    bool justWeaponSwitchHit_ = false;
    bool weaponSwitchAttackPending_ = false;
    bool weaponSwitchAttackActive_ = false;
    float weaponSwitchWindow_ = 0.0f;
    static constexpr float kWeaponSwitchWindow_ = 0.75f;
    static constexpr float kWeaponSwitchLunge_ = 0.8f;
    int comboStep_ = 0;
    bool justFired_ = false;
    bool justChargedGauge_ = false;
    bool justAwakened_ = false;
    static constexpr float kGaugeCharge_ = 0.18f;

    // 近接コンボ（武器タイプ別の段・タイミング・スイングは MeleeCombo.cpp のテーブルが持つ）
    MeleeComboController meleeCombo_;
    // 射撃コンボ（銃種別の段・弾数・リコイルは GunCombo.cpp のテーブルが持つ）
    GunComboController gunCombo_;
    float launchFollowTimer_ = 0.0f; ///< 打ち上げ直後の追撃ジャンプ強化の残り秒数
    static constexpr float kLaunchFollowWindow_ = 0.45f;
    static constexpr float kLaunchFollowJumpMult_ = 1.35f;
    static constexpr float kAirAttackFallDamping_ = 0.5f; // 空中攻撃中の落下速度減衰倍率（エアコンボを繋ぎやすくする）

    // スペースキー スピン連射
    bool justSpinShot_ = false;
    bool isUpsideDown_ = false;
    float spinAngle_ = 0.0f; // 度0=正立, 180=逆さ
    float shootCooldown_ = 0.0f;
    static constexpr float kShootInterval_ = 0.12f; // 連射間隔（秒）
    static constexpr float kSpinSpeed_ = 5.0f; // 空中回転速度（度/フレーム）

    // 固有技（スペースキー、武器種別ごと）
    // 共通: 瞬間移動に見えないよう、開始位置→目標位置へ短時間でイージング移動する水平ダッシュ
    // （踏み込み/後退/突進系の全技が共有する。ヒット判定は移動完了フレームで発生させること）
    struct DashMotion {
        bool active = false;
        float timer = 0.0f;
        float startX = 0.0f;
        float targetX = 0.0f;
    };
    static constexpr float kDashDuration_ = 0.10f; // どの技もこの秒数で移動しきる
    /** @brief 現在位置からworldDeltaXぶん先（minX_/maxX_でクランプ）へダッシュを開始する */
    void BeginDash(DashMotion& dash, float worldDeltaX);
    /** @brief ダッシュを1フレーム進める（pos_.xを更新）。ちょうど完了したフレームだけtrueを返す */
    bool AdvanceDash(DashMotion& dash);

    // ダガー: スティンガー（踏み込みながら3連続で刺す多段突き。数合わせでなく手数武器らしい連続ヒットを体感させる）
    bool justDaggerStingerHit_ = false;
    int daggerStingerHitIndex_ = -1; // -1=非発動中、0始まりで刺突が進む
    float daggerStingerTimer_ = 0.0f;
    float daggerStingerCooldown_ = 0.0f;
    DashMotion daggerStingerDash_; ///< 1段目が突き刺さるまでの踏み込み
    static constexpr float kDaggerStingerDashDist_ = 3.0f;
    static constexpr float kDaggerStingerHitInterval_ = 0.09f; // 刺突ごとの間隔（秒）
    static constexpr int kDaggerStingerHitCount_ = 3;
    static constexpr float kDaggerStingerCooldown_ = 0.9f;
    // ソード: 瞬迅斬り（短距離ダッシュ斬り、全能武器らしく癖のない攻守一体の一撃）
    bool justSwordDash_ = false;
    float swordSkillCooldown_ = 0.0f;
    DashMotion swordDash_;
    static constexpr float kSwordDashDist_ = 2.0f;
    static constexpr float kSwordSkillCooldown_ = 0.55f;
    // スピア: 間合い外し（後退しながら突く、牽制役らしいヒットアンドアウェイ）
    bool justSpearRetreat_ = false;
    float spearSkillCooldown_ = 0.0f;
    DashMotion spearDash_;
    static constexpr float kSpearRetreatDist_ = 2.2f;
    static constexpr float kSpearSkillCooldown_ = 0.75f;
    // グレートソード/ハンマー共通: 大地砕き（設置型の叩きつけAoE、地上限定・重量級らしい長めのクールタイム）
    bool justGreatswordSlam_ = false;
    float greatswordSkillCooldown_ = 0.0f;
    static constexpr float kGreatswordSkillCooldown_ = 1.3f;
    // グレートソード専用: 投げ回転斬り（自身を投げ、途中で静止して渦のように回転し周囲を吸い込みながら多段ヒット）
    bool justGreatswordSpinHit_ = false;
    bool justGreatswordThrown_ = false;
    bool greatswordThrowActive_ = false; ///< 投げてから渦が終わるまでtrue（飛行中も含む）
    Vector3 greatswordThrowStartPos_ { }; ///< 投げた瞬間の手元位置（飛行中の補間の始点）
    Vector3 greatswordThrowPos_ { }; ///< 大剣が静止する位置（渦の中心、飛行中は補間の終点）
    float greatswordThrowTimer_ = 0.0f; ///< 投げてからの経過秒（飛行→渦の順で進む）
    float greatswordSpinHitTimer_ = 0.0f; ///< 渦の中でのヒット間隔用タイマー
    float greatswordThrowCooldown_ = 0.0f;
    static constexpr float kGreatswordThrowDist_ = 4.0f; ///< 静止するまでの距離
    static constexpr float kGreatswordThrowTravelTime_ = 0.12f; ///< 投げてから静止するまでの飛行時間（秒）
    // 渦は「もう一度スペースを押す」か「この時間が経つ」のどちらか早い方で終わる
    // （手動リコール前提にすると出しっぱなしが強すぎるため、上限として最後まで残す）
    static constexpr float kGreatswordVortexMaxDuration_ = 10.0f;
    static constexpr float kGreatswordSpinHitInterval_ = 0.15f; ///< 渦の中でのヒット間隔（秒）
    static constexpr float kGreatswordThrowCooldown_ = 1.6f;
    Vector3 greatswordReturnTargetPos_ { }; ///< 帰還先（渦が終わった瞬間の手元位置を1回だけ記録する）
    bool greatswordReturnCaptured_ = false;
    static constexpr float kGreatswordReturnTime_ = 0.2f; ///< 渦の終了後、手元へ飛んで戻るまでの時間（秒）
    // シザー: 滞空ホバー（空中限定、時間制のリソースで無限滞空を防ぐ）
    bool isScytheHovering_ = false;
    bool justScytheSpin_ = false;
    float scytheHoverTimer_ = kScytheHoverMax_;
    static constexpr float kScytheHoverMax_ = 0.9f; // 最大連続ホバー時間（秒）
    static constexpr float kScytheHoverRecoverRate_ = 2.0f; // 接地中の回復速度倍率
    static constexpr float kScytheHoverVYCap_ = -0.02f; // ホバー中の落下速度の下限
    // アックス: バーサーク突進（突進しつつ命中で一定時間ダメージ強化、狂戦士らしいリスク覚悟の一撃）
    bool justAxeCharge_ = false;
    float axeSkillCooldown_ = 0.0f;
    float axeRageTimer_ = 0.0f;
    DashMotion axeDash_;
    static constexpr float kAxeChargeDist_ = 2.3f;
    static constexpr float kAxeSkillCooldown_ = 1.0f;
    static constexpr float kAxeRageDuration_ = 2.5f;
    static constexpr float kAxeRageDamageMult_ = 1.3f;

    // 覚醒乱舞（Sword + 覚醒 + L）
    RampagePhase rampagePhase_ = RampagePhase::Inactive;
    bool justLaunched_ = false;
    bool justRampageHit_ = false;
    bool justRampageFinish_ = false;
    int juggleSlashCount_ = 0; // 今回の乱舞で何回切ったか
    int juggleAngleIdx_ = 0; // 次のスラッシュ角度インデックス
    static constexpr float kRampageSpeed_ = 0.45f; // 打ち上げ突進速度
    static constexpr float kJuggleRadius_ = 2.5f; // 敵からのスラッシュ距離
    static constexpr int kJuggleMaxSlashes_ = 8; // 乱舞の最大回数

    // フィニッシャースラッシュ（覚醒ゲージ満タン + F）
    bool justFinisherSlash_ = false;

    // スキル補正（ローグライト）
    SkillMods skillMods_;

    // Physics State パターン
    // 水中/水上で横移動・重力・ジャンプの処理を切り替える
    /** @brief 環境ごとの移動と重力処理を抽象化する物理状態 */
    class IPhysicsState {
    public:
        virtual ~IPhysicsState() = default;
        virtual void Update(Player& player, Input* input) const = 0;
    };
    /** @brief 地上環境の移動と重力処理を適用する物理状態 */
    class GroundedPhysicsState : public IPhysicsState {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief 水中環境の浮力と移動処理を適用する物理状態 */
    class UnderwaterPhysicsState : public IPhysicsState {
    public:
        void Update(Player& player, Input* input) const override;
    };
    static const IPhysicsState& GetPhysicsState(bool inWater);

    // Rampage State パターン
    // 覚醒乱舞の進行フェーズ（RampagePhase）ごとに L キー入力の意味と
    // 毎フレームの物理更新内容を切り替える
    /** @brief 覚醒乱舞の段階固有処理を抽象化する状態 */
    class IRampageState {
    public:
        virtual ~IRampageState() = default;
        virtual void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const = 0;
        virtual void UpdatePhysics(Player& player, const Vector3& enemyPos) const = 0;
    };
    /** @brief 覚醒乱舞を開始していない通常状態 */
    class InactiveRampageState : public IRampageState {
    public:
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override;
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override { }
    };
    /** @brief 覚醒乱舞の打ち上げ段階を処理する状態 */
    class LaunchRampageState : public IRampageState {
    public:
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override { }
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override;
    };
    /** @brief 覚醒乱舞の空中追撃段階を処理する状態 */
    class JuggleRampageState : public IRampageState {
    public:
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override;
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override;
    };
    static const IRampageState& GetRampageState(RampagePhase phase);

    // Weapon Behavior Strategy パターン
    // 武器種別ごとのスペースキー挙動（ブリンク/ゲージチャージ/スピン連射）を切り替える
    /** @brief 装備武器ごとの固有更新を抽象化するStrategy */
    class IWeaponBehavior {
    public:
        virtual ~IWeaponBehavior() = default;
        virtual void Update(Player& player, Input* input) const = 0;
    };
    /** @brief 短剣の高速移動と攻撃挙動を適用するStrategy */
    class DaggerBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief ハンマーの重量攻撃挙動を適用するStrategy */
    class HammerBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief ボール武器の固有挙動を適用するStrategy */
    class BallBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief 剣の突進攻撃挙動を適用するStrategy */
    class SwordBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief 槍の間合い制御を適用するStrategy */
    class SpearBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief 大剣の叩きつけ挙動を適用するStrategy */
    class GreatswordBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief 鎌の広範囲攻撃挙動を適用するStrategy */
    class ScytheBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief 斧の溜め攻撃挙動を適用するStrategy */
    class AxeBehavior : public IWeaponBehavior {
    public:
        void Update(Player& player, Input* input) const override;
    };
    /** @brief 固有処理を持たない武器へ共通挙動を適用するStrategy */
    class DefaultWeaponBehavior : public IWeaponBehavior {
    public:
        void Update(Player&, Input*) const override { }
    };
    static const IWeaponBehavior& GetWeaponBehavior(WeaponType type);

    // 覚醒残像
    AfterImageRenderer afterImageRenderer_;

    // アウトラインパス後に通常描画 PSO へ戻すために保持（Player::Draw() で使用）
    ModelCommon* modelCommon_ = nullptr;
    std::unique_ptr<Model> staticOverrideModel_;
    std::unique_ptr<Object3d> staticOverrideObject_;
    float staticOverrideFootOffset_ = 0.0f; // モデル原点から最下点までの距離（スケール後）中心原点のモデルでも足元を合わせる
    std::string staticOverrideModelPath_;
    std::string staticOverrideTexturePath_;
    bool weaponsVisible_ = true; // falseなら近接武器・銃の描画をスキップする

    // スキンメッシュ描画の共通設定（両フォームのリグで共有）
    std::unique_ptr<SkinCommon> skinCommon_;

    // 見た目1体ぶんのリグ。通常時と覚醒中でモデルごと差し替えるため、
    // アニメーションや武器アタッチ先ボーン名などモデル依存の情報をセットで持つ
    // （アセットパス・アニメ名の定義は CharacterVisuals.h の kNormalRigVisual / kAwakenedRigVisual）
    /** @brief 通常/覚醒フォーム1体ぶんのモデル・アニメーション・武器アタッチ用ボーン名一式 */
    struct CharacterRig {
        std::unique_ptr<Model> staticModel; ///< 残像・分身演出用（ボーンなし、本体と同じ見た目）
        std::unique_ptr<SkinnedModel> skinnedModel; ///< 本体描画（ボーンアニメーション付き）
        std::unique_ptr<SkinnedObject3d> object;
        float modelScale = 1.0f;
        float modelOffsetY = 0.0f; ///< モデル原点（足元）を AABB 中心の pos_ に合わせる下げ幅
        const char* meleeBoneName = ""; ///< 近接武器のアタッチ先ボーン
        const char* gunBoneName = ""; ///< 銃のアタッチ先ボーン
        Animation idleAnim;
        Animation runAnim;
        Animation jumpAnim;
        Animation runningJumpAnim; ///< 移動しながらのジャンプ
        Animation swimAnim; ///< 水中
        Animation idleHoldAnim; ///< 武器持ち待機（構え系スタイル）
        Animation runHoldAnim; ///< 武器持ち走り（構え系スタイル）
        Animation slashAnim; ///< 斬撃（ソード攻撃・乱舞・フィニッシャー）
        Animation punchAnim; ///< パンチ（ソード以外のスタイルのコンボ）
    };
    CharacterRig normalRig_; ///< 通常フォーム
    CharacterRig awakenedRig_; ///< 覚醒フォーム（メカ）
    CharacterRig* rig_ = &normalRig_; ///< 現在表示中のリグ（覚醒の開始/終了で切り替え）

    // 右手ボーンに持たせる近接武器（現在のスタイルに対応する1つだけ表示）
    // 種類が多いため個別メンバーではなくテーブルで持つ（追加は CharacterVisuals.h の kHeldWeaponVisuals）
    /** @brief 右手に持たせる近接武器1種ぶんのモデルと、握らせる際のローカルスケール/回転/位置 */
    struct HeldWeaponSlot {
        WeaponType type;
        std::unique_ptr<Model> model;
        std::unique_ptr<Object3d> object;
        Vector3 gripScale;
        Vector3 gripRotate;
        Vector3 gripTranslate;
    };
    std::vector<HeldWeaponSlot> heldWeapons_;
    int activeHeldIndex_ = -1; ///< 現在表示中の heldWeapons_ インデックス（-1=非表示）

    /** @brief グレートソード投げ回転斬りの飛行中/渦の間、手のボーンから外して位置・回転を直接動かす */
    void UpdateThrownGreatswordVisual(HeldWeaponSlot& slot);

    /** @brief 右手ボーンに武器オブジェクトを追従させる（握りローカル行列は呼び出し側で指定） */
    void AttachHeldWeapon(Object3d* obj, const char* boneName,
        const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate);

    // 銃装備（左手ボーンに毎フレーム追従、G キーで切り替えた1丁だけ表示）
    // 近接の heldWeapons_ と同じテーブル方式（追加は CharacterVisuals.h の kGunVisuals）
    struct GunSlot {
        GunType type;
        std::unique_ptr<Model> model;
        std::unique_ptr<Object3d> object;
        Vector3 gripScale;
        Vector3 gripRotate;
        Vector3 gripTranslate;
    };
    std::vector<GunSlot> guns_;
    int activeGunIndex_ = -1; ///< 現在表示中の guns_ インデックス（-1=非表示）
    bool gunVisible_ = true; ///< 銃のアタッチ先ボーンが見つかったか

    /** @brief 再生中のアニメーション状態 */
    enum class AnimState { Idle,
        Run,
        Jump,
        Swim,
        Attack };
    AnimState animState_ = AnimState::Idle;
    bool animHold_ = false; ///< 武器持ちバリエーション（IdleHold/RunHold）を再生中か
    float attackAnimTimer_ = 0.0f; ///< 攻撃モーションの残り再生秒数（0以下で通常状態へ復帰）

    // フィニッシャー 静止集中 → 一閃
    bool finisherCharging_ = false; ///< 静止して溜めている最中か
    float finisherChargeTimer_ = 0.0f; ///< 残り溜め時間（0以下で解放＝一閃へ）

    /** @brief 現在の移動・接地状況から再生すべきアニメーション状態を切り替える */
    void UpdateAnimationState(bool isMoving);

    /** @brief 攻撃モーションを頭から再生し、再生し切るまで状態遷移をロックする */
    void PlayAttackAnim(const Animation& anim, float speed);

    // Update() 分割ヘルパー（呼び出し順に定義、詳細は Player.cpp 参照）
    /** @brief 毎フレーム冒頭でJust～系の単発フラグ（ジャンプ・着地・被弾等）を全てリセットする */
    void ResetFrameFlags();
    /**
     * @brief 数字キー/十字キーによる武器スロット切り替えを処理する
     * @param input 入力マネージャー
     */
    void HandleStyleSwitch(Input* input);
    /**
     * @brief 銃切り替え（Gキー）と射撃コンボ（Kキー）の入力、および射撃反動の移動を処理する
     * @param input 入力マネージャー
     */
    void HandleRangedCombat(Input* input);
    /**
     * @brief 近接攻撃コンボ・打ち上げ・乱舞（Lキー）の入力受付とコンボ進行を処理する
     * @param input 入力マネージャー
     * @param enemyPos 乱舞攻撃のターゲット座標
     */
    void HandleMeleeCombat(Input* input, const Vector3& enemyPos);
    /**
     * @brief フィニッシャースラッシュ（Fキー）の発動条件判定と、静止溜め→解放までの進行を処理する
     * @param input 入力マネージャー
     */
    void HandleFinisherSlash(Input* input);
    /**
     * @brief スペースキーによる武器固有技（ダッシュ斬り・間合い外し・大地砕き等）の入力とクールダウンを処理する
     * @param input 入力マネージャー
     */
    void HandleWeaponSkill(Input* input);
    /**
     * @brief 覚醒乱舞の現フェーズに応じた突進/追撃物理を進め、ヒットフレームで斬撃モーションを再生する
     * @param enemyPos 乱舞スラッシュのターゲット座標
     */
    void UpdateRampagePhysics(const Vector3& enemyPos);
    /**
     * @brief 覚醒の発動判定（Rキー）とゲージ消費、時間切れ/ゲージ切れによる覚醒終了を処理する
     * @param input 入力マネージャー
     */
    void UpdateAwakenState(Input* input);
    /**
     * @brief 乱舞中でない時、敵と一定距離以上重ならないようX座標を押し出す
     * @param enemyPos 押し出し判定に使う敵の座標
     */
    void ResolveEnemyOverlap(const Vector3& enemyPos);
    /** @brief 水面Yとの比較で入水/出水を判定し、入水時は落下速度を減衰させる */
    void UpdateWaterState();
    /**
     * @brief 覚醒残像・アニメーション状態・プレイヤー色などUpdate()末尾の表示関連処理をまとめて更新する
     * @param input 入力マネージャー（移動入力の参照に使用）
     */
    void UpdateVisualState(Input* input);
    /** @brief 現在のスタイルに対応する近接武器と銃を、それぞれの手ボーンへ追従させる */
    void AttachActiveWeapons();
};

} // namespace engine::game
