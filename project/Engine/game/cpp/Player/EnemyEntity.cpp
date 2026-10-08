/**
 * @file EnemyEntity.cpp
 * @brief EnemyEntityのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "EnemyEntity.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "ModelManager.h"
#include "TimeManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr const char* kAnimatedKnightPath = "Resources/Knight/glTF/KnightCharacter.gltf";
constexpr const char* kAnimatedKnightDirectory = "Resources/Knight/glTF";
constexpr const char* kAnimatedKnightFile = "KnightCharacter.gltf";
constexpr const char* kKnightTexture = "Resources/Knight/OBJ/KnightCharacterPalette.png";

/** @brief モンスターの見た目1種ぶん（静止モデルとパレットテクスチャ、騎士と同じくらいの背丈になる縮尺） */
struct MonsterVisualDef {
    const char* kind;
    const char* model;
    const char* texture;
    float scale;
    float yawOffset; ///< モデルの正面を騎士と同じ+Zへ揃えるための向き補正（ラジアン）
};
constexpr float kFrontPlusX = -GameConstants::kHalfPi; // 正面が+Xを向いて作られたモデルを+Z正面に揃える
// 縮尺は各モデルの実測の高さ（Slime約1.9 / Bat約4.7 / Dragon約3.5 / Skeleton約5.0）から、画面上の背丈を揃えて決めた
// 正面は目の頂点の位置から判定した（Slimeだけ+X正面、他は騎士と同じ+Z正面）
constexpr MonsterVisualDef kMonsterVisuals[] = {
    { "Slime", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Slime.obj", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SlimePalette.png", 0.47f, kFrontPlusX },
    { "Bat", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Bat_Palette.obj", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/BatPalette.png", 0.21f, 0.0f },
    { "Dragon", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Dragon_Palette.obj", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/DragonPalette.png", 0.38f, 0.0f },
    { "Skeleton", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Skeleton_Palette.obj", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SkeletonPalette.png", 0.215f, 0.0f },
};

const MonsterVisualDef* FindMonsterVisual(const std::string& kind)
{
    for (const MonsterVisualDef& def : kMonsterVisuals) {
        if (kind == def.kind) {
            return &def;
        }
    }
    return nullptr;
}
}

EnemyEntity::EnemyEntity()
    : archetype_(&FindArchetype("basic"))
{
    ResetAttackState();
}

//  Archetype（敵の性格）

namespace engine::game {
class EnemyEntity::BasicArchetype : public IArchetype {
public:
    Vector4 Color() const override { return kDefaultArchetypeColor_; }
    bool HasOwnColor() const override { return false; }
};
class EnemyEntity::FlyingArchetype : public IArchetype {
public:
    Vector4 Color() const override { return kFlyingColor_; }
    bool Hovers() const override { return true; }
};
class EnemyEntity::HealerArchetype : public IArchetype {
public:
    Vector4 Color() const override { return kHealerColor_; }
    bool Attacks() const override { return false; }
    bool HealsAllies() const override { return true; }
};
}

const EnemyEntity::IArchetype& EnemyEntity::FindArchetype(const std::string& name)
{
    static const BasicArchetype basic;
    static const FlyingArchetype flying;
    static const HealerArchetype healer;
    static const std::pair<const char*, const IArchetype*> kArchetypes[] = {
        { "basic", &basic },
        { "flying", &flying },
        { "healer", &healer },
    };
    for (const auto& [archetypeName, archetype] : kArchetypes) {
        if (name == archetypeName) {
            return *archetype;
        }
    }
    return basic;
}

void EnemyEntity::SetArchetype(const std::string& archetype)
{
    archetype_ = &FindArchetype(archetype);
    SetColor(archetype_->Color());
}

void EnemyEntity::Initialize(ModelCommon* modelCommon, const Vector3& startPos, WeaponType weaponType)
{
    pos_ = startPos;
    spawnX_ = startPos.x;
    spawnY_ = startPos.y;
    weaponType_ = weaponType;
    ResetAttackState();
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
    // ヒットストップ中はプレイヤーと同じく世界ごと止める（止めないと吹き飛びだけが先に進んで手応えが薄れる）
    if (TimeManager::GetInstance()->IsHitStopped()) {
        UpdateDuringHitStop();
        return;
    }
    hitStopShakeFrame_ = 0;

    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    archetypeTimer_ += GameConstants::kFrameDeltaTime;
    justLanded_ = false;
    slowTimer_ = (std::max)(slowTimer_ - GameConstants::kFrameDeltaTime, 0.0f);
    hitstunTimer_ = (std::max)(hitstunTimer_ - GameConstants::kFrameDeltaTime, 0.0f);

    // 撃破後は倒れた時の向きを保ち、プレイヤーの移動に反応しない。
    if (!defeated_) {
        facingSign_ = (playerX >= pos_.x) ? 1.0f : -1.0f;
    }

    bool walkedThisFrame = false;
    if (std::abs(knockVelX_) > kKnockbackStopSpeed_) {
        pos_.x += knockVelX_ * (slowTimer_ > 0.0f ? tuning.knockbackSlowMultiplier : 1.0f);
        knockVelX_ *= tuning.knockbackDecay;
    } else if (!defeated_ && !isLaunched_ && !IsInHitstun() && attackState_->IsIdle()) {
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

    if (archetype_->Hovers() && !isLaunched_ && !defeated_) {
        pos_.y = spawnY_ + std::sin(archetypeTimer_ * tuning.flyingBobSpeed) * tuning.flyingBobAmplitude;
    }

    object_->SetPosition(pos_);
    UpdateDroppedWeapon();
    PlaceWeapon(pos_);

    if (archetype_->Attacks()) {
        UpdateAttack(playerX);
    } else {
        ResetAttackState();
        justFiredAttack_ = false;
    }

    // 攻撃動作が最優先、次に接近歩行の走り、どちらでもなければ立ち姿勢
    // （歩行中にIdleのままだと棒立ちで滑って見える）
    const VisualAnim desiredAnimationState = !attackState_->IsIdle() ? VisualAnim::Attack
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
        if (IsInHitstun()) {
            // 硬直が残っているほど大きく反らせ、明けるにつれて構えへ戻す
            const float leanRatio = (std::min)(hitstunTimer_ / kHitstunLeanFullSeconds_, 1.0f);
            bodyLean = kHitstunBodyLean_ * leanRatio;
            weaponSwing = kHitstunWeaponSwing_;
        } else {
            attackState_->ApplyPose(bodyLean, weaponSwing);
        }
    }
    if (homeRunSpinTimer_ > 0.0f) {
        // 打ち飛ばされている間は縦に回転しながら飛ぶ
        homeRunSpinTimer_ = (std::max)(homeRunSpinTimer_ - GameConstants::kFrameDeltaTime, 0.0f);
        homeRunSpinAngle_ += kHomeRunSpinSpeed_;
        bodyLean += homeRunSpinAngle_;
    }
    const float facingYaw = facingSign_ >= 0.0f ? GameConstants::kHalfPi : -GameConstants::kHalfPi;
    object_->SetRotation({ 0.0f, facingYaw, bodyLean });
    if (weaponDropped_) {
        weaponObject_->SetRotation({ 0.0f, facingYaw + droppedWeaponSpin_, kDroppedWeaponLieTilt_ });
    } else {
        weaponObject_->SetRotation({ 0.0f, facingYaw, weaponSwing });
    }

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
    SetBodyColor(flashColor);
    weaponObject_->SetColor(flashColor);
    const float bodyScale = kBodyScale_ * (1.0f + kHitScalePunch_ * flash);
    object_->SetScale({ bodyScale, bodyScale, bodyScale });

    object_->Update();
    weaponObject_->Update();
    SyncMonsterVisual(true);
}

void EnemyEntity::UpdateAttack(float playerX)
{
    justFiredAttack_ = false;
    justStartedTelegraph_ = false;

    // 打ち上げ中/撃破後はステートマシンを止め、丸腰の演出中に攻撃が発生しないようにする
    if (defeated_ || isLaunched_) {
        return;
    }
    // 硬直中は攻撃の進行ごと止める（殴られながら平然と攻撃を続けないように）
    if (IsInHitstun()) {
        return;
    }

    attackTimer_ -= GameConstants::kFrameDeltaTime;
    if (attackTimer_ > 0.0f) {
        return;
    }

    attackState_->Advance(*this, EnemyTuning::GetInstance()->Basic(), playerX);
}

//  Attack State（攻撃の進行フェーズ）

namespace engine::game {
class EnemyEntity::IdleAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    void ApplyPose(float&, float&) const override { }
    bool IsIdle() const override { return true; }
};
class EnemyEntity::TelegraphAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    void ApplyPose(float& bodyLean, float& weaponSwing) const override
    {
        bodyLean = kTelegraphBodyLean_;
        weaponSwing = kTelegraphWeaponSwing_;
    }
    bool IsTelegraph() const override { return true; }
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
    enemy.attackState_ = &AttackStateOf<TelegraphAttackState>();
    enemy.justStartedTelegraph_ = true;
    enemy.attackTimer_ = type == WeaponType::Dagger                     ? tuning.daggerTelegraph
        : type == WeaponType::Spear                                     ? tuning.spearTelegraph
        : (type == WeaponType::Hammer || type == WeaponType::Axe)       ? tuning.heavyTelegraph
                                                                        : tuning.attackTelegraph;
}

void EnemyEntity::TelegraphAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float) const
{
    enemy.attackState_ = &AttackStateOf<ActiveAttackState>();
    enemy.attackTimer_ = tuning.attackActive;
    enemy.justFiredAttack_ = true;
}

void EnemyEntity::ActiveAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float) const
{
    const WeaponType type = enemy.weaponType_;
    enemy.attackState_ = &AttackStateOf<IdleAttackState>();
    enemy.attackTimer_ = type == WeaponType::Dagger                     ? tuning.daggerRecovery
        : type == WeaponType::Spear                                     ? tuning.spearRecovery
        : (type == WeaponType::Hammer || type == WeaponType::Axe)       ? tuning.heavyRecovery
                                                                        : tuning.attackInterval;
}

template <class T>
const EnemyEntity::IAttackState& EnemyEntity::AttackStateOf()
{
    static const T instance;
    return instance;
}

void EnemyEntity::ResetAttackState()
{
    attackState_ = &AttackStateOf<IdleAttackState>();
}

void EnemyEntity::Draw()
{
    if (!visible_) {
        return;
    }
    // モンスターは武器を持たないので、差し替えた本体だけを描く
    if (monsterObject_) {
        monsterObject_->Draw();
        return;
    }
    object_->Draw();
    weaponObject_->Draw();
}

void EnemyEntity::SetMonsterVisual(ModelCommon* modelCommon, const std::string& kind)
{
    const MonsterVisualDef* def = FindMonsterVisual(kind);
    if (def == nullptr) {
        return;
    }
    monsterObject_ = std::make_unique<Object3d>();
    monsterObject_->Initialize(modelCommon);
    monsterObject_->SetModel(ModelManager::GetInstance()->GetOrLoad(modelCommon, def->model, def->texture));
    monsterObject_->SetEnableLighting(true);
    monsterScale_ = def->scale;
    monsterYawOffset_ = def->yawOffset;
    // 素材の色をそのまま見せる（回復役だけは緑で見分けられるようにする）
    SetColor(IsHealer() ? kHealerColor_ : kDefaultArchetypeColor_);
    SyncMonsterVisual(false);
}

void EnemyEntity::SyncMonsterVisual(bool advanceBob)
{
    if (!monsterObject_) {
        return;
    }
    if (advanceBob) {
        monsterBobTimer_ += GameConstants::kFrameDeltaTime;
    }
    // 素材は静止モデルなので、生きている間はその場で小さく弾ませて動きを足す
    const float bob = defeated_ ? 0.0f : std::sin(monsterBobTimer_ * kMonsterBobSpeed_) * kMonsterBobAmplitude_;
    const Vector3 bodyPos = object_->GetPosition();
    // 騎士の本体スケールに対する比（被弾した瞬間の膨らみ）をモンスターにも掛ける
    const float punch = object_->GetScale().x / kBodyScale_;
    const float scale = monsterScale_ * punch;
    monsterObject_->SetPosition({ bodyPos.x, bodyPos.y + bob, bodyPos.z });
    Vector3 rotation = object_->GetRotation();
    rotation.y += monsterYawOffset_;
    monsterObject_->SetRotation(rotation);
    monsterObject_->SetScale({ scale, scale, scale });
    monsterObject_->SetColor(bodyColor_);
    monsterObject_->Update();
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
    bool switchPull, float playerX, bool finisher)
{
    if (defeated_) {
        return;
    }
    if (switchPull) {
        const float toPlayer = playerX - pos_.x;
        knockVelX_ = std::clamp(toPlayer * kSwitchPullStrength_, -kSwitchPullClamp_, kSwitchPullClamp_);
        airComboTimer_ = EnemyTuning::GetInstance()->Basic().airComboHold + kSwitchPullAirComboBonus_;
    } else {
        // 加算すると段を重ねるほど遠ざかって次の段が空振りするため、毎ヒット上書きして途中の段は前進距離より短く押すだけにする
        knockVelX_ = knockDirX * (finisher ? kFinisherKnockDirXScale_ : kKnockDirXScale_);
    }
    if (knockY > kLaunchThreshold_) {
        Launch(knockY);
    }
}

void EnemyEntity::ApplyHitstun(float seconds, bool interruptAttack)
{
    if (defeated_) {
        return;
    }
    hitstunTimer_ = (std::max)(hitstunTimer_, seconds);
    if (interruptAttack) {
        ResetAttackState();
        attackTimer_ = (std::max)(attackTimer_, kPostHitstunAttackDelay_);
    }
}

void EnemyEntity::ApplyHomeRun(float dirX)
{
    // 撃破済みでも飛ばす（とどめの一撃で遠くまで打ち飛ばすのが見せ場なので、ApplyComboReactionの撃破ガードは通さない）
    knockVelX_ = dirX * kHomeRunSpeedX_;
    Launch(kHomeRunLaunchY_);
    homeRunSpinTimer_ = kHomeRunSpinSeconds_;
    homeRunSpinAngle_ = 0.0f;
    ResetAttackState();
    DropWeapon();
}

void EnemyEntity::DropWeapon()
{
    if (weaponDropped_) {
        return;
    }
    weaponDropped_ = true;
    droppedWeaponLanded_ = false;
    droppedWeaponPos_ = { pos_.x + facingSign_ * kWeaponOffsetX_, pos_.y + kWeaponOffsetY_, pos_.z + kWeaponOffsetZ_ };
    droppedWeaponVelY_ = kDroppedWeaponPopY_;
    droppedWeaponSpin_ = 0.0f;
    // 打ち上げ中なら打ち上げ前の足元、地上ならいまの足元を地面とみなす
    droppedWeaponGroundY_ = (isLaunched_ ? launchOriginY_ : pos_.y) + kDroppedWeaponRestHeight_;
}

void EnemyEntity::PullDroppedWeaponToward(const Vector3& target, float rate)
{
    if (!weaponDropped_) {
        return;
    }
    droppedWeaponLanded_ = true; // 吸い込み中は落下させない
    droppedWeaponPos_.x += (target.x - droppedWeaponPos_.x) * rate;
    droppedWeaponPos_.y += (target.y - droppedWeaponPos_.y) * rate;
    weaponObject_->SetPosition(droppedWeaponPos_);
    weaponObject_->Update();
}

void EnemyEntity::UpdateDroppedWeapon()
{
    if (!weaponDropped_ || droppedWeaponLanded_) {
        return;
    }
    droppedWeaponVelY_ -= kDroppedWeaponGravity_;
    droppedWeaponPos_.y += droppedWeaponVelY_;
    droppedWeaponSpin_ += kDroppedWeaponSpinPerFrame_;
    if (droppedWeaponPos_.y <= droppedWeaponGroundY_) {
        droppedWeaponPos_.y = droppedWeaponGroundY_;
        droppedWeaponLanded_ = true;
    }
}

void EnemyEntity::PlaceWeapon(const Vector3& bodyPos)
{
    if (weaponDropped_) {
        weaponObject_->SetPosition(droppedWeaponPos_);
        return;
    }
    weaponObject_->SetPosition({ bodyPos.x + facingSign_ * kWeaponOffsetX_, bodyPos.y + kWeaponOffsetY_, bodyPos.z + kWeaponOffsetZ_ });
}

void EnemyEntity::UpdateDuringHitStop()
{
    // 1フレームだけ立つフラグは停止前のフレームでシーン側が読み終えているため、停止明けに二重に拾われないよう落とす
    justLanded_ = false;
    justFiredAttack_ = false;
    justStartedTelegraph_ = false;

    Vector3 visualPos = pos_;
    if (hitFlashTimer_ > 0.0f) {
        // 殴られた敵だけを白く光らせたまま左右に小刻みに震わせる
        ++hitStopShakeFrame_;
        visualPos.x += (hitStopShakeFrame_ % 2 == 0 ? 1.0f : -1.0f) * kHitStopShakeAmplitude_;
        SetBodyColor(defeated_ ? kDefeatedColor_ : kHitFlashColor_);
        weaponObject_->SetColor(defeated_ ? kDefeatedColor_ : kHitFlashColor_);
        const float punchedScale = kBodyScale_ * (1.0f + kHitScalePunch_);
        object_->SetScale({ punchedScale, punchedScale, punchedScale });
    }
    object_->SetPosition(visualPos);
    PlaceWeapon(visualPos);

    // アニメーションも止めたまま行列だけ更新する（カメラ揺れで画面に張り付かないように）
    const float animSpeed = object_->GetAnimSpeed();
    object_->SetAnimSpeed(0.0f);
    object_->Update();
    object_->SetAnimSpeed(animSpeed);
    weaponObject_->Update();
    SyncMonsterVisual(false);
}
