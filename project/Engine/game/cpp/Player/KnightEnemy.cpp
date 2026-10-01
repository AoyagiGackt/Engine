/**
 * @file KnightEnemy.cpp
 * @brief KnightEnemyのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "KnightEnemy.h"
#include "EnemyTuning.h"
#include "GameConstants.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr const char* kKnightModelPath = "Resources/Knight/glTF/KnightCharacter.gltf";
constexpr const char* kKnightModelDirectory = "Resources/Knight/glTF";
constexpr const char* kKnightModelFile = "KnightCharacter.gltf";
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
// 剣の振り角を目標角へ寄せる1フレームあたりの補間率（状態ごと）
constexpr float kIdleSwordFollowRate = 0.15f;
constexpr float kTelegraphSwordFollowRate = 0.3f;
constexpr float kDashSwordFollowRate = 0.5f;
constexpr float kRecoverSwordFollowRate = 0.12f;

// 突進中に残す暗い靄の軌跡
constexpr const char* kDashSmokeEmitterName = "bt_knight_dash_smoke";
constexpr float kDashSmokeHeight = 0.5f; // 足元からの高さ
constexpr Vector4 kDashSmokeColor = { 0.25f, 0.15f, 0.3f, 0.5f };
constexpr float kDashSmokeSize = 0.6f;
constexpr float kDashSmokeLifetime = 0.25f;

// AI タイミング・被弾ノックバック・最大HPはResources/Config/enemy_params.jsonのknight節で調整する
const KnightEnemyTuning& Tuning() { return EnemyTuning::GetInstance()->Knight(); }

constexpr float kGroundY = 0.4f; // Dummy等と同じ地面の高さ

constexpr float kAbsorbDuration = 0.5f;

constexpr float kInitialSwordPullRatio = 0.35f; // 生成直後に予備動作の途中から始めるための剣の引き具合

// 抑制的な暗い紫のリムライト（本体・剣で共通）
constexpr Vector3 kRimColor = { 0.35f, 0.2f, 0.45f };
constexpr float kRimPower = 3.0f;
constexpr float kRimIntensity = 0.6f;

// 被弾・撃破
constexpr float kHitFlashDuration = 0.12f;
constexpr Vector4 kDefeatedColor = { 0.4f, 0.4f, 0.4f, 1.0f };
constexpr float kKnockbackGravity = 0.02f;
constexpr float kKnockbackStopSpeed = 0.001f; // これ未満の水平ノックバックは止める
constexpr float kDefeatedBodyLean = 1.35f;
constexpr float kDroppedSwordTilt = 1.45f;

// 吸収演出
constexpr float kBodyAbsorbDelay = 0.1f; // 剣より遅れて体が吸い込まれ始めるまでの割合
constexpr float kAbsorbGlowStartRed = 0.5f; // 光の色の始まり。進行に応じて1.0へ寄せる
constexpr float kAbsorbGlowStartGreen = 0.85f;
constexpr float kSwordTrailScaleStart = 0.35f;
constexpr float kSwordTrailScaleEnd = 0.12f;
constexpr float kSwordTrailLifetime = 0.2f;
constexpr float kBodyTrailScaleStart = 0.55f;
constexpr float kBodyTrailScaleEnd = 0.1f;
constexpr float kBodyTrailLifetime = 0.16f;
constexpr float kSwordAbsorbShrink = 0.6f; // 吸収完了時に剣を縮める割合
constexpr float kBodyAbsorbSpins = 3.0f; // 吸い込まれるまでの回転数

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
    state_ = State::Telegraph;
    // 初回更新から攻撃の予備動作へ移り、生成直後の棒立ちをなくす
    stateTimer_ = Tuning().telegraphDuration;
    swordSwing_ = kSwordPullBack * kInitialSwordPullRatio;
    maxHp_ = Tuning().maxHp;
    hp_ = maxHp_;

    skinCommon_ = std::make_unique<SkinCommon>();
    skinCommon_->Initialize(modelCommon->GetDxCommon());
    SkinnedObject3d::SetCommonModelCommon(modelCommon);
    SkinnedObject3d::SetCommonCamera(Object3d::GetCommonCamera());

    model_ = ModelManager::GetInstance()->GetOrLoadSkinned(
        modelCommon->GetDxCommon(), kKnightModelPath, kKnightTexture);
    object_ = std::make_unique<SkinnedObject3d>();
    object_->Initialize(skinCommon_.get());
    object_->SetModel(model_);
    object_->SetSkeleton(Skeleton::Create(
        LoadNodeHierarchyFromFile(kKnightModelDirectory, kKnightModelFile)));
    idleAnimation_ = LoadAnimationFile(kKnightModelDirectory, kKnightModelFile, "Idle_swordRight");
    attackAnimation_ = LoadAnimationFile(kKnightModelDirectory, kKnightModelFile, "Run_swordAttack");
    object_->SetAnimation(attackAnimation_);
    animationState_ = State::Telegraph;
    object_->SetEnableLighting(true);
    // Vを意識した抑制的な色  目立つ発光ではなく、わずかに暗い紫のリムに留める
    object_->SetRimColor(kRimColor);
    object_->SetRimPower(kRimPower);
    object_->SetRimIntensity(kRimIntensity);
    object_->SetEnableRim(true);

    swordModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon, kSwordModelPath, kSwordTexture);
    swordObject_ = std::make_unique<Object3d>();
    swordObject_->Initialize(modelCommon);
    swordObject_->SetModel(swordModel_);
    swordObject_->SetEnableLighting(true);
    swordObject_->SetRimColor(kRimColor);
    swordObject_->SetRimPower(kRimPower);
    swordObject_->SetRimIntensity(kRimIntensity);
    swordObject_->SetEnableRim(true);

    ApplyTransforms();
}

bool KnightEnemy::IsAlive() const
{
    return GetAIState(state_) != nullptr;
}

void KnightEnemy::TakeDamage(int damage, float knockDirX, float knockY)
{
    if (!IsAlive()) {
        return;
    }
    hp_ -= damage;
    hitFlash_ = kHitFlashDuration;
    knockVelX_ += knockDirX * Tuning().knockbackSpeed;
    knockVelY_ = knockY;
    if (hp_ <= 0) {
        ChangeState(State::Defeated);
        object_->SetAnimSpeed(0.0f);
        object_->SetColor(kDefeatedColor);
        object_->SetEnableRim(false);
        swordObject_->SetColor(kDefeatedColor);
        swordObject_->SetEnableRim(false);
    }
}

bool KnightEnemy::TryBeginAbsorb()
{
    if (state_ != State::Defeated) {
        return false;
    }
    ChangeState(State::Absorbing);
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
        knockVelX_ *= Tuning().knockbackDecay;
        knockVelY_ -= kKnockbackGravity; // 重力っぽく落ちる
        if (pos_.y <= kGroundY) {
            pos_.y = kGroundY;
            knockVelY_ = 0.0f;
        }
        if (std::abs(knockVelX_) < kKnockbackStopSpeed) {
            knockVelX_ = 0.0f;
        }
    }

    const State desiredAnimationState = (state_ == State::Telegraph || state_ == State::Dash) ? State::Dash : State::Idle;
    if (IsAlive() && desiredAnimationState != animationState_) {
        object_->SetAnimation(desiredAnimationState == State::Dash ? attackAnimation_ : idleAnimation_);
        animationState_ = desiredAnimationState;
    }

    ApplyTransforms();
}

//  AI State（生存中の行動フェーズ）

namespace engine::game {
class KnightEnemy::IdleAIState : public IAIState {
public:
    void Update(KnightEnemy& knight, ParticleManager* pm, const Vector3& playerPos) const override;
};
class KnightEnemy::TelegraphAIState : public IAIState {
public:
    void Update(KnightEnemy& knight, ParticleManager* pm, const Vector3& playerPos) const override;
};
class KnightEnemy::DashAIState : public IAIState {
public:
    void Update(KnightEnemy& knight, ParticleManager* pm, const Vector3& playerPos) const override;
};
class KnightEnemy::RecoverAIState : public IAIState {
public:
    void Update(KnightEnemy& knight, ParticleManager* pm, const Vector3& playerPos) const override;
};
}

void KnightEnemy::IdleAIState::Update(KnightEnemy& knight, ParticleManager*, const Vector3& playerPos) const
{
    knight.swordSwing_ = LerpF(knight.swordSwing_, 0.0f, kIdleSwordFollowRate);
    if (knight.stateTimer_ >= Tuning().idleDuration) {
        knight.ChangeState(State::Telegraph);
        knight.dashStart_ = knight.pos_;
        float dx = std::clamp(playerPos.x - knight.pos_.x, -Tuning().maxDashDistance, Tuning().maxDashDistance);
        knight.dashTarget_ = { knight.pos_.x + dx, knight.pos_.y, knight.pos_.z };
    }
}

void KnightEnemy::TelegraphAIState::Update(KnightEnemy& knight, ParticleManager*, const Vector3&) const
{
    float t = std::clamp(knight.stateTimer_ / Tuning().telegraphDuration, 0.0f, 1.0f);
    knight.swordSwing_ = LerpF(knight.swordSwing_, kSwordPullBack, kTelegraphSwordFollowRate);
    if (t >= 1.0f) {
        knight.ChangeState(State::Dash);
    }
}

void KnightEnemy::DashAIState::Update(KnightEnemy& knight, ParticleManager* pm, const Vector3&) const
{
    float t = std::clamp(knight.stateTimer_ / Tuning().dashDuration, 0.0f, 1.0f);
    float eased = EaseOutQuad(t);
    knight.pos_.x = LerpF(knight.dashStart_.x, knight.dashTarget_.x, eased);
    if (knight.dashTarget_.x != knight.dashStart_.x) {
        knight.yaw_ = (knight.dashTarget_.x >= knight.dashStart_.x) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;
    }
    knight.swordSwing_ = LerpF(knight.swordSwing_, kSwordSwingFwd, kDashSwordFollowRate);
    // 派手な閃光とは対照的な、暗い靄の軌跡
    if (pm) {
        pm->EmitTrail(kDashSmokeEmitterName, { knight.pos_.x, knight.pos_.y + kDashSmokeHeight, knight.pos_.z },
            kDashSmokeColor, kDashSmokeSize, kDashSmokeLifetime);
    }
    if (t >= 1.0f) {
        knight.ChangeState(State::Recover);
    }
}

void KnightEnemy::RecoverAIState::Update(KnightEnemy& knight, ParticleManager*, const Vector3&) const
{
    knight.swordSwing_ = LerpF(knight.swordSwing_, 0.0f, kRecoverSwordFollowRate);
    if (knight.stateTimer_ >= Tuning().recoverDuration) {
        knight.ChangeState(State::Idle);
    }
}

const KnightEnemy::IAIState* KnightEnemy::GetAIState(State state)
{
    static IdleAIState idle;
    static TelegraphAIState telegraph;
    static DashAIState dash;
    static RecoverAIState recover;
    switch (state) {
    case State::Idle:
        return &idle;
    case State::Telegraph:
        return &telegraph;
    case State::Dash:
        return &dash;
    case State::Recover:
        return &recover;
    default:
        return nullptr;
    }
}

void KnightEnemy::ChangeState(State next)
{
    state_ = next;
    stateTimer_ = 0.0f;
}

void KnightEnemy::UpdateAI(ParticleManager* pm, const Vector3& playerPos)
{
    stateTimer_ += GameConstants::kFrameDeltaTime;
    if (const IAIState* aiState = GetAIState(state_)) {
        aiState->Update(*this, pm, playerPos);
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
    float bodyEased = EaseInQuad(std::clamp((t - kBodyAbsorbDelay) / (1.0f - kBodyAbsorbDelay), 0.0f, 1.0f));
    Vector3 bodyFlyPos = {
        LerpF(pos_.x, target.x, bodyEased),
        LerpF(pos_.y, target.y, bodyEased),
        LerpF(pos_.z, target.z, bodyEased)
    };
    float bodyScale = kKnightModelScale * (1.0f - bodyEased);

    // 剣・体ともに光の粒に変わりながら吸い込まれていく軌跡
    Vector4 glowColor = { LerpF(kAbsorbGlowStartRed, 1.0f, t), LerpF(kAbsorbGlowStartGreen, 1.0f, t), 1.0f, 1.0f };
    if (pm) {
        pm->EmitTrail("bt_weapon_orb", swordFlyPos, glowColor, LerpF(kSwordTrailScaleStart, kSwordTrailScaleEnd, t), kSwordTrailLifetime);
        pm->EmitTrail("bt_weapon_orb", bodyFlyPos, glowColor, LerpF(kBodyTrailScaleStart, kBodyTrailScaleEnd, bodyEased), kBodyTrailLifetime);
    }

    swordObject_->SetPosition(swordFlyPos);
    swordObject_->SetRotation({ 0.0f, yaw_, facingSign * kSwordBaseTilt });
    const float swordAbsorbScale = kSwordScale * (1.0f - kSwordAbsorbShrink * t);
    swordObject_->SetScale({ swordAbsorbScale, swordAbsorbScale, swordAbsorbScale });
    swordObject_->SetColor(glowColor);
    swordObject_->Update();

    // 体は高速回転させながら縮め、球状に丸まっていくように見せる
    object_->SetPosition(bodyFlyPos);
    object_->SetRotation({ bodyEased * GameConstants::kTwoPi * kBodyAbsorbSpins, yaw_ + bodyEased * GameConstants::kTwoPi * kBodyAbsorbSpins, 0.0f });
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
        float f = hitFlash_ / kHitFlashDuration;
        object_->SetColor({ 1.0f + f, 1.0f + f, 1.0f + f, 1.0f });
    }

    object_->SetPosition(pos_);
    const float defeatedLean = state_ == State::Defeated ? facingSign * kDefeatedBodyLean : 0.0f;
    object_->SetRotation({ 0.0f, yaw_, defeatedLean });
    object_->SetScale({ kKnightModelScale, kKnightModelScale, kKnightModelScale });
    object_->Update();

    Vector3 swordPos = {
        pos_.x + facingSign * kSwordOffset.x,
        pos_.y + kSwordOffset.y,
        pos_.z + kSwordOffset.z
    };
    swordObject_->SetPosition(swordPos);
    const float droppedSwordTilt = state_ == State::Defeated ? facingSign * kDroppedSwordTilt : facingSign * (kSwordBaseTilt + swordSwing_);
    swordObject_->SetRotation({ 0.0f, yaw_, droppedSwordTilt });
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
