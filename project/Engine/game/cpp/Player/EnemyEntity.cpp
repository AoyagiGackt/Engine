/**
 * @file EnemyEntity.cpp
 * @brief EnemyEntityのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "EnemyEntity.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "ModelManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr const char* kAnimatedKnightPath = "Resources/Knight/glTF/KnightCharacter.gltf";
constexpr const char* kAnimatedKnightDirectory = "Resources/Knight/glTF";
constexpr const char* kAnimatedKnightFile = "KnightCharacter.gltf";
constexpr const char* kKnightTexture = "Resources/Knight/OBJ/KnightCharacterPalette.png";
}

void EnemyEntity::Initialize(ModelCommon* modelCommon, const Vector3& startPos, WeaponType weaponType)
{
    pos_ = startPos;
    spawnX_ = startPos.x;
    spawnY_ = startPos.y;
    weaponType_ = weaponType;
    attackState_ = AttackState::Idle;
    attackTimer_ = 0.0f;
    justFiredAttack_ = false;

    model_ = ModelManager::GetInstance()->GetOrLoad(modelCommon,
        "Resources/Knight/OBJ/KnightCharacter.obj",
        "Resources/Knight/OBJ/KnightCharacterPalette.png");

    skinCommon_ = std::make_unique<SkinCommon>();
    skinCommon_->Initialize(modelCommon->GetDxCommon());
    SkinnedObject3d::SetCommonModelCommon(modelCommon);
    SkinnedObject3d::SetCommonCamera(Object3d::GetCommonCamera());
    animatedModel_ = ModelManager::GetInstance()->GetOrLoadSkinned(
        modelCommon->GetDxCommon(), kAnimatedKnightPath, kKnightTexture);
    object_ = std::make_unique<SkinnedObject3d>();
    object_->Initialize(skinCommon_.get());
    object_->SetModel(animatedModel_);
    object_->SetSkeleton(Skeleton::Create(
        LoadNodeHierarchyFromFile(kAnimatedKnightDirectory, kAnimatedKnightFile)));
    idleAnimation_ = LoadAnimationFile(
        kAnimatedKnightDirectory, kAnimatedKnightFile, "Idle_swordRight");
    runAnimation_ = LoadAnimationFile(
        kAnimatedKnightDirectory, kAnimatedKnightFile, "Run_swordRight");
    attackAnimation_ = LoadAnimationFile(
        kAnimatedKnightDirectory, kAnimatedKnightFile, "Run_swordAttack");
    object_->SetAnimation(attackAnimation_);
    animationState_ = VisualAnim::Attack;
    object_->SetEnableLighting(true);
    object_->SetScale({ kBodyScale_, kBodyScale_, kBodyScale_ });
    object_->SetPosition(pos_);
    object_->Update();

    const char* weaponPath = "Resources/Knight/OBJ/Sword.obj";
    const char* weaponTexture = "Resources/Knight/OBJ/SwordPalette.png";
    if (weaponType == WeaponType::Spear) {
        weaponPath = "Resources/Knight/OBJ/Katana.obj";
        weaponTexture = "Resources/Knight/OBJ/KatanaPalette.png";
        weaponScale_ = kSpearWeaponScale_;
    } else if (weaponType == WeaponType::Hammer || weaponType == WeaponType::Axe) {
        weaponPath = "Resources/Knight/OBJ/Club.obj";
        weaponTexture = "Resources/Knight/OBJ/KnightCharacterPalette.png";
        weaponScale_ = kHeavyWeaponScale_;
    }
    weaponModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon, weaponPath, weaponTexture);
    weaponObject_ = std::make_unique<Object3d>();
    weaponObject_->Initialize(modelCommon);
    weaponObject_->SetModel(weaponModel_);
    weaponObject_->SetEnableLighting(true);
    weaponObject_->SetScale(weaponScale_);
    weaponObject_->SetPosition({ pos_.x + facingSign_ * kWeaponOffsetX_, pos_.y + kWeaponOffsetY_, pos_.z + kWeaponOffsetZ_ });
    weaponObject_->SetRotation({ 0.0f, facingSign_ >= 0.0f ? GameConstants::kHalfPi : -GameConstants::kHalfPi, kWeaponRestTilt_ });
    weaponObject_->Update();
}

void EnemyEntity::Update(float playerX)
{
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    archetypeTimer_ += GameConstants::kFrameDeltaTime;
    justLanded_ = false;
    slowTimer_ = (std::max)(slowTimer_ - GameConstants::kFrameDeltaTime, 0.0f);

    // 撃破後は倒れた時の向きを保ち、プレイヤーの移動に反応しない。
    if (!defeated_) {
        facingSign_ = (playerX >= pos_.x) ? 1.0f : -1.0f;
    }

    bool walkedThisFrame = false;
    if (std::abs(knockVelX_) > kKnockbackStopSpeed_) {
        pos_.x += knockVelX_ * (slowTimer_ > 0.0f ? tuning.knockbackSlowMultiplier : 1.0f);
        knockVelX_ *= tuning.knockbackDecay;
    } else if (!defeated_ && !isLaunched_ && attackState_ == AttackState::Idle) {
        // 予備動作/攻撃中やノックバック中は歩かせない。プレイヤーが持ち場に近づいてくるまでは待機し、
        // 近づいてきたら間合いの外にいる間だけ追うが、持ち場から離れすぎたら止まる（全員が団子にならないように）
        const bool playerNearPost = std::abs(playerX - spawnX_) <= tuning.aggroRange;
        const bool withinLeash = std::abs(pos_.x - spawnX_) < tuning.leashDistance;
        const float dx = playerX - pos_.x;
        if (playerNearPost && withinLeash && std::abs(dx) > tuning.engageRange) {
            pos_.x += dx > 0.0f ? tuning.approachSpeed : -tuning.approachSpeed;
            walkedThisFrame = true;
        }
    }

    if (isLaunched_) {
        airComboTimer_ = (std::max)(airComboTimer_ - GameConstants::kFrameDeltaTime, 0.0f);
        const float gravity = airComboTimer_ > 0.0f ? tuning.gravity * tuning.airComboGravityScale : tuning.gravity;
        if (ApplyGravityAndClampY(pos_.y, velY_, gravity, launchOriginY_, tuning.ceilingY, kCeilingBounceFactor_)) {
            isLaunched_ = false;
            justLanded_ = true;
        }
    }
    // 非打ち上げ中はpos_.yに一切触れない。ステージエディタで配置・ドラッグした高さをそのまま信用する

    if (archetype_ == "flying" && !isLaunched_ && !defeated_) {
        pos_.y = spawnY_ + std::sin(archetypeTimer_ * tuning.flyingBobSpeed) * tuning.flyingBobAmplitude;
    }

    object_->SetPosition(pos_);
    weaponObject_->SetPosition({ pos_.x + facingSign_ * kWeaponOffsetX_, pos_.y + kWeaponOffsetY_, pos_.z + kWeaponOffsetZ_ });

    if (archetype_ != "healer") {
        UpdateAttack(playerX);
    } else {
        attackState_ = AttackState::Idle;
        justFiredAttack_ = false;
    }

    // 攻撃動作が最優先、次に接近歩行の走り、どちらでもなければ立ち姿勢
    // （歩行中にIdleのままだと棒立ちで滑って見える）
    const VisualAnim desiredAnimationState = attackState_ != AttackState::Idle ? VisualAnim::Attack
        : walkedThisFrame                                                      ? VisualAnim::Run
                                                                               : VisualAnim::Idle;
    if (!defeated_ && desiredAnimationState != animationState_) {
        object_->SetAnimation(desiredAnimationState == VisualAnim::Attack ? attackAnimation_
                : desiredAnimationState == VisualAnim::Run                ? runAnimation_
                                                                          : idleAnimation_);
        animationState_ = desiredAnimationState;
    }

    float bodyLean = defeated_ ? facingSign_ * kDefeatedBodyLean_ : 0.0f;
    float weaponSwing = defeated_ ? facingSign_ * kDefeatedWeaponTilt_ : kWeaponRestTilt_;
    if (!defeated_ && !isLaunched_) {
        GetAttackState(attackState_).ApplyPose(bodyLean, weaponSwing);
    }
    const float facingYaw = facingSign_ >= 0.0f ? GameConstants::kHalfPi : -GameConstants::kHalfPi;
    object_->SetRotation({ 0.0f, facingYaw, bodyLean });
    weaponObject_->SetRotation({ 0.0f, facingYaw, weaponSwing });

    // 被弾フラッシュ: 基準色→白へ寄せ、本体を一瞬膨らませてから戻す
    float flash = 0.0f;
    if (hitFlashTimer_ > 0.0f) {
        hitFlashTimer_ = (std::max)(hitFlashTimer_ - GameConstants::kFrameDeltaTime, 0.0f);
        flash = hitFlashTimer_ / kHitFlashDuration_;
    }
    Vector4 flashColor = kDefeatedColor_;
    if (!defeated_) {
        // 予備動作中は基準色を警告色へ寄せる（被弾フラッシュはその上から白へ寄せる）
        const float warn = IsTelegraphing() ? kTelegraphTintStrength_ : 0.0f;
        const Vector4 tintedColor = {
            baseColor_.x + (kTelegraphTint_.x - baseColor_.x) * warn,
            baseColor_.y + (kTelegraphTint_.y - baseColor_.y) * warn,
            baseColor_.z + (kTelegraphTint_.z - baseColor_.z) * warn,
            baseColor_.w
        };
        flashColor = {
            tintedColor.x + (1.0f - tintedColor.x) * flash,
            tintedColor.y + (1.0f - tintedColor.y) * flash,
            tintedColor.z + (1.0f - tintedColor.z) * flash,
            tintedColor.w
        };
    }
    object_->SetColor(flashColor);
    weaponObject_->SetColor(flashColor);
    const float bodyScale = kBodyScale_ * (1.0f + kHitScalePunch_ * flash);
    object_->SetScale({ bodyScale, bodyScale, bodyScale });

    object_->Update();
    weaponObject_->Update();
}

void EnemyEntity::UpdateAttack(float playerX)
{
    justFiredAttack_ = false;
    justStartedTelegraph_ = false;

    // 打ち上げ中/撃破後はステートマシンを止め、丸腰の演出中に攻撃が発生しないようにする
    if (defeated_ || isLaunched_) {
        return;
    }

    attackTimer_ -= GameConstants::kFrameDeltaTime;
    if (attackTimer_ > 0.0f) {
        return;
    }

    GetAttackState(attackState_).Advance(*this, EnemyTuning::GetInstance()->Basic(), playerX);
}

//  Attack State（攻撃の進行フェーズ）

namespace engine::game {
class EnemyEntity::IdleAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    void ApplyPose(float&, float&) const override { }
};
class EnemyEntity::TelegraphAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    void ApplyPose(float& bodyLean, float& weaponSwing) const override
    {
        bodyLean = kTelegraphBodyLean_;
        weaponSwing = kTelegraphWeaponSwing_;
    }
};
class EnemyEntity::ActiveAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    void ApplyPose(float& bodyLean, float& weaponSwing) const override
    {
        bodyLean = kActiveBodyLean_;
        weaponSwing = kActiveWeaponSwing_;
    }
};
}

void EnemyEntity::IdleAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const
{
    // 遠隔の敵は持ち場基準の索敵距離、近接の敵は自分からの間合いで攻撃開始を判定する
    // （遠くの敵が延々と素振り・発砲を繰り返さないように。近接敵は届く距離でしか振らない）
    const bool inStartRange = enemy.IsMeleeAttacker()
        ? std::abs(playerX - enemy.pos_.x) <= tuning.meleeAttackRange
        : std::abs(playerX - enemy.spawnX_) <= tuning.aggroRange;
    if (!inStartRange) {
        enemy.attackTimer_ = 0.0f;
        return;
    }
    const WeaponType type = enemy.weaponType_;
    enemy.attackState_ = AttackState::Telegraph;
    enemy.justStartedTelegraph_ = true;
    enemy.attackTimer_ = type == WeaponType::Dagger                     ? tuning.daggerTelegraph
        : type == WeaponType::Spear                                     ? tuning.spearTelegraph
        : (type == WeaponType::Hammer || type == WeaponType::Axe)       ? tuning.heavyTelegraph
                                                                        : tuning.attackTelegraph;
}

void EnemyEntity::TelegraphAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float) const
{
    enemy.attackState_ = AttackState::Active;
    enemy.attackTimer_ = tuning.attackActive;
    enemy.justFiredAttack_ = true;
}

void EnemyEntity::ActiveAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float) const
{
    const WeaponType type = enemy.weaponType_;
    enemy.attackState_ = AttackState::Idle;
    enemy.attackTimer_ = type == WeaponType::Dagger                     ? tuning.daggerRecovery
        : type == WeaponType::Spear                                     ? tuning.spearRecovery
        : (type == WeaponType::Hammer || type == WeaponType::Axe)       ? tuning.heavyRecovery
                                                                        : tuning.attackInterval;
}

const EnemyEntity::IAttackState& EnemyEntity::GetAttackState(AttackState state)
{
    static IdleAttackState idle;
    static TelegraphAttackState telegraph;
    static ActiveAttackState active;
    switch (state) {
    case AttackState::Telegraph:
        return telegraph;
    case AttackState::Active:
        return active;
    default:
        return idle;
    }
}

void EnemyEntity::Draw()
{
    if (!visible_) {
        return;
    }
    object_->Draw();
    weaponObject_->Draw();
}

void EnemyEntity::Launch(float velY)
{
    if (!isLaunched_) {
        launchOriginY_ = pos_.y; // 最初の打ち上げ時点の高さを着地目標として記録する（空中コンボ中は上書きしない）
    }
    isLaunched_ = true;
    velY_ = velY;
    airComboTimer_ = EnemyTuning::GetInstance()->Basic().airComboHold;
}

void EnemyEntity::ApplyComboReaction(float knockDirX, float knockY,
    bool switchPull, float playerX)
{
    if (defeated_) {
        return;
    }
    if (switchPull) {
        const float toPlayer = playerX - pos_.x;
        knockVelX_ = std::clamp(toPlayer * kSwitchPullStrength_, -kSwitchPullClamp_, kSwitchPullClamp_);
        airComboTimer_ = EnemyTuning::GetInstance()->Basic().airComboHold + kSwitchPullAirComboBonus_;
    } else {
        knockVelX_ += knockDirX * kKnockDirXScale_;
    }
    if (knockY > kLaunchThreshold_) {
        Launch(knockY);
    }
}
