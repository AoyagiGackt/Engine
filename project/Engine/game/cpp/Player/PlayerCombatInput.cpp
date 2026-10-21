/**
 * @file PlayerCombatInput.cpp
 * @brief Playerの戦闘入力処理（近接/銃コンボ・固有技・フィニッシャー・乱舞・覚醒・水中判定）を実装するファイル
 * @note Player の戦闘入力だけを Player.cpp から分けて置いている（クラスは Player のまま）
 */
#include "Player.h"
#include "CombatTuning.h"
#include "CharacterVisuals.h"
#include "GameSettings.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "ModelCommon.h"
#include "OutlineEffect.h"
#include "PipelineStateGuard.h"
#include "PlayerTuning.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
using namespace engine::graphics;

namespace engine::game {
namespace {
/** @brief 読み込み済みの調整値を取得する */
const PlayerTuning& Tuning()
{
    return PlayerTuning::Get();
}

/** @brief 武器スロットを直接選ぶゲームパッドのボタン（並び順がスロット番号） */
constexpr WORD kSlotButtons[] = { XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_DOWN };
}

// ══════════════════════════════════════════════════════
// 入力と戦闘処理
// ══════════════════════════════════════════════════════

/** @brief フレーム冒頭で単発イベントを消し、前フレームの接地・位置・水中状態を記録する */
void Player::ResetFrameFlags()
{
    frameEvents_.reset();
    prevOnGround_ = onGround_;
    prevFrameX_ = pos_.x;
    prevInWater_ = inWater_;
}

/** @brief 数字キー/十字キーによる武器スロット切り替えを処理する */
void Player::HandleStyleSwitch(Input* input)
{
    // 数字キーと十字キー（上・右・下）を武器スロットへ対応させる
    auto* wm = WeaponManager::GetInstance();
    const int oldSlot = wm->GetSelectedSlot();
    for (int i = 0; i < WeaponManager::kSlotCount; ++i)
    {
        if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i)))
        {
            wm->SelectSlot(i);
        }
    }
    for (int slot = 0; slot < static_cast<int>(std::size(kSlotButtons)); ++slot)
    {
        if (input->TriggerButton(kSlotButtons[slot]))
        {
            wm->SelectSlot(slot);
            break;
        }
    }
    if (wm->HasEquippedWeapon() && wm->GetSelectedSlot() != oldSlot)
    {
        meleeCombo_.Reset();
        ResetWeaponSkills(); // 技の途中で持ち替えても居残らないよう仕切り直す
        weaponSwitchAttackPending_ = true;
        weaponSwitchAttackActive_ = false;
        weaponSwitchWindow_ = Tuning().weaponSwitchWindow;
    }
}

/** @brief 銃切り替え（Gキー）と射撃コンボ（Kキー）の入力、および射撃反動の移動を処理する */
void Player::HandleRangedCombat(Input* input)
{
    auto* wm = WeaponManager::GetInstance();

    // ── 銃切り替え（G キー、循環）────────────────────────────────────
    if (input->TriggerAction(Input::Action::GunSwitch))
    {
        wm->SelectNextRanged();
        gunCombo_.Reset(); // 撃ちかけのコンボは持ち越さない
    }

    // ── 射撃コンボ（K キー、水上のみ）────────────────────────────────
    // 押下の瞬間ではなく段の shotTime で発砲する。段数・弾数・リコイルは銃種別の GunShotDef が持つ
    if (!inWater_ && !finisherCharging_ && input->TriggerAction(Input::Action::Shoot))
    {
        gunCombo_.TryShoot(wm->GetRanged().type);
    }
    gunCombo_.Update(GameConstants::kFrameDeltaTime);
    if (gunCombo_.JustShot())
    {
        SetFrameEvent(FrameEvent::Fired);
    }
    // 前後移動（踏み込み / 反動バックステップ）。空中では暴れるので地上のみ
    if (onGround_ && gunCombo_.GetMoveDelta() != 0.0f)
    {
        pos_.x = std::clamp(pos_.x + lastDirX_ * gunCombo_.GetMoveDelta(), minX_, maxX_);
    }
}

/** @brief 近接攻撃コンボ・打ち上げ・乱舞（Lキー）の入力受付とコンボ進行を処理する */
void Player::HandleMeleeCombat(Input* input, const Vector3& enemyPos)
{
    auto* wm = WeaponManager::GetInstance();
    weaponSwitchWindow_ = (std::max)(weaponSwitchWindow_ - GameConstants::kFrameDeltaTime, 0.0f);
    if (weaponSwitchWindow_ <= 0.0f)
    {
        weaponSwitchAttackPending_ = false;
    }
    // 投げ回転斬りで大剣が手元を離れている間は、素手同然として近接コンボを封じる
    // （手元にない武器でコンボ攻撃までできると、渦を出しっぱなしにしつつ通常戦闘もできてしまい強すぎるため）
    const bool disarmed = wm->HasEquippedWeapon() && BehaviorFor(wm->GetCurrent().type).IsDisarmed();
    if (!wm->HasEquippedWeapon() || disarmed)
    {
        meleeCombo_.Reset();
        return;
    }

    autoCombo_.SetEnabled(GameSettingsManager::GetInstance()->Get().autoCombo);
    autoCombo_.Update(GameConstants::kFrameDeltaTime);

    // ── 格闘コンボ / 打ち上げ / 乱舞（L キー、水上のみ）─────────────
    // S(↓)+L は打ち上げ技。覚醒ソードの L は従来どおり乱舞へ
    // 乱舞は押した瞬間の入力で段を進めるため、オートコンボの長押し連続入力は通常コンボにだけ使う
    const bool attackTriggered = input->TriggerAction(Input::Action::Attack);
    const bool startsRampage = BehaviorFor(wm->GetCurrent().type).StartsRampage(*this);
    const bool rampageAttack = IsRampaging() || startsRampage;
    const bool attackPressed = rampageAttack
        ? attackTriggered
        : autoCombo_.WantsAttack(attackTriggered, input->PushAction(Input::Action::Attack));
    if (!inWater_ && !finisherCharging_ && !meleeCombo_.IsSequenceActive() && attackPressed)
    {
        if (IsRampaging())
        {
            rampage_->HandleAttackInput(*this, input, enemyPos);
        }
        else if (startsRampage)
        {
            // 乱舞開始 まず敵に向かって突進（打ち上げフェーズ）
            meleeCombo_.Reset();
            rampage_ = &LaunchRampage();
            juggleSlashCount_ = 0;
            juggleAngleIdx_ = 0;
        }
        else
        {
            const bool launcherInput = input->PushAction(Input::Action::Down)
                || autoCombo_.WantsLauncher(wm->GetCurrent().type, meleeCombo_, !onGround_);
            const bool accepted = meleeCombo_.TryAttack(wm->GetCurrent().type, launcherInput, !onGround_);
            if (accepted && weaponSwitchAttackPending_)
            {
                weaponSwitchAttackPending_ = false;
                weaponSwitchAttackActive_ = true;
                pos_.x = std::clamp(pos_.x + lastDirX_ * Tuning().weaponSwitchLunge, minX_, maxX_);
            }
        }
    }

    // ── 近接コンボ進行（段の開始・ヒット発火・前進・打ち上げ追撃猶予）──
    // 覚醒中は1段の振りで多段ヒットさせる
    meleeCombo_.SetExtraHits(isAwakened_ ? Tuning().awakenExtraHits : 0, Tuning().awakenExtraHitInterval);
    meleeCombo_.Update(GameConstants::kFrameDeltaTime);
    if (const MeleeAttackDef* atk = meleeCombo_.GetActive())
    {
        if (meleeCombo_.JustStartedStep())
        {
            // 段ごとにモーションを頭から再生（速度も段の定義に従う）
            PlayAttackAnim(atk->slashAnim ? rig_->slashAnim : rig_->punchAnim, atk->animSpeed);
            autoCombo_.OnStepStarted(wm->GetCurrent().type, meleeCombo_, !onGround_);
        }
        if (meleeCombo_.JustHit())
        {
            SetFrameEvent(FrameEvent::ComboHit);
            SetFrameEvent(FrameEvent::ComboExtraHit, meleeCombo_.JustExtraHit());
            SetFrameEvent(FrameEvent::WeaponSwitchHit, weaponSwitchAttackActive_);
            weaponSwitchAttackActive_ = false;
            comboStep_ = meleeCombo_.GetStep();
            if (!HasFrameEvent(FrameEvent::ComboExtraHit))
            {
                autoCombo_.OnStepHit(wm->GetCurrent().type, meleeCombo_, !onGround_);
            }
            if (atk->launcher)
            {
                launchFollowTimer_ = Tuning().launchFollowWindow;
                autoCombo_.OnLauncherHit();
            }
            // ハンマーの大車輪は締めで地面を叩き、周囲へ衝撃波を出す（習得したボス技の強化もここに乗る）
            if (meleeCombo_.IsSequenceActive() && atk->finisher && BehaviorFor(wm->GetCurrent().type).SlamsOnSkillFinish())
            {
                SetFrameEvent(FrameEvent::GreatswordSlam);
            }
        }
        // 踏み込み（斬りながら前へ出ることで空振り感を減らす）
        if (meleeCombo_.GetLungeDelta() > 0.0f)
        {
            pos_.x = std::clamp(pos_.x + lastDirX_ * meleeCombo_.GetLungeDelta(), minX_, maxX_);
        }
        // 空中攻撃中は滞空（落下を弱めてエアコンボを繋ぎやすくする）
        if (!onGround_ && velocityY_ < 0.0f)
        {
            velocityY_ *= Tuning().airAttackFallDamping;
        }
    }
    if (launchFollowTimer_ > 0.0f)
    {
        launchFollowTimer_ -= GameConstants::kFrameDeltaTime;
    }
}

/** @brief フィニッシャースラッシュ（Fキー）の発動条件判定と、静止溜め→解放までの進行を処理する */
void Player::HandleFinisherSlash(Input* input)
{
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon())
    {
        return;
    }

    // ── フィニッシャースラッシュ（F キー、水上のみ、覚醒ゲージ満タン時のみ）─────
    // 静止して集中 → 溜め切ったら一閃、の2段構成にする
    if (!inWater_ && !isAwakened_ && !finisherCharging_
        && input->TriggerAction(Input::Action::Finisher) && awakenGauge_ >= 1.0f)
    {
        SetFrameEvent(FrameEvent::FinisherSlash);
        awakenGauge_ = 0.0f; // ゲージを全消費
        finisherCharging_ = true;
        finisherChargeTimer_ = CombatTune().FinisherChargeDuration();
        meleeCombo_.Reset(); // 進行中のコンボは打ち切って静止に入る
        {
            // 銃は常時左手に追従表示されるため、構え系近接武器と同様に扱う（UpdateAnimationStateと同じ判定）
            const Skeleton& skel = rig_->object->GetSkeleton();
            bool hasGunBone = skel.HasJoint(rig_->gunBoneName);
            bool hold = UsesHoldPose(wm->GetCurrent().type) || hasGunBone;
            rig_->object->SetAnimation(hold ? rig_->idleHoldAnim : rig_->idleAnim);
        }
        rig_->object->SetAnimSpeed(0.0f); // 呼吸すら止めるように完全静止
    }

    // ── フィニッシャー溜めの経過 → 解放（一閃）─────────────────────
    if (finisherCharging_)
    {
        finisherChargeTimer_ -= GameConstants::kFrameDeltaTime;
        if (finisherChargeTimer_ <= 0.0f)
        {
            finisherCharging_ = false;
            PlayAttackAnim(rig_->slashAnim, Tuning().finisherReleaseAnimSpeed);
        }
    }
}

/** @brief スペースキーによる武器固有技（ダッシュ斬り・間合い外し・大地砕き等）の入力とクールダウンを処理する */
void Player::HandleWeaponSkill(Input* input)
{
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon())
    {
        return;
    }

    // ── スペースキー（武器タイプ別）──────────────────────────────
    if (!inWater_ && !finisherCharging_)
    {
        // 固有技のクールダウン/バフや手元を離れた技は、武器切替中も凍結させず常に進める
        // 覚醒中は武器ごとの倍率（weapons.jsonのawakened.skillCooldownMult）ぶん早く回る
        const float cooldownStep = GameConstants::kFrameDeltaTime
            / (isAwakened_ ? wm->GetCurrent().awakened.skillCooldownMult : 1.0f);
        TickWeaponSkills(input, cooldownStep);

        IWeaponBehavior& behavior = BehaviorFor(wm->GetCurrent().type);
        behavior.Update(*this, input);

        if (!behavior.KeepsSpin(*this))
        {
            spinAngle_ = 0.0f;
        }
        isUpsideDown_ = (spinAngle_ > kUpsideDownMinDegrees && spinAngle_ < kUpsideDownMaxDegrees);
    }
}

// ══════════════════════════════════════════════════════
// 状態と物理更新
// ══════════════════════════════════════════════════════

/** @brief 乱舞フェーズの移動を進め、乱舞の斬撃に合わせて攻撃モーションを再生する */
void Player::UpdateRampagePhysics(const Vector3& enemyPos)
{
    // ── 乱舞フェーズ更新 ──────────────────────────────────────────
    rampage_->UpdatePhysics(*this, enemyPos);

    // 乱舞中の自動スラッシュにも斬撃モーションを合わせる（フィニッシャーは溜め→解放側で再生する）
    if (HasFrameEvent(FrameEvent::RampageHit) || HasFrameEvent(FrameEvent::RampageFinish))
    {
        PlayAttackAnim(rig_->slashAnim, Tuning().attackAnimSpeed);
    }
}

/** @brief 覚醒の発動判定（Rキー）とゲージ消費、時間切れ/ゲージ切れによる覚醒終了を処理する */
void Player::UpdateAwakenState(Input* input)
{
    // ── 覚醒発動（R キー）────────────────────────────────────────
    if (!finisherCharging_ && input->TriggerAction(Input::Action::Awaken)
        && awakenGauge_ >= Tuning().awakenActivationThreshold && !isAwakened_)
    {
        isAwakened_ = true;
        SetFrameEvent(FrameEvent::Awakened);
        awakenTimer_ = Tuning().awakenDuration;
    }

    // ── 覚醒ゲージ管理 ────────────────────────────────────────────
    // 覚醒中のみ消費する未覚醒時は自然減衰させず、溜めた分を維持する
    if (isAwakened_)
    {
        awakenTimer_ -= GameConstants::kFrameDeltaTime;
        awakenGauge_ = (std::max)(awakenGauge_ - GameConstants::kFrameDeltaTime / Tuning().awakenDuration, 0.0f);
        if (awakenTimer_ <= 0.0f || awakenGauge_ <= 0.0f)
        {
            isAwakened_ = false;
            awakenTimer_ = 0.0f;
            awakenGauge_ = 0.0f;
        }
    }

    // ── 覚醒フォーム切り替え（覚醒中はメカモデルへ丸ごと差し替え）──────
    // visualPreset_（-1=自動 0=通常 1=覚醒）の順。自動は覚醒しているかで決める
    CharacterRig* const presetRigs[] = { isAwakened_ ? &awakenedRig_ : &normalRig_, &normalRig_, &awakenedRig_ };
    CharacterRig* desiredRig = presetRigs[visualPreset_ + 1];
    if (rig_ != desiredRig)
    {
        rig_ = desiredRig;
        afterImageRenderer_.SetModel(rig_->staticModel, rig_->modelScale);
        // 旧リグで再生中の攻撃モーションは持ち越せないため打ち切り、
        // 直後の UpdateAnimationState に新リグの通常モーションを選び直させる
        // （animState_ は移動系と必ず不一致になる Attack を番兵にする）
        attackAnimTimer_ = 0.0f;
        rig_->object->SetAnimSpeed(1.0f);
        animState_ = &AttackAnim();
    }
}

/** @brief 乱舞中でない時、敵と一定距離以上重ならないようX座標を押し出す */
void Player::ResolveEnemyOverlap(const Vector3& enemyPos)
{
    // ── 敵とのめり込み防止（乱舞の突進/ジャグルは意図的に密着させる演出なので対象外）──
    // Y距離も見て、ジャンプで頭上を飛び越えている間は押し出さないようにする
    if (!IsRampaging())
    {
        float dx = pos_.x - enemyPos.x;
        float dy = pos_.y - enemyPos.y;
        if (std::abs(dx) < Tuning().minEnemyDistanceX && std::abs(dy) < Tuning().minEnemyDistanceY)
        {
            float dir = (dx >= 0.0f) ? 1.0f : -1.0f;
            pos_.x = std::clamp(enemyPos.x + dir * Tuning().minEnemyDistanceX, minX_, maxX_);
        }
    }
}

/** @brief 水面Yとの比較で入水/出水を判定し、入水時は落下速度を減衰させる */
void Player::UpdateWaterState()
{
    // 入水・出水判定（物理後の位置で確定）
    inWater_ = (pos_.y < waterLevel_);
    SetFrameEvent(FrameEvent::EnteredWater, !prevInWater_ && inWater_);
    SetFrameEvent(FrameEvent::ExitedWater, prevInWater_ && !inWater_);

    // 入水衝撃吸収（落下速度を大きく減衰）
    if (HasFrameEvent(FrameEvent::EnteredWater))
    {
        velocityY_ *= Tuning().waterEntryImpactDamping;
    }
}

} // namespace engine::game
