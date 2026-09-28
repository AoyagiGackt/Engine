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

void GamePlayScene::UpdateWeaponEnemies()
{
    const Vector3& playerPos = player_->GetPosition();
    const auto* wm = WeaponManager::GetInstance();

    // entry.enemyの物理/アニメーション更新自体はStageEditor所有のためGetStageEditor().UpdateObjects()
    // （BaseScene::Tick()がUpdate()の直後に呼ぶ）が担う。ここではヒット判定・武器奪取だけを行う
    for (auto& entry : weaponEnemies_) {
        if (!entry.enemy->IsDefeated()) {
            const Vector3& enemyPos = entry.enemy->GetPosition();
            const AABB enemyBounds = {
                { enemyPos.x - Tune().enemyHitBoxHalfExtent, enemyPos.y - Tune().enemyHitBoxHalfExtent, -0.5f },
                { enemyPos.x + Tune().enemyHitBoxHalfExtent, enemyPos.y + Tune().enemyHitBoxHalfExtent, 0.5f }
            };

            bool hit = false;
            if (wm->HasEquippedWeapon() && player_->JustComboHit()) {
                const AABB range = SceneShared::MakeDirectionalShotRange(
                    playerPos, player_->GetLastDirX(), wm->GetCurrent().range,
                    wm->GetCurrent().range * GameConstants::kSkillRearReachMult);
                hit = Collision::CheckCollision(range, enemyBounds);
            }
            if (!hit && player_->JustFired()) {
                const AABB range = SceneShared::MakeDirectionalRange(
                    playerPos, player_->GetLastDirX(), wm->GetRanged().range, Tune().gunBackRange);
                hit = Collision::CheckCollision(range, enemyBounds);
            }
            if (!hit && (player_->JustSwordDash() || player_->JustSpearRetreat() || player_->JustDaggerStingerHit() || player_->JustGreatswordSlam() || player_->JustSpinShot() || player_->JustScytheSpin() || player_->JustAxeCharge())) {
                const float skillRadius = SkillRadiusFor(Tune().weaponEnemySkillRadius, player_->JustGreatswordSlam());
                const AABB range = {
                    { playerPos.x - skillRadius, playerPos.y - Tune().skillRangeHalfHeight, -0.5f },
                    { playerPos.x + skillRadius, playerPos.y + Tune().skillRangeHalfHeight, 0.5f }
                };
                hit = Collision::CheckCollision(range, enemyBounds);
            }
            if (hit) {
                const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
                const float damageMult = attack != nullptr ? attack->damageMult : 1.0f;
                const float baseDamage = wm->HasEquippedWeapon() ? wm->GetCurrent().damage : 20.0f;
                constexpr int kSlamBaseDamage = 3;
                const int slamBonus = (player_->JustGreatswordSlam() && HasBossSlamTechnique())
                    ? GameRules::GetInstance()->Get().bossTechniqueBonusDamage
                    : 0;
                const float rawDamage = player_->JustGreatswordSlam()
                    ? static_cast<float>(kSlamBaseDamage + slamBonus)
                    : baseDamage * damageMult / Tune().meleeDamageDivisor;
                const int damage = (std::max)(1, static_cast<int>(std::round(rawDamage * CurrentDamageMult())));
                const float knockbackMult = (wm->HasEquippedWeapon()
                                                ? wm->GetCurrent().knockbackMult
                                                : 1.0f)
                    * player_->GetAwakenedKnockbackMult();
                entry.enemy->TakeDamage(damage);
                const float knockY = (attack != nullptr ? attack->knockY : 0.05f) * knockbackMult;
                entry.enemy->ApplyComboReaction(player_->GetLastDirX() * knockbackMult, knockY,
                    player_->JustWeaponSwitchHit(), playerPos.x);
                if (wm->HasEquippedWeapon() && wm->GetCurrent().type == WeaponType::Dagger) {
                    entry.enemy->ApplySlow(0.8f);
                }
                constexpr Vector4 kUnarmedHitColor = { 1.0f, 0.8f, 0.25f, 1.0f };
                const Vector4 hitColor = wm->HasEquippedWeapon()
                    ? Vector4 { wm->GetCurrent().effectColor[0], wm->GetCurrent().effectColor[1],
                          wm->GetCurrent().effectColor[2], wm->GetCurrent().effectColor[3] }
                    : kUnarmedHitColor;
                EmitEnemyHitEffect(enemyPos, hitColor, 1.0f);
            }
        }

        if (!entry.weaponAcquired && entry.enemy->IsDefeated()) {
            const Vector3 enemyPos = entry.enemy->GetPosition();
            const float dx = playerPos.x - enemyPos.x;
            const float dy = playerPos.y - enemyPos.y;
            constexpr float kAbsorbRange = 2.0f;
            constexpr float kAbsorbDuration = 0.5f;
            if (!entry.absorbing && dx * dx + dy * dy <= kAbsorbRange * kAbsorbRange
                && input_->TriggerKey(DIK_J)) {
                entry.absorbing = true;
                entry.absorbTimer = kAbsorbDuration;
                player_->PlayStealStab();
                pm_->EmitRing("weapon_orb", enemyPos, 3.0f,
                    { 0.5f, 0.9f, 1.0f, 1.0f }, 18, 0.4f, 0.3f);
                ScreenFlash::GetInstance()->Request(
                    { 0.6f, 0.9f, 1.0f, 0.35f }, 0.12f);
            }
            if (entry.absorbing) {
                entry.absorbTimer -= GameConstants::kFrameDeltaTime;
                Vector3& absorbPos = entry.enemy->GetPositionRef();
                absorbPos.x += (playerPos.x - absorbPos.x) * 0.16f;
                absorbPos.y += (playerPos.y + 0.5f - absorbPos.y) * 0.16f;
                entry.enemy->RefreshVisualTransforms();
                if (entry.absorbTimer <= 0.0f) {
                    entry.weaponAcquired = true;
                    entry.enemy->SetVisible(false);
                    if (WeaponManager::GetInstance()->Acquire(entry.weaponType) == WeaponManager::AcquireResult::Duplicate) {
                        player_->ChargeAwakenGauge(CombatTuning::GetInstance()->Get().duplicateWeaponAwakenBonus);
                    }
                }
            }
        }
    }
}

void GamePlayScene::UpdateExplosiveBarrels()
{
    // 収集物（pickup）の回収はStageEditor側が行う。ここでは壊せる物（breakable）のヒット判定と爆風ダメージだけを担当する
    constexpr float kBreakablePulseSpeed = 4.0f;
    constexpr float kBreakablePulseAmplitude = 0.25f;
    constexpr float kBreakableHalfDepth = 0.5f;
    constexpr float kBreakableHitPadding = 0.1f; // 見た目の箱よりわずかに広く当たりを取り、かすった攻撃も拾う
    constexpr int kBreakableHitStopFrames = 8;
    constexpr float kBreakableShakeAmount = 0.28f;
    constexpr float kBreakableShakeSeconds = 0.2f;
    constexpr int kBreakableRingCount = 26;
    constexpr float kBreakableRingLifetime = 0.5f;
    constexpr float kBreakableRingSize = 0.32f;
    constexpr int kBreakableSparkCount = 18;

    const Vector3& playerPos = player_->GetPosition();
    const auto* wm = WeaponManager::GetInstance();
    explosiveBarrelPulse_ += GameConstants::kFrameDeltaTime;

    for (BreakableRef barrel : GetStageEditor().GetBreakables()) {
        const ObjectDesc& desc = *barrel.desc;

        // 赤く脈動させて攻撃で壊せる設置物だと分かりやすくする（表示行列の更新はStageEditor側が毎フレーム行う）
        const float pulse = 1.0f - kBreakablePulseAmplitude + std::sin(explosiveBarrelPulse_ * kBreakablePulseSpeed) * kBreakablePulseAmplitude;
        barrel.object->SetColor({ desc.breakableColor.x, desc.breakableColor.y * pulse, desc.breakableColor.z, desc.breakableColor.w });
        barrel.object->Update();

        const float halfExtentX = 0.5f * std::abs(desc.scale.x) + kBreakableHitPadding;
        const float halfExtentY = 0.5f * std::abs(desc.scale.y) + kBreakableHitPadding;
        const AABB barrelBounds = {
            { barrel.position.x - halfExtentX, barrel.position.y - halfExtentY, -kBreakableHalfDepth },
            { barrel.position.x + halfExtentX, barrel.position.y + halfExtentY, kBreakableHalfDepth }
        };

        // ヒット判定は道中の武器敵(UpdateWeaponEnemies)と同じ3つの攻撃窓（近接コンボ/射撃/固有技）を流用する
        bool hit = false;
        if (wm->HasEquippedWeapon() && player_->JustComboHit()) {
            const AABB range = SceneShared::MakeDirectionalRange(
                playerPos, player_->GetLastDirX(), wm->GetCurrent().range,
                wm->GetCurrent().range * GameConstants::kSkillRearReachMult);
            hit = Collision::CheckCollision(range, barrelBounds);
        }
        if (!hit && player_->JustFired()) {
            const AABB range = SceneShared::MakeDirectionalRange(
                playerPos, player_->GetLastDirX(), wm->GetRanged().range, Tune().gunBackRange);
            hit = Collision::CheckCollision(range, barrelBounds);
        }
        if (!hit && (player_->JustSwordDash() || player_->JustSpearRetreat() || player_->JustDaggerStingerHit()
                || player_->JustGreatswordSlam() || player_->JustSpinShot() || player_->JustScytheSpin() || player_->JustAxeCharge())) {
            const AABB range = {
                { playerPos.x - Tune().weaponEnemySkillRadius, playerPos.y - Tune().skillRangeHalfHeight, -0.5f },
                { playerPos.x + Tune().weaponEnemySkillRadius, playerPos.y + Tune().skillRangeHalfHeight, 0.5f }
            };
            hit = Collision::CheckCollision(range, barrelBounds);
        }

        // 武器指定つき（壊せる壁など）は、その武器の近接攻撃・固有技でしか壊れない。銃では壊れない
        if (hit && !desc.breakableWeapon.empty()) {
            const bool weaponMatches = wm->HasEquippedWeapon()
                && wm->GetCurrent().type == ParseWeaponTypeName(desc.breakableWeapon);
            const bool meleeOrSkillHit = player_->JustComboHit() || !player_->JustFired();
            if (!weaponMatches || !meleeOrSkillHit) {
                pm_->EmitHitStar("hit_spark", barrel.position, { 0.6f, 0.6f, 0.7f, 1.0f }); // 弾かれた手応え
                hit = false;
            }
        }
        if (!hit) {
            continue;
        }

        (*barrel.hp)--;
        pm_->EmitHitStar("hit_spark", barrel.position, { 1.0f, 0.6f, 0.15f, 1.0f });
        if (*barrel.hp > 0) {
            continue;
        }

        // 破壊: 爆風範囲内のプレイヤー/敵にまとめてダメージを与える（環境を利用した攻撃手段）
        *barrel.destroyed = true;
        GameFlags::GetInstance()->SetFlag("broken_" + desc.name, true);

        const float radiusSq = desc.breakableRadius * desc.breakableRadius;
        const float pdx = playerPos.x - barrel.position.x;
        const float pdy = playerPos.y - barrel.position.y;
        if (pdx * pdx + pdy * pdy <= radiusSq && !TryJustDodge(barrel.position) && !player_->IsInvincible()) {
            RunData::GetInstance()->TakeDamage(desc.breakablePlayerDamage);
            player_->OnHit();
        }
        if (enemy_ && !enemy_->IsDefeated()) {
            const Vector3& epos = enemy_->GetPosition();
            const float edx = epos.x - barrel.position.x;
            const float edy = epos.y - barrel.position.y;
            if (edx * edx + edy * edy <= radiusSq) {
                enemy_->TakeDamage(desc.breakableEnemyDamage);
            }
        }
        for (auto& entry : weaponEnemies_) {
            if (entry.enemy->IsDefeated()) {
                continue;
            }
            const Vector3& wepos = entry.enemy->GetPosition();
            const float wdx = wepos.x - barrel.position.x;
            const float wdy = wepos.y - barrel.position.y;
            if (wdx * wdx + wdy * wdy <= radiusSq) {
                entry.enemy->TakeDamage(desc.breakableEnemyDamage);
            }
        }

        auto* tm = TimeManager::GetInstance();
        tm->RequestHitStop(kBreakableHitStopFrames);
        cameraShaker_.Request(kBreakableShakeAmount, kBreakableShakeSeconds);
        pm_->EmitRing("hit_ring", barrel.position, desc.breakableRadius,
            { 1.0f, 0.55f, 0.15f, 1.0f }, kBreakableRingCount, kBreakableRingLifetime, kBreakableRingSize);
        std::uniform_real_distribution<float> vxB(-5.0f, 5.0f);
        std::uniform_real_distribution<float> vyB(2.5f, 7.0f);
        for (int i = 0; i < kBreakableSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", barrel.position,
                { vxB(rng_), vyB(rng_), 0.0f },
                { 1.0f, 0.4f, 0.1f, 1.0f }, 0.9f, 0.2f);
        }
    }
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
    float stageLeft = 1.5f;
    float stageRight = 36.5f;
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
    return { { epos.x - Tune().enemyHitBoxHalfExtent, epos.y - Tune().enemyHitBoxHalfExtent, -0.5f },
        { epos.x + Tune().enemyHitBoxHalfExtent, epos.y + Tune().enemyHitBoxHalfExtent, 0.5f } };
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
    const float knockbackMult = weapon.knockbackMult * player_->GetAwakenedKnockbackMult();
    enemy_->TakeDamage(damage);
    enemy_->ApplyComboReaction(player_->GetLastDirX() * knockbackMult,
        (attack != nullptr ? attack->knockY : 0.05f) * knockbackMult,
        player_->JustWeaponSwitchHit(), ppos.x);

    // 段が進むほど敵側の弾け方も大きくする（コンボの伸びを敵の見た目でも返す）
    constexpr float kComboStepStrengthGain = 0.2f;
    constexpr float kWeaponSwitchHitStrength = 1.8f;
    const float hitStrength = player_->JustWeaponSwitchHit()
        ? kWeaponSwitchHitStrength
        : 1.0f + static_cast<float>((std::max)(player_->GetComboStep() - 1, 0)) * kComboStepStrengthGain;
    EmitEnemyHitEffect(enemy_->GetPosition(),
        { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] }, hitStrength);

    const WeaponType element = wm->GetCurrent().type;
    if (element == WeaponType::Sword && player_->GetComboStep() >= 3) {
        enemy_->TakeDamage(1); // 炎: コンボ後半で追加ダメージ
    } else if (element == WeaponType::Dagger) {
        enemy_->ApplySlow(0.9f); // 氷: 行動速度を落とす
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
    const bool slam = player_->JustGreatswordSlam();
    const float radius = SkillRadiusFor(slam ? Tune().skillSlamRadius : Tune().skillDefaultRadius, slam);
    if (slam && HasBossSlamTechnique()) {
        // 習得したボス技: 叩きつけの衝撃波を広げて見せる（判定半径の拡大と対応）
        constexpr int kShockwaveRingCount = 22;
        constexpr float kShockwaveRingLifetime = 0.35f;
        constexpr float kShockwaveRingSize = 0.3f;
        pm_->EmitRing("hit_ring", ppos, radius, { 1.0f, 0.7f, 0.25f, 0.9f },
            kShockwaveRingCount, kShockwaveRingLifetime, kShockwaveRingSize);
    }
    const AABB skillRange = {
        { ppos.x - radius, ppos.y - Tune().skillRangeHalfHeight, -0.5f },
        { ppos.x + radius, ppos.y + Tune().skillRangeHalfHeight, 0.5f }
    };
    if (!Collision::CheckCollision(skillRange, enemyAABB)) {
        return;
    }
    const int techniqueId = 1000 + static_cast<int>(wm->GetCurrent().type);
    const float varietyBonus = techniqueId == lastTechniqueId_ ? Tune().skillVarietyBonusRepeat : Tune().skillVarietyBonusFresh;
    lastTechniqueId_ = techniqueId;
    styleMeter_ = std::clamp(styleMeter_ + varietyBonus, 0.0f, 1.0f);
    player_->ChargeAwakenGauge(Tune().skillAwakenGaugeGain);
    constexpr int kSlamBaseDamage = 3;
    constexpr int kSkillBaseDamage = 2;
    const int slamBonus = (slam && HasBossSlamTechnique()) ? GameRules::GetInstance()->Get().bossTechniqueBonusDamage : 0;
    const float rawDamage = static_cast<float>((slam ? kSlamBaseDamage : kSkillBaseDamage) + slamBonus);
    enemy_->TakeDamage((std::max)(1, static_cast<int>(std::round(rawDamage * CurrentDamageMult()))));

    // 固有技は通常段より大きく弾けさせる（叩きつけはさらに大きく）
    constexpr float kSkillHitStrength = 1.6f;
    constexpr float kSlamHitStrength = 2.2f;
    const WeaponData& weapon = wm->GetCurrent();
    EmitEnemyHitEffect(enemy_->GetPosition(),
        { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] },
        slam ? kSlamHitStrength : kSkillHitStrength);
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
}

void GamePlayScene::ApplyRampageStyleHit(const AABB& enemyAABB)
{
    if (!player_->JustRampageHit()) {
        return;
    }
    const Vector3& ppos = player_->GetPosition();
    AABB rushRange = { { ppos.x - Tune().rampageRushRadiusX, ppos.y - Tune().rampageRushRadiusY, -0.5f },
        { ppos.x + Tune().rampageRushRadiusX, ppos.y + Tune().rampageRushRadiusY, 0.5f } };
    if (!Collision::CheckCollision(rushRange, enemyAABB)) {
        return;
    }
    // 乱舞スラッシュ 回数が増えるほど多くゲージが溜まる
    styleMeter_ = std::clamp(
        styleMeter_ + Tune().rampageBaseStyleGain + player_->GetJuggleCount() * Tune().rampageJuggleStyleGain, 0.0f, 1.0f);
    enemy_->TakeDamage(1);

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
}

void GamePlayScene::UpdateLandingAndJumpDustParticles()
{
    const Vector3& ppos = player_->GetPosition();

    // 着地ほこり
    if (player_->JustLanded()) {
        std::uniform_real_distribution<float> vxL(-3.5f, -1.2f);
        std::uniform_real_distribution<float> vxR(1.2f, 3.5f);
        std::uniform_real_distribution<float> vyD(0.8f, 2.2f);
        for (int i = 0; i < 4; ++i) {
            pm_->EmitGravity("land_dust", ppos, { vxL(rng_), vyD(rng_), 0.0f },
                { 0.85f, 0.78f, 0.65f, 0.7f }, 0.4f, 0.22f);
            pm_->EmitGravity("land_dust", ppos, { vxR(rng_), vyD(rng_), 0.0f },
                { 0.85f, 0.78f, 0.65f, 0.7f }, 0.4f, 0.22f);
        }
    }

    // ジャンプ煙
    if (player_->JustJumped()) {
        pm_->EmitRing("jump_smoke", ppos, 1.8f, { 0.9f, 0.9f, 0.9f, 0.45f }, 7, 0.22f, 0.28f);
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
            ghostSpawnTimer_ = 0.05f;
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

void GamePlayScene::UpdatePlayerEnemyContactHit(float dt)
{
    auto* tm = TimeManager::GetInstance();
    const Vector3& ppos = player_->GetPosition();

    // 敵との当たり判定
    hitCooldown_ -= dt;
    {
        Collider playerCol = player_->GetCollider();
        const Vector3& epos = enemy_->GetPosition();
        AABB enemyAABB = { { epos.x - Tune().enemyHitBoxHalfExtent, epos.y - Tune().enemyHitBoxHalfExtent, -0.5f },
            { epos.x + Tune().enemyHitBoxHalfExtent, epos.y + Tune().enemyHitBoxHalfExtent, 0.5f } };
        if (Collision::CheckCollision(playerCol.aabb, enemyAABB) && hitCooldown_ <= 0.0f) {
            hitCooldown_ = 0.5f;
            enemy_->TakeDamage(1);
            tm->RequestHitStop(5);
            cameraShaker_.Request(0.18f, 0.15f);

            Vector3 hitPos = { (ppos.x + epos.x) * 0.5f,
                (ppos.y + epos.y) * 0.5f, 0.0f };
            pm_->EmitRing("hit_ring", hitPos, 4.0f, { 1.0f, 0.85f, 0.2f, 1.0f }, 16, 0.3f, 0.2f);
            std::uniform_real_distribution<float> vxD(-3.0f, 3.0f);
            std::uniform_real_distribution<float> vyD(2.0f, 5.5f);
            for (int i = 0; i < 8; ++i) {
                pm_->EmitGravity("hit_spark", hitPos,
                    { vxD(rng_), vyD(rng_), 0.0f },
                    { 1.0f, 0.55f, 0.1f, 1.0f }, 0.7f, 0.15f);
            }
        }
    }
}

void GamePlayScene::UpdateEnemyAttackOnPlayer(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    // 予備動作に入った敵には警告リングを出す（攻撃が来る前に知らせ、回避を狙えるようにする）
    EmitEnemyTelegraphCue(enemy_);
    for (const auto& entry : weaponEnemies_) {
        EmitEnemyTelegraphCue(entry.enemy);
    }

    // 予備動作明けの瞬間 近接の敵は前方を薙ぎ払い、遠隔の敵（槍・ボール）は狙いを一度だけ計算して実弾を撃つ
    // ボスは間合いなら薙ぎ払い、遠ければ弾（近づいても離れても攻撃が来る）
    // （発射の瞬間にプレイヤーが射程外にいる敵は撃たない遠くの敵に一方的に狙撃されないように）
    auto fireBulletFrom = [&](EnemyEntity* shooter) {
        if (!shooter->JustFiredAttack()) {
            return;
        }
        if (shooter->IsMeleeAttacker()) {
            const bool isBoss = shooter == enemy_;
            const float reach = EnemyTuning::GetInstance()->Basic().meleeReach;
            const bool inReach = std::abs(ppos.x - shooter->GetPosition().x) <= reach;
            if (!isBoss || inReach) {
                ApplyEnemyMeleeSwing(shooter);
                return;
            }
        }
        const Vector3& epos = shooter->GetPosition();
        // pos_ はAABB中心（当たり判定の基準点）そのものなので、狙い・発射位置ともにオフセットを足さずここから直接計算する
        Vector3 dir = { ppos.x - epos.x, ppos.y - epos.y, 0.0f };
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > EnemyTuning::GetInstance()->Bullet().fireRange) {
            return;
        }
        if (len > 0.001f) {
            dir.x /= len;
            dir.y /= len;
        }
        EnemyBullet bullet;
        bullet.pos = epos;
        bullet.vel = { dir.x * EnemyTuning::GetInstance()->Bullet().speed, dir.y * EnemyTuning::GetInstance()->Bullet().speed, 0.0f };
        bullet.timer = EnemyTuning::GetInstance()->Bullet().lifetime;
        bullet.damage = shooter->GetAttackDamage();
        enemyBullets_.push_back(bullet);
        pm_->EmitRing("hit_ring", epos, 1.2f, { 1.0f, 0.35f, 0.25f, 0.8f }, 8, 0.15f, 0.1f);
    };
    fireBulletFrom(enemy_);
    for (auto& entry : weaponEnemies_) {
        fireBulletFrom(entry.enemy);
    }

    for (auto it = enemyBullets_.begin(); it != enemyBullets_.end();) {
        EnemyBullet& bullet = *it;
        bullet.pos.x += bullet.vel.x * dt;
        bullet.pos.y += bullet.vel.y * dt;
        bullet.timer -= dt;
        // 曳光弾の見た目実体（Object3d）は持たず、毎フレーム現在位置に粒を撒いて弾の軌跡に見せる
        pm_->EmitWithColor("gun_shot", bullet.pos, { 0.0f, 0.0f, 0.0f },
            { 1.0f, 0.3f, 0.2f, 1.0f }, 0.12f, 0.28f);

        if (bullet.timer <= 0.0f) {
            it = enemyBullets_.erase(it);
            continue;
        }

        AABB bulletAABB = { { bullet.pos.x - 0.15f, bullet.pos.y - 0.15f, -0.5f },
            { bullet.pos.x + 0.15f, bullet.pos.y + 0.15f, 0.5f } };
        Collider playerCol = player_->GetCollider();
        if (!Collision::CheckCollision(playerCol.aabb, bulletAABB)) {
            ++it;
            continue;
        }

        // 回避中に弾が体を通過したらジャスト回避。弾はそのまま飛び続ける（避けた感を残す）
        if (TryJustDodge(bullet.pos)) {
            ++it;
            continue;
        }
        if (player_->IsInvincible()) {
            ++it;
            continue;
        }

        RunData::GetInstance()->TakeDamage(bullet.damage);
        player_->OnHit();

        auto* tm = TimeManager::GetInstance();
        tm->RequestHitStop(7);
        cameraShaker_.Request(0.22f, 0.18f);

        pm_->EmitRing("hit_ring", bullet.pos, 4.0f, { 1.0f, 0.2f, 0.2f, 1.0f }, 16, 0.3f, 0.2f);
        std::uniform_real_distribution<float> vxD(-3.0f, 3.0f);
        std::uniform_real_distribution<float> vyD(2.0f, 5.5f);
        for (int i = 0; i < 8; ++i) {
            pm_->EmitGravity("hit_spark", bullet.pos,
                { vxD(rng_), vyD(rng_), 0.0f },
                { 1.0f, 0.15f, 0.15f, 1.0f }, 0.7f, 0.15f);
        }
        it = enemyBullets_.erase(it);
    }
}

void GamePlayScene::ApplyEnemyMeleeSwing(EnemyEntity* attacker)
{
    constexpr float kSwingSlashRadius = 1.6f;
    constexpr float kSwingHalfDepth = 0.5f;
    constexpr int kSwingHitStopFrames = 6;
    constexpr float kSwingShakeAmount = 0.2f;
    constexpr float kSwingShakeSeconds = 0.15f;
    constexpr int kSwingSparkCount = 8;
    constexpr Vector4 kSwingColor = { 1.0f, 0.4f, 0.3f, 0.9f };

    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    const Vector3& epos = attacker->GetPosition();
    const Vector3& ppos = player_->GetPosition();
    const float dirX = ppos.x >= epos.x ? 1.0f : -1.0f;

    // 振った軌跡を見せる（当たらなくても振ったことが分かるように）
    const Vector3 swingCenter = { epos.x + dirX * tuning.meleeReach * 0.5f, epos.y + 0.4f, 0.0f };
    pm_->EmitSlash("sword_slash", swingCenter, dirX > 0.0f ? 0.0f : GameConstants::kPi, kSwingColor, kSwingSlashRadius);

    // 前方だけに判定を出す（背後にいるプレイヤーには当たらない＝回り込みが有効）
    const AABB swingRange = {
        { (std::min)(epos.x, epos.x + dirX * tuning.meleeReach), epos.y - tuning.meleeHitHalfHeight, -kSwingHalfDepth },
        { (std::max)(epos.x, epos.x + dirX * tuning.meleeReach), epos.y + tuning.meleeHitHalfHeight, kSwingHalfDepth }
    };
    if (!Collision::CheckCollision(player_->GetCollider().aabb, swingRange)) {
        return;
    }
    if (TryJustDodge(swingCenter) || player_->IsInvincible()) {
        return;
    }

    RunData::GetInstance()->TakeDamage(attacker->GetAttackDamage());
    player_->OnHit();
    TimeManager::GetInstance()->RequestHitStop(kSwingHitStopFrames);
    cameraShaker_.Request(kSwingShakeAmount, kSwingShakeSeconds);
    std::uniform_real_distribution<float> vxD(-3.0f, 3.0f);
    std::uniform_real_distribution<float> vyD(2.0f, 5.5f);
    for (int i = 0; i < kSwingSparkCount; ++i) {
        pm_->EmitGravity("hit_spark", ppos, { vxD(rng_), vyD(rng_), 0.0f }, { 1.0f, 0.15f, 0.15f, 1.0f }, 0.7f, 0.15f);
    }
}

void GamePlayScene::UpdateBossSlamAttack(float dt)
{
    if (!enemy_ || enemy_->IsDefeated() || !enemy_->IsVisible()) {
        return;
    }

    if (!bossSlamWarningActive_) {
        const Vector3& ppos = player_->GetPosition();
        const Vector3& epos = enemy_->GetPosition();
        const float dx = ppos.x - epos.x;
        const float dy = ppos.y - epos.y;
        if (dx * dx + dy * dy > EnemyTuning::GetInstance()->BossSlam().engageRange * EnemyTuning::GetInstance()->BossSlam().engageRange) {
            return; // ボスと交戦中でなければタイマーを進めない（戦闘開始前に発動しないように）
        }
        bossSlamTimer_ -= dt;
        if (bossSlamTimer_ <= 0.0f) {
            bossSlamWarningActive_ = true;
            bossSlamWarningTimer_ = EnemyTuning::GetInstance()->BossSlam().warningDuration;
            bossSlamTargetPos_ = ppos; // 着弾地点は予告開始時点のプレイヤー位置に固定する（避けられるように）
        }
        return;
    }

    // 予告円の警告演出（円形グロー画像を着弾範囲の見かけ半径まで拡大し、点滅させながら見せる）
    bossSlamWarningTimer_ -= dt;
    const float progress = std::clamp(1.0f - bossSlamWarningTimer_ / EnemyTuning::GetInstance()->BossSlam().warningDuration, 0.0f, 1.0f);

    const Vector3& cam = camera_->GetTranslate();
    float sx, sy;
    SceneShared::WorldToScreen(bossSlamTargetPos_.x, bossSlamTargetPos_.y, cam.x, cam.y, sx, sy);
    const float pxPerWorldUnit = GameConstants::kScreenCenterX / GameConstants::kCameraHalfW;
    const float diameterPx = EnemyTuning::GetInstance()->BossSlam().radius * 2.0f * pxPerWorldUnit * (0.6f + 0.4f * progress);
    const float blink = 0.5f + 0.5f * std::sin(bossSlamWarningTimer_ * 22.0f); // 素早く点滅させて視線を引く
    bossSlamWarningSprite_->SetColor({ 1.0f, 0.15f + progress * 0.1f, 0.1f, 0.35f + blink * 0.4f });
    bossSlamWarningSprite_->SetPosition({ sx - diameterPx * 0.5f, sy - diameterPx * 0.5f });
    bossSlamWarningSprite_->SetSize({ diameterPx, diameterPx });
    bossSlamWarningSprite_->Update();

    pm_->EmitRing("hit_ring", bossSlamTargetPos_, EnemyTuning::GetInstance()->BossSlam().radius,
        { 1.0f, 0.3f - progress * 0.15f, 0.15f - progress * 0.1f, 0.5f }, 6, 0.15f, 0.06f);

    if (bossSlamWarningTimer_ > 0.0f) {
        return;
    }

    // 着弾判定
    bossSlamWarningActive_ = false;
    bossSlamTimer_ = EnemyTuning::GetInstance()->BossSlam().interval;

    const Vector3& ppos = player_->GetPosition();
    const float dx = ppos.x - bossSlamTargetPos_.x;
    const float dy = ppos.y - bossSlamTargetPos_.y;
    const float slamRadius = EnemyTuning::GetInstance()->BossSlam().radius;
    if (dx * dx + dy * dy <= slamRadius * slamRadius && !TryJustDodge(bossSlamTargetPos_) && !player_->IsInvincible()) {
        RunData::GetInstance()->TakeDamage(EnemyTuning::GetInstance()->BossSlam().damage);
        player_->OnHit();
        auto* tm = TimeManager::GetInstance();
        tm->RequestHitStop(9);
        cameraShaker_.Request(0.3f, 0.22f);
    }

    pm_->EmitRing("hit_ring", bossSlamTargetPos_, EnemyTuning::GetInstance()->BossSlam().radius,
        { 1.0f, 0.6f, 0.2f, 1.0f }, 32, 0.55f, 0.34f);
    pm_->EmitRing("hit_ring", bossSlamTargetPos_, EnemyTuning::GetInstance()->BossSlam().radius * 1.4f,
        { 1.0f, 0.85f, 0.4f, 0.6f }, 20, 0.4f, 0.24f);
    std::uniform_real_distribution<float> vxS(-4.0f, 4.0f);
    std::uniform_real_distribution<float> vyS(2.5f, 6.0f);
    for (int i = 0; i < 20; ++i) {
        pm_->EmitGravity("hit_spark", bossSlamTargetPos_,
            { vxS(rng_), vyS(rng_), 0.0f },
            { 1.0f, 0.45f, 0.1f, 1.0f }, 0.85f, 0.18f);
    }
}

void GamePlayScene::UpdateStyleTechniqueParticles(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    EmitComboHitParticles(ppos);
    EmitGunFireParticles(ppos);
    EmitBlinkAndGaugeParticles(ppos);
    EmitAwakenParticles(ppos, dt);
    EmitStyleRankUpParticles(ppos);
}

void GamePlayScene::EmitEnemyHitEffect(const Vector3& enemyPos, const Vector4& color, float strength)
{
    constexpr float kSurfaceLerp = 0.3f; // 敵の中心からプレイヤー側へ寄せる割合（体の表面で弾けたように見せる）
    constexpr Vector4 kCoreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    constexpr float kCoreLifetime = 0.08f;
    constexpr float kCoreScale = 0.9f;
    constexpr float kRingSpeed = 2.4f;
    constexpr int kRingCount = 10;
    constexpr float kRingLifetime = 0.22f;
    constexpr float kRingSize = 0.16f;
    constexpr int kSparkCount = 6;
    constexpr float kSparkSpreadX = 2.5f;
    constexpr float kSparkRiseMin = 1.5f;
    constexpr float kSparkRiseMax = 4.5f;
    constexpr float kSparkLifetime = 0.45f;
    constexpr float kSparkSize = 0.13f;

    const Vector3& ppos = player_->GetPosition();
    const Vector3 hitPos = {
        enemyPos.x + (ppos.x - enemyPos.x) * kSurfaceLerp,
        enemyPos.y + (ppos.y - enemyPos.y) * kSurfaceLerp,
        0.0f
    };

    // 白い芯→属性色の星→広がるリング→散る火花の順に重ねて、当たった一点をはっきり見せる
    pm_->EmitWithColor("hit_spark", hitPos, { 0.0f, 0.0f, 0.0f }, kCoreColor, kCoreLifetime, kCoreScale * strength);
    pm_->EmitHitStar("hit_spark", hitPos, color);
    pm_->EmitRing("hit_ring", hitPos, kRingSpeed * strength, color, kRingCount, kRingLifetime, kRingSize * strength);
    std::uniform_real_distribution<float> vxD(-kSparkSpreadX, kSparkSpreadX);
    std::uniform_real_distribution<float> vyD(kSparkRiseMin, kSparkRiseMax);
    for (int i = 0; i < kSparkCount; ++i) {
        pm_->EmitGravity("hit_spark", hitPos, { vxD(rng_) * strength, vyD(rng_) * strength, 0.0f },
            color, kSparkLifetime, kSparkSize * strength);
    }
}

void GamePlayScene::EmitEnemyTelegraphCue(const EnemyEntity* enemy)
{
    constexpr Vector4 kTelegraphColor = { 1.0f, 0.35f, 0.2f, 0.9f };
    constexpr float kTelegraphRingSpeed = 1.8f;
    constexpr int kTelegraphRingCount = 12;
    constexpr float kTelegraphRingLifetime = 0.3f;
    constexpr float kTelegraphRingSize = 0.2f;
    constexpr float kTelegraphMarkOffsetY = 1.4f; // 頭上に出す印の高さ
    constexpr float kTelegraphMarkLifetime = 0.35f;
    constexpr float kTelegraphMarkScale = 0.6f;

    if (enemy == nullptr || !enemy->JustStartedTelegraph()) {
        return;
    }
    const Vector3& epos = enemy->GetPosition();
    pm_->EmitRing("hit_ring", epos, kTelegraphRingSpeed, kTelegraphColor,
        kTelegraphRingCount, kTelegraphRingLifetime, kTelegraphRingSize);
    const Vector3 markPos = { epos.x, epos.y + kTelegraphMarkOffsetY, 0.0f };
    pm_->EmitWithColor("hit_spark", markPos, { 0.0f, 0.0f, 0.0f }, kTelegraphColor,
        kTelegraphMarkLifetime, kTelegraphMarkScale, true);
}

void GamePlayScene::EmitComboHitParticles(const Vector3& ppos)
{
    if (!player_->JustComboHit()) {
        return;
    }
    auto* tm = TimeManager::GetInstance();
    auto* wm = WeaponManager::GetInstance();
    const auto& styles = wm->GetList();

    int step = player_->GetComboStep();
    float dir = player_->GetLastDirX();
    float ang = (dir > 0.0f) ? 0.0f : GameConstants::kPi;
    const auto& sc = styles[wm->GetIndex()].styleColor;
    Vector4 col = { sc[0], sc[1], sc[2], sc[3] };
    float rad = 0.8f + (step - 1) * 0.45f;
    pm_->EmitSlash("sword_slash", ppos, ang, col, rad);
    if (step == 3) {
        pm_->EmitRing("sword_slash", ppos, 3.5f, col, 10, 0.3f, 0.22f);
    }

    // 属性演出はConfig/weapons.jsonの共通プリセットから生成する
    // 武器追加時にシーン側へtype分岐を増やさず色と密度を調整できるようにする
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 effectColor = { weapon.effectColor[0], weapon.effectColor[1],
        weapon.effectColor[2], weapon.effectColor[3] };
    for (int i = 0; i < weapon.effectBurstCount; ++i) {
        const float spread = static_cast<float>((i % 5) - 2) * 0.45f;
        pm_->EmitGravity("hit_spark", ppos,
            { dir * (2.0f + i * 0.3f), 1.8f + (i % 4) * 0.65f, spread },
            effectColor, 0.5f, 0.16f);
    }
    if (weapon.effectRingRadius > 0.0f) {
        pm_->EmitRing("sword_slash", ppos, weapon.effectRingRadius,
            effectColor, 12 + weapon.effectBurstCount, 0.28f, 0.16f);
    }

    if (player_->JustWeaponSwitchHit()) {
        pm_->EmitRing("sword_slash", ppos, 4.8f, col, 18, 0.38f, 0.28f);
        tm->RequestHitStop(5);
        cameraShaker_.Request(0.35f, 0.16f);
    }

    tm->RequestHitStop(3);
    cameraShaker_.Request(0.10f * step, 0.10f);
}

void GamePlayScene::EmitGunFireParticles(const Vector3& ppos)
{
    if (!player_->JustFired()) {
        return;
    }
    // 弾数・拡散・色は選択中の銃と段の定義に従う（散弾は扇状に、単発は直線に飛ぶ）
    auto* wm = WeaponManager::GetInstance();
    const GunShotDef* shot = player_->GetActiveGunShot();
    const RangedWeaponData& gun = wm->GetRanged();
    float dir = player_->GetLastDirX();
    Vector4 col = { gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
    int n = (shot != nullptr) ? (std::max)(shot->bullets, 2) : 4;
    float spread = (shot != nullptr) ? shot->spreadDeg * GameConstants::kDegToRad : 0.15f;
    for (int i = 0; i < n; ++i) {
        float t = (n > 1) ? (i / (n - 1.0f) - 0.5f) : 0.0f; // -0.5〜+0.5
        float speed = 7.0f + i * 1.0f;
        pm_->EmitWithColor("gun_shot", ppos,
            { dir * speed, speed * spread * t, 0.0f },
            col, 0.35f, 0.14f);
    }
}

void GamePlayScene::EmitBlinkAndGaugeParticles(const Vector3& ppos)
{
    auto* wm = WeaponManager::GetInstance();
    const auto& styles = wm->GetList();

    if (player_->JustDaggerStingerHit()) {
        const auto& sc = styles[2].styleColor;
        Vector4 col = { sc[0], sc[1], sc[2], 0.75f };
        pm_->EmitRing("blink_trail", ppos, 2.8f, col, 10, 0.28f, 0.17f);
    }

    if (player_->JustChargedGauge()) {
        pm_->EmitRing("awaken_aura", ppos, 1.6f, { 0.75f, 0.25f, 1.0f, 0.9f }, 8, 0.38f, 0.2f);
    }
}

void GamePlayScene::EmitAwakenParticles(const Vector3& ppos, float dt)
{
    auto* tm = TimeManager::GetInstance();

    // 覚醒中オーラ（連続）
    if (player_->IsAwakened()) {
        auraTimer_ += dt;
        if (auraTimer_ >= 0.07f) {
            auraTimer_ = 0.0f;
            pm_->EmitWithColor("awaken_aura", ppos,
                { 0.0f, 0.4f, 0.0f },
                { 1.0f, 0.88f, 0.25f, 0.45f }, 0.45f, 0.28f, true);
        }
    } else {
        auraTimer_ = 0.0f;
    }

    // 覚醒発動の瞬間（衝撃波バースト）
    if (player_->JustAwakened()) {
        pm_->EmitRing("awaken_aura", ppos, 5.0f, { 1.0f, 0.85f, 0.15f, 1.0f }, 24, 0.5f, 0.5f);
        pm_->EmitRing("awaken_aura", ppos, 2.5f, { 1.0f, 1.0f, 0.9f, 1.0f }, 16, 0.35f, 0.3f);
        pm_->EmitHitStar("awaken_aura", ppos, { 1.0f, 0.9f, 0.3f, 1.0f });
        tm->RequestHitStop(4);
        cameraShaker_.Request(0.18f, 0.15f);
    }
}

void GamePlayScene::EmitStyleRankUpParticles(const Vector3& ppos)
{
    // ランク閾値をここで別管理すると表示側(styleRankHud_)とズレるため、判定はHUD自身に問い合わせる
    if (!styleRankHud_.JustRankedUp()) {
        return;
    }
    constexpr float kRankUpRingSpeed = 3.6f;
    constexpr int kRankUpRingCount = 20;
    constexpr float kRankUpRingLifetime = 0.4f;
    constexpr float kRankUpRingScale = 0.28f;
    constexpr float kRankUpShakeAmount = 0.14f;
    constexpr float kRankUpShakeSeconds = 0.14f;
    constexpr float kRankUpFlashAlpha = 0.16f;
    constexpr float kRankUpFlashSeconds = 0.12f;

    const Vector4 rankColor = styleRankHud_.GetRankColor();
    pm_->EmitRing("hit_ring", ppos, kRankUpRingSpeed, rankColor, kRankUpRingCount, kRankUpRingLifetime, kRankUpRingScale);
    pm_->EmitHitStar("hit_spark", ppos, rankColor);
    cameraShaker_.Request(kRankUpShakeAmount, kRankUpShakeSeconds);
    ScreenFlash::GetInstance()->Request(
        { rankColor.x, rankColor.y, rankColor.z, kRankUpFlashAlpha }, kRankUpFlashSeconds);
}

void GamePlayScene::UpdateWeaponTrail()
{
    // 近接コンボのモーション中、装備武器の手元に色付きの残像を撒いて振りの軌跡を見せる
    const auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon() || !player_->IsMeleeAttacking()) {
        return;
    }
    constexpr float kWeaponTrailScale = 0.32f;
    constexpr float kWeaponTrailLifetime = 0.16f;

    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 color = { weapon.effectColor[0], weapon.effectColor[1],
        weapon.effectColor[2], weapon.effectColor[3] };
    pm_->EmitTrail("weapon_trail", player_->GetActiveWeaponWorldPosition(), color,
        kWeaponTrailScale, kWeaponTrailLifetime);
}

// ══════════════════════════════════════════════════════
// フィニッシャーとクリア判定
