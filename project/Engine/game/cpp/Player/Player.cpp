#include "Player.h"
#include "GameConstants.h"
#include "Input.h"
#include "ModelCommon.h"
#include "Weapon.h"
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

    GetPhysicsState(inWater_).Update(*this, input);

    pos_.x = std::clamp(pos_.x, kMinX_, kMaxX_);

    // ── 射撃（K キー、水上のみ）─────────────────────────────────────
    if (!inWater_ && input->TriggerKey(DIK_K)) {
        justFired_ = true;
    }

    // ── 格闘コンボ / 乱舞（L キー、水上のみ）────────────────────────
    if (!inWater_ && input->TriggerKey(DIK_L)) {
        GetRampageState(rampagePhase_).HandleAttackInput(*this, input, enemyPos);
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

        GetWeaponBehavior(wtype).Update(*this, input);

        // Ball モード以外 / 着地時はスピン角をリセット
        if (wtype != WeaponType::Ball || onGround_) { spinAngle_ = 0.0f; }
        isUpsideDown_ = (spinAngle_ > 90.0f && spinAngle_ < 270.0f);
    }

    // ── 乱舞フェーズ更新 ──────────────────────────────────────────
    GetRampageState(rampagePhase_).UpdatePhysics(*this, enemyPos);

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
    inWater_          = (pos_.y < waterLevel_);
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

// ============================================================
//  Physics State（水中/水上）
// ============================================================

void Player::UnderwaterPhysicsState::Update(Player& player, Input* input) const
{
    const float speedMult = (player.isAwakened_ ? 1.5f : 1.0f) * player.skillMods_.speedMult;

    // 横移動（水の抵抗で遅い）
    if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT))  { player.pos_.x -= kWaterSpeed_ * speedMult; player.lastDirX_ = -1.0f; }
    if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { player.pos_.x += kWaterSpeed_ * speedMult; player.lastDirX_ =  1.0f; }

    // 浮力（弱い下向き加速）
    player.velocityY_ -= kWaterGravity_;
    player.velocityY_  = (std::max)(player.velocityY_, kSinkMaxVY_);

    // ジャンプ長押し = 上昇スイム
    if (input->PushKey(DIK_W) || input->PushKey(DIK_UP) || input->PushKey(DIK_SPACE)) {
        player.velocityY_ = (std::min)(player.velocityY_ + kSwimAccel_, kSwimMaxVY_);
    }

    player.pos_.y += player.velocityY_;

    // 床クランプ（水底でも止まる）
    if (player.pos_.y <= kGroundY_) {
        player.pos_.y     = kGroundY_;
        player.velocityY_ = 0.0f;
        player.onGround_  = true;
    } else {
        player.onGround_ = false;
    }

    // 天井クランプ
    if (player.pos_.y > kCeilingY_) {
        player.pos_.y     = kCeilingY_;
        player.velocityY_ = 0.0f;
    }
}

void Player::GroundedPhysicsState::Update(Player& player, Input* input) const
{
    const float speedMult = (player.isAwakened_ ? 1.5f : 1.0f) * player.skillMods_.speedMult;
    const float jumpMult  = (player.isAwakened_ ? 1.3f : 1.0f) * player.skillMods_.jumpMult;

    if (player.rampagePhase_ == RampagePhase::Inactive) {
        if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT))  { player.pos_.x -= kSpeed_ * speedMult; player.lastDirX_ = -1.0f; }
        if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { player.pos_.x += kSpeed_ * speedMult; player.lastDirX_ =  1.0f; }
    }

    if (player.onGround_) {
        if (input->TriggerKey(DIK_W) || input->TriggerKey(DIK_UP)) {
            player.velocityY_  = kJumpPower_ * jumpMult;
            player.onGround_   = false;
            player.justJumped_ = true;
        }
    }

    player.velocityY_ -= kGravity_;
    player.pos_.y     += player.velocityY_;

    if (player.pos_.y <= kGroundY_) {
        player.pos_.y     = kGroundY_;
        player.velocityY_ = 0.0f;
        player.onGround_  = true;
    }
    if (player.pos_.y > kCeilingY_) {
        player.pos_.y     = kCeilingY_;
        player.velocityY_ = 0.0f;
    }

    player.justLanded_ = !player.prevOnGround_ && player.onGround_;
}

const Player::IPhysicsState& Player::GetPhysicsState(bool inWater)
{
    static GroundedPhysicsState   grounded;
    static UnderwaterPhysicsState underwater;
    return inWater ? static_cast<const IPhysicsState&>(underwater) : static_cast<const IPhysicsState&>(grounded);
}

// ============================================================
//  Rampage State（覚醒乱舞の進行フェーズ）
// ============================================================

void Player::InactiveRampageState::HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const
{
    if (WeaponManager::GetInstance()->GetCurrent().type == WeaponType::Sword && player.isAwakened_) {
        // 乱舞開始：まず敵に向かって突進（打ち上げフェーズ）
        player.rampagePhase_     = RampagePhase::Launch;
        player.juggleSlashCount_ = 0;
        player.juggleAngleIdx_   = 0;
    } else {
        // 通常コンボ
        if (player.comboStep_ == 0 || player.comboTimer_ > 0.0f) {
            player.comboStep_    = player.comboStep_ % (kComboMax_ + player.skillMods_.comboMaxBonus) + 1;
            player.comboTimer_   = kComboWindow_;
            player.justComboHit_ = true;
        }
    }
}

void Player::LaunchRampageState::UpdatePhysics(Player& player, const Vector3& enemyPos) const
{
    // 敵X座標へ向かって突進（重力無効）
    // ステージ外の座標が渡されてもプレイヤーが到達できる位置にクランプ
    float targetX = std::clamp(enemyPos.x, kMinX_ + 0.5f, kMaxX_ - 0.5f);
    float dx  = targetX - player.pos_.x;
    float dir = (dx > 0.0f) ? 1.0f : -1.0f;
    player.lastDirX_  = dir;
    player.velocityY_ = 0.0f;
    player.pos_.x += dir * kRampageSpeed_;
    player.pos_.x  = std::clamp(player.pos_.x, kMinX_, kMaxX_);

    // 十分近づいたら打ち上げヒット → ジャグルフェーズへ
    if (std::abs(dx) < 1.0f) {
        player.justLaunched_ = true;
        player.velocityY_    = 0.25f;
        player.rampagePhase_ = RampagePhase::Juggle;
    }
}

void Player::JuggleRampageState::HandleAttackInput(Player& player, Input* input, const Vector3& enemyPos) const
{
    const int effectiveMax = kJuggleMaxSlashes_ + player.skillMods_.juggleMaxBonus;
    if (player.juggleSlashCount_ >= effectiveMax) { return; }

    // 乱舞中 L → 敵の周囲の次の角度へテレポートしてスラッシュ
    float angle = player.juggleAngleIdx_ * (GameConstants::kTwoPi / effectiveMax);
    float dx = std::cos(angle) * kJuggleRadius_;
    float dy = std::sin(angle) * kJuggleRadius_;
    player.pos_.x = std::clamp(enemyPos.x + dx, kMinX_, kMaxX_);
    player.pos_.y = std::clamp(enemyPos.y + dy, kGroundY_, kCeilingY_);
    player.velocityY_ = 0.0f;
    player.lastDirX_  = (enemyPos.x >= player.pos_.x) ? 1.0f : -1.0f;
    player.juggleAngleIdx_   = (player.juggleAngleIdx_ + 1) % effectiveMax;
    player.juggleSlashCount_++;

    bool isLast = (player.juggleSlashCount_ >= effectiveMax);
    player.justRampageHit_    = true;
    player.justRampageFinish_ = isLast;
    if (isLast) {
        player.rampagePhase_ = RampagePhase::Inactive;
    }
}

void Player::JuggleRampageState::UpdatePhysics(Player& player, const Vector3& enemyPos) const
{
    // ジャグル中は重力無効でホバリング
    player.velocityY_ = 0.0f;
}

const Player::IRampageState& Player::GetRampageState(RampagePhase phase)
{
    static InactiveRampageState inactive;
    static LaunchRampageState   launch;
    static JuggleRampageState   juggle;
    switch (phase) {
    case RampagePhase::Launch: return launch;
    case RampagePhase::Juggle: return juggle;
    default:                   return inactive;
    }
}

// ============================================================
//  Weapon Behavior Strategy（武器種別ごとのスペースキー挙動）
// ============================================================

void Player::DaggerBehavior::Update(Player& player, Input* input) const
{
    // ブリンク（瞬間移動）
    if (input->TriggerKey(DIK_SPACE)) {
        player.pos_.x += player.lastDirX_ * kBlinkDist_ * player.skillMods_.blinkDistMult;
        player.pos_.x = std::clamp(player.pos_.x, kMinX_, kMaxX_);
        player.justBlinked_ = true;
    }
}

void Player::HammerBehavior::Update(Player& player, Input* input) const
{
    // ゲージチャージ（長押し約3秒で満タン）。覚醒中は蓄積しない
    if (!player.isAwakened_ && input->PushKey(DIK_SPACE)) {
        player.justChargedGauge_ = true;
        player.awakenGauge_ = (std::min)(
            player.awakenGauge_ + kGaugeCharge_ * GameConstants::kFrameDeltaTime * player.skillMods_.gaugeChargeMult, 1.0f);
    }
}

void Player::BallBehavior::Update(Player& player, Input* input) const
{
    // スピン連射 + 空中くるくる
    if (input->PushKey(DIK_SPACE)) {
        if (player.shootCooldown_ <= 0.0f) {
            player.justSpinShot_  = true;
            player.shootCooldown_ = kShootInterval_ * player.skillMods_.fireIntervalMult;
        }
        if (!player.onGround_) {
            player.spinAngle_ += kSpinSpeed_;
            if (player.spinAngle_ >= 360.0f) { player.spinAngle_ -= 360.0f; }
        }
    }
}

const Player::IWeaponBehavior& Player::GetWeaponBehavior(WeaponType type)
{
    static DaggerBehavior        dagger;
    static HammerBehavior        hammer;
    static BallBehavior          ball;
    static DefaultWeaponBehavior def;
    switch (type) {
    case WeaponType::Dagger: return dagger;
    case WeaponType::Hammer: return hammer;
    case WeaponType::Ball:   return ball;
    default:                 return def;
    }
}
