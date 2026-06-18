#pragma once
#include "AfterImageRenderer.h"
#include "CollisionConfig.h"
#include "Model.h"
#include "Object3d.h"
#include <memory>

class Input;
class ModelCommon;

class Player {
public:
    // 乱舞フェーズ
    enum class RampagePhase { Inactive, Launch, Juggle };

    void Initialize(ModelCommon* modelCommon);
    void Update(Input* input, const Vector3& enemyPos = {});
    void Draw();
    void EndRampage() { if (rampagePhase_ == RampagePhase::Juggle) rampagePhase_ = RampagePhase::Inactive; }

    const Vector3& GetPosition() const { return pos_; }
    Model* GetModel() const { return model_.get(); }

    // プレイヤーの現在位置から AABB コライダーを返す（当たり判定に使用）
    Collider GetCollider() const {
        Collider c;
        c.SetAsAABB({ { pos_.x - 0.5f, pos_.y - 0.5f, -0.5f },
                      { pos_.x + 0.5f, pos_.y + 0.5f,  0.5f } });
        return c;
    }
    bool  IsOnGround()        const { return onGround_; }
    bool  IsInWater()         const { return inWater_; }
    bool  JustJumped()        const { return justJumped_; }
    bool  JustLanded()        const { return justLanded_; }
    bool  JustEnteredWater()  const { return justEnteredWater_; }
    bool  JustExitedWater()   const { return justExitedWater_; }

    // 覚醒ゲージ
    float GetAwakenGauge()    const { return awakenGauge_; }
    bool  IsAwakened()        const { return isAwakened_; }

    // スタイル技フラグ（その1フレームだけ true）
    bool  JustComboHit()      const { return justComboHit_; }
    int   GetComboStep()      const { return comboStep_; }
    bool  JustFired()         const { return justFired_; }
    bool  JustBlinked()       const { return justBlinked_; }
    bool  JustChargedGauge()  const { return justChargedGauge_; }
    float GetLastDirX()       const { return lastDirX_; }
    int   GetComboMax()       const { return kComboMax_; }

    // スペースキー スピン連射
    bool  JustSpinShot()      const { return justSpinShot_; }
    bool  IsUpsideDown()      const { return isUpsideDown_; }
    float GetSpinAngle()      const { return spinAngle_; }

    // 覚醒乱舞（Sword + 覚醒 + L）
    bool  IsRampaging()        const { return rampagePhase_ != RampagePhase::Inactive; }
    bool  JustLaunched()       const { return justLaunched_; }
    bool  JustRampageHit()     const { return justRampageHit_; }
    bool  JustRampageFinish()  const { return justRampageFinish_; }
    int   GetJuggleCount()     const { return juggleSlashCount_; }
    int   GetJuggleMax()       const { return kJuggleMaxSlashes_; }

private:
    // 通常物理
    static constexpr float kGroundY_   =  0.4f;
    static constexpr float kCeilingY_  = 12.0f;
    static constexpr float kMinX_      =  3.0f;
    static constexpr float kMaxX_      = 35.0f;
    static constexpr float kGravity_   =  0.012f;
    static constexpr float kJumpPower_ =  0.4f;
    static constexpr float kSpeed_     =  0.15f;

    // 水中物理（水なしステージでは -1.0f にして水中判定を無効化）
    // 水ありステージでは WaterPool::kPoolTop（3.0f）に合わせること
    static constexpr float kWaterLevel_  = -1.0f;
    static constexpr float kWaterGravity_=  0.003f;  // 浮力で弱い沈下加速度
    static constexpr float kWaterSpeed_  =  0.10f;   // 水中横移動速度
    static constexpr float kSwimAccel_   =  0.025f;  // 長押しで上昇する加速度
    static constexpr float kSwimMaxVY_   =  0.28f;   // 上昇最大速度
    static constexpr float kSinkMaxVY_   = -0.12f;   // 沈下最大速度

    Vector3 pos_          = { 8.0f, 0.4f, 0.0f };
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
    static constexpr float kAwakenDecay_    = 0.0003f;

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

    // 覚醒残像
    AfterImageRenderer afterImageRenderer_;

    std::unique_ptr<Model>    model_;
    std::unique_ptr<Object3d> object_;
};
