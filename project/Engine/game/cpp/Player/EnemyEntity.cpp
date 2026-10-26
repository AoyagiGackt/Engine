/**
 * @file EnemyEntity.cpp
 * @brief 武器を持つ敵（EnemyEntity）の性格・攻撃の状態・被弾・描画の実装
 */
#include "EnemyEntity.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "ModelManager.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <iterator>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kAnimatedKnightPath = "Resources/Knight/glTF/KnightCharacter.gltf";
constexpr const char* kAnimatedKnightDirectory = "Resources/Knight/glTF";
constexpr const char* kAnimatedKnightFile = "KnightCharacter.gltf";
constexpr const char* kKnightTexture = "Resources/Knight/OBJ/KnightCharacterPalette.png";

/** @brief 敵が持つ武器種ごとの攻撃の性質（時間とダメージは EnemyTuning のどの項目を使うか） */
struct EnemyAttackProfile {
    bool melee; ///< 近接で殴るか（false なら投擲＝遠隔）
    float BasicEnemyTuning::* telegraph; ///< 予備動作の長さ
    float BasicEnemyTuning::* recovery; ///< 攻撃後の隙
    int BasicEnemyTuning::* damage; ///< 与えるダメージ
};
constexpr EnemyAttackProfile kStandardAttack = { true, &BasicEnemyTuning::attackTelegraph, &BasicEnemyTuning::attackInterval, &BasicEnemyTuning::attackDamage };
constexpr EnemyAttackProfile kDaggerAttack = { true, &BasicEnemyTuning::daggerTelegraph, &BasicEnemyTuning::daggerRecovery, &BasicEnemyTuning::attackDamage };
constexpr EnemyAttackProfile kSpearAttack = { false, &BasicEnemyTuning::spearTelegraph, &BasicEnemyTuning::spearRecovery, &BasicEnemyTuning::attackDamage };
constexpr EnemyAttackProfile kHeavyAttack = { true, &BasicEnemyTuning::heavyTelegraph, &BasicEnemyTuning::heavyRecovery, &BasicEnemyTuning::heavyAttackDamage };
constexpr EnemyAttackProfile kThrowAttack = { false, &BasicEnemyTuning::attackTelegraph, &BasicEnemyTuning::attackInterval, &BasicEnemyTuning::attackDamage };
// WeaponType の並び順
constexpr EnemyAttackProfile kAttackProfiles[] = {
    kStandardAttack, // Sword
    kSpearAttack, // Spear
    kHeavyAttack, // Hammer
    kDaggerAttack, // Dagger
    kThrowAttack, // Ball
    kStandardAttack, // Greatsword
    kStandardAttack, // Scythe
    kHeavyAttack, // Axe
};

/** @brief 武器の種類に合った敵の攻撃パラメーターを表から返す */
const EnemyAttackProfile& AttackProfileOf(WeaponType type) {
    const size_t index = static_cast<size_t>(type);
    return index < std::size(kAttackProfiles) ? kAttackProfiles[index] : kStandardAttack;
}


}

/** @brief 性格を基本の敵にし、攻撃状態を待機に戻して初期化する */
EnemyEntity::EnemyEntity()
    : archetype_(&FindArchetype("basic")) {
    ResetAttackState();
}

/** @brief 近接で殴ってくる敵か（槍とボールは投擲＝遠隔扱い） */
bool EnemyEntity::IsMeleeAttacker() const {
    return AttackProfileOf(weaponType_).melee;
}

/** @brief 攻撃がヒットした際に与えるダメージ量を返す */
int EnemyEntity::GetAttackDamage() const {
    // 倍率を掛けても、当たったのに無傷にはならないよう最低1にする
    const int base = EnemyTuning::GetInstance()->Basic().*AttackProfileOf(weaponType_).damage;
    return (std::max)(1, static_cast<int>(std::round(static_cast<float>(base) * damageScale_)));
}

//  Archetype（敵の性格）


/** @brief 地上で武器を使って攻撃する基本の敵の性格 */
class EnemyEntity::BasicArchetype : public IArchetype {
public:
    /** @brief 基本の敵の色を返す */
    Vector4 Color() const override {
        return kDefaultArchetypeColor;
    }
    /** @brief 装備の色に代えて性格固有の色を使用するか返す */
    bool HasOwnColor() const override {
        return false;
    }
};
/** @brief 持ち場の高さで浮遊する敵の性格 */
class EnemyEntity::FlyingArchetype : public IArchetype {
public:
    /** @brief 飛ぶ敵の色を返す */
    Vector4 Color() const override {
        return kFlyingColor;
    }
    /** @brief 持ち場の高さで浮遊する性格か返す */
    bool Hovers() const override {
        return true;
    }
};
/** @brief 自分から攻撃せず周囲の味方を回復する敵の性格 */
class EnemyEntity::HealerArchetype : public IArchetype {
public:
    /** @brief 回復役の敵の色を返す */
    Vector4 Color() const override {
        return kHealerColor;
    }
    /** @brief 自分から攻撃を開始する性格か返す */
    bool Attacks() const override {
        return false;
    }
    /** @brief 周囲の味方を回復する性格か返す */
    bool HealsAllies() const override {
        return true;
    }
};


/** @brief 名前に対応する性格を返す（未知の名前はbasic） */
const EnemyEntity::IArchetype& EnemyEntity::FindArchetype(const std::string& name) {
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

/** @brief 性格名（レベルJSONの spawnType）が空を飛ぶ性格か（エディタの配置チェック用） */
bool EnemyEntity::IsFlyingArchetype(const std::string& archetype) {
    return FindArchetype(archetype).Hovers();
}

/** @brief 敵の性格（"basic" "flying" "healer"）を設定し、性格ごとの色に塗る */
void EnemyEntity::SetArchetype(const std::string& archetype) {
    archetype_ = &FindArchetype(archetype);
    SetColor(archetype_->Color());
}

/** @brief モデルを生成し、ワールド座標を設定して初期化する */
void EnemyEntity::Initialize(ModelCommon* modelCommon, const Vector3& startPos, WeaponType weaponType) {
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

    skinCommon_ = SkinnedObject3d::CreateSharedCommon(modelCommon);
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
    animationState_ = VisualAnim::kAttack;
    object_->SetEnableLighting(true);
    object_->SetScale({ kBodyScale, kBodyScale, kBodyScale });
    object_->SetPosition(pos_);
    object_->Update();

    // WeaponType の並び順に、手に持たせる武器の見た目
    struct HeldWeaponVisual {
        const char* model;
        const char* texture;
        Vector3 scale;
    };
    static constexpr HeldWeaponVisual kSword = { "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png", kDefaultWeaponScale };
    static constexpr HeldWeaponVisual kKatana = { "Resources/Knight/OBJ/Katana.obj", "Resources/Knight/OBJ/KatanaPalette.png", kSpearWeaponScale };
    static constexpr HeldWeaponVisual kClub = { "Resources/Knight/OBJ/Club.obj", "Resources/Knight/OBJ/KnightCharacterPalette.png", kHeavyWeaponScale };
    static constexpr HeldWeaponVisual kHeldWeapons[] = {
        kSword, // Sword
        kKatana, // Spear
        kClub, // Hammer
        kSword, // Dagger
        kSword, // Ball
        kSword, // Greatsword
        kSword, // Scythe
        kClub, // Axe
    };
    const size_t weaponIndex = static_cast<size_t>(weaponType);
    const HeldWeaponVisual& visual = weaponIndex < std::size(kHeldWeapons) ? kHeldWeapons[weaponIndex] : kSword;
    weaponScale_ = visual.scale;
    weaponModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon, visual.model, visual.texture);
    weaponObject_ = std::make_unique<Object3d>();
    weaponObject_->Initialize(modelCommon);
    weaponObject_->SetModel(weaponModel_);
    weaponObject_->SetEnableLighting(true);
    weaponObject_->SetScale(weaponScale_);
    weaponObject_->SetPosition({ pos_.x + facingSign_ * kWeaponOffsetX, pos_.y + kWeaponOffsetY, pos_.z + kWeaponOffsetZ });
    weaponObject_->SetRotation({ 0.0f, facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi, kWeaponRestTilt });
    weaponObject_->Update();
}

/** @brief 物理・アニメーションを毎フレーム更新する */
void EnemyEntity::Update(float playerX) {
    // ヒットストップ中はプレイヤーと同じく世界ごと止める（止めないと吹き飛びだけが先に進んで手応えが薄れる）
    if (TimeManager::GetInstance()->IsHitStopped()) {
        UpdateDuringHitStop();
        return;
    }
    hitStopShakeFrame_ = 0;

    // 各タイマーを1フレーム進める（減速とのけぞりは0で止める）
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    archetypeTimer_ += gameConstants::kFrameDeltaTime;
    justLanded_ = false;
    slowTimer_ = (std::max)(slowTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
    hitstunTimer_ = (std::max)(hitstunTimer_ - gameConstants::kFrameDeltaTime, 0.0f);

    // 撃破後は倒れた時の向きを保ち、プレイヤーの移動に反応しない。
    if (!defeated_) {
        facingSign_ = (playerX >= pos_.x) ? 1.0f : -1.0f;
    }

    // 横移動: ノックバック中はその勢いで滑り、そうでなければ条件付きでプレイヤーへ歩く
    bool walkedThisFrame = false;
    if (std::abs(knockVelX_) > kKnockbackStopSpeed) {
        pos_.x += knockVelX_ * (slowTimer_ > 0.0f ? tuning.knockbackSlowMultiplier : 1.0f);
        knockVelX_ *= tuning.knockbackDecay;
    } else if (!defeated_ && !isLaunched_ && !IsInHitstun() && attackState_->IsIdle()) {
        // 予備動作/攻撃中やノックバック中は歩かせない。プレイヤーが持ち場に近づいてくるまでは待機し、
        // 近づいてきたら間合いの外にいる間だけ追うが、持ち場から離れすぎたら止まる（全員が団子にならないように）
        const bool playerNearPost = std::abs(playerX - spawnX_) <= tuning.aggroRange;
        const bool withinLeash = std::abs(pos_.x - spawnX_) < tuning.leashDistance;
        const float dx = playerX - pos_.x;
        if (playerNearPost && withinLeash && std::abs(dx) > tuning.engageRange) {
            const float speed = tuning.approachSpeed * speedScale_;
            pos_.x += dx > 0.0f ? speed : -speed;
            walkedThisFrame = true;
        }
    }

    // 打ち上げ中だけ重力で落とし、着地したら着地した瞬間として知らせる
    if (isLaunched_) {
        airComboTimer_ = (std::max)(airComboTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
        const float gravity = airComboTimer_ > 0.0f ? tuning.gravity * tuning.airComboGravityScale : tuning.gravity;
        if (ApplyGravityAndClampY(pos_.y, velY_, gravity, launchOriginY_, tuning.ceilingY, kCeilingBounceFactor)) {
            isLaunched_ = false;
            justLanded_ = true;
        }
    }
    // 非打ち上げ中はpos_.yに一切触れない。ステージエディタで配置・ドラッグした高さをそのまま信用する

    // 飛ぶ種類は出現位置の高さを中心に上下にゆらゆらさせる
    if (archetype_->Hovers() && !isLaunched_ && !defeated_) {
        pos_.y = spawnY_ + std::sin(archetypeTimer_ * tuning.flyingBobSpeed) * tuning.flyingBobAmplitude;
    }

    // 本体・落とした武器・手持ちの武器の位置を反映し、攻撃できる種類なら攻撃の状態を進める
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
    const VisualAnim desiredAnimationState = !attackState_->IsIdle() ? VisualAnim::kAttack
        : walkedThisFrame                                                      ? VisualAnim::kRun
                                                                               : VisualAnim::kIdle;
    if (!defeated_ && desiredAnimationState != animationState_) {
        object_->SetAnimation(desiredAnimationState == VisualAnim::kAttack ? attackAnimation_
                : desiredAnimationState == VisualAnim::kRun                ? runAnimation_
                                                                          : idleAnimation_);
        animationState_ = desiredAnimationState;
    }

    // 体の傾きと武器の角度: 撃破後は倒れた姿勢、のけぞり中は反らせ、それ以外は攻撃の状態に合わせる
    float bodyLean = defeated_ ? facingSign_ * kDefeatedBodyLean : 0.0f;
    float weaponSwing = defeated_ ? facingSign_ * kDefeatedWeaponTilt : kWeaponRestTilt;
    if (!defeated_ && !isLaunched_) {
        if (IsInHitstun()) {
            // 硬直が残っているほど大きく反らせ、明けるにつれて構えへ戻す
            const float leanRatio = (std::min)(hitstunTimer_ / kHitstunLeanFullSeconds, 1.0f);
            bodyLean = kHitstunBodyLean * leanRatio;
            weaponSwing = kHitstunWeaponSwing;
        } else {
            attackState_->ApplyPose(bodyLean, weaponSwing);
        }
    }
    if (homeRunSpinTimer_ > 0.0f) {
        // 打ち飛ばされている間は縦に回転しながら飛ぶ
        homeRunSpinTimer_ = (std::max)(homeRunSpinTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
        homeRunSpinAngle_ += kHomeRunSpinSpeed;
        bodyLean += homeRunSpinAngle_;
    }
    // 向きと傾きをモデルへ反映する（落とした武器は寝かせて回す）
    const float facingYaw = facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    object_->SetRotation({ 0.0f, facingYaw, bodyLean });
    if (weaponDropped_) {
        weaponObject_->SetRotation({ 0.0f, facingYaw + droppedWeaponSpin_, kDroppedWeaponLieTilt });
    } else {
        weaponObject_->SetRotation({ 0.0f, facingYaw, weaponSwing });
    }

    // 被弾フラッシュ: 基準色→白へ寄せ、本体を一瞬膨らませてから戻す
    float flash = 0.0f;
    if (hitFlashTimer_ > 0.0f) {
        hitFlashTimer_ = (std::max)(hitFlashTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
        flash = hitFlashTimer_ / kHitFlashDuration;
    }
    Vector4 flashColor = kDefeatedColor;
    if (!defeated_) {
        // 予備動作中は基準色を警告色へ寄せる（被弾フラッシュはその上から白へ寄せる）
        const float warn = IsTelegraphing() ? kTelegraphTintStrength : 0.0f;
        const Vector4 tintedColor = {
            baseColor_.x + (kTelegraphTint.x - baseColor_.x) * warn,
            baseColor_.y + (kTelegraphTint.y - baseColor_.y) * warn,
            baseColor_.z + (kTelegraphTint.z - baseColor_.z) * warn,
            baseColor_.w
        };
        flashColor = {
            tintedColor.x + (1.0f - tintedColor.x) * flash,
            tintedColor.y + (1.0f - tintedColor.y) * flash,
            tintedColor.z + (1.0f - tintedColor.z) * flash,
            tintedColor.w
        };
    }
    // 決めた色と被弾時の膨らみを反映して行列を更新する
    SetBodyColor(flashColor);
    weaponObject_->SetColor(flashColor);
    UpdateOfferedWeapon(true);
    const float bodyScale = kBodyScale * (1.0f + kHitScalePunch * flash);
    object_->SetScale({ bodyScale, bodyScale, bodyScale });

    object_->Update();
    weaponObject_->Update();
    SyncMonsterVisual(true);
}

/** @brief 攻撃ステートマシンを毎フレーム進める（Update() から呼ぶ） */
void EnemyEntity::UpdateAttack(float playerX) {
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

    attackTimer_ -= gameConstants::kFrameDeltaTime;
    if (attackTimer_ > 0.0f) {
        return;
    }

    attackState_->Advance(*this, EnemyTuning::GetInstance()->Basic(), playerX);
}

//  Attack State（攻撃の進行フェーズ）
/** @brief 攻撃間隔とプレイヤーとの距離が条件を満たすまで待機する状態 */
class EnemyEntity::IdleAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    /** @brief 待機中は体の傾きと武器の振りを変えない */
    void ApplyPose(float&, float&) const override { }
    /** @brief 待機状態かを返す */
    bool IsIdle() const override {
        return true;
    }
};
/** @brief 予備動作を表示して攻撃の発生を待つ状態 */
class EnemyEntity::TelegraphAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    /** @brief 予備動作の体の傾きと武器の振りかぶりを設定する */
    void ApplyPose(float& bodyLean, float& weaponSwing) const override {
        bodyLean = kTelegraphBodyLean;
        weaponSwing = kTelegraphWeaponSwing;
    }
    /** @brief 予備動作の状態かを返す */
    bool IsTelegraph() const override {
        return true;
    }
};
/** @brief 攻撃判定を発生させ、持続時間の終了後に待機へ戻る状態 */
class EnemyEntity::ActiveAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    /** @brief 攻撃中の体の傾きと武器の振りを設定する */
    void ApplyPose(float& bodyLean, float& weaponSwing) const override {
        bodyLean = kActiveBodyLean;
        weaponSwing = kActiveWeaponSwing;
    }
};
/** @brief プレイヤーが攻撃開始の間合いに入ったら予備動作へ移る */
void EnemyEntity::IdleAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const {
    // 遠隔の敵は持ち場基準の索敵距離、近接の敵は自分からの間合いで攻撃開始を判定する
    // （遠くの敵が延々と素振り・発砲を繰り返さないように。近接敵は届く距離でしか振らない）
    const bool inStartRange = enemy.IsMeleeAttacker()
        ? std::abs(playerX - enemy.pos_.x) <= tuning.meleeAttackRange
        : std::abs(playerX - enemy.spawnX_) <= tuning.aggroRange;
    if (!inStartRange) {
        enemy.attackTimer_ = 0.0f;
        return;
    }
    enemy.attackState_ = &AttackStateOf<TelegraphAttackState>();
    enemy.justStartedTelegraph_ = true;
    enemy.attackTimer_ = tuning.*AttackProfileOf(enemy.weaponType_).telegraph;
}
/** @brief 予備動作が終わったので攻撃を出す状態へ移る */
void EnemyEntity::TelegraphAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float) const {
    enemy.attackState_ = &AttackStateOf<ActiveAttackState>();
    enemy.attackTimer_ = tuning.attackActive;
    enemy.justFiredAttack_ = true;
}
/** @brief 攻撃が終わったので硬直時間を設定して待機へ戻る */
void EnemyEntity::ActiveAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float) const {
    enemy.attackState_ = &AttackStateOf<IdleAttackState>();
    enemy.attackTimer_ = tuning.*AttackProfileOf(enemy.weaponType_).recovery;
}
/** @brief 現在の攻撃状態の共有インスタンスを返す */
template <class T>
const EnemyEntity::IAttackState& EnemyEntity::AttackStateOf() {
    static const T instance;
    return instance;
}
/** @brief クールダウン状態へ戻す（被弾・打ち飛ばし・回復役など、攻撃を中断する時に使う） */
void EnemyEntity::ResetAttackState() {
    attackState_ = &AttackStateOf<IdleAttackState>();
}
/** @brief モデルを描画する */
void EnemyEntity::Draw() {
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
/** @brief 見た目を騎士からモンスターのモデルに差し替える（動き・攻撃・被弾の挙動は騎士と同じものを使う） */
void EnemyEntity::SetMonsterVisual(ModelCommon* modelCommon, const std::string& kind) {
    const EnemyVisualTuning* def = EnemyTuning::GetInstance()->FindVisual(kind);
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
    SetColor(IsHealer() ? kHealerColor : kDefaultArchetypeColor);
    SyncMonsterVisual(false);
}
/** @brief 騎士の本体に計算した位置・向き・大きさ・色を、モンスターの見た目へ写す */
void EnemyEntity::SyncMonsterVisual(bool advanceBob) {
    if (!monsterObject_) {
        return;
    }
    if (advanceBob) {
        monsterBobTimer_ += gameConstants::kFrameDeltaTime;
    }
    // 素材は静止モデルなので、生きている間はその場で小さく弾ませて動きを足す
    const float bob = defeated_ ? 0.0f : std::sin(monsterBobTimer_ * kMonsterBobSpeed) * kMonsterBobAmplitude;
    const Vector3 bodyPos = object_->GetPosition();
    // 騎士の本体スケールに対する比（被弾した瞬間の膨らみ）をモンスターにも掛ける
    const float punch = object_->GetScale().x / kBodyScale;
    const float scale = monsterScale_ * punch;
    monsterObject_->SetPosition({ bodyPos.x, bodyPos.y + bob, bodyPos.z });
    Vector3 rotation = object_->GetRotation();
    rotation.y += monsterYawOffset_;
    monsterObject_->SetRotation(rotation);
    monsterObject_->SetScale({ scale, scale, scale });
    monsterObject_->SetColor(bodyColor_);
    monsterObject_->Update();
}
/** @brief AIを止めている間（ポーズ・開始演出など）も、向きだけは指定したX座標の方へ合わせる */
void EnemyEntity::FaceTowardX(float targetX) {
    if (defeated_) {
        return;
    }
    facingSign_ = (targetX >= pos_.x) ? 1.0f : -1.0f;
    const float facingYaw = facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    // 体の傾きなど他の軸はそのまま、向き（Y軸回転）だけを差し替える
    Vector3 bodyRotation = object_->GetRotation();
    bodyRotation.y = facingYaw;
    object_->SetRotation(bodyRotation);
    if (weaponObject_ && !weaponDropped_) {
        Vector3 weaponRotation = weaponObject_->GetTransform().rotate;
        weaponRotation.y = facingYaw;
        weaponObject_->SetRotation(weaponRotation);
    }
}
/** @brief 倒れた体を指定した高さまで重力で落とす（宙で倒れた敵が浮いたまま止まらないように） */
void EnemyEntity::DropCorpseTo(float floorY) {
    corpseFallStarted_ = true;
    if (pos_.y <= floorY) {
        return; // もう床の上にいる
    }
    // 打ち上げと同じ重力の処理で落とし、着地目標だけ床の高さへ差し替える（落ちた武器もここへ落ちる）
    launchOriginY_ = floorY;
    if (!isLaunched_) {
        isLaunched_ = true;
        velY_ = 0.0f;
    }
}
/** @brief 上方向の初速を与えて打ち上げる */
void EnemyEntity::Launch(float velY) {
    if (!isLaunched_) {
        launchOriginY_ = pos_.y; // 最初の打ち上げ時点の高さを着地目標として記録する（空中コンボ中は上書きしない）
    }
    isLaunched_ = true;
    velY_ = velY;
    airComboTimer_ = EnemyTuning::GetInstance()->Basic().airComboHold;
}
/** @brief 近接コンボのヒットに対する吹き飛び・打ち上げを与える */
void EnemyEntity::ApplyComboReaction(float knockDirX, float knockY,
    bool switchPull, float playerX, bool finisher) {
    if (defeated_) {
        return;
    }
    if (switchPull) {
        const float toPlayer = playerX - pos_.x;
        knockVelX_ = std::clamp(toPlayer * kSwitchPullStrength, -kSwitchPullClamp, kSwitchPullClamp);
        airComboTimer_ = EnemyTuning::GetInstance()->Basic().airComboHold + kSwitchPullAirComboBonus;
    } else {
        // 加算すると段を重ねるほど遠ざかって次の段が空振りするため、毎ヒット上書きして途中の段は前進距離より短く押すだけにする
        knockVelX_ = knockDirX * (finisher ? kFinisherKnockDirXScale : kKnockDirXScale);
    }
    if (knockY > kLaunchThreshold) {
        Launch(knockY);
    }
}
/** @brief 被弾硬直を与える（硬直中は歩かず攻撃も進めず、のけぞった姿勢になる） */
void EnemyEntity::ApplyHitstun(float seconds, bool interruptAttack) {
    if (defeated_) {
        return;
    }
    hitstunTimer_ = (std::max)(hitstunTimer_, seconds);
    if (interruptAttack) {
        ResetAttackState();
        attackTimer_ = (std::max)(attackTimer_, kPostHitstunAttackDelay);
    }
}
/** @brief 大技の締めで斜め上へ大きく打ち飛ばし、回転させながら飛ばす（撃破済みでも飛ばす） */
void EnemyEntity::ApplyHomeRun(float dirX) {
    // 撃破済みでも飛ばす（とどめの一撃で遠くまで打ち飛ばすのが見せ場なので、ApplyComboReactionの撃破ガードは通さない）
    knockVelX_ = dirX * kHomeRunSpeedX;
    Launch(kHomeRunLaunchY);
    homeRunSpinTimer_ = kHomeRunSpinSeconds;
    homeRunSpinAngle_ = 0.0f;
    ResetAttackState();
    DropWeapon();
}
/** @brief 持っている武器をその場に落とす（以後は本体から離れて地面に転がる） */
void EnemyEntity::DropWeapon() {
    if (weaponDropped_) {
        return;
    }
    weaponDropped_ = true;
    droppedWeaponLanded_ = false;
    droppedWeaponPos_ = { pos_.x + facingSign_ * kWeaponOffsetX, pos_.y + kWeaponOffsetY, pos_.z + kWeaponOffsetZ };
    droppedWeaponVelY_ = kDroppedWeaponPopY;
    droppedWeaponSpin_ = 0.0f;
    // 打ち上げ中なら打ち上げ前の足元、地上ならいまの足元を地面とみなす
    droppedWeaponGroundY_ = (isLaunched_ ? launchOriginY_ : pos_.y) + kDroppedWeaponRestHeight;
}
/** @brief 落とした武器を指定位置へ寄せる（武器回収の吸い込み演出用） */
void EnemyEntity::PullDroppedWeaponToward(const Vector3& target, float rate) {
    if (!weaponDropped_) {
        return;
    }
    droppedWeaponLanded_ = true; // 吸い込み中は落下させない
    weaponPulled_ = true; // 浮かべるのをやめ、吸い寄せる位置をそのまま使う
    droppedWeaponPos_.x += (target.x - droppedWeaponPos_.x) * rate;
    droppedWeaponPos_.y += (target.y - droppedWeaponPos_.y) * rate;
    weaponObject_->SetPosition(droppedWeaponPos_);
    weaponObject_->Update();
}
} // namespace engine::game
