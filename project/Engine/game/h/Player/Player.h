/**
 * @file Player.h
 * @brief プレイヤーキャラクターの物理・アクション・スタイルシステムを定義するファイル
 */
#pragma once
#include "AfterImageRenderer.h"
#include "CollisionConfig.h"
#include "Model.h"
#include "Object3d.h"
#include <memory>
namespace engine { class Input; }
namespace engine::graphics { class ModelCommon; }

namespace engine::game {
using engine::Collider;
using engine::AABB;
using engine::graphics::Model;
using engine::graphics::Object3d;
using engine::Input;
using engine::graphics::ModelCommon;

enum class WeaponType; // Weapon.h で定義

/**
 * @brief プレイヤーキャラクターを制御するクラス
 * @note スタイリッシュアクション（コンボ・ブリンク・連射・覚醒乱舞）と
 * ローグライト用のスキル補正（SkillMods）を統合管理する。
 */
class Player {
public:
    /** @brief 覚醒乱舞の進行フェーズ */
    enum class RampagePhase { Inactive, Launch, Juggle };

    /**
     * @brief ローグライトのスキルによる各種パラメータ補正を保持する構造体
     * @note RunData のスキル一覧を ApplySkillMods() に渡して適用する
     */
    struct SkillMods {
        float blinkDistMult    = 1.0f; ///< ブリンク距離の倍率
        int   comboMaxBonus    = 0;    ///< コンボ最大数への加算
        float fireIntervalMult = 1.0f; ///< 連射間隔の倍率（<1 = 速く）
        float gaugeChargeMult  = 1.0f; ///< 覚醒ゲージ蓄積量の倍率
        float speedMult        = 1.0f; ///< 移動速度の倍率
        float jumpMult         = 1.0f; ///< ジャンプ力の倍率
        int   juggleMaxBonus   = 0;    ///< 乱舞スラッシュ数への加算
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
    void Update(Input* input, const Vector3& enemyPos = {});

    /** @brief プレイヤーモデルを描画する */
    void Draw();

    /** @brief 乱舞フェーズを強制終了する（外部から撃破時などに呼ぶ） */
    void EndRampage() { if (rampagePhase_ == RampagePhase::Juggle) { rampagePhase_ = RampagePhase::Inactive; } }

    /**
     * @brief ローグライトのスキル補正を適用する
     * @param mods RunData のスキル一覧から計算した補正値
     */
    void ApplySkillMods(const SkillMods& mods) { skillMods_ = mods; }

    /** @brief 現在のワールド座標を返す */
    const Vector3& GetPosition() const { return pos_; }
    /** @brief スポーン位置を上書きする（Initialize 直後に呼ぶこと） */
    void SetPosition(const Vector3& pos) { pos_ = pos; }
    /**
     * @brief 水面のY座標を設定する（WaterPool::GetSurfaceY() の値を渡す）
     * @note 呼ばない場合は水中判定が無効のまま（水なしステージ用のデフォルト）
     */
    void SetWaterLevel(float waterLevelY) { waterLevel_ = waterLevelY; }
    /** @brief プレイヤーが使用しているモデルのポインタを返す */
    Model* GetModel() const { return model_.get(); }

    /**
     * @brief プレイヤーの現在位置から AABB コライダーを生成して返す
     * @return 当たり判定に使用する Collider
     */
    Collider GetCollider() const {
        Collider c;
        c.SetAsAABB({ { pos_.x - 0.5f, pos_.y - 0.5f, -0.5f },
                      { pos_.x + 0.5f, pos_.y + 0.5f,  0.5f } });
        return c;
    }
    bool  IsOnGround()        const { return onGround_; }       ///< 地面に接触中か
    bool  IsInWater()         const { return inWater_; }        ///< 水中にいるか
    bool  JustJumped()        const { return justJumped_; }     ///< このフレームにジャンプしたか
    bool  JustLanded()        const { return justLanded_; }     ///< このフレームに着地したか
    bool  JustEnteredWater()  const { return justEnteredWater_; } ///< このフレームに入水したか
    bool  JustExitedWater()   const { return justExitedWater_; }  ///< このフレームに出水したか

    /** @brief 覚醒ゲージの現在値を返す（0.0〜1.0） */
    float GetAwakenGauge()    const { return awakenGauge_; }
    /** @brief 覚醒状態かどうかを返す */
    bool  IsAwakened()        const { return isAwakened_; }

    // スタイル技フラグ（その1フレームだけ true）
    bool  JustComboHit()      const { return justComboHit_; }       ///< コンボヒット発生フレーム
    int   GetComboStep()      const { return comboStep_; }           ///< 現在のコンボステップ
    bool  JustFired()         const { return justFired_; }           ///< 射撃発生フレーム
    bool  JustBlinked()       const { return justBlinked_; }         ///< ブリンク発動フレーム
    bool  JustChargedGauge()  const { return justChargedGauge_; }    ///< ゲージチャージ発生フレーム
    float GetLastDirX()       const { return lastDirX_; }            ///< 最後に入力した横方向（+1=右, -1=左）
    /** @brief スキル補正込みのコンボ最大数を返す */
    int   GetComboMax()       const { return kComboMax_ + skillMods_.comboMaxBonus; }

    // スペースキー スピン連射
    bool  JustSpinShot()      const { return justSpinShot_; }   ///< スピン連射発生フレーム
    bool  IsUpsideDown()      const { return isUpsideDown_; }   ///< 逆さま状態か
    float GetSpinAngle()      const { return spinAngle_; }      ///< スピン角度（度、0=正立, 180=逆さ）

    // 覚醒乱舞（Sword + 覚醒 + L）
    bool  IsRampaging()        const { return rampagePhase_ != RampagePhase::Inactive; } ///< 乱舞中か
    bool  JustLaunched()       const { return justLaunched_; }       ///< 打ち上げ発生フレーム
    bool  JustRampageHit()     const { return justRampageHit_; }     ///< 乱舞スラッシュヒットフレーム
    bool  JustRampageFinish()  const { return justRampageFinish_; }  ///< 乱舞終了フレーム
    int   GetJuggleCount()     const { return juggleSlashCount_; }   ///< 現在の乱舞スラッシュ回数
    /** @brief スキル補正込みの乱舞最大スラッシュ数を返す */
    int   GetJuggleMax()       const { return kJuggleMaxSlashes_ + skillMods_.juggleMaxBonus; }

    // フィニッシャースラッシュ（覚醒ゲージ満タン + F）
    bool  JustFinisherSlash()  const { return justFinisherSlash_; } ///< フィニッシャースラッシュ発動フレーム

private:
    // 通常物理
    static constexpr float kGroundY_   =  0.4f;
    static constexpr float kCeilingY_  = 12.0f;
    static constexpr float kMinX_      =  3.0f;
    static constexpr float kMaxX_      = 35.0f;
    static constexpr float kGravity_   =  0.012f;
    static constexpr float kJumpPower_ =  0.4f;
    static constexpr float kSpeed_     =  0.15f;

    // 水中物理（水なしステージでは無効化された状態のままにする）
    static constexpr float kWaterLevelDisabled_ = -1.0f;
    static constexpr float kWaterGravity_=  0.003f;  // 浮力で弱い沈下加速度
    static constexpr float kWaterSpeed_  =  0.10f;   // 水中横移動速度
    static constexpr float kSwimAccel_   =  0.025f;  // 長押しで上昇する加速度
    static constexpr float kSwimMaxVY_   =  0.28f;   // 上昇最大速度
    static constexpr float kSinkMaxVY_   = -0.12f;   // 沈下最大速度

    Vector3 pos_          = { 8.0f, 0.4f, 0.0f };
    float   waterLevel_   = kWaterLevelDisabled_; // SetWaterLevel() で上書きされるまで水中判定は無効
    float   velocityY_    = 0.0f;
    bool    onGround_     = true;
    bool    prevOnGround_ = true;
    bool    justJumped_   = false;
    bool    justLanded_   = false;

    bool    inWater_          = false;
    bool    prevInWater_      = false;
    bool    justEnteredWater_ = false;
    bool    justExitedWater_  = false;

    // 向き（最後に入力した横方向。+1=右 -1=左）
    float   lastDirX_         = 1.0f;

    // 覚醒ゲージ
    float   awakenGauge_      = 0.0f;
    bool    isAwakened_       = false;
    float   awakenTimer_      = 0.0f;
    static constexpr float kAwakenDuration_ = 8.0f;

    // スタイル技（L / K キー）
    bool    justComboHit_     = false;
    int     comboStep_        = 0;
    float   comboTimer_       = 0.0f;
    bool    justFired_        = false;
    bool    justBlinked_      = false;
    bool    justChargedGauge_ = false;
    static constexpr float kComboWindow_ = 0.55f;
    static constexpr int   kComboMax_    = 3;
    static constexpr float kBlinkDist_   = 5.0f;
    static constexpr float kGaugeCharge_ = 0.18f;

    // スペースキー スピン連射
    bool    justSpinShot_     = false;
    bool    isUpsideDown_     = false;
    float   spinAngle_        = 0.0f; // 度。0=正立, 180=逆さ
    float   shootCooldown_    = 0.0f;
    static constexpr float kShootInterval_ = 0.12f; // 連射間隔（秒）
    static constexpr float kSpinSpeed_     = 5.0f;  // 空中回転速度（度/フレーム）

    // 覚醒乱舞（Sword + 覚醒 + L）
    RampagePhase rampagePhase_     = RampagePhase::Inactive;
    bool  justLaunched_            = false;
    bool  justRampageHit_          = false;
    bool  justRampageFinish_       = false;
    int   juggleSlashCount_        = 0;  // 今回の乱舞で何回切ったか
    int   juggleAngleIdx_          = 0;  // 次のスラッシュ角度インデックス
    static constexpr float kRampageSpeed_      = 0.45f; // 打ち上げ突進速度
    static constexpr float kJuggleRadius_      = 2.5f;  // 敵からのスラッシュ距離
    static constexpr int   kJuggleMaxSlashes_  = 8;     // 乱舞の最大回数

    // フィニッシャースラッシュ（覚醒ゲージ満タン + F）
    bool justFinisherSlash_ = false;

    // スキル補正（ローグライト）
    SkillMods skillMods_;

    // ---- Physics State パターン ----
    // 水中/水上で横移動・重力・ジャンプの処理を切り替える。
    class IPhysicsState {
    public:
        virtual ~IPhysicsState() = default;
        virtual void Update(Player& player, Input* input) const = 0;
    };
    class GroundedPhysicsState   : public IPhysicsState { public: void Update(Player& player, Input* input) const override; };
    class UnderwaterPhysicsState : public IPhysicsState { public: void Update(Player& player, Input* input) const override; };
    static const IPhysicsState& GetPhysicsState(bool inWater);

    // ---- Rampage State パターン ----
    // 覚醒乱舞の進行フェーズ（RampagePhase）ごとに L キー入力の意味と
    // 毎フレームの物理更新内容を切り替える。
    class IRampageState {
    public:
        virtual ~IRampageState() = default;
        virtual void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const = 0;
        virtual void UpdatePhysics(Player& player, const Vector3& enemyPos) const = 0;
    };
    class InactiveRampageState : public IRampageState {
    public:
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override;
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override {}
    };
    class LaunchRampageState : public IRampageState {
    public:
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override {}
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override;
    };
    class JuggleRampageState : public IRampageState {
    public:
        void HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const override;
        void UpdatePhysics(Player& player, const Vector3& enemyPos) const override;
    };
    static const IRampageState& GetRampageState(RampagePhase phase);

    // ---- Weapon Behavior Strategy パターン ----
    // 武器種別ごとのスペースキー挙動（ブリンク/ゲージチャージ/スピン連射）を切り替える。
    class IWeaponBehavior {
    public:
        virtual ~IWeaponBehavior() = default;
        virtual void Update(Player& player, Input* input) const = 0;
    };
    class DaggerBehavior : public IWeaponBehavior { public: void Update(Player& player, Input* input) const override; };
    class HammerBehavior : public IWeaponBehavior { public: void Update(Player& player, Input* input) const override; };
    class BallBehavior    : public IWeaponBehavior { public: void Update(Player& player, Input* input) const override; };
    class DefaultWeaponBehavior : public IWeaponBehavior { public: void Update(Player&, Input*) const override {} };
    static const IWeaponBehavior& GetWeaponBehavior(WeaponType type);

    // 覚醒残像
    AfterImageRenderer afterImageRenderer_;

    std::unique_ptr<Model>    model_;
    std::unique_ptr<Object3d> object_;
};

} // namespace engine::game
