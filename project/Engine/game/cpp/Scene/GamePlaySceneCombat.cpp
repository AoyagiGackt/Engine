/**
 * @file GamePlaySceneCombat.cpp
 * @brief GamePlaySceneの戦闘関連更新処理（近接/銃コンボ・武器付き敵・ギミック・被弾・スタイル演出）を実装するファイル
 * @note GamePlayScene.cppからの分割ファイルクラス自体はGamePlaySceneのまま、定義の置き場所だけを分けている
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "EnemyTuning.h"
#include "GameConstants.h"
#include "GameFlags.h"
#include "GameRules.h"
#include "GamePlaySceneInitializer.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ImageFilter.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <random>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// スタイルゲージ・覚醒ゲージ・ヒット判定範囲の調整値はResources/Config/combat_tuning.json（CombatTuning）で持つ
static const CombatTuningData& Tune() { return CombatTuning::GetInstance()->Get(); }

namespace {
constexpr float kStageHalfDepth = 0.5f; // 2.5Dステージの奥行き半分
constexpr float kDefaultKnockY = 0.05f; // 段定義がない時の打ち上げ量
constexpr int kDefaultHitStopFrames = 3;
}

void GamePlayScene::UpdateTargetLock()
{
    // Shiftを押している間だけロックオンし、その間は常に一番近い敵を対象にし続ける
    // （BattleTestSceneのダミーロックオンと同じ規約。押した瞬間の対象に固定しない）
    if (!input_->PushKey(DIK_LSHIFT)) {
        lockedKind_ = LockTargetKind::None;
        return;
    }

    // 画面外の敵まで拾うとロック対象の方へカメラが大きく寄ってしまうため、
    // 画面内に映る範囲（カメラ半幅）より遠い敵はロック対象から除外する
    constexpr float kMaxLockRange = GameConstants::kCameraHalfW;

    const Vector3& pp = player_->GetPosition();
    float minDist = FLT_MAX;
    LockTargetKind nearestKind = LockTargetKind::None;
    size_t nearestWeaponIndex = 0;

    if (!enemy_->IsDefeated()) {
        const float dist = std::abs(enemy_->GetPosition().x - pp.x);
        if (dist <= kMaxLockRange && dist < minDist) {
            minDist = dist;
            nearestKind = LockTargetKind::MainEnemy;
        }
    }
    for (size_t i = 0; i < weaponEnemies_.size(); ++i) {
        if (weaponEnemies_[i].enemy->IsDefeated()) {
            continue;
        }
        const float dist = std::abs(weaponEnemies_[i].enemy->GetPosition().x - pp.x);
        if (dist <= kMaxLockRange && dist < minDist) {
            minDist = dist;
            nearestKind = LockTargetKind::WeaponEnemy;
            nearestWeaponIndex = i;
        }
    }

    lockedKind_ = nearestKind;
    lockedWeaponEnemyIndex_ = nearestWeaponIndex;
}

void GamePlayScene::UpdateCamera()
{
    // カメラをプレイヤーに追従（境界ブロックが画面外に出ないよう clamp）
    const Vector3& ppos = player_->GetPosition();
    const std::vector<AABB> stageSolids = GetStageEditor().GetSolidColliders();
    // レベルに当たり判定ブロックがない時の既定のステージ範囲
    constexpr float kDefaultStageLeft = 1.5f;
    constexpr float kDefaultStageRight = 36.5f;
    float stageLeft = kDefaultStageLeft;
    float stageRight = kDefaultStageRight;
    if (!stageSolids.empty()) {
        stageLeft = stageSolids.front().min.x;
        stageRight = stageSolids.front().max.x;
        for (const AABB& solid : stageSolids) {
            stageLeft = (std::min)(stageLeft, solid.min.x);
            stageRight = (std::max)(stageRight, solid.max.x);
        }
    }
    const float cameraMinX = stageLeft + GameConstants::kCameraHalfW;
    const float cameraMaxX = stageRight - GameConstants::kCameraHalfW;
    float cameraX = cameraMinX <= cameraMaxX
        ? std::clamp(ppos.x, cameraMinX, cameraMaxX)
        : (stageLeft + stageRight) * 0.5f;

    // ロック中はカメラをほんの少しだけ対象側へ寄せて、狙っていることに気付きやすくする
    // （BattleTestScene/SceneShared::UpdateCameraFollowと同じ控えめな比率）
    constexpr float kLockOnCameraShiftRatio = 0.15f;
    const Vector3* lockTargetPos = nullptr;
    Vector3 lockTargetPosValue { };
    if (lockedKind_ == LockTargetKind::MainEnemy) {
        lockTargetPosValue = enemy_->GetPosition();
        lockTargetPos = &lockTargetPosValue;
    } else if (lockedKind_ == LockTargetKind::WeaponEnemy && lockedWeaponEnemyIndex_ < weaponEnemies_.size()) {
        lockTargetPosValue = weaponEnemies_[lockedWeaponEnemyIndex_].enemy->GetPosition();
        lockTargetPos = &lockTargetPosValue;
    }
    if (lockTargetPos != nullptr) {
        cameraX += (lockTargetPos->x - ppos.x) * kLockOnCameraShiftRatio;
        if (cameraMinX <= cameraMaxX) {
            cameraX = std::clamp(cameraX, cameraMinX, cameraMaxX);
        }
    }

    cameraTargetPos_ = {
        cameraX,
        ppos.y + GameConstants::kCameraFollowOffsetY,
        GameConstants::kCameraDistanceZ
    };

    UpdateCameraSmoothing();

    // カメラシェイク（スムージングの後に直接カメラ座標へ加算）
    Vector3 shake = cameraShaker_.Update(GameConstants::kFrameDeltaTime);
    if (shake.x != 0.0f || shake.y != 0.0f) {
        Vector3 cam = camera_->GetTranslate();
        camera_->SetTranslate({ cam.x + shake.x, cam.y + shake.y, cam.z });
    }

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
}

AABB GamePlayScene::GetEnemyHitBox() const
{
    const Vector3& epos = enemy_->GetPosition();
    return { { epos.x - Tune().enemyHitBoxHalfExtent, epos.y - Tune().enemyHitBoxHalfExtent, -kStageHalfDepth },
        { epos.x + Tune().enemyHitBoxHalfExtent, epos.y + Tune().enemyHitBoxHalfExtent, kStageHalfDepth } };
}

void GamePlayScene::ApplyMeleeComboStyleHit(const AABB& enemyAABB)
{
    const auto* wm = WeaponManager::GetInstance();
    if (!(wm->HasEquippedWeapon() && player_->JustComboHit())) {
        return;
    }
    const Vector3& ppos = player_->GetPosition();
    // 前方に厚く、背後は振り抜きぶんだけ（左右対称だと背後の遠い敵にまで当たってしまう）
    AABB meleeRange = SceneShared::MakeDirectionalRange(
        ppos, player_->GetLastDirX(), wm->GetCurrent().range,
        wm->GetCurrent().range * GameConstants::kSkillRearReachMult);
    if (!player_->IsOnGround()) {
        // 空中では上下移動が同時に起きるため、見た目の武器軌跡に合わせて縦判定と先端を少し広げる。
        constexpr float kAirRangeHalfHeight = 1.35f;
        constexpr float kAirRangeWidthPadding = 0.4f;
        meleeRange.min.y = ppos.y - kAirRangeHalfHeight;
        meleeRange.max.y = ppos.y + kAirRangeHalfHeight;
        meleeRange.min.x -= kAirRangeWidthPadding;
        meleeRange.max.x += kAirRangeWidthPadding;
    }
    if (!Collision::CheckCollision(meleeRange, enemyAABB)) {
        return;
    }
    const int techniqueId = static_cast<int>(wm->GetCurrent().type) * 16
        + player_->GetComboStep();
    if (techniqueId == lastTechniqueId_) {
        repeatedTechniqueCount_++;
    } else {
        lastTechniqueId_ = techniqueId;
        repeatedTechniqueCount_ = 0;
    }
    const float repeatPenalty = (std::min)(repeatedTechniqueCount_ * Tune().meleeRepeatPenaltyPerHit, Tune().meleeRepeatPenaltyCap);
    const float switchBonus = player_->JustWeaponSwitchHit() ? Tune().meleeWeaponSwitchBonus : 0.0f;
    // 足場に乗って空中から仕掛けたコンボは地上より評価を上げる（高い位置のブロックに戦闘上の意味を持たせる）
    const float airborneBonus = player_->IsOnGround() ? 0.0f : Tune().airborneHitStyleBonus;
    styleMeter_ = std::clamp(styleMeter_ + Tune().meleeBaseStyleGain
            + player_->GetComboStep() * Tune().meleeComboStepStyleGain + switchBonus + airborneBonus - repeatPenalty,
        0.0f, 1.0f);
    player_->ChargeAwakenGauge(Tune().meleeAwakenGaugeGain);
    const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
    const WeaponData& weapon = wm->GetCurrent();
    const float damageMult = attack != nullptr ? attack->damageMult : 1.0f;
    const int damage = (std::max)(1,
        static_cast<int>(std::round(weapon.damage * damageMult / Tune().meleeDamageDivisor * CurrentDamageMult())));
    constexpr float kGameplayKnockbackScale = 0.72f;
    const float knockbackMult = weapon.knockbackMult * player_->GetAwakenedKnockbackMult() * kGameplayKnockbackScale;
    enemy_->TakeDamage(damage);
    enemy_->ApplyComboReaction(player_->GetLastDirX() * knockbackMult,
        (attack != nullptr ? attack->knockY : kDefaultKnockY) * knockbackMult,
        player_->JustWeaponSwitchHit(), ppos.x);
    styleRankHud_.RegisterHit(attack != nullptr ? attack->id : "melee", 1.0f);
    const int chainHits = styleRankHud_.GetHitCount();

    // 段が進むほど敵側の弾け方も大きくする（コンボの伸びを敵の見た目でも返す）
    constexpr float kComboStepStrengthGain = 0.2f;
    constexpr float kWeaponSwitchHitStrength = 1.8f;
    const float hitStrength = player_->JustWeaponSwitchHit()
        ? kWeaponSwitchHitStrength
        : 1.0f + static_cast<float>((std::max)(player_->GetComboStep() - 1, 0)) * kComboStepStrengthGain;
    EmitEnemyHitEffect(enemy_->GetPosition(),
        { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] }, hitStrength,
        weapon.effectBurstCount, weapon.effectRingRadius);
    EmitElementalHitEffect(weapon, enemy_->GetPosition(), player_->GetComboStep());

    // 段ごとのhitStop（MeleeCombo.cppで武器・段別に調整済み）をヒットストップとカメラ揺れへ反映する
    constexpr float kHitStopShakeScale = 0.028f;
    constexpr float kHitStopShakeDurationBase = 0.06f;
    constexpr float kHitStopShakeDurationScale = 0.01f;
    const int hitStopFrames = attack != nullptr
        ? (attack->launcher ? GameConstants::kHitStopLaunch : attack->hitStop)
        : kDefaultHitStopFrames;
    TimeManager::GetInstance()->RequestHitStop(hitStopFrames);
    cameraShaker_.Request(kHitStopShakeScale * static_cast<float>(hitStopFrames),
        kHitStopShakeDurationBase + kHitStopShakeDurationScale * static_cast<float>(hitStopFrames));

    // 段の締めと5ヒット刻みを明確な「山」にして、連続攻撃の途中と成功時の手応えを分ける。
    constexpr int kComboFinisherStep = 3;
    constexpr int kChainMilestoneInterval = 5;
    constexpr float kFinisherFlashAlpha = 0.16f;
    constexpr float kMilestoneFlashAlpha = 0.10f;
    constexpr float kFinisherFlashSeconds = 0.09f;
    constexpr float kMilestoneFlashSeconds = 0.06f;
    constexpr float kFinisherShakeAmount = 0.24f;
    constexpr float kMilestoneShakeAmount = 0.16f;
    constexpr float kImpactShakeSeconds = 0.12f;
    constexpr float kFinisherRingSpeed = 5.0f;
    constexpr float kMilestoneRingSpeed = 3.8f;
    constexpr float kImpactRingAlpha = 0.7f;
    constexpr int kFinisherRingCount = 18;
    constexpr int kMilestoneRingCount = 12;
    constexpr float kImpactRingLifetime = 0.2f;
    constexpr float kImpactRingSize = 0.09f;
    constexpr float kFinisherSlashAngleRight = -0.45f;
    constexpr float kFinisherSlashAngleLeft = 3.59f;
    constexpr float kFinisherSlashAlpha = 0.9f;
    constexpr float kFinisherSlashRadius = 2.2f;
    constexpr float kDaggerSlowSeconds = 0.9f;
    const bool comboFinisher = player_->GetComboStep() >= kComboFinisherStep;
    const bool chainMilestone = chainHits > 0 && (chainHits % kChainMilestoneInterval) == 0;
    if (comboFinisher || chainMilestone) {
        const Vector4 impactColor = { weapon.effectColor[0], weapon.effectColor[1],
            weapon.effectColor[2], comboFinisher ? kFinisherFlashAlpha : kMilestoneFlashAlpha };
        ScreenFlash::GetInstance()->Request(impactColor, comboFinisher ? kFinisherFlashSeconds : kMilestoneFlashSeconds);
        cameraShaker_.Request(comboFinisher ? kFinisherShakeAmount : kMilestoneShakeAmount, kImpactShakeSeconds);
        pm_->EmitRing("hit_ring", enemy_->GetPosition(), comboFinisher ? kFinisherRingSpeed : kMilestoneRingSpeed,
            { impactColor.x, impactColor.y, impactColor.z, kImpactRingAlpha },
            comboFinisher ? kFinisherRingCount : kMilestoneRingCount, kImpactRingLifetime, kImpactRingSize);
        if (comboFinisher) {
            const float slashAngle = player_->GetLastDirX() >= 0.0f ? kFinisherSlashAngleRight : kFinisherSlashAngleLeft;
            pm_->EmitSlash("sword_slash", enemy_->GetPosition(), slashAngle,
                { impactColor.x, impactColor.y, impactColor.z, kFinisherSlashAlpha }, kFinisherSlashRadius);
        }
    }

    const WeaponType element = wm->GetCurrent().type;
    if (element == WeaponType::Sword && player_->GetComboStep() >= kComboFinisherStep) {
        enemy_->TakeDamage(1); // 炎: コンボ後半で追加ダメージ
    } else if (element == WeaponType::Dagger) {
        enemy_->ApplySlow(kDaggerSlowSeconds); // 氷: 行動速度を落とす
    } else if (element == WeaponType::Spear) {
        // 雷: 周囲の武器敵へ連鎖する。
        for (auto& entry : weaponEnemies_) {
            if (!entry.enemy->IsDefeated()
                && std::abs(entry.enemy->GetPosition().x - enemy_->GetPosition().x) < Tune().chainSkillRange) {
                entry.enemy->TakeDamage(1);
            }
        }
    }
}

void GamePlayScene::ApplyWeaponSkillStyleHit(const AABB& enemyAABB)
{
    if (!(player_->JustSwordDash() || player_->JustSpearRetreat()
            || player_->JustDaggerStingerHit() || player_->JustGreatswordSlam()
            || player_->JustSpinShot() || player_->JustScytheSpin()
            || player_->JustAxeCharge())) {
        return;
    }
    const auto* wm = WeaponManager::GetInstance();
    const Vector3& ppos = player_->GetPosition();
    const Vector3 skillCenter = player_->JustSwordDash() ? player_->GetSwordSkillImpactPosition() : ppos;
    const bool slam = player_->JustGreatswordSlam();
    const float radius = SkillRadiusFor(slam ? Tune().skillSlamRadius : Tune().skillDefaultRadius, slam);
    if (slam && HasBossSlamTechnique()) {
        // 習得したボス技: 叩きつけの衝撃波を広げて見せる（判定半径の拡大と対応）
        constexpr int kShockwaveRingCount = 22;
        constexpr float kShockwaveRingLifetime = 0.35f;
        constexpr float kShockwaveRingSize = 0.3f;
        constexpr Vector4 kShockwaveRingColor = { 1.0f, 0.7f, 0.25f, 0.9f };
        pm_->EmitRing("hit_ring", ppos, radius, kShockwaveRingColor,
            kShockwaveRingCount, kShockwaveRingLifetime, kShockwaveRingSize);
    }
    const AABB skillRange = {
        { skillCenter.x - radius, skillCenter.y - Tune().skillRangeHalfHeight, -kStageHalfDepth },
        { skillCenter.x + radius, skillCenter.y + Tune().skillRangeHalfHeight, kStageHalfDepth }
    };
    if (!Collision::CheckCollision(skillRange, enemyAABB)) {
        return;
    }
    constexpr int kSkillTechniqueIdBase = 1000; // 近接段のIDと重ならないよう固有技に割り当てる番号の起点
    const int techniqueId = kSkillTechniqueIdBase + static_cast<int>(wm->GetCurrent().type);
    const float varietyBonus = techniqueId == lastTechniqueId_ ? Tune().skillVarietyBonusRepeat : Tune().skillVarietyBonusFresh;
    lastTechniqueId_ = techniqueId;
    styleMeter_ = std::clamp(styleMeter_ + varietyBonus, 0.0f, 1.0f);
    player_->ChargeAwakenGauge(Tune().skillAwakenGaugeGain);
    constexpr int kSlamBaseDamage = 3;
    constexpr int kSkillBaseDamage = 2;
    const int slamBonus = (slam && HasBossSlamTechnique()) ? GameRules::GetInstance()->Get().bossTechniqueBonusDamage : 0;
    const float rawDamage = static_cast<float>((slam ? kSlamBaseDamage : kSkillBaseDamage) + slamBonus);
    enemy_->TakeDamage((std::max)(1, static_cast<int>(std::round(rawDamage * CurrentDamageMult()))));
    styleRankHud_.RegisterHit("skill_" + std::to_string(static_cast<int>(wm->GetCurrent().type)), 1.0f);

    // 固有技は通常段より大きく弾けさせる（叩きつけはさらに大きく）
    constexpr float kSkillHitStrength = 1.6f;
    constexpr float kSlamHitStrength = 2.2f;
    const WeaponData& weapon = wm->GetCurrent();
    EmitEnemyHitEffect(enemy_->GetPosition(),
        { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] },
        slam ? kSlamHitStrength : kSkillHitStrength,
        weapon.effectBurstCount, weapon.effectRingRadius);

    // 固有技は通常コンボより一段大きいヒットストップ・カメラ揺れにする（叩きつけはさらに大きく）
    constexpr int kSkillHitStopFrames = 7;
    constexpr int kSlamHitStopFrames = 12;
    constexpr float kSkillShakeAmount = 0.16f;
    constexpr float kSlamShakeAmount = 0.26f;
    constexpr float kSkillShakeSeconds = 0.14f;
    constexpr float kSlamShakeSeconds = 0.22f;
    const int hitStopFrames = slam ? kSlamHitStopFrames : kSkillHitStopFrames;
    TimeManager::GetInstance()->RequestHitStop(hitStopFrames);
    cameraShaker_.Request(slam ? kSlamShakeAmount : kSkillShakeAmount, slam ? kSlamShakeSeconds : kSkillShakeSeconds);
}

float GamePlayScene::CurrentDamageMult() const
{
    const float dodgeBonus = player_->IsJustDodgeWindowActive() ? Tune().justDodgeDamageMult : 1.0f;
    return player_->GetAwakenedDamageMult() * dodgeBonus;
}

float GamePlayScene::SkillRadiusFor(float baseRadius, bool slam) const
{
    float radius = baseRadius * player_->GetAwakenedSkillRadiusMult();
    if (slam && HasBossSlamTechnique()) {
        radius *= GameRules::GetInstance()->Get().bossTechniqueRadiusMult;
    }
    return radius;
}

bool GamePlayScene::HasBossSlamTechnique() const
{
    return RunData::GetInstance()->HasBossTechnique(GameRules::GetInstance()->Get().bossTechnique);
}

void GamePlayScene::ApplyGunShotStyleHit(const AABB& enemyAABB)
{
    if (!player_->JustFired()) {
        return;
    }
    const auto* wm = WeaponManager::GetInstance();
    const Vector3& ppos = player_->GetPosition();
    const GunShotDef* shot = player_->GetActiveGunShot();
    const RangedWeaponData& gun = wm->GetRanged();
    const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    // 銃口の向きにだけ飛ぶ（背後は銃身ぶんの余裕のみ）
    AABB shotRange = SceneShared::MakeDirectionalShotRange(
        ppos, player_->GetLastDirX(), rangeX, Tune().gunBackRange);
    if (!Collision::CheckCollision(shotRange, enemyAABB)) {
        return;
    }
    // 段が進むほどスタイルが伸びる（銃コンボを回す動機付け）
    float gain = Tune().gunBaseStyleGain + ((shot != nullptr) ? player_->GetGunComboStep() * Tune().gunComboStepStyleGain : 0.0f);
    styleMeter_ = std::clamp(styleMeter_ + gain, 0.0f, 1.0f);
    player_->ChargeAwakenGauge(Tune().gunAwakenGaugeGain);
    constexpr int kGunBaseDamage = 1;
    enemy_->TakeDamage((std::max)(1, static_cast<int>(std::round(kGunBaseDamage * CurrentDamageMult()))));
    styleRankHud_.RegisterHit(shot != nullptr ? shot->id : "shot", 1.0f);

    // 銃は連射するので小さめの弾けで着弾を示す
    constexpr Vector4 kGunHitColor = { 1.0f, 0.85f, 0.4f, 1.0f };
    constexpr float kGunHitStrength = 0.7f;
    EmitEnemyHitEffect(enemy_->GetPosition(), kGunHitColor, kGunHitStrength);
}

void GamePlayScene::ApplyDaggerStingerStyleBonus()
{
    if (!player_->JustDaggerStingerHit()) {
        return;
    }
    styleMeter_ = std::clamp(styleMeter_ + Tune().stingerStyleGain, 0.0f, 1.0f);
    styleRankHud_.RegisterHit("dagger_stinger", 1.0f);
}

void GamePlayScene::ApplyRampageStyleHit(const AABB& enemyAABB)
{
    if (!player_->JustRampageHit()) {
        return;
    }
    const Vector3& ppos = player_->GetPosition();
    AABB rushRange = { { ppos.x - Tune().rampageRushRadiusX, ppos.y - Tune().rampageRushRadiusY, -kStageHalfDepth },
        { ppos.x + Tune().rampageRushRadiusX, ppos.y + Tune().rampageRushRadiusY, kStageHalfDepth } };
    if (!Collision::CheckCollision(rushRange, enemyAABB)) {
        return;
    }
    // 乱舞スラッシュ 回数が増えるほど多くゲージが溜まる
    styleMeter_ = std::clamp(
        styleMeter_ + Tune().rampageBaseStyleGain + player_->GetJuggleCount() * Tune().rampageJuggleStyleGain, 0.0f, 1.0f);
    enemy_->TakeDamage(1);
    styleRankHud_.RegisterHit("rampage", 1.0f);

    // 乱舞は手数が多いので控えめな弾けで1発ごとの命中を示す
    constexpr Vector4 kRampageHitColor = { 0.8f, 0.35f, 1.0f, 1.0f };
    constexpr float kRampageHitStrength = 0.8f;
    EmitEnemyHitEffect(enemy_->GetPosition(), kRampageHitColor, kRampageHitStrength);
}

void GamePlayScene::DecayStyleMeter(float dt)
{
    const float decayMult = RunData::GetInstance()->HasSkill(RunData::Skill::StylePersist) ? Tune().stylePersistDecayMult : 1.0f;
    styleMeter_ = std::clamp(styleMeter_ - Tune().styleDecayRate * dt * decayMult, 0.0f, 1.0f);
    peakStyle_ = (std::max)(peakStyle_, styleMeter_);
}

void GamePlayScene::UpdateDodgeStyle()
{
    // 回避そのものは加点しない。危険の無い場面での連打だけスタイルを削る（引きこもり回避の抑止）
    constexpr int kSpamPenaltyStartCount = 2;
    if (!player_->JustDodged()) {
        return;
    }
    const int spam = player_->GetDodgeSpamCount();
    if (spam >= kSpamPenaltyStartCount) {
        styleMeter_ = std::clamp(styleMeter_ - Tune().dodgeSpamPenalty * static_cast<float>(spam - kSpamPenaltyStartCount + 1), 0.0f, 1.0f);
    }
}

bool GamePlayScene::TryJustDodge(const Vector3& hitPos)
{
    constexpr float kJustDodgeRingRadius = 2.6f;
    constexpr int kJustDodgeRingCount = 14;
    constexpr float kJustDodgeRingLifetime = 0.3f;
    constexpr float kJustDodgeRingSize = 0.18f;
    constexpr Vector4 kJustDodgeColor = { 0.55f, 0.95f, 1.0f, 0.9f };
    constexpr float kJustDodgeFlashAlpha = 0.18f;
    constexpr float kJustDodgeFlashSeconds = 0.1f;
    constexpr float kJustDodgeShakeAmount = 0.1f;
    constexpr float kJustDodgeShakeSeconds = 0.12f;

    if (!player_->IsDodging()) {
        return false;
    }
    if (!player_->ConsumeJustDodge()) {
        return true; // 同じ回避の2発目以降は無効化だけ
    }
    styleMeter_ = std::clamp(styleMeter_ + Tune().justDodgeStyleGain, 0.0f, 1.0f);
    player_->ChargeAwakenGauge(Tune().justDodgeGaugeGain);
    player_->BeginJustDodgeWindow(Tune().justDodgeBonusSeconds); // 直後の攻撃を強化し、回避を攻めの起点にする
    TimeManager::GetInstance()->RequestHitStop(Tune().justDodgeHitStopFrames);
    cameraShaker_.Request(kJustDodgeShakeAmount, kJustDodgeShakeSeconds);
    pm_->EmitRing("hit_ring", hitPos, kJustDodgeRingRadius, kJustDodgeColor,
        kJustDodgeRingCount, kJustDodgeRingLifetime, kJustDodgeRingSize);
    ScreenFlash::GetInstance()->Request(
        { kJustDodgeColor.x, kJustDodgeColor.y, kJustDodgeColor.z, kJustDodgeFlashAlpha }, kJustDodgeFlashSeconds);
    return true;
}

void GamePlayScene::UpdateStyleAndUI(float dt)
{
    if (dt <= 0.0f) {
        UpdateWeaponSlotHud();
        DrawStyleUI();
        return;
    }

    UpdateWeaponSlotHud();

    const AABB enemyAABB = GetEnemyHitBox();
    ApplyMeleeComboStyleHit(enemyAABB);
    ApplyWeaponSkillStyleHit(enemyAABB);
    ApplyGunShotStyleHit(enemyAABB);
    ApplyDaggerStingerStyleBonus();
    ApplyRampageStyleHit(enemyAABB);
    UpdateDodgeStyle();
    // フィニッシャースラッシュのダメージは UpdateFinisherSlash の本命ヒットで適用する
    DecayStyleMeter(dt);

    // 採点はstyleMeter_(0〜1)のまま、右上ランクの表示だけStyleMeterへ渡す
    styleRankHud_.SetNormalizedPoints(styleMeter_);
    styleRankHud_.Update(dt);

    DrawStyleUI();
}

// ══════════════════════════════════════════════════════
// パーティクルとヒット演出
// ══════════════════════════════════════════════════════

void GamePlayScene::UpdateParticles(float dt)
{
    if (dt <= 0.0f) {
        return;
    }

    UpdateLandingAndJumpDustParticles();
    UpdateGhostTrail(dt);
    UpdatePlayerEnemyContactHit(dt);
    UpdateEnemyAttackOnPlayer(dt);
    UpdateBossSlamAttack(dt);
    UpdateStyleTechniqueParticles(dt);

    auto emitDefeat = [this](const Vector3& pos) {
        constexpr Vector4 coreColor = { 0.75f, 0.95f, 1.0f, 1.0f };
        constexpr Vector4 edgeColor = { 0.3f, 0.6f, 1.0f, 0.85f };
        constexpr float kStarHeight = 0.45f;
        constexpr float kRingHeight = 0.35f;
        constexpr float kRingSpeed = 5.0f;
        constexpr int kRingCount = 28;
        constexpr float kRingLifetime = 0.55f;
        constexpr float kRingSize = 0.24f;
        constexpr float kOrbHeight = 0.55f;
        constexpr float kOrbSpeed = 3.2f;
        constexpr int kOrbCount = 18;
        constexpr float kOrbLifetime = 0.42f;
        constexpr float kOrbSize = 0.18f;
        constexpr float kSparkSpreadX = 4.5f;
        constexpr float kSparkRiseMin = 1.5f;
        constexpr float kSparkRiseMax = 6.5f;
        constexpr int kSparkCount = 24;
        constexpr float kSparkHeight = 0.4f;
        constexpr float kSparkLifetime = 0.75f;
        constexpr float kSparkSize = 0.16f;
        pm_->EmitHitStar("hit_spark", { pos.x, pos.y + kStarHeight, pos.z }, coreColor);
        pm_->EmitRing("hit_ring", { pos.x, pos.y + kRingHeight, pos.z }, kRingSpeed, edgeColor, kRingCount, kRingLifetime, kRingSize);
        pm_->EmitRing("weapon_orb", { pos.x, pos.y + kOrbHeight, pos.z }, kOrbSpeed, coreColor, kOrbCount, kOrbLifetime, kOrbSize);
        std::uniform_real_distribution<float> vx(-kSparkSpreadX, kSparkSpreadX);
        std::uniform_real_distribution<float> vy(kSparkRiseMin, kSparkRiseMax);
        for (int i = 0; i < kSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", { pos.x, pos.y + kSparkHeight, pos.z },
                { vx(rng_), vy(rng_), 0.0f }, (i % 2 == 0) ? coreColor : edgeColor, kSparkLifetime, kSparkSize);
        }
    };
    if (enemy_ && enemy_->IsDefeated() && !mainEnemyDefeatEffectEmitted_) {
        mainEnemyDefeatEffectEmitted_ = true;
        emitDefeat(enemy_->GetPosition());
    }
    for (auto& entry : weaponEnemies_) {
        if (entry.enemy && entry.enemy->IsDefeated() && !entry.defeatEffectEmitted) {
            entry.defeatEffectEmitted = true;
            emitDefeat(entry.enemy->GetPosition());
        }
    }
}

void GamePlayScene::UpdateLandingAndJumpDustParticles()
{
    constexpr float kDustSpeedMin = 1.2f; // 左右それぞれへ飛ばす横速度の範囲
    constexpr float kDustSpeedMax = 3.5f;
    constexpr float kDustRiseMin = 0.8f;
    constexpr float kDustRiseMax = 2.2f;
    constexpr int kDustPairCount = 4;
    constexpr Vector4 kDustColor = { 0.85f, 0.78f, 0.65f, 0.7f };
    constexpr float kDustLifetime = 0.4f;
    constexpr float kDustSize = 0.22f;
    constexpr float kJumpSmokeSpeed = 1.8f;
    constexpr Vector4 kJumpSmokeColor = { 0.9f, 0.9f, 0.9f, 0.45f };
    constexpr int kJumpSmokeCount = 7;
    constexpr float kJumpSmokeLifetime = 0.22f;
    constexpr float kJumpSmokeSize = 0.28f;

    const Vector3& ppos = player_->GetPosition();

    // 着地ほこり
    if (player_->JustLanded()) {
        std::uniform_real_distribution<float> vxL(-kDustSpeedMax, -kDustSpeedMin);
        std::uniform_real_distribution<float> vxR(kDustSpeedMin, kDustSpeedMax);
        std::uniform_real_distribution<float> vyD(kDustRiseMin, kDustRiseMax);
        for (int i = 0; i < kDustPairCount; ++i) {
            pm_->EmitGravity("land_dust", ppos, { vxL(rng_), vyD(rng_), 0.0f },
                kDustColor, kDustLifetime, kDustSize);
            pm_->EmitGravity("land_dust", ppos, { vxR(rng_), vyD(rng_), 0.0f },
                kDustColor, kDustLifetime, kDustSize);
        }
    }

    // ジャンプ煙
    if (player_->JustJumped()) {
        pm_->EmitRing("jump_smoke", ppos, kJumpSmokeSpeed, kJumpSmokeColor, kJumpSmokeCount, kJumpSmokeLifetime, kJumpSmokeSize);
    }
}

void GamePlayScene::UpdateGhostTrail(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    // 残像: 覚醒中/乱舞中の横移動 or 空中 → プレイヤーモデルのゴーストを一定間隔でスポーン
    // （Player::afterImageRenderer_ と同じ、残像=覚醒時だけの演出という前提に揃える）
    bool movingX = input_->PushKey(DIK_A) || input_->PushKey(DIK_LEFT)
        || input_->PushKey(DIK_D) || input_->PushKey(DIK_RIGHT);
    bool awakenActive = player_->IsRampaging();
    if (player_->IsWarping() || (awakenActive && (movingX || !player_->IsOnGround()))) {
        ghostSpawnTimer_ -= dt;
        if (ghostSpawnTimer_ <= 0.0f) {
            constexpr float kGhostSpawnInterval = 0.05f;
            ghostSpawnTimer_ = kGhostSpawnInterval;
            ghostTrail_.push_back({ ppos, 0.0f });
        }
    } else {
        ghostSpawnTimer_ = 0.0f;
    }
    for (auto& g : ghostTrail_) {
        g.age += dt;
    }
    while (!ghostTrail_.empty() && ghostTrail_.front().age >= kGhostLifetime) {
        ghostTrail_.pop_front();
    }
}

// ══════════════════════════════════════════════════════
// フィニッシャーとクリア判定
