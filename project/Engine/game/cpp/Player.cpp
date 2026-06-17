#include "Player.h"
#include "GameConstants.h"
#include "Input.h"
#include "ModelCommon.h"
#include "WeaponManager.h"
#include <algorithm>

void Player::Initialize(ModelCommon* modelCommon)
{
    model_ = std::make_unique<Model>();
    model_->Initialize(modelCommon,
        "Resources/player/player.obj",
        "Resources/player/player.png");

    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model_.get());
    object_->SetEnableLighting(false);
    object_->SetPosition(pos_);
    object_->Update();

    afterImageObj_ = std::make_unique<Object3d>();
    afterImageObj_->Initialize(modelCommon);
    afterImageObj_->SetModel(model_.get());
    afterImageObj_->SetEnableLighting(false);
    afterImageObj_->Update();
}

void Player::Update(Input* input)
{
    justJumped_       = false;
    justLanded_       = false;
    justEnteredWater_ = false;
    justExitedWater_  = false;
    justComboHit_     = false;
    justFired_        = false;
    justBlinked_      = false;
    justChargedGauge_ = false;
    justSpinShot_     = false;
    justRampageHit_   = false;
    prevOnGround_     = onGround_;
    prevInWater_      = inWater_;

    // スタイルチェンジ（1〜5キー）
    auto* wm = WeaponManager::GetInstance();
    for (int i = 0; i < wm->GetCount(); ++i) {
        if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) { wm->SelectIndex(i); }
    }

    const float speedMult = isAwakened_ ? 1.5f : 1.0f;
    const float jumpMult  = isAwakened_ ? 1.3f : 1.0f;

    if (inWater_) {
        // ========== 水中物理 ==========
        // 横移動（水の抵抗で遅い）
        if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT))  { pos_.x -= kWaterSpeed_ * speedMult; lastDirX_ = -1.0f; }
        if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { pos_.x += kWaterSpeed_ * speedMult; lastDirX_ =  1.0f; }

        // 浮力（弱い下向き加速）
        velocityY_ -= kWaterGravity_;
        velocityY_  = (std::max)(velocityY_, kSinkMaxVY_);

        // ジャンプ長押し = 上昇スイム
        if (input->PushKey(DIK_W) || input->PushKey(DIK_UP) || input->PushKey(DIK_SPACE)) {
            velocityY_ = (std::min)(velocityY_ + kSwimAccel_, kSwimMaxVY_);
        }

        pos_.y += velocityY_;

        // 床クランプ（水底でも止まる）
        if (pos_.y <= kGroundY_) {
            pos_.y     = kGroundY_;
            velocityY_ = 0.0f;
            onGround_  = true;
        } else {
            onGround_ = false;
        }

        // 天井クランプ
        if (pos_.y > kCeilingY_) {
            pos_.y     = kCeilingY_;
            velocityY_ = 0.0f;
        }
    } else {
        // ========== 通常物理（水上）==========
        if (!isRampaging_) {
            if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT))  { pos_.x -= kSpeed_ * speedMult; lastDirX_ = -1.0f; }
            if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { pos_.x += kSpeed_ * speedMult; lastDirX_ =  1.0f; }
        }

        if (onGround_) {
            if (input->TriggerKey(DIK_W) || input->TriggerKey(DIK_UP)) {
                velocityY_  = kJumpPower_ * jumpMult;
                onGround_   = false;
                justJumped_ = true;
            }
        }

        velocityY_ -= kGravity_;
        pos_.y     += velocityY_;

        if (pos_.y <= kGroundY_) {
            pos_.y     = kGroundY_;
            velocityY_ = 0.0f;
            onGround_  = true;
        }
        if (pos_.y > kCeilingY_) {
            pos_.y     = kCeilingY_;
            velocityY_ = 0.0f;
        }

        justLanded_ = !prevOnGround_ && onGround_;
    }

    pos_.x = std::clamp(pos_.x, kMinX_, kMaxX_);

    // ── 射撃（K キー、水上のみ）─────────────────────────────────────
    if (!inWater_ && input->TriggerKey(DIK_K)) {
        justFired_ = true;
    }

    // ── 格闘コンボ（L キー、水上のみ）───────────────────────────────
    if (!inWater_ && !isRampaging_ && input->TriggerKey(DIK_L)) {
        // Sword + 覚醒中 → 乱舞発動
        if (wm->GetCurrent().type == WeaponType::Sword && isAwakened_) {
            isRampaging_    = true;
            rampageTimer_   = kRampageHitInterval_ * kRampageMaxHits_ + 0.3f;
            rampageHitCount_ = 0;
            rampageHitCool_ = 0.0f;
        } else {
            if (comboStep_ == 0 || comboTimer_ > 0.0f) {
                comboStep_    = comboStep_ % kComboMax_ + 1;
                comboTimer_   = kComboWindow_;
                justComboHit_ = true;
            }
        }
    }

    // ── 攻撃ヒットによるゲージ蓄積 ───────────────────────────────────
    if (!isAwakened_) {
        if (justComboHit_) { awakenGauge_ = (std::min)(awakenGauge_ + 0.08f, 1.0f); }
        if (justFired_)    { awakenGauge_ = (std::min)(awakenGauge_ + 0.04f, 1.0f); }
        if (justSpinShot_) { awakenGauge_ = (std::min)(awakenGauge_ + 0.02f, 1.0f); }
    }

    // ── スペースキー（武器タイプ別）──────────────────────────────
    if (!inWater_) {
        WeaponType wtype = wm->GetCurrent().type;

        // 連射クールダウンは常に消化（Ball モード以外でも自然に減る）
        shootCooldown_ -= GameConstants::kFrameDeltaTime;
        if (shootCooldown_ < 0.0f) { shootCooldown_ = 0.0f; }

        switch (wtype) {
        case WeaponType::Dagger:
            // ブリンク（瞬間移動）
            if (input->TriggerKey(DIK_SPACE)) {
                pos_.x += lastDirX_ * kBlinkDist_;
                pos_.x = std::clamp(pos_.x, kMinX_, kMaxX_);
                justBlinked_ = true;
            }
            break;

        case WeaponType::Hammer:
            // ゲージチャージ（長押し約3秒で満タン）
            if (input->PushKey(DIK_SPACE)) {
                justChargedGauge_ = true;
                awakenGauge_ = (std::min)(
                    awakenGauge_ + kGaugeCharge_ * GameConstants::kFrameDeltaTime, 1.0f);
            }
            break;

        case WeaponType::Ball:
            // スピン連射 + 空中くるくる
            if (input->PushKey(DIK_SPACE)) {
                if (shootCooldown_ <= 0.0f) {
                    justSpinShot_  = true;
                    shootCooldown_ = kShootInterval_;
                }
                if (!onGround_) {
                    spinAngle_ += kSpinSpeed_;
                    if (spinAngle_ >= 360.0f) { spinAngle_ -= 360.0f; }
                }
            }
            break;

        default:
            break;
        }

        // Ball モード以外 / 着地時はスピン角をリセット
        if (wtype != WeaponType::Ball || onGround_) { spinAngle_ = 0.0f; }
        isUpsideDown_ = (spinAngle_ > 90.0f && spinAngle_ < 270.0f);
    }

    // ── 乱舞更新 ─────────────────────────────────────────────────
    if (isRampaging_) {
        rampageTimer_ -= GameConstants::kFrameDeltaTime;
        if (rampageTimer_ <= 0.0f || rampageHitCount_ >= kRampageMaxHits_) {
            isRampaging_ = false;
        } else {
            // 前方に自動突進（重力一時無効）
            pos_.x  += lastDirX_ * kRampageSpeed_;
            pos_.x   = std::clamp(pos_.x, kMinX_, kMaxX_);
            velocityY_ = 0.0f;
            // 一定間隔でヒット判定を発行
            rampageHitCool_ -= GameConstants::kFrameDeltaTime;
            if (rampageHitCool_ <= 0.0f) {
                justRampageHit_ = true;
                rampageHitCount_++;
                rampageHitCool_ = kRampageHitInterval_;
            }
        }
    }

    // ── 覚醒発動（R キー）────────────────────────────────────────
    if (input->TriggerKey(DIK_R) && awakenGauge_ >= 0.3f && !isAwakened_) {
        isAwakened_  = true;
        awakenTimer_ = kAwakenDuration_;
    }

    // ── コンボタイマー ────────────────────────────────────────────
    if (comboTimer_ > 0.0f) {
        comboTimer_ -= GameConstants::kFrameDeltaTime;
        if (comboTimer_ <= 0.0f) {
            comboTimer_ = 0.0f;
            comboStep_  = 0;
        }
    }

    // ── 覚醒ゲージ管理 ────────────────────────────────────────────
    if (isAwakened_) {
        awakenTimer_ -= GameConstants::kFrameDeltaTime;
        awakenGauge_  = (std::max)(awakenGauge_ - GameConstants::kFrameDeltaTime / kAwakenDuration_, 0.0f);
        if (awakenTimer_ <= 0.0f || awakenGauge_ <= 0.0f) {
            isAwakened_  = false;
            awakenTimer_ = 0.0f;
            awakenGauge_ = 0.0f;
        }
    } else {
        awakenGauge_ = (std::max)(awakenGauge_ - kAwakenDecay_, 0.0f);
    }

    // 入水・出水判定（物理後の位置で確定）
    inWater_          = (pos_.y < kWaterLevel_);
    justEnteredWater_ = !prevInWater_ && inWater_;
    justExitedWater_  = prevInWater_  && !inWater_;

    // 入水衝撃吸収（落下速度を大きく減衰）
    if (justEnteredWater_) { velocityY_ *= 0.4f; }

    constexpr float kHalfPi   = 3.14159265f / 2.0f;
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    float yaw = (lastDirX_ >= 0.0f) ? (kHalfPi) : (-kHalfPi);

    // ── 覚醒残像スポーン＆フェード ──
    for (auto& ai : afterImages_) {
        if (ai.alpha > 0.0f) {
            ai.alpha -= GameConstants::kFrameDeltaTime * 4.0f;
            if (ai.alpha < 0.0f) ai.alpha = 0.0f;
        }
    }
    if (isAwakened_ || isRampaging_) {
        afterImageTimer_ -= GameConstants::kFrameDeltaTime;
        if (afterImageTimer_ <= 0.0f) {
            afterImageTimer_ = isRampaging_ ? 0.03f : 0.05f; // 乱舞中は高頻度
            auto& ai = afterImages_[afterImageIdx_];
            ai.pos   = pos_;
            ai.yaw   = yaw;
            ai.spinZ = spinAngle_;
            ai.alpha = isRampaging_ ? 0.75f : 0.55f;
            afterImageIdx_ = (afterImageIdx_ + 1) % kMaxAfterImages;
        }
    }

    // ── プレイヤー色 ──
    if (isRampaging_) {
        float t = std::sin(rampageTimer_ * 25.0f) * 0.5f + 0.5f;
        object_->SetColor({ 1.0f, 1.0f - t * 0.4f, 1.0f - t * 0.6f, 1.0f }); // 白→青白点滅
    } else if (justChargedGauge_) {
        object_->SetColor({ 1.0f, 0.85f, 0.0f, 1.0f }); // 黄（ハンマーチャージ）
    } else if (isAwakened_) {
        float t = std::sin(awakenTimer_ * 6.0f) * 0.3f + 0.7f;
        object_->SetColor({ 0.15f * t, 0.55f * t, 1.0f, 1.0f }); // 青くパルス
    } else {
        object_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    }

    object_->SetPosition(pos_);
    object_->SetRotation({ 0.0f, yaw, spinAngle_ * kDegToRad });
    object_->Update();
}

void Player::Draw()
{
    // 残像（プレイヤーより先に描画して後ろに見えるようにする）
    constexpr float kDeg = 3.14159265f / 180.0f;
    for (const auto& ai : afterImages_) {
        if (ai.alpha <= 0.0f) continue;
        afterImageObj_->SetPosition(ai.pos);
        afterImageObj_->SetRotation({ 0.0f, ai.yaw, ai.spinZ * kDeg });
        afterImageObj_->SetColor({ 0.05f, 0.35f, 1.0f, ai.alpha * 0.65f });
        afterImageObj_->Update();
        afterImageObj_->Draw();
    }
    object_->Draw();
}
