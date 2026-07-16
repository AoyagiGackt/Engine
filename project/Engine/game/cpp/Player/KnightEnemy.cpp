#include "KnightEnemy.h"
#include "GameConstants.h"
#include "ModelCommon.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr const char* kKnightModelPath = "Resources/Knight/OBJ/KnightCharacter.obj";
constexpr const char* kKnightTexture = "Resources/Knight/OBJ/KnightCharacterPalette.png";
// モデル身長は約5.58、プレイヤーと並んでもおかしくない高さ(約1.1)に合わせる
constexpr float kKnightModelScale = 0.2f;

constexpr const char* kSwordModelPath = "Resources/Knight/OBJ/Sword.obj";
constexpr const char* kSwordTexture = "Resources/Knight/OBJ/SwordPalette.png";
// モデル身長は約4.35。ナイトの体格に対して手頃な長さ(約0.6)に縮小
constexpr float kSwordScale = 0.14f;
// ナイトのローカル空間での構え位置・傾き（要目視調整、facingSignでX/Zとひねりを反転）
constexpr Vector3 kSwordOffset = { 0.35f, 0.75f, 0.15f };
constexpr float kSwordBaseTilt = 0.4f; // 基本の刀身傾き（ラジアン）
constexpr float kSwordPullBack = 0.9f; // 溜め時に引く角度
constexpr float kSwordSwingFwd = -1.3f; // 突進時に振り出す角度

// AI タイミング
constexpr float kIdleDuration = 1.2f;
constexpr float kTelegraphDuration = 0.35f;
constexpr float kDashDuration = 0.22f;
constexpr float kRecoverDuration = 0.6f;
constexpr float kMaxDashDistance = 5.0f;
constexpr float kGroundY = 0.4f; // Dummy等と同じ地面の高さ

constexpr float kAbsorbDuration = 0.5f;

// 被弾ノックバック
constexpr float kKnockbackSpeed = 0.18f; // 水平方向の初速
constexpr float kKnockbackDecay = 0.85f; // 毎フレームの減衰率

float EaseOutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
float EaseInQuad(float t) { return t * t; }
// engine::Lerp(Vector3,Vector3,float) と同名だと、KnightEnemyがengine::game所属のため
// メンバ関数内の非修飾名探索がengine名前空間側で先に見つかって止まってしまい、
// こちらの無名名前空間版（float版）に届かない。衝突を避けるため別名にする
float LerpF(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

void KnightEnemy::Initialize(ModelCommon* modelCommon, const Vector3& spawnPos)
{
    pos_ = spawnPos;
    pos_.y = kGroundY;
    state_ = State::Idle;
    stateTimer_ = 0.0f;
    hp_ = kMaxHp;

    model_ = std::make_unique<Model>();
    model_->Initialize(modelCommon, kKnightModelPath, kKnightTexture);
    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model_.get());
    object_->SetEnableLighting(true);
    // Vを意識した抑制的な色: 目立つ発光ではなく、わずかに暗い紫のリムに留める
    object_->SetRimColor({ 0.35f, 0.2f, 0.45f });
    object_->SetRimPower(3.0f);
    object_->SetRimIntensity(0.6f);
    object_->SetEnableRim(true);

    swordModel_ = std::make_unique<Model>();
    swordModel_->Initialize(modelCommon, kSwordModelPath, kSwordTexture);
    swordObject_ = std::make_unique<Object3d>();
    swordObject_->Initialize(modelCommon);
    swordObject_->SetModel(swordModel_.get());
    swordObject_->SetEnableLighting(true);
    swordObject_->SetRimColor({ 0.35f, 0.2f, 0.45f });
    swordObject_->SetRimPower(3.0f);
    swordObject_->SetRimIntensity(0.6f);
    swordObject_->SetEnableRim(true);

    ApplyTransforms();
}

bool KnightEnemy::IsAlive() const
{
    return state_ == State::Idle || state_ == State::Telegraph
        || state_ == State::Dash || state_ == State::Recover;
}

void KnightEnemy::TakeDamage(int damage, float knockDirX, float knockY)
{
    if (!IsAlive()) {
        return;
    }
    hp_ -= damage;
    hitFlash_ = 0.12f;
    knockVelX_ += knockDirX * kKnockbackSpeed;
    knockVelY_ = knockY;
    if (hp_ <= 0) {
        state_ = State::Defeated;
        stateTimer_ = 0.0f;
        object_->SetColor({ 0.4f, 0.4f, 0.4f, 1.0f });
        object_->SetEnableRim(false);
        swordObject_->SetColor({ 0.4f, 0.4f, 0.4f, 1.0f });
        swordObject_->SetEnableRim(false);
    }
}

bool KnightEnemy::TryBeginAbsorb()
{
    if (state_ != State::Defeated) {
        return false;
    }
    state_ = State::Absorbing;
    stateTimer_ = 0.0f;
    return true;
}

void KnightEnemy::RefreshVisualTransforms()
{
    // 位置・AI状態は変えず、現在のpos_を使って見た目のトランスフォームだけ再計算する
    // （ApplyTransforms()はpos_/yaw_等の現在値を読むだけでAIやタイマーは一切進めない）
    ApplyTransforms();
}

void KnightEnemy::ResolveBlockCollision(const std::vector<AABB>& blocks)
{
    if (blocks.empty() || !IsAlive()) {
        return;
    } // 撃破後は演出中なので押し出さない

    AABB self = GetAABB();
    for (const auto& b : blocks) {
        bool overlapY = self.max.y > b.min.y && self.min.y < b.max.y;
        bool overlapZ = self.max.z > b.min.z && self.min.z < b.max.z;
        if (!overlapY || !overlapZ) {
            continue;
        }

        bool overlapX = self.max.x > b.min.x && self.min.x < b.max.x;
        if (!overlapX) {
            continue;
        }

        // 侵入量が小さい側へ押し出す（ジャンプしない敵なので水平方向だけで十分）
        float pushLeft = b.min.x - self.max.x; // 負値＝左へ押し出す量
        float pushRight = b.max.x - self.min.x; // 正値＝右へ押し出す量
        pos_.x += (std::abs(pushLeft) < std::abs(pushRight)) ? pushLeft : pushRight;
        self = GetAABB(); // 押し出し後の位置で以降のブロックも判定する
    }
}

void KnightEnemy::Update(ParticleManager* pm, const Vector3& playerPos)
{
    justAbsorbed_ = false;
    if (hitFlash_ > 0.0f) {
        hitFlash_ = (std::max)(0.0f, hitFlash_ - GameConstants::kFrameDeltaTime);
    }

    // 打ち上げ等でノックバック中はAIを一時停止する（さもないと空中でDash等の地上移動が割り込む）
    bool inKnockback = (knockVelX_ != 0.0f || knockVelY_ != 0.0f);
    if (IsAlive()) {
        if (!inKnockback) {
            UpdateAI(pm, playerPos);
        }
    } else if (state_ == State::Absorbing) {
        UpdateAbsorb(pm, playerPos);
    }

    // 被弾ノックバック（AIの位置更新の後に上乗せし、時間で減衰させる）
    if (inKnockback) {
        pos_.x += knockVelX_;
        pos_.y += knockVelY_;
        knockVelX_ *= kKnockbackDecay;
        knockVelY_ -= 0.02f; // 重力っぽく落ちる
        if (pos_.y <= kGroundY) {
            pos_.y = kGroundY;
            knockVelY_ = 0.0f;
        }
        if (std::abs(knockVelX_) < 0.001f) {
            knockVelX_ = 0.0f;
        }
    }

    ApplyTransforms();
}

void KnightEnemy::UpdateAI(ParticleManager* pm, const Vector3& playerPos)
{
    stateTimer_ += GameConstants::kFrameDeltaTime;

    switch (state_) {
    case State::Idle:
        swordSwing_ = LerpF(swordSwing_, 0.0f, 0.15f);
        if (stateTimer_ >= kIdleDuration) {
            state_ = State::Telegraph;
            stateTimer_ = 0.0f;
            dashStart_ = pos_;
            float dx = std::clamp(playerPos.x - pos_.x, -kMaxDashDistance, kMaxDashDistance);
            dashTarget_ = { pos_.x + dx, pos_.y, pos_.z };
        }
        break;

    case State::Telegraph: {
        float t = std::clamp(stateTimer_ / kTelegraphDuration, 0.0f, 1.0f);
        swordSwing_ = LerpF(swordSwing_, kSwordPullBack, 0.3f);
        if (t >= 1.0f) {
            state_ = State::Dash;
            stateTimer_ = 0.0f;
        }
        break;
    }
    case State::Dash: {
        float t = std::clamp(stateTimer_ / kDashDuration, 0.0f, 1.0f);
        float eased = EaseOutQuad(t);
        pos_.x = LerpF(dashStart_.x, dashTarget_.x, eased);
        if (dashTarget_.x != dashStart_.x) {
            yaw_ = (dashTarget_.x >= dashStart_.x) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;
        }
        swordSwing_ = LerpF(swordSwing_, kSwordSwingFwd, 0.5f);
        // 派手な閃光とは対照的な、暗い靄の軌跡
        if (pm) {
            pm->EmitTrail("bt_knight_dash_smoke", { pos_.x, pos_.y + 0.5f, pos_.z },
                { 0.25f, 0.15f, 0.3f, 0.5f }, 0.6f, 0.25f);
        }
        if (t >= 1.0f) {
            state_ = State::Recover;
            stateTimer_ = 0.0f;
        }
        break;
    }
    case State::Recover:
        swordSwing_ = LerpF(swordSwing_, 0.0f, 0.12f);
        if (stateTimer_ >= kRecoverDuration) {
            state_ = State::Idle;
            stateTimer_ = 0.0f;
        }
        break;

    default:
        break;
    }
}

void KnightEnemy::UpdateAbsorb(ParticleManager* pm, const Vector3& playerPos)
{
    stateTimer_ += GameConstants::kFrameDeltaTime;
    float t = std::clamp(stateTimer_ / kAbsorbDuration, 0.0f, 1.0f);
    float eased = EaseInQuad(t);

    float facingSign = (yaw_ >= 0.0f) ? 1.0f : -1.0f;
    Vector3 handPos = {
        pos_.x + facingSign * kSwordOffset.x,
        pos_.y + kSwordOffset.y,
        pos_.z + kSwordOffset.z
    };
    Vector3 target = { playerPos.x, playerPos.y + 0.5f, playerPos.z };

    Vector3 swordFlyPos = {
        LerpF(handPos.x, target.x, eased),
        LerpF(handPos.y, target.y, eased),
        LerpF(handPos.z, target.z, eased)
    };

    // 体は剣より少し遅れて発進し、丸まりながら吸い込まれていく(丸呑み演出)
    float bodyEased = EaseInQuad(std::clamp((t - 0.1f) / 0.9f, 0.0f, 1.0f));
    Vector3 bodyFlyPos = {
        LerpF(pos_.x, target.x, bodyEased),
        LerpF(pos_.y, target.y, bodyEased),
        LerpF(pos_.z, target.z, bodyEased)
    };
    float bodyScale = kKnightModelScale * (1.0f - bodyEased);

    // 剣・体ともに光の粒に変わりながら吸い込まれていく軌跡
    Vector4 glowColor = { 0.5f + 0.5f * t, 0.85f + 0.15f * t, 1.0f, 1.0f };
    if (pm) {
        pm->EmitTrail("bt_weapon_orb", swordFlyPos, glowColor, LerpF(0.35f, 0.12f, t), 0.2f);
        pm->EmitTrail("bt_weapon_orb", bodyFlyPos, glowColor, LerpF(0.55f, 0.1f, bodyEased), 0.16f);
    }

    swordObject_->SetPosition(swordFlyPos);
    swordObject_->SetRotation({ 0.0f, yaw_, facingSign * kSwordBaseTilt });
    swordObject_->SetScale({ kSwordScale * (1.0f - 0.6f * t), kSwordScale * (1.0f - 0.6f * t), kSwordScale * (1.0f - 0.6f * t) });
    swordObject_->SetColor(glowColor);
    swordObject_->Update();

    // 体は高速回転させながら縮め、球状に丸まっていくように見せる
    object_->SetPosition(bodyFlyPos);
    object_->SetRotation({ bodyEased * GameConstants::kTwoPi * 3.0f, yaw_ + bodyEased * GameConstants::kTwoPi * 3.0f, 0.0f });
    object_->SetScale({ bodyScale, bodyScale, bodyScale });
    object_->SetColor(glowColor);
    object_->Update();

    if (t >= 1.0f) {
        state_ = State::Consumed;
        justAbsorbed_ = true;
    }
}

void KnightEnemy::ApplyTransforms()
{
    float facingSign = (yaw_ >= 0.0f) ? 1.0f : -1.0f;

    // 吸収中は体・剣を独立した軌道で飛ばすため、通常のアタッチ計算は上書きしない
    if (state_ == State::Absorbing || state_ == State::Consumed) {
        return;
    }

    if (IsAlive()) {
        // 被弾直後は白く明滅させ、ヒットがはっきり伝わるようにする
        float f = hitFlash_ / 0.12f;
        object_->SetColor({ 1.0f + f, 1.0f + f, 1.0f + f, 1.0f });
    }

    object_->SetPosition(pos_);
    object_->SetRotation({ 0.0f, yaw_, 0.0f });
    object_->SetScale({ kKnightModelScale, kKnightModelScale, kKnightModelScale });
    object_->Update();

    Vector3 swordPos = {
        pos_.x + facingSign * kSwordOffset.x,
        pos_.y + kSwordOffset.y,
        pos_.z + kSwordOffset.z
    };
    swordObject_->SetPosition(swordPos);
    swordObject_->SetRotation({ 0.0f, yaw_, facingSign * (kSwordBaseTilt + swordSwing_) });
    swordObject_->SetScale({ kSwordScale, kSwordScale, kSwordScale });
    swordObject_->Update();
}

void KnightEnemy::Draw()
{
    if (state_ == State::Consumed) {
        return;
    }
    object_->Draw();
    swordObject_->Draw();
}
