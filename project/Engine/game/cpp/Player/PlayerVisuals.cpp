/**
 * @file PlayerVisuals.cpp
 * @brief プレイヤーの見た目・アニメーション・武器装着・描画
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
constexpr Vector4 kGaugeChargedColor = { 1.0f, 0.85f, 0.0f, 1.0f };
constexpr float kAwakenPulseSpeed = 6.0f;
constexpr float kAwakenPulseAmplitude = 0.3f;
constexpr float kAwakenPulseBase = 0.7f;
constexpr float kAwakenRed = 0.15f;
constexpr float kAwakenGreen = 0.55f;

// フィニッシャー 静止集中の長さ（GamePlayScene/BattleTestScene の斬撃線バラマキ演出と一致させる
// kFinisherChargeDelay → 斬撃線を kFinisherSlashLines 本 kFinisherLineInterval 間隔で出す → kFinisherImpactDelay で解放）
// 専用の納刀ポーズ素材が無いため、Idle/IdleHold を静止させて代用する
// （PlayerCombatInput.cppのHandleFinisherSlashでも同じ値を使うため定義を重複させている）
constexpr float kFinisherChargeDuration = GameConstants::kFinisherChargeDelay
    + GameConstants::kFinisherSlashLines * GameConstants::kFinisherLineInterval
    + GameConstants::kFinisherImpactDelay;

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

    const IAnimState* newState = inWater_ ? &SwimAnim()
        : !onGround_                      ? &JumpAnim()
        : isMoving                        ? &RunAnim()
                                          : &IdleAnim();
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

void Player::UpdateVisualState(Input* input)
{
    float yaw = (lastDirX_ >= 0.0f) ? GameConstants::kHalfPi : -GameConstants::kHalfPi;

    // ── 覚醒残像スポーン＆フェード ──
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    // バックステップは後ろへ反りながら小さく跳ねる弧で見せる（当たり判定の位置は動かさない）
    float backDodgeLean = 0.0f;
    if (dodgeActive_ && dodgeBackward_) {
        const float arc = std::sin(std::clamp(dodgeTimer_ / kDodgeDuration_, 0.0f, 1.0f) * GameConstants::kPi);
        backDodgeLean = kBackDodgeLean_ * arc;
        modelPos.y += kBackDodgeHopHeight_ * arc;
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

    // ── プレイヤー色 ──
    if (finisherCharging_) {
        rig_->object->SetColor(kWhite); // 色自体は白のまま、リムの発光だけで魅せる
    } else if (IsRampaging()) {
        float t = std::sin(juggleSlashCount_ * kRampageBlinkSpeed) * 0.5f + 0.5f;
        rig_->object->SetColor({ 1.0f, 1.0f - t * kRampageGreenFade, 1.0f - t * kRampageBlueFade, 1.0f }); // 白→青白点滅
    } else if (justChargedGauge_) {
        rig_->object->SetColor(kGaugeChargedColor); // 黄（ハンマーチャージ）
    } else if (isAwakened_) {
        float t = std::sin(awakenTimer_ * kAwakenPulseSpeed) * kAwakenPulseAmplitude + kAwakenPulseBase;
        rig_->object->SetColor({ kAwakenRed * t, kAwakenGreen * t, 1.0f, 1.0f }); // 青くパルス
    } else {
        rig_->object->SetColor(kWhite);
    }

    // 溜めが深まるほどリムライトを強めて集中が高まる感じを出す（解放の瞬間が一番明るい）
    float rimIntensity = finisherCharging_
        ? kRimIntensity_ + (1.0f - finisherChargeTimer_ / kFinisherChargeDuration) * kFinisherRimBoost_
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
    rig_->object->Update();
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
    if (activeHeldIndex_ >= 0 && !(heldWeapons_[activeHeldIndex_].type == WeaponType::Greatsword && greatswordThrowActive_)) {
        auto& slot = heldWeapons_[activeHeldIndex_];
        Vector3 rot = slot.gripRotate + meleeCombo_.GetSwingOffset();
        AttachHeldWeapon(slot.object.get(), rig_->meleeBoneName, slot.gripScale, rot, slot.gripTranslate);
    }

    // 投げ回転斬りの最中は、途中で他の武器に持ち替えてもactiveHeldIndex_とは別に
    // 大剣モデルを飛行/渦の位置へ動かし続ける（持ち替え直後に消えて見えないようにする）
    thrownGreatswordIndex_ = -1;
    if (greatswordThrowActive_) {
        for (int i = 0; i < static_cast<int>(heldWeapons_.size()); ++i) {
            if (heldWeapons_[i].type == WeaponType::Greatsword) {
                thrownGreatswordIndex_ = i;
                UpdateThrownGreatswordVisual(heldWeapons_[i]);
                break;
            }
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

void Player::UpdateThrownGreatswordVisual(HeldWeaponSlot& slot)
{
    // 目にも留まらぬ速さで回転させ続ける（飛行中→静止後の渦で途切れず連続した見た目にする）
    constexpr float kThrowSpinSpeed = 22.0f; // ラジアン/秒
    constexpr float kSpinBobAmplitude = 0.06f; // 静止後、渦の間だけ小さく上下に揺らす
    constexpr float kSpinBobSpeed = 10.0f;

    Vector3 pos;
    if (greatswordThrowTimer_ < kGreatswordThrowTravelTime_) {
        // 飛行中: 手元から静止地点へ補間しながら飛んでいく
        const float t = Easing::EaseOutQuad(greatswordThrowTimer_ / kGreatswordThrowTravelTime_);
        pos = {
            greatswordThrowStartPos_.x + (greatswordThrowPos_.x - greatswordThrowStartPos_.x) * t,
            greatswordThrowStartPos_.y + (greatswordThrowPos_.y - greatswordThrowStartPos_.y) * t,
            greatswordThrowStartPos_.z + (greatswordThrowPos_.z - greatswordThrowStartPos_.z) * t,
        };
    } else {
        const float spinElapsed = greatswordThrowTimer_ - kGreatswordThrowTravelTime_;
        if (spinElapsed < kGreatswordVortexMaxDuration_) {
            // 渦の最中: その場に留まり、渦らしく小さく上下に揺れる
            pos = greatswordThrowPos_;
            pos.y += std::sin(spinElapsed * kSpinBobSpeed) * kSpinBobAmplitude;
        } else {
            // 帰還中: 渦の中心から手元へ飛んで戻る（瞬間移動に見えないよう補間する）
            const float returnElapsed = spinElapsed - kGreatswordVortexMaxDuration_;
            const float t = Easing::EaseInQuad((std::min)(returnElapsed / kGreatswordReturnTime_, 1.0f));
            pos = {
                greatswordThrowPos_.x + (greatswordReturnTargetPos_.x - greatswordThrowPos_.x) * t,
                greatswordThrowPos_.y + (greatswordReturnTargetPos_.y - greatswordThrowPos_.y) * t,
                greatswordThrowPos_.z + (greatswordReturnTargetPos_.z - greatswordThrowPos_.z) * t,
            };
        }
    }

    slot.object->ClearLocalMatrix(); // 手のボーン追従（SetLocalMatrix）を解除し、直接のTransform制御に戻す
    slot.object->SetPosition(pos);
    slot.object->SetRotation({ slot.gripRotate.x, slot.gripRotate.y, greatswordThrowTimer_ * kThrowSpinSpeed });
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

void Player::Draw()
{
    if (staticOverrideObject_) {
        staticOverrideObject_->SetPosition({ pos_.x, pos_.y - 0.5f + groundVisualCorrection_ + staticOverrideFootOffset_, pos_.z });
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
        if (thrownGreatswordIndex_ >= 0 && thrownGreatswordIndex_ != activeHeldIndex_) {
            heldWeapons_[thrownGreatswordIndex_].object->DrawOutline(outline);
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
    if (thrownGreatswordIndex_ >= 0 && thrownGreatswordIndex_ != activeHeldIndex_) {
        heldWeapons_[thrownGreatswordIndex_].object->Draw();
    }
    if (gunVisible_ && activeGunIndex_ >= 0) {
        guns_[activeGunIndex_].object->Draw();
    }
    rig_->object->Draw();
}
