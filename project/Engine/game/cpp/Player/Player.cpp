#include "Player.h"
#include "GameConstants.h"
#include "Input.h"
#include "ModelCommon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

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

    afterImageRenderer_.Initialize(modelCommon, model_.get());
}

void Player::Update(Input* input, const Vector3& enemyPos)
{
    justJumped_        = false;
    justLanded_        = false;
    justEnteredWater_  = false;
    justExitedWater_   = false;
    justComboHit_      = false;
    justFired_         = false;
    justBlinked_       = false;
    justChargedGauge_  = false;
    justSpinShot_      = false;
    justLaunched_      = false;
    justRampageHit_    = false;
    justRampageFinish_ = false;
    justFinisherSlash_ = false;
    prevOnGround_     = onGround_;
    prevInWater_      = inWater_;

    // スタイルチェンジ（1〜5キー）
    auto* wm = WeaponManager::GetInstance();
    for (int i = 0; i < wm->GetCount(); ++i) {
        if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) { wm->SelectIndex(i); }
    }

    const float speedMult = (isAwakened_ ? 1.5f : 1.0f) * skillMods_.speedMult;
    const float jumpMult  = (isAwakened_ ? 1.3f : 1.0f) * skillMods_.jumpMult;

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
        if (rampagePhase_ == RampagePhase::Inactive) {
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

    // ── 格闘コンボ / 乱舞（L キー、水上のみ）────────────────────────
    if (!inWater_ && input->TriggerKey(DIK_L)) {
        if (rampagePhase_ == RampagePhase::Inactive &&
            wm->GetCurrent().type == WeaponType::Sword && isAwakened_) {
            // 乱舞開始：まず敵に向かって突進（打ち上げフェーズ）
            rampagePhase_     = RampagePhase::Launch;
            juggleSlashCount_ = 0;
            juggleAngleIdx_   = 0;

        } else if (rampagePhase_ == RampagePhase::Juggle &&
                   juggleSlashCount_ < kJuggleMaxSlashes_ + skillMods_.juggleMaxBonus) {
            // 乱舞中 L → 敵の周囲の次の角度へテレポートしてスラッシュ
            const int effectiveMax = kJuggleMaxSlashes_ + skillMods_.juggleMaxBonus;
            float angle = juggleAngleIdx_ * (GameConstants::kTwoPi / effectiveMax);
            float dx = std::cos(angle) * kJuggleRadius_;
            float dy = std::sin(angle) * kJuggleRadius_;
            pos_.x = std::clamp(enemyPos.x + dx, kMinX_, kMaxX_);
            pos_.y = std::clamp(enemyPos.y + dy, kGroundY_, kCeilingY_);
            velocityY_ = 0.0f;
            lastDirX_  = (enemyPos.x >= pos_.x) ? 1.0f : -1.0f;
            juggleAngleIdx_   = (juggleAngleIdx_ + 1) % effectiveMax;
            juggleSlashCount_++;

            bool isLast = (juggleSlashCount_ >= effectiveMax);
            justRampageHit_    = true;
            justRampageFinish_ = isLast;
            if (isLast) {
                rampagePhase_ = RampagePhase::Inactive;
            }

        } else if (rampagePhase_ == RampagePhase::Inactive) {
            // 通常コンボ
            if (comboStep_ == 0 || comboTimer_ > 0.0f) {
                comboStep_    = comboStep_ % (kComboMax_ + skillMods_.comboMaxBonus) + 1;
                comboTimer_   = kComboWindow_;
                justComboHit_ = true;
            }
        }
    }

    // ── フィニッシャースラッシュ（F キー、水上のみ、覚醒ゲージ満タン時のみ）─────
    if (!inWater_ && !isAwakened_ && input->TriggerKey(DIK_F) && awakenGauge_ >= 1.0f) {
        justFinisherSlash_ = true;
        awakenGauge_       = 0.0f; // ゲージを全消費
    }

    // ── 攻撃ヒットによるゲージ蓄積 ───────────────────────────────────
    if (!isAwakened_) {
        if (justComboHit_) { awakenGauge_ = (std::min)(awakenGauge_ + 0.08f * skillMods_.gaugeChargeMult, 1.0f); }
        if (justFired_)    { awakenGauge_ = (std::min)(awakenGauge_ + 0.04f * skillMods_.gaugeChargeMult, 1.0f); }
        if (justSpinShot_) { awakenGauge_ = (std::min)(awakenGauge_ + 0.02f * skillMods_.gaugeChargeMult, 1.0f); }
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
                pos_.x += lastDirX_ * kBlinkDist_ * skillMods_.blinkDistMult;
                pos_.x = std::clamp(pos_.x, kMinX_, kMaxX_);
                justBlinked_ = true;
            }
            break;

        case WeaponType::Hammer:
            // ゲージチャージ（長押し約3秒で満タン）。覚醒中は蓄積しない
            if (!isAwakened_ && input->PushKey(DIK_SPACE)) {
                justChargedGauge_ = true;
                awakenGauge_ = (std::min)(
                    awakenGauge_ + kGaugeCharge_ * GameConstants::kFrameDeltaTime * skillMods_.gaugeChargeMult, 1.0f);
            }
            break;

        case WeaponType::Ball:
            // スピン連射 + 空中くるくる
            if (input->PushKey(DIK_SPACE)) {
                if (shootCooldown_ <= 0.0f) {
                    justSpinShot_  = true;
                    shootCooldown_ = kShootInterval_ * skillMods_.fireIntervalMult;
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

    // ── 乱舞フェーズ更新 ──────────────────────────────────────────
    if (rampagePhase_ == RampagePhase::Launch) {
        // 敵X座標へ向かって突進（重力無効）
        // ステージ外の座標が渡されてもプレイヤーが到達できる位置にクランプ
        float targetX = std::clamp(enemyPos.x, kMinX_ + 0.5f, kMaxX_ - 0.5f);
        float dx  = targetX - pos_.x;
        float dir = (dx > 0.0f) ? 1.0f : -1.0f;
        lastDirX_  = dir;
        velocityY_ = 0.0f;
        pos_.x += dir * kRampageSpeed_;
        pos_.x  = std::clamp(pos_.x, kMinX_, kMaxX_);

        // 十分近づいたら打ち上げヒット → ジャグルフェーズへ
        if (std::abs(dx) < 1.0f) {
            justLaunched_ = true;
            velocityY_    = 0.25f;
            rampagePhase_ = RampagePhase::Juggle;
        }
    } else if (rampagePhase_ == RampagePhase::Juggle) {
        // ジャグル中は重力無効でホバリング
        velocityY_ = 0.0f;
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
    // 覚醒中のみ消費する。未覚醒時は自然減衰させず、溜めた分を維持する
    if (isAwakened_) {
        awakenTimer_ -= GameConstants::kFrameDeltaTime;
        awakenGauge_  = (std::max)(awakenGauge_ - GameConstants::kFrameDeltaTime / kAwakenDuration_, 0.0f);
        if (awakenTimer_ <= 0.0f || awakenGauge_ <= 0.0f) {
            isAwakened_  = false;
            awakenTimer_ = 0.0f;
            awakenGauge_ = 0.0f;
        }
    }

    // 入水・出水判定（物理後の位置で確定）
    inWater_          = (pos_.y < kWaterLevel_);
    justEnteredWater_ = !prevInWater_ && inWater_;
    justExitedWater_  = prevInWater_  && !inWater_;

    // 入水衝撃吸収（落下速度を大きく減衰）
    if (justEnteredWater_) { velocityY_ *= 0.4f; }

    float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;

    // ── 覚醒残像スポーン＆フェード ──
    bool isRampage = (rampagePhase_ != RampagePhase::Inactive);
    afterImageRenderer_.Update(isAwakened_ || isRampage, isRampage, pos_, yaw, spinAngle_);

    // ── プレイヤー色 ──
    if (rampagePhase_ != RampagePhase::Inactive) {
        float t = std::sin(juggleSlashCount_ * 3.0f) * 0.5f + 0.5f;
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
    object_->SetRotation({ 0.0f, yaw, spinAngle_ * GameConstants::kDegToRad });
    object_->Update();
}

void Player::Draw()
{
    // 残像（プレイヤーより先に描画して後ろに見えるようにする）
    afterImageRenderer_.Draw();
    object_->Draw();
}
