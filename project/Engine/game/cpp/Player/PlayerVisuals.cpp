/**
 * @file PlayerVisuals.cpp
 * @brief プレイヤーの見た目・アニメーション・武器装着・描画
 */
#include "Player.h"
#include "CharacterVisuals.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "OutlineEffect.h"
#include "PipelineStateGuard.h"
#include "PlayerTuning.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
const PlayerTuning& Tuning()
{
    return PlayerTuning::Get();
}

// 本体・武器の黒縁アウトライン（背景との同化と武器の視認性の低さを補う）
// 幅・濃さともに控えめにし、異常が起きているように見えるほど主張しないようにする
constexpr Vector4 kOutlineColor = { 0.0f, 0.0f, 0.0f, 0.55f };
constexpr float kOutlineWidth = 0.006f;

// 差し替えモデルの高さが取れない時の既定スケール
constexpr float kMinModelHeight = 0.001f;
constexpr float kFallbackOverrideScale = 0.5f;

// 状態ごとの本体の色
constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr float kRampageBlinkSpeed = 3.0f; // 乱舞の斬撃回数あたりの明滅の速さ
constexpr float kRampageGreenFade = 0.4f;
constexpr float kRampageBlueFade = 0.6f;
constexpr float kAwakenPulseSpeed = 6.0f;
constexpr float kAwakenPulseAmplitude = 0.3f;
constexpr float kAwakenPulseBase = 0.7f;
constexpr float kAwakenRed = 0.15f;
constexpr float kAwakenGreen = 0.55f;
constexpr float kHalfWave = 0.5f; // sin を 0〜1 へ寄せる振幅とオフセット

/** @brief 状態ごとの本体の色（条件が成り立つ行のうち先頭を使う） */
struct BodyTint {
    bool active;
    Vector4 color;
};


}

// ══════════════════════════════════════════════════════
// 初期化
// ══════════════════════════════════════════════════════

void Player::SetStaticVisualModel(const std::string& modelPath, const std::string& texturePath)
{
    if (modelPath.empty() || !modelCommon_) {
        staticOverrideObject_.reset();
        staticOverrideModel_ = nullptr;
        staticOverrideFootOffset_ = 0.0f;
        staticOverrideModelPath_.clear();
        staticOverrideTexturePath_.clear();
        return;
    }
    staticOverrideModelPath_ = modelPath;
    staticOverrideTexturePath_ = texturePath.empty() ? "Resources/white.png" : texturePath;
    staticOverrideModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_, modelPath, staticOverrideTexturePath_);
    staticOverrideObject_ = std::make_unique<Object3d>();
    staticOverrideObject_->Initialize(modelCommon_);
    staticOverrideObject_->SetModel(staticOverrideModel_);
    staticOverrideObject_->SetEnableLighting(true);

    // 差し替えモデルは身長がまちまちなので、当たり判定(1x1x1 AABB)の高さに合わせて自動スケールする
    // 原点が中心にあるモデル（足元が0でない）でも、最下点をAABB下端に合わせられるよう足元オフセットも計算する
    float minY = (std::numeric_limits<float>::max)();
    float maxY = (std::numeric_limits<float>::lowest)();
    for (const auto& v : staticOverrideModel_->GetVertices()) {
        minY = (std::min)(minY, v.position.y);
        maxY = (std::max)(maxY, v.position.y);
    }
    const float height = maxY - minY;
    const float scale = (height > kMinModelHeight) ? (1.0f / height) : kFallbackOverrideScale;
    staticOverrideObject_->SetScale({ scale, scale, scale });
    staticOverrideFootOffset_ = -minY * scale;
}

void Player::UpdateAnimationState(bool isMoving)
{
    // フィニッシャー溜め中は静止ポーズを維持（タイマー管理は Update() 側）
    if (finisherCharging_) {
        return;
    }

    // 攻撃モーション中は再生し切るまで状態遷移しない
    if (attackAnimTimer_ > 0.0f) {
        attackAnimTimer_ -= GameConstants::kFrameDeltaTime;
        if (attackAnimTimer_ > 0.0f) {
            return;
        }
        rig_->object->SetAnimSpeed(1.0f); // 攻撃用の速度倍率を戻す
    }

    // 上の行ほど優先。どれにも当たらなければ待機
    const struct {
        bool active;
        const IAnimState* state;
    } candidates[] = {
        { inWater_, &SwimAnim() },
        { !onGround_, &JumpAnim() },
        { isMoving, &RunAnim() },
    };
    const IAnimState* newState = &IdleAnim();
    for (const auto& candidate : candidates) {
        if (candidate.active) {
            newState = candidate.state;
            break;
        }
    }
    // 構え系の近接武器、または銃を左手に構えている間は武器持ちバリエーション（IdleHold/RunHold）を使う
    // （銃は常時左手に追従表示されるため、素のIdle/Runのままだと構えていないように見えてしまう）
    const Skeleton& skel = rig_->object->GetSkeleton();
    bool hasGunBone = skel.GetJointMap().find(rig_->gunBoneName) != skel.GetJointMap().end();
    auto* weaponManager = WeaponManager::GetInstance();
    bool hold = (weaponManager->HasEquippedWeapon()
                    && UsesHoldPose(weaponManager->GetCurrent().type))
        || hasGunBone;
    if (newState == animState_ && hold == animHold_) {
        return;
    }
    animState_ = newState;
    animHold_ = hold;
    animState_->Enter(*this, hold, isMoving);
}

//  Anim State（移動系アニメーション状態）

namespace engine::game {
class Player::IdleAnimState : public IAnimState {
public:
    void Enter(Player& player, bool hold, bool) const override
    {
        player.rig_->object->SetAnimation(hold ? player.rig_->idleHoldAnim : player.rig_->idleAnim);
    }
};
class Player::RunAnimState : public IAnimState {
public:
    void Enter(Player& player, bool hold, bool) const override
    {
        player.rig_->object->SetAnimation(hold ? player.rig_->runHoldAnim : player.rig_->runAnim);
    }
};
class Player::JumpAnimState : public IAnimState {
public:
    void Enter(Player& player, bool, bool isMoving) const override
    {
        player.rig_->object->SetAnimation(isMoving ? player.rig_->runningJumpAnim : player.rig_->jumpAnim);
    }
};
class Player::SwimAnimState : public IAnimState {
public:
    void Enter(Player& player, bool, bool) const override
    {
        player.rig_->object->SetAnimation(player.rig_->swimAnim);
    }
};
class Player::AttackAnimState : public IAnimState {
public:
    void Enter(Player&, bool, bool) const override { }
};
}

const Player::IAnimState& Player::IdleAnim()
{
    static const IdleAnimState instance;
    return instance;
}

const Player::IAnimState& Player::RunAnim()
{
    static const RunAnimState instance;
    return instance;
}

const Player::IAnimState& Player::JumpAnim()
{
    static const JumpAnimState instance;
    return instance;
}

const Player::IAnimState& Player::SwimAnim()
{
    static const SwimAnimState instance;
    return instance;
}

const Player::IAnimState& Player::AttackAnim()
{
    static const AttackAnimState instance;
    return instance;
}

void Player::RefreshVisualTransforms()
{
    // アニメ状態は一切変えず、現在のpos_を使って見た目のトランスフォームだけ再計算する
    // （StageEditorのギズモ等、Update()を通さずpos_だけ直接書き換えられた場合に追従させるため）
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    rig_->object->SetPosition(modelPos);

    // SkinnedObject3d::Update()はアニメ時刻も進めてしまうため、一時的に速度0にして完全静止させる
    float savedSpeed = rig_->object->GetAnimSpeed();
    rig_->object->SetAnimSpeed(0.0f);
    SyncMotionLayer();
    rig_->object->Update();
    rig_->object->SetAnimSpeed(savedSpeed);

    // 武器/銃は本体のボーンに追従しているため、本体の位置更新後に付け直さないと古い位置に取り残される
    AttachActiveWeapons();

    for (auto& slot : heldWeapons_) {
        slot.object->Update();
    }
    for (auto& slot : guns_) {
        slot.object->Update();
    }
}

void Player::UpdateCinematicPose(bool airborne)
{
    // アニメの選び方は通常と同じ（宙なら落下、地上なら待機）。移動入力は無いものとして扱う
    onGround_ = !airborne;
    UpdateAnimationState(false);

    const float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;
    const Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    rig_->object->SetPosition(modelPos);
    rig_->object->SetRotation({ 0.0f, yaw, 0.0f });
    SyncMotionLayer();
    rig_->object->Update(); // アニメの時刻も通常どおり進める

    // 手に持つ武器と銃をボーンへ付け直す（付けないと初期位置のまま本体の中に見える）
    AttachActiveWeapons();
    for (auto& slot : heldWeapons_) {
        slot.object->Update();
    }
    for (auto& slot : guns_) {
        slot.object->Update();
    }
}

void Player::UpdateVisualState(Input* input)
{
    float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;

    // ── 覚醒残像スポーン＆フェード ──
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    // バックステップは後ろへ反りながら小さく跳ねる弧で見せる（当たり判定の位置は動かさない）
    float backDodgeLean = 0.0f;
    if (dodgeActive_ && dodgeBackward_) {
        const float arc = std::sin(std::clamp(dodgeTimer_ / Tuning().dodgeDuration, 0.0f, 1.0f) * GameConstants::kPi);
        backDodgeLean = Tuning().backDodgeLean * arc;
        modelPos.y += Tuning().backDodgeHopHeight * arc;
    }
    bool isRampage = IsRampaging();
    // 回避中も薄い残像を出して、瞬間的に位置が飛んだのではなく素早く動いたことを見せる
    // 固有技の連撃中も残像を引き、回転の軌跡を見せる
    afterImageRenderer_.Update(isRampage || dodgeActive_ || meleeCombo_.IsSequenceActive(), isRampage, modelPos,
        yaw + meleeCombo_.GetBodyTurnOffset(), spinAngle_);

    // ── アニメーション状態（接地中の左右移動入力で Idle/Run、空中で Jump）──
    bool isMovingHoriz = input->PushAction(Input::Action::MoveLeft)
        || input->PushAction(Input::Action::MoveRight);
    UpdateAnimationState(isMovingHoriz);

    // ── プレイヤー色（上の行ほど優先。どれにも当たらなければ白）──
    const float rampageBlink = std::sin(juggleSlashCount_ * kRampageBlinkSpeed) * kHalfWave + kHalfWave;
    const float awakenPulse = std::sin(awakenTimer_ * kAwakenPulseSpeed) * kAwakenPulseAmplitude + kAwakenPulseBase;
    const BodyTint tints[] = {
        { finisherCharging_, kWhite }, // 色自体は白のまま、リムの発光だけで魅せる
        { IsRampaging(), { 1.0f, 1.0f - rampageBlink * kRampageGreenFade, 1.0f - rampageBlink * kRampageBlueFade, 1.0f } }, // 白→青白点滅
        { isAwakened_, { kAwakenRed * awakenPulse, kAwakenGreen * awakenPulse, 1.0f, 1.0f } }, // 青くパルス
    };
    Vector4 bodyColor = kWhite;
    for (const BodyTint& tint : tints) {
        if (tint.active) {
            bodyColor = tint.color;
            break;
        }
    }
    rig_->object->SetColor(bodyColor);

    // 溜めが深まるほどリムライトを強めて集中が高まる感じを出す（解放の瞬間が一番明るい）
    float rimIntensity = finisherCharging_
        ? kRimIntensity_ + (1.0f - finisherChargeTimer_ / GameConstants::kFinisherChargeDuration) * kFinisherRimBoost_
        : kRimIntensity_;
    rig_->object->SetRimIntensity(rimIntensity);
    for (auto& slot : heldWeapons_) {
        slot.object->SetRimIntensity(rimIntensity);
    }
    for (auto& slot : guns_) {
        slot.object->SetRimIntensity(rimIntensity);
    }

    // 攻撃段ごとの体の傾き（前後=X/左右ロール=Z）。Ball旋回のspinAngle_とはZ軸を共有するが
    // 両者は排他（旋回はBall選択中のみ、攻撃レンはコンボ中のみ）なので単純加算でよい
    Vector3 bodyLean = meleeCombo_.GetBodyLeanOffset();
    rig_->object->SetPosition(modelPos);
    // 回転斬りの段では体ごとその場で回る（向きに足すので、武器もボーン追従でいっしょに回る）
    rig_->object->SetRotation({ bodyLean.x + backDodgeLean, yaw + meleeCombo_.GetBodyTurnOffset(), spinAngle_ * GameConstants::kDegToRad + bodyLean.z });
    SyncMotionLayer();
    rig_->object->Update();
}

void Player::SyncMotionLayer()
{
    // コンボの段タイマーを唯一の時計にして、加算キーを段の経過割合で評価する
    const MeleeAttackDef* attack = meleeCombo_.GetActive();
    const ComboMotionLibrary* library = ComboMotionLibrary::GetInstance();
    const MotionClip* clip = (attack != nullptr && library->IsEnabled())
        ? library->Find(rig_->motionRigId, attack->id)
        : nullptr;
    motionLayer_.SetClip(clip, meleeCombo_.GetProgress());
}

void Player::PreviewMeleePose(const MeleeAttackDef* attack, float normalizedTime)
{
    if (attack == nullptr) {
        EndMeleePreview();
        return;
    }
    meleeCombo_.SetPreviewPose(attack, normalizedTime);

    // ベースアニメもゲーム中と同じ対応（段の経過秒 × 再生速度）で同じ時刻に合わせる
    const Animation& anim = attack->slashAnim ? rig_->slashAnim : rig_->punchAnim;
    if (previewAnim_ != &anim) {
        rig_->object->SetAnimation(anim);
        previewAnim_ = &anim;
    }
    float animTime = std::clamp(normalizedTime, 0.0f, 1.0f) * attack->duration * attack->animSpeed;
    if (anim.duration > 0.0f) {
        animTime = std::fmod(animTime, anim.duration);
    }
    rig_->object->SetAnimTime(animTime);

    // 体の傾き・その場回転も同じ段時刻で評価する
    const float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;
    const Vector3 bodyLean = meleeCombo_.GetBodyLeanOffset();
    rig_->object->SetRotation({ bodyLean.x, yaw + meleeCombo_.GetBodyTurnOffset(), bodyLean.z });
    RefreshVisualTransforms();
}

void Player::EndMeleePreview()
{
    meleeCombo_.Reset();
    if (previewAnim_ != nullptr) {
        // 攻撃の残り時間と速度を戻し、今の接地・構えの条件から移動系アニメへ入り直させる
        // （状態を攻撃扱いにしておくと、UpdateAnimationState()が同じ状態とみなして入り直しを省かない）
        attackAnimTimer_ = 0.0f;
        rig_->object->SetAnimSpeed(1.0f);
        animState_ = &AttackAnim();
        UpdateAnimationState(false);
        previewAnim_ = nullptr;
    }
    const float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;
    rig_->object->SetRotation({ 0.0f, yaw, 0.0f });
    RefreshVisualTransforms();
}

void Player::AttachActiveWeapons()
{
    auto* wm = WeaponManager::GetInstance();

    // ── 現在のスタイルに対応する武器を右手ボーンに追従 ──────────────
    // 攻撃中は段ごとのスイング回転（振りかぶり→振り抜き）をグリップ回転へ加算し、
    // 共通の腕モーションでも武器ごとに違う軌道に見せる
    activeHeldIndex_ = -1;
    if (wm->HasEquippedWeapon()) {
        WeaponType meleeType = wm->GetCurrent().type;
        for (int i = 0; i < static_cast<int>(heldWeapons_.size()); ++i) {
            if (heldWeapons_[i].type == meleeType) {
                activeHeldIndex_ = i;
                break;
            }
        }
    }
    if (activeHeldIndex_ >= 0 && !BehaviorFor(heldWeapons_[activeHeldIndex_].type).IsDisarmed()) {
        auto& slot = heldWeapons_[activeHeldIndex_];
        Vector3 rot = slot.gripRotate + meleeCombo_.GetSwingOffset();
        AttachHeldWeapon(slot.object.get(), rig_->meleeBoneName, slot.gripScale, rot, slot.gripTranslate);
    }

    // 投げた武器は、途中で他の武器に持ち替えてもactiveHeldIndex_とは別に
    // 飛行/渦の位置へ動かし続ける（持ち替え直後に消えて見えないようにする）
    thrownWeaponIndex_ = -1;
    for (int i = 0; i < static_cast<int>(heldWeapons_.size()); ++i) {
        Vector3 thrownPos;
        float thrownSpinZ = 0.0f;
        if (BehaviorFor(heldWeapons_[i].type).GetThrownPose(thrownPos, thrownSpinZ)) {
            thrownWeaponIndex_ = i;
            PlaceThrownWeapon(heldWeapons_[i], thrownPos, thrownSpinZ);
            break;
        }
    }

    // ── 選択中の銃を左手ボーンに追従（Gキーで切り替えた1丁だけ表示）──────
    // 射撃コンボ中は段ごとの構え→リコイルの回転オフセットをグリップ回転へ加算し、
    // 同じ左手ボーンでも銃ごとに違う撃ち方に見せる
    {
        GunType gunType = wm->GetRanged().type;
        activeGunIndex_ = -1;
        for (int i = 0; i < static_cast<int>(guns_.size()); ++i) {
            if (guns_[i].type == gunType) {
                activeGunIndex_ = i;
                break;
            }
        }
        const Skeleton& skel = rig_->object->GetSkeleton();
        gunVisible_ = (skel.GetJointMap().find(rig_->gunBoneName) != skel.GetJointMap().end());
        if (gunVisible_ && activeGunIndex_ >= 0) {
            auto& slot = guns_[activeGunIndex_];
            Vector3 rot = slot.gripRotate + gunCombo_.GetPoseOffset();
            AttachHeldWeapon(slot.object.get(), rig_->gunBoneName, slot.gripScale, rot, slot.gripTranslate);
        }
    }
}

void Player::PlaceThrownWeapon(HeldWeaponSlot& slot, const Vector3& position, float spinZ)
{
    slot.object->ClearLocalMatrix(); // 手のボーン追従（SetLocalMatrix）を解除し、直接のTransform制御に戻す
    slot.object->SetPosition(position);
    slot.object->SetRotation({ slot.gripRotate.x, slot.gripRotate.y, spinZ });
    slot.object->SetScale(slot.gripScale);
    slot.object->Update();
}

void Player::AttachHeldWeapon(Object3d* obj, const char* boneName,
    const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate)
{
    AttachToBone(obj, rig_->object->GetSkeleton(), rig_->object->GetWorldMatrix(),
        boneName, gripScale, gripRotate, gripTranslate);
}

Vector3 Player::GetActiveWeaponWorldPosition() const
{
    if (activeHeldIndex_ >= 0) {
        return heldWeapons_[activeHeldIndex_].object->GetWorldPosition();
    }
    return { pos_.x, pos_.y + rig_->modelOffsetY, pos_.z };
}

bool Player::GetHeldWeaponTransform(WeaponType& outType, Matrix4x4& outMatrix) const
{
    if (activeHeldIndex_ < 0 || activeHeldIndex_ == thrownWeaponIndex_) {
        return false;
    }
    const HeldWeaponSlot& slot = heldWeapons_[activeHeldIndex_];
    // 手のボーン追従中はSetLocalMatrix()の行列がそのままワールド行列になっている
    if (!slot.object->IsUsingLocalMatrix()) {
        return false;
    }
    outType = slot.type;
    outMatrix = slot.object->GetLocalMatrix();
    return true;
}

void Player::Draw()
{
    if (staticOverrideObject_) {
        staticOverrideObject_->SetPosition({ pos_.x, pos_.y - kBodyHalfExtent_ + groundVisualCorrection_ + staticOverrideFootOffset_, pos_.z });
        staticOverrideObject_->SetRotation({ 0.0f, lastDirX_ >= 0.0f ? GameConstants::kHalfPi : -GameConstants::kHalfPi, 0.0f });
        staticOverrideObject_->Update();
        staticOverrideObject_->Draw();
        return;
    }
    // 残像（プレイヤーより先に描画して後ろに見えるようにする）
    afterImageRenderer_.Draw();

    // 黒縁アウトライン（本体・武器とも先に描いてから通常描画で上書きする）
    auto* outline = OutlineEffect::GetInstance();
    outline->SetColor(kOutlineColor);
    outline->SetWidth(kOutlineWidth);
    {
        // スコープを抜けた瞬間に必ず通常描画用のPSOへ復帰する（早期returnや将来のコード追加があっても復帰忘れが起きない）
        PipelineStateGuard restoreGuard([this] {
            if (modelCommon_) {
                modelCommon_->CommonDrawSettings();
                // ルートシグネチャの切り替えでライト/シャドウマップの束縛が失われているため再バインドする
                Object3d::RebindCommonLighting(modelCommon_->GetDxCommon()->GetCommandList());
            }
        });
        outline->BeginOutlinePass();
        if (activeHeldIndex_ >= 0) {
            heldWeapons_[activeHeldIndex_].object->DrawOutline(outline);
        }
        if (thrownWeaponIndex_ >= 0 && thrownWeaponIndex_ != activeHeldIndex_) {
            heldWeapons_[thrownWeaponIndex_].object->DrawOutline(outline);
        }
        if (gunVisible_ && activeGunIndex_ >= 0) {
            guns_[activeGunIndex_].object->DrawOutline(outline);
        }
        rig_->object->DrawOutline(outline);
    }

    // 通常描画
    if (activeHeldIndex_ >= 0) {
        heldWeapons_[activeHeldIndex_].object->Draw();
    }
    if (thrownWeaponIndex_ >= 0 && thrownWeaponIndex_ != activeHeldIndex_) {
        heldWeapons_[thrownWeaponIndex_].object->Draw();
    }
    if (gunVisible_ && activeGunIndex_ >= 0) {
        guns_[activeGunIndex_].object->Draw();
    }
    rig_->object->Draw();
}
