/**
 * @file PlayerCombatInput.cpp
 * @brief Playerの戦闘入力処理（近接/銃コンボ・固有技・フィニッシャー・乱舞・覚醒・水中判定）を実装するファイル
 * @note Player.cppからの分割ファイルクラス自体はPlayerのまま、定義の置き場所だけを分けている
 */
#include "Player.h"
#include "CharacterVisuals.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "ModelCommon.h"
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
// 斬撃モーションの再生速度倍率（コンボのテンポに合わせて少し速める）
constexpr float kAttackAnimSpeed = 1.5f;

// フィニッシャー 静止集中の長さ（GamePlayScene/BattleTestScene の斬撃線バラマキ演出と一致させる
// kFinisherChargeDelay → 斬撃線を kFinisherSlashLines 本 kFinisherLineInterval 間隔で出す → kFinisherImpactDelay で解放）
// 専用の納刀ポーズ素材が無いため、Idle/IdleHold を静止させて代用する
constexpr float kFinisherChargeDuration = GameConstants::kFinisherChargeDelay
    + GameConstants::kFinisherSlashLines * GameConstants::kFinisherLineInterval
    + GameConstants::kFinisherImpactDelay;
constexpr float kFinisherReleaseAnimSpeed = 3.0f; // 解放の一閃は目にも留まらぬ速さで
}

// ══════════════════════════════════════════════════════
// 入力と戦闘処理
// ══════════════════════════════════════════════════════

void Player::ResetFrameFlags()
{
    justJumped_ = false;
    justLanded_ = false;
    justEnteredWater_ = false;
    justExitedWater_ = false;
    justComboHit_ = false;
    justWeaponSwitchHit_ = false;
    justFired_ = false;
    justDaggerStingerHit_ = false;
    justChargedGauge_ = false;
    justAwakened_ = false;
    justSpinShot_ = false;
    justSwordDash_ = false;
    justSpearRetreat_ = false;
    justGreatswordSlam_ = false;
    justGreatswordSpinHit_ = false;
    justGreatswordThrown_ = false;
    justAxeCharge_ = false;
    justScytheSpin_ = false;
    justLaunched_ = false;
    justRampageHit_ = false;
    justRampageFinish_ = false;
    justFinisherSlash_ = false;
    prevOnGround_ = onGround_;
    prevInWater_ = inWater_;
}

void Player::HandleStyleSwitch(Input* input)
{
    // 数字キーと十字キーを4つの武器スロットへ対応させる
    auto* wm = WeaponManager::GetInstance();
    const int oldSlot = wm->GetSelectedSlot();
    for (int i = 0; i < 4; ++i) {
        if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) {
            wm->SelectSlot(i);
        }
    }
    if (input->TriggerButton(XINPUT_GAMEPAD_DPAD_UP)) {
        wm->SelectSlot(0);
    } else if (input->TriggerButton(XINPUT_GAMEPAD_DPAD_RIGHT)) {
        wm->SelectSlot(1);
    } else if (input->TriggerButton(XINPUT_GAMEPAD_DPAD_DOWN)) {
        wm->SelectSlot(2);
    } else if (input->TriggerButton(XINPUT_GAMEPAD_DPAD_LEFT)) {
        wm->SelectSlot(3);
    }
    if (wm->HasEquippedWeapon() && wm->GetSelectedSlot() != oldSlot) {
        meleeCombo_.Reset();
        daggerStingerHitIndex_ = -1; // 刺突の途中で持ち替えても居残らないよう仕切り直す
        daggerStingerDash_.active = false;
        swordDash_.active = false;
        spearDash_.active = false;
        axeDash_.active = false;
        greatswordThrowActive_ = false; // 渦の途中で持ち替えても居残らないよう仕切り直す
        greatswordReturnCaptured_ = false;
        weaponSwitchAttackPending_ = true;
        weaponSwitchAttackActive_ = false;
        weaponSwitchWindow_ = kWeaponSwitchWindow_;
    }
}

void Player::HandleRangedCombat(Input* input)
{
    auto* wm = WeaponManager::GetInstance();

    // ── 銃切り替え（G キー、循環）────────────────────────────────────
    if (input->TriggerAction(Input::Action::GunSwitch)) {
        wm->SelectNextRanged();
        gunCombo_.Reset(); // 撃ちかけのコンボは持ち越さない
    }

    // ── 射撃コンボ（K キー、水上のみ）────────────────────────────────
    // 押下の瞬間ではなく段の shotTime で発砲する。段数・弾数・リコイルは銃種別の GunShotDef が持つ
    if (!inWater_ && !finisherCharging_ && input->TriggerAction(Input::Action::Shoot)) {
        gunCombo_.TryShoot(wm->GetRanged().type);
    }
    gunCombo_.Update(GameConstants::kFrameDeltaTime);
    if (gunCombo_.JustShot()) {
        justFired_ = true;
    }
    // 前後移動（踏み込み / 反動バックステップ）。空中では暴れるので地上のみ
    if (onGround_ && gunCombo_.GetMoveDelta() != 0.0f) {
        pos_.x = std::clamp(pos_.x + lastDirX_ * gunCombo_.GetMoveDelta(), minX_, maxX_);
    }
}

void Player::HandleMeleeCombat(Input* input, const Vector3& enemyPos)
{
    auto* wm = WeaponManager::GetInstance();
    weaponSwitchWindow_ = (std::max)(weaponSwitchWindow_ - GameConstants::kFrameDeltaTime, 0.0f);
    if (weaponSwitchWindow_ <= 0.0f) {
        weaponSwitchAttackPending_ = false;
    }
    // 投げ回転斬りで大剣が手元を離れている間は、素手同然として近接コンボを封じる
    // （手元にない武器でコンボ攻撃までできると、渦を出しっぱなしにしつつ通常戦闘もできてしまい強すぎるため）
    const bool disarmed = wm->HasEquippedWeapon() && wm->GetCurrent().type == WeaponType::Greatsword && greatswordThrowActive_;
    if (!wm->HasEquippedWeapon() || disarmed) {
        meleeCombo_.Reset();
        return;
    }

    // ── 格闘コンボ / 打ち上げ / 乱舞（L キー、水上のみ）─────────────
    // S(↓)+L は打ち上げ技。覚醒ソードの L は従来どおり乱舞へ
    if (!inWater_ && !finisherCharging_ && input->TriggerAction(Input::Action::Attack)) {
        if (rampagePhase_ != RampagePhase::Inactive) {
            GetRampageState(rampagePhase_).HandleAttackInput(*this, input, enemyPos);
        } else if (wm->GetCurrent().type == WeaponType::Sword && isAwakened_) {
            // 乱舞開始 まず敵に向かって突進（打ち上げフェーズ）
            meleeCombo_.Reset();
            rampagePhase_ = RampagePhase::Launch;
            juggleSlashCount_ = 0;
            juggleAngleIdx_ = 0;
        } else {
            bool launcherInput = input->PushAction(Input::Action::Down);
            const bool accepted = meleeCombo_.TryAttack(wm->GetCurrent().type, launcherInput, !onGround_);
            if (accepted && weaponSwitchAttackPending_) {
                weaponSwitchAttackPending_ = false;
                weaponSwitchAttackActive_ = true;
                pos_.x = std::clamp(pos_.x + lastDirX_ * kWeaponSwitchLunge_, minX_, maxX_);
            }
        }
    }

    // ── 近接コンボ進行（段の開始・ヒット発火・前進・打ち上げ追撃猶予）──
    meleeCombo_.Update(GameConstants::kFrameDeltaTime);
    if (const MeleeAttackDef* atk = meleeCombo_.GetActive()) {
        if (meleeCombo_.JustStartedStep()) {
            // 段ごとにモーションを頭から再生（速度も段の定義に従う）
            PlayAttackAnim(atk->slashAnim ? rig_->slashAnim : rig_->punchAnim, atk->animSpeed);
        }
        if (meleeCombo_.JustHit()) {
            justComboHit_ = true;
            justWeaponSwitchHit_ = weaponSwitchAttackActive_;
            weaponSwitchAttackActive_ = false;
            comboStep_ = meleeCombo_.GetStep();
            if (atk->launcher) {
                launchFollowTimer_ = kLaunchFollowWindow_;
            }
        }
        // 踏み込み（斬りながら前へ出ることで空振り感を減らす）
        if (meleeCombo_.GetLungeDelta() > 0.0f) {
            pos_.x = std::clamp(pos_.x + lastDirX_ * meleeCombo_.GetLungeDelta(), minX_, maxX_);
        }
        // 空中攻撃中は滞空（落下を弱めてエアコンボを繋ぎやすくする）
        if (!onGround_ && velocityY_ < 0.0f) {
            velocityY_ *= kAirAttackFallDamping_;
        }
    }
    if (launchFollowTimer_ > 0.0f) {
        launchFollowTimer_ -= GameConstants::kFrameDeltaTime;
    }
}

void Player::HandleFinisherSlash(Input* input)
{
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon()) {
        return;
    }

    // ── フィニッシャースラッシュ（F キー、水上のみ、覚醒ゲージ満タン時のみ）─────
    // 静止して集中 → 溜め切ったら一閃、の2段構成にする
    if (!inWater_ && !isAwakened_ && !finisherCharging_
        && input->TriggerAction(Input::Action::Finisher) && awakenGauge_ >= 1.0f) {
        justFinisherSlash_ = true;
        awakenGauge_ = 0.0f; // ゲージを全消費
        finisherCharging_ = true;
        finisherChargeTimer_ = kFinisherChargeDuration;
        meleeCombo_.Reset(); // 進行中のコンボは打ち切って静止に入る
        {
            // 銃は常時左手に追従表示されるため、構え系近接武器と同様に扱う（UpdateAnimationStateと同じ判定）
            const Skeleton& skel = rig_->object->GetSkeleton();
            bool hasGunBone = skel.GetJointMap().find(rig_->gunBoneName) != skel.GetJointMap().end();
            bool hold = UsesHoldPose(wm->GetCurrent().type) || hasGunBone;
            rig_->object->SetAnimation(hold ? rig_->idleHoldAnim : rig_->idleAnim);
        }
        rig_->object->SetAnimSpeed(0.0f); // 呼吸すら止めるように完全静止
    }

    // ── フィニッシャー溜めの経過 → 解放（一閃）─────────────────────
    if (finisherCharging_) {
        finisherChargeTimer_ -= GameConstants::kFrameDeltaTime;
        if (finisherChargeTimer_ <= 0.0f) {
            finisherCharging_ = false;
            PlayAttackAnim(rig_->slashAnim, kFinisherReleaseAnimSpeed);
        }
    }
}

void Player::HandleWeaponSkill(Input* input)
{
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon()) {
        return;
    }

    // ── スペースキー（武器タイプ別）──────────────────────────────
    if (!inWater_ && !finisherCharging_) {
        WeaponType wtype = wm->GetCurrent().type;

        // 連射クールダウンは常に消化（Ball モード以外でも自然に減る）
        shootCooldown_ -= GameConstants::kFrameDeltaTime;
        if (shootCooldown_ < 0.0f) {
            shootCooldown_ = 0.0f;
        }

        // 固有技のクールダウン/バフも武器切替中に凍結させず常に消化する
        daggerStingerCooldown_ = (std::max)(daggerStingerCooldown_ - GameConstants::kFrameDeltaTime, 0.0f);
        swordSkillCooldown_ = (std::max)(swordSkillCooldown_ - GameConstants::kFrameDeltaTime, 0.0f);
        spearSkillCooldown_ = (std::max)(spearSkillCooldown_ - GameConstants::kFrameDeltaTime, 0.0f);
        greatswordSkillCooldown_ = (std::max)(greatswordSkillCooldown_ - GameConstants::kFrameDeltaTime, 0.0f);
        greatswordThrowCooldown_ = (std::max)(greatswordThrowCooldown_ - GameConstants::kFrameDeltaTime, 0.0f);
        axeSkillCooldown_ = (std::max)(axeSkillCooldown_ - GameConstants::kFrameDeltaTime, 0.0f);
        axeRageTimer_ = (std::max)(axeRageTimer_ - GameConstants::kFrameDeltaTime, 0.0f);

        GetWeaponBehavior(wtype).Update(*this, input);

        // Ball モード以外 / 着地時はスピン角をリセット
        if (wtype != WeaponType::Ball || onGround_) {
            spinAngle_ = 0.0f;
        }
        isUpsideDown_ = (spinAngle_ > 90.0f && spinAngle_ < 270.0f);
    }
}

// ══════════════════════════════════════════════════════
// 状態と物理更新
// ══════════════════════════════════════════════════════

void Player::UpdateRampagePhysics(const Vector3& enemyPos)
{
    // ── 乱舞フェーズ更新 ──────────────────────────────────────────
    GetRampageState(rampagePhase_).UpdatePhysics(*this, enemyPos);

    // 乱舞中の自動スラッシュにも斬撃モーションを合わせる（フィニッシャーは溜め→解放側で再生する）
    if (justRampageHit_ || justRampageFinish_) {
        PlayAttackAnim(rig_->slashAnim, kAttackAnimSpeed);
    }
}

void Player::UpdateAwakenState(Input* input)
{
    // ── 覚醒発動（R キー）────────────────────────────────────────
    if (!finisherCharging_ && input->TriggerAction(Input::Action::Awaken)
        && awakenGauge_ >= kAwakenActivationThreshold_ && !isAwakened_) {
        isAwakened_ = true;
        justAwakened_ = true;
        awakenTimer_ = kAwakenDuration_;
    }

    // ── 覚醒ゲージ管理 ────────────────────────────────────────────
    // 覚醒中のみ消費する未覚醒時は自然減衰させず、溜めた分を維持する
    if (isAwakened_) {
        awakenTimer_ -= GameConstants::kFrameDeltaTime;
        awakenGauge_ = (std::max)(awakenGauge_ - GameConstants::kFrameDeltaTime / kAwakenDuration_, 0.0f);
        if (awakenTimer_ <= 0.0f || awakenGauge_ <= 0.0f) {
            isAwakened_ = false;
            awakenTimer_ = 0.0f;
            awakenGauge_ = 0.0f;
        }
    }

    // ── 覚醒フォーム切り替え（覚醒中はメカモデルへ丸ごと差し替え）──────
    CharacterRig* desiredRig = visualPreset_ == 0 ? &normalRig_
        : visualPreset_ == 1                      ? &awakenedRig_
                                                  : (isAwakened_ ? &awakenedRig_ : &normalRig_);
    if (rig_ != desiredRig) {
        rig_ = desiredRig;
        afterImageRenderer_.SetModel(rig_->staticModel.get(), rig_->modelScale);
        // 旧リグで再生中の攻撃モーションは持ち越せないため打ち切り、
        // 直後の UpdateAnimationState に新リグの通常モーションを選び直させる
        // （animState_ は移動系と必ず不一致になる Attack を番兵にする）
        attackAnimTimer_ = 0.0f;
        rig_->object->SetAnimSpeed(1.0f);
        animState_ = AnimState::Attack;
    }
}

void Player::ResolveEnemyOverlap(const Vector3& enemyPos)
{
    // ── 敵とのめり込み防止（乱舞の突進/ジャグルは意図的に密着させる演出なので対象外）──
    // Y距離も見て、ジャンプで頭上を飛び越えている間は押し出さないようにする
    if (rampagePhase_ == RampagePhase::Inactive) {
        float dx = pos_.x - enemyPos.x;
        float dy = pos_.y - enemyPos.y;
        if (std::abs(dx) < kMinEnemyDistanceX_ && std::abs(dy) < kMinEnemyDistanceY_) {
            float dir = (dx >= 0.0f) ? 1.0f : -1.0f;
            pos_.x = std::clamp(enemyPos.x + dir * kMinEnemyDistanceX_, minX_, maxX_);
        }
    }
}

void Player::UpdateWaterState()
{
    // 入水・出水判定（物理後の位置で確定）
    inWater_ = (pos_.y < waterLevel_);
    justEnteredWater_ = !prevInWater_ && inWater_;
    justExitedWater_ = prevInWater_ && !inWater_;

    // 入水衝撃吸収（落下速度を大きく減衰）
    if (justEnteredWater_) {
        velocityY_ *= kWaterEntryImpactDamping_;
    }
}
