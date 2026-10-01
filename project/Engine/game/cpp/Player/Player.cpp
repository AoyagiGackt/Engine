/**
 * @file Player.cpp
 * @brief Playerのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "Player.h"
#include "CharacterVisuals.h"
#include "Easing.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "OutlineEffect.h"
#include "PipelineStateGuard.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
// 武器奪取の刺突（ぶっ刺す→奪う演出の仮モーション、専用素材が無いため斬撃を流用）
constexpr float kStealStabAnimSpeed = 1.2f;

void LockJumpClipVerticalRootMotion(Animation& animation)
{
    auto body = animation.nodeAnimations.find("Body");
    if (body == animation.nodeAnimations.end() || body->second.translate.keyframes.empty()) {
        return;
    }

    const float baseY = body->second.translate.keyframes.front().value.y;
    for (auto& keyframe : body->second.translate.keyframes) {
        keyframe.value.y = baseY;
    }
}
}

// ══════════════════════════════════════════════════════
// 初期化
// ══════════════════════════════════════════════════════

void Player::Initialize(ModelCommon* modelCommon)
{
    modelCommon_ = modelCommon;

    // スキンメッシュ共通設定（両フォームのリグで共有）
    skinCommon_ = std::make_unique<SkinCommon>();
    skinCommon_->Initialize(modelCommon->GetDxCommon());

    SkinnedObject3d::SetCommonModelCommon(modelCommon);
    SkinnedObject3d::SetCommonCamera(Object3d::GetCommonCamera());

    // 見た目リグの共通セットアップ（静的モデル + スキンモデル + アニメーション一式）
    auto initRig = [&](CharacterRig& rig, const RigVisualDef& def) {
        // 残像・分身演出用の静的モデル（ボーンなし、ボーン付きモデルと同じ見た目）
        rig.staticModel = ModelManager::GetInstance()->GetOrLoad(modelCommon, def.staticModelPath, def.texture);

        // 本体（ボーンアニメーション付き）
        std::string modelPath = std::string(def.dir) + "/" + def.file;
        rig.skinnedModel = ModelManager::GetInstance()->GetOrLoadSkinned(modelCommon->GetDxCommon(), modelPath, def.texture);

        rig.modelScale = def.scale;
        rig.modelOffsetY = def.offsetY;
        rig.meleeBoneName = def.meleeBone;
        rig.gunBoneName = def.gunBone;

        rig.idleAnim = LoadAnimationFile(def.dir, def.file, def.idle);
        rig.runAnim = LoadAnimationFile(def.dir, def.file, def.run);
        rig.jumpAnim = LoadAnimationFile(def.dir, def.file, def.jump);
        rig.runningJumpAnim = LoadAnimationFile(def.dir, def.file, def.runningJump);
        LockJumpClipVerticalRootMotion(rig.jumpAnim);
        LockJumpClipVerticalRootMotion(rig.runningJumpAnim);
        rig.swimAnim = LoadAnimationFile(def.dir, def.file, def.swim);
        rig.idleHoldAnim = LoadAnimationFile(def.dir, def.file, def.idleHold);
        rig.runHoldAnim = LoadAnimationFile(def.dir, def.file, def.runHold);
        rig.slashAnim = LoadAnimationFile(def.dir, def.file, def.slash);
        rig.punchAnim = LoadAnimationFile(def.dir, def.file, def.punch);

        rig.object = std::make_unique<SkinnedObject3d>();
        rig.object->Initialize(skinCommon_.get());
        rig.object->SetModel(rig.skinnedModel);
        rig.object->SetSkeleton(Skeleton::Create(LoadNodeHierarchyFromFile(def.dir, def.file)));
        rig.object->SetAnimation(rig.idleAnim);
        // ライティング有効 + リムライトで背景からシルエットを分離させる
        rig.object->SetEnableLighting(true);
        rig.object->SetRimColor(kRimColor_);
        rig.object->SetRimPower(kRimPower_);
        rig.object->SetRimIntensity(kRimIntensity_);
        rig.object->SetEnableRim(true);
        rig.object->SetScale({ def.scale, def.scale, def.scale });
        rig.object->SetPosition({ pos_.x, pos_.y + def.offsetY + groundVisualCorrection_, pos_.z });
        rig.object->Update();
    };
    initRig(normalRig_, kNormalRigVisual);
    initRig(awakenedRig_, kAwakenedRigVisual);
    rig_ = &normalRig_;
    animState_ = AnimState::Idle;

    afterImageRenderer_.Initialize(modelCommon, rig_->staticModel, rig_->modelScale);

    // 手持ち武器の共通セットアップ（読み込み + リムライト設定）
    auto initHeldWeapon = [&](Model*& model, std::unique_ptr<Object3d>& object,
                              const char* modelPath, const char* texturePath) {
        model = ModelManager::GetInstance()->GetOrLoad(modelCommon, modelPath, texturePath);
        object = std::make_unique<Object3d>();
        object->Initialize(modelCommon);
        object->SetModel(model);
        object->SetEnableLighting(true);
        object->SetRimColor(kRimColor_);
        object->SetRimPower(kRimPower_);
        object->SetRimIntensity(kRimIntensity_);
        object->SetEnableRim(true);
    };

    // 右手武器（トランスフォームは毎フレーム SetLocalMatrix で与える、現在のスタイルの1つだけ表示）
    heldWeapons_.clear();
    for (const auto& asset : kHeldWeaponVisuals) {
        HeldWeaponSlot slot;
        slot.type = asset.type;
        slot.gripScale = asset.gripScale;
        slot.gripRotate = asset.gripRotate;
        slot.gripTranslate = asset.gripTranslate;
        initHeldWeapon(slot.model, slot.object, asset.modelPath, asset.texturePath);
        heldWeapons_.push_back(std::move(slot));
    }

    // 銃（左手ボーン追従、Gキーで切り替えた1丁だけ表示）
    guns_.clear();
    for (const auto& asset : kGunVisuals) {
        GunSlot slot;
        slot.type = asset.type;
        slot.gripScale = asset.gripScale;
        slot.gripRotate = asset.gripRotate;
        slot.gripTranslate = asset.gripTranslate;
        initHeldWeapon(slot.model, slot.object, asset.modelPath, asset.texturePath);
        guns_.push_back(std::move(slot));
    }
}

void Player::PlayStealStab()
{
    PlayAttackAnim(rig_->slashAnim, kStealStabAnimSpeed);
}

int Player::GetComboMax() const
{
    if (!WeaponManager::GetInstance()->HasEquippedWeapon()) {
        return 0;
    }
    // 現在の武器の地上コンボ段数を基準にする（HUDのx段目/最大表示用）
    const MeleeComboSet& set = GetMeleeComboSet(WeaponManager::GetInstance()->GetCurrent().type);
    return set.ground.count + skillMods_.comboMaxBonus;
}

float Player::GetAwakenedDamageMult() const
{
    const auto* wm = WeaponManager::GetInstance();
    return (isAwakened_ && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.damageMult : 1.0f;
}

float Player::GetAwakenedSkillRadiusMult() const
{
    const auto* wm = WeaponManager::GetInstance();
    return (isAwakened_ && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.skillRadiusMult : 1.0f;
}

float Player::GetAwakenedKnockbackMult() const
{
    const auto* wm = WeaponManager::GetInstance();
    return (isAwakened_ && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.knockbackMult : 1.0f;
}

void Player::BufferDodgeInput(Input* input)
{
    if (input->TriggerAction(Input::Action::Dodge)) {
        dodgeBufferFrames_ = kDodgeBufferFrames_;
    }
}

void Player::HandleDodge(Input* input)
{
    justDodged_ = false;
    dodgeCooldown_ = (std::max)(dodgeCooldown_ - GameConstants::kFrameDeltaTime, 0.0f);
    dodgeSpamTimer_ += GameConstants::kFrameDeltaTime;

    // 先行入力の残りフレームを消費しつつ、このフレームの押下も同じバッファに載せる
    // （シーンがBufferDodgeInput()を呼んでいなくても直押しで回避できるように）
    BufferDodgeInput(input);
    const bool dodgeRequested = dodgeBufferFrames_ > 0;
    dodgeBufferFrames_ = (std::max)(dodgeBufferFrames_ - 1, 0);

    // 進行中の回避を進める（イージングで滑らかに移動し、終わったら無敵を解く）
    if (dodgeActive_) {
        dodgeTimer_ += GameConstants::kFrameDeltaTime;
        const float t = std::clamp(dodgeTimer_ / kDodgeDuration_, 0.0f, 1.0f);
        pos_.x = std::clamp(dodgeStartX_ + (dodgeTargetX_ - dodgeStartX_) * Easing::EaseOutQuad(t), minX_, maxX_);
        if (t >= 1.0f) {
            dodgeActive_ = false;
            dodgeCooldown_ = kDodgeCooldown_;
        }
        return;
    }

    const bool blocked = inWater_ || finisherCharging_ || rampagePhase_ != RampagePhase::Inactive
        || dodgeCooldown_ > 0.0f || warpActive_;
    if (blocked || !dodgeRequested) {
        return;
    }
    dodgeBufferFrames_ = 0; // 消費した先行入力で二重に回避しない

    // 方向は移動入力優先、無ければ向いている方向の逆へバックステップ（向き自体は変えない＝敵を見たまま退く）
    float dirX = -lastDirX_;
    if (input->PushAction(Input::Action::MoveLeft)) {
        dirX = -1.0f;
    } else if (input->PushAction(Input::Action::MoveRight)) {
        dirX = 1.0f;
    }

    // 短い間隔で繰り返した回避は連打として数える（危険の無い場面での乱用をスタイル評価で咎めるため）
    dodgeSpamCount_ = dodgeSpamTimer_ < kDodgeSpamWindow_ ? dodgeSpamCount_ + 1 : 0;
    dodgeSpamTimer_ = 0.0f;

    dodgeActive_ = true;
    justDodged_ = true;
    dodgeRewardClaimed_ = false;
    dodgeTimer_ = 0.0f;
    dodgeStartX_ = pos_.x;
    dodgeTargetX_ = std::clamp(pos_.x + dirX * kDodgeDistance_, minX_, maxX_);

    // 回避はコンボの逃げ道。振りかけの段は打ち切り、踏み込み系の技も止める
    meleeCombo_.Reset();
    daggerStingerHitIndex_ = -1;
    daggerStingerDash_.active = false;
    airDash_.active = false;
    swordDash_.active = false;
    spearDash_.active = false;
    axeDash_.active = false;
    PlayAttackAnim(rig_->runningJumpAnim, kDodgeAnimSpeed_);
}

void Player::HandleWarp(Input* input)
{
    warpCooldown_ = (std::max)(warpCooldown_ - GameConstants::kFrameDeltaTime, 0.0f);

    // 進行中のワープを進める（イージングで滑らかに移動し、終わったら無敵を解く）
    if (warpActive_) {
        warpTimer_ += GameConstants::kFrameDeltaTime;
        const float t = std::clamp(warpTimer_ / kWarpDuration_, 0.0f, 1.0f);
        pos_.x = std::clamp(warpStartX_ + (warpTargetX_ - warpStartX_) * Easing::EaseOutQuad(t), minX_, maxX_);
        if (t >= 1.0f) {
            warpActive_ = false;
            warpCooldown_ = isAwakened_ ? kWarpAwakenedCooldown_ : kWarpCooldown_;
        }
        return;
    }

    const bool blocked = inWater_ || finisherCharging_ || rampagePhase_ != RampagePhase::Inactive
        || warpCooldown_ > 0.0f || dodgeActive_;
    const bool hasGauge = isAwakened_ || awakenGauge_ >= kWarpGaugeCost_;
    if (blocked || !hasGauge || !input->TriggerAction(Input::Action::Warp)) {
        return;
    }

    if (!isAwakened_) {
        awakenGauge_ -= kWarpGaugeCost_;
    }

    warpActive_ = true;
    justWarped_ = true;
    warpTimer_ = 0.0f;
    warpStartX_ = pos_.x;
    const float distance = isAwakened_ ? kWarpAwakenedDistance_ : kWarpDistance_;
    warpTargetX_ = std::clamp(pos_.x + lastDirX_ * distance, minX_, maxX_);

    // ワープはコンボの逃げ道兼割り込み技。振りかけの段は打ち切り、踏み込み系の技も止める
    meleeCombo_.Reset();
    daggerStingerHitIndex_ = -1;
    daggerStingerDash_.active = false;
    airDash_.active = false;
    swordDash_.active = false;
    spearDash_.active = false;
    axeDash_.active = false;
    PlayAttackAnim(rig_->slashAnim, kWarpAnimSpeed_);
}

void Player::PlayAttackAnim(const Animation& anim, float speed)
{
    rig_->object->SetAnimation(anim);
    rig_->object->SetAnimSpeed(speed);
    animState_ = AnimState::Attack;
    attackAnimTimer_ = anim.duration / speed;
}

// ══════════════════════════════════════════════════════
// フレーム更新
// ══════════════════════════════════════════════════════

void Player::Update(Input* input, const Vector3& enemyPos, bool enemySolid, bool useStageFloor)
{
    useStageFloor_ = useStageFloor;
    ResetFrameFlags();

    if (invincibleTimer_ > 0.0f) {
        invincibleTimer_ -= GameConstants::kFrameDeltaTime;
    }

    justDodgeWindowTimer_ = (std::max)(justDodgeWindowTimer_ - GameConstants::kFrameDeltaTime, 0.0f);

    HandleStyleSwitch(input);
    HandleDodge(input);
    HandleWarp(input);

    GetPhysicsState(inWater_).Update(*this, input);
    pos_.x = std::clamp(pos_.x, minX_, maxX_);
    if (onGround_) {
        airDashAvailable_ = true; // 空中ダッシュは着地で回復する
    }

    HandleRangedCombat(input);
    HandleMeleeCombat(input, enemyPos);
    HandleFinisherSlash(input);
    HandleWeaponSkill(input);
    UpdateRampagePhysics(enemyPos);
    UpdateAwakenState(input);
    if (enemySolid) {
        ResolveEnemyOverlap(enemyPos);
    }
    UpdateWaterState();
    UpdateVisualState(input);
    AttachActiveWeapons();
}

void Player::ResolveBlockCollision(const std::vector<AABB>& blocks)
{
    groundVisualCorrection_ = 0.0f;
    if (blocks.empty()) {
        if (pos_.y > kGroundY_ && velocityY_ <= 0.0f) {
            onGround_ = false;
        }
        return;
    }

    // プレイヤーの当たり判定はダミー等と同じpos_を中心とした1x1x1規約に合わせる
    constexpr float kHalf = 0.5f;

    // 上昇中に頭がブロック下面を横切った場合は、下面の直下へ戻して上昇速度を止める。
    if (velocityY_ > 0.0f) {
        const float previousHeadY = pos_.y - velocityY_ + kHalf;
        const float currentHeadY = pos_.y + kHalf;
        float nearestCeiling = (std::numeric_limits<float>::max)();
        for (const auto& b : blocks) {
            const bool overlapXZ = (pos_.x + kHalf) > b.min.x && (pos_.x - kHalf) < b.max.x
                && (pos_.z + kHalf) > b.min.z && (pos_.z - kHalf) < b.max.z;
            if (!overlapXZ) {
                continue;
            }
            if (previousHeadY <= b.min.y && currentHeadY >= b.min.y) {
                nearestCeiling = (std::min)(nearestCeiling, b.min.y);
            }
        }
        if (nearestCeiling != (std::numeric_limits<float>::max)()) {
            pos_.y = nearestCeiling - kHalf;
            velocityY_ = 0.0f;
            onGround_ = false;
        }
    }

    // 1フレームの落下量は側面判定の余白より大きくなり得るため、今フレームで上面を
    // 上から跨いだブロックは着地扱いにする（側面押し出しで横へ弾くと着地できない）
    const float feetYBeforeResolve = pos_.y - kHalf;
    const float prevFeetY = pos_.y - velocityY_ - kHalf;
    // 上昇する床は1フレームで足元へ少し食い込むことがある。
    // その食い込みを側面衝突として横へ押し出さず、上面への着地として扱う。
    // 60fps時の移動床の最大移動量を少しだけ上回る値に留める。
    // 大きすぎる許容値は、床から離れた後も上面へ吸着して浮いて見える原因になる。
    constexpr float kMovingFloorTopTolerance = 0.04f;
    auto landedOnTop = [&](const AABB& b) {
        const bool crossedTopWhileFalling = prevFeetY >= b.max.y - kTopCrossTolerance_;
        const bool shallowMovingFloorPenetration = onGround_
            && feetYBeforeResolve >= b.max.y - kMovingFloorTopTolerance
            && feetYBeforeResolve <= b.max.y + kMovingFloorTopTolerance;
        return velocityY_ <= 0.0f && (crossedTopWhileFalling || shallowMovingFloorPenetration);
    };

    // 水平方向  側面から重なっているブロックがあれば侵入量が小さい側へ押し出す
    for (const auto& b : blocks) {
        if (landedOnTop(b)) {
            continue;
        }
        bool overlapY = (pos_.y + kHalf) > b.min.y + kSideOverlapMargin_ && (pos_.y - kHalf) < b.max.y - kSideOverlapMargin_;
        if (!overlapY) {
            continue;
        } // 乗っているだけの上面はここでは無視する

        bool overlapX = (pos_.x + kHalf) > b.min.x && (pos_.x - kHalf) < b.max.x;
        bool overlapZ = (pos_.z + kHalf) > b.min.z && (pos_.z - kHalf) < b.max.z;
        if (!overlapX || !overlapZ) {
            continue;
        }

        float pushLeft = b.min.x - (pos_.x + kHalf); // 負値＝左へ押し出す量
        float pushRight = b.max.x - (pos_.x - kHalf); // 正値＝右へ押し出す量
        float pushBack = b.min.z - (pos_.z + kHalf); // 負値＝手前へ押し出す量
        float pushFront = b.max.z - (pos_.z - kHalf); // 正値＝奥へ押し出す量

        float pushX = (std::abs(pushLeft) < std::abs(pushRight)) ? pushLeft : pushRight;
        float pushZ = (std::abs(pushBack) < std::abs(pushFront)) ? pushBack : pushFront;

        // 侵入量がより小さい軸だけを押し出す（角にめり込んだ場合に誤った軸へ押し出さないため）
        if (std::abs(pushX) < std::abs(pushZ)) {
            pos_.x += pushX;
        } else {
            pos_.z += pushZ;
        }
    }

    // 垂直方向  足元付近に上面があるブロックのうち一番高いものへ着地させる
    float feetY = pos_.y - kHalf;
    float bestTop = useStageFloor_ ? (std::numeric_limits<float>::lowest)() : kGroundY_;
    constexpr float kMaxStepHeight = 0.12f;
    // 壁のように同じX/Zに積み重なったブロックの途中の継ぎ目に、ジャンプで壁へ突っ込んだ際に
    // 乗ってしまわないよう、頭上（立った時に体が収まる高さ）に別のブロックが無いことも確認する
    auto hasClearanceAbove = [&](const AABB& candidate) {
        for (const auto& other : blocks) {
            if (&other == &candidate) {
                continue;
            }
            bool otherOverlapXZ = (pos_.x + kHalf) > other.min.x && (pos_.x - kHalf) < other.max.x
                && (pos_.z + kHalf) > other.min.z && (pos_.z - kHalf) < other.max.z;
            if (!otherOverlapXZ) {
                continue;
            }
            if (other.min.y < candidate.max.y + kHalf * 2.0f && other.max.y > candidate.max.y) {
                return false;
            }
        }
        return true;
    };
    // 支持判定は体の幅よりかなり狭くし、見た目上ブロックの外（空中）なのに乗れてしまうのを防ぐ
    constexpr float kSupportHalf = 0.15f;
    for (const auto& b : blocks) {
        bool overlapXZ = (pos_.x + kSupportHalf) > b.min.x && (pos_.x - kSupportHalf) < b.max.x
            && (pos_.z + kSupportHalf) > b.min.z && (pos_.z - kSupportHalf) < b.max.z;
        if (!overlapXZ) {
            continue;
        }

        // X/Z が重なっている床のうち、足元より上に出ていない一番高い面を採用する
        // 床をエディタで上下させたときも、古い高さに張り付かないようにする
        // 上から跨いで落ちてきたフレームは、めり込み量が段差許容を超えていても着地を成立させる
        const bool reachable = b.max.y <= feetY + kMaxStepHeight || landedOnTop(b);
        if (reachable && (b.max.y + kHalf) > bestTop && hasClearanceAbove(b)) {
            bestTop = b.max.y + kHalf;
        }
    }
    if (velocityY_ <= 0.0f && pos_.y <= bestTop + kLandingSnapTolerance_) {
        pos_.y = bestTop;
        velocityY_ = 0.0f;
        onGround_ = true;
        justLanded_ = !prevOnGround_;
        airDashAvailable_ = true;
    } else if (velocityY_ <= 0.0f) {
        onGround_ = false;
        justLanded_ = false;
    }

    pos_.x = std::clamp(pos_.x, minX_, maxX_);
}

// ══════════════════════════════════════════════════════
// 表示更新と描画
// ══════════════════════════════════════════════════════

void Player::BeginDash(DashMotion& dash, float worldDeltaX)
{
    dash.active = true;
    dash.timer = 0.0f;
    dash.startX = pos_.x;
    dash.targetX = std::clamp(pos_.x + worldDeltaX, minX_, maxX_);
}

bool Player::AdvanceDash(DashMotion& dash)
{
    if (!dash.active) {
        return false;
    }
    dash.timer += GameConstants::kFrameDeltaTime;
    const float t = std::clamp(dash.timer / kDashDuration_, 0.0f, 1.0f);
    pos_.x = dash.startX + (dash.targetX - dash.startX) * Easing::EaseOutQuad(t);
    if (t >= 1.0f) {
        dash.active = false;
        return true;
    }
    return false;
}
