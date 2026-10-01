/**
 * @file GamePlaySceneEnemyAttacks.cpp
 * @brief 敵・ギミックの更新とプレイヤーへの被弾処理
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

// 被弾・接触時に飛び散る火花（プレイヤー被弾・接触ヒットで共通）
constexpr float kHitSparkSpreadX = 3.0f;
constexpr float kHitSparkRiseMin = 2.0f;
constexpr float kHitSparkRiseMax = 5.5f;
constexpr int kHitSparkCount = 8;
constexpr float kHitSparkLifetime = 0.7f;
constexpr float kHitSparkSize = 0.15f;
constexpr float kHitRingSpeed = 4.0f;
constexpr int kHitRingCount = 16;
constexpr float kHitRingLifetime = 0.3f;
constexpr float kHitRingSize = 0.2f;
constexpr Vector4 kPlayerHitSparkColor = { 1.0f, 0.15f, 0.15f, 1.0f };
}

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
                { enemyPos.x - Tune().enemyHitBoxHalfExtent, enemyPos.y - Tune().enemyHitBoxHalfExtent, -kStageHalfDepth },
                { enemyPos.x + Tune().enemyHitBoxHalfExtent, enemyPos.y + Tune().enemyHitBoxHalfExtent, kStageHalfDepth }
            };

            bool hit = false;
            if (wm->HasEquippedWeapon() && player_->JustComboHit()) {
                const AABB range = SceneShared::MakeDirectionalShotRange(
                    playerPos, player_->GetLastDirX(), wm->GetCurrent().range,
                    wm->GetCurrent().range * GameConstants::kSkillRearReachMult);
                AABB airFriendlyRange = range;
                if (!player_->IsOnGround()) {
                    // 空中では上下と横を少し広げ、高さが合わずに空振りするのを減らす
                    constexpr float kAirRangeHalfHeight = 1.25f;
                    constexpr float kAirRangeWidthPadding = 0.35f;
                    airFriendlyRange.min.y = playerPos.y - kAirRangeHalfHeight;
                    airFriendlyRange.max.y = playerPos.y + kAirRangeHalfHeight;
                    airFriendlyRange.min.x -= kAirRangeWidthPadding;
                    airFriendlyRange.max.x += kAirRangeWidthPadding;
                }
                hit = Collision::CheckCollision(airFriendlyRange, enemyBounds);
            }
            if (!hit && player_->JustFired()) {
                const AABB range = SceneShared::MakeDirectionalRange(
                    playerPos, player_->GetLastDirX(), wm->GetRanged().range, Tune().gunBackRange);
                hit = Collision::CheckCollision(range, enemyBounds);
            }
            if (!hit && (player_->JustSwordDash() || player_->JustSpearRetreat() || player_->JustDaggerStingerHit() || player_->JustGreatswordSlam() || player_->JustSpinShot() || player_->JustScytheSpin() || player_->JustAxeCharge())) {
                const float skillRadius = SkillRadiusFor(Tune().weaponEnemySkillRadius, player_->JustGreatswordSlam());
                const Vector3 skillCenter = player_->JustSwordDash() ? player_->GetSwordSkillImpactPosition() : playerPos;
                const AABB range = {
                    { skillCenter.x - skillRadius, skillCenter.y - Tune().skillRangeHalfHeight, -kStageHalfDepth },
                    { skillCenter.x + skillRadius, skillCenter.y + Tune().skillRangeHalfHeight, kStageHalfDepth }
                };
                hit = Collision::CheckCollision(range, enemyBounds);
            }
            if (hit) {
                const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
                const float damageMult = attack != nullptr ? attack->damageMult : 1.0f;
                constexpr float kUnarmedBaseDamage = 20.0f;
                const float baseDamage = wm->HasEquippedWeapon() ? wm->GetCurrent().damage : kUnarmedBaseDamage;
                constexpr int kSlamBaseDamage = 3;
                const int slamBonus = (player_->JustGreatswordSlam() && HasBossSlamTechnique())
                    ? GameRules::GetInstance()->Get().bossTechniqueBonusDamage
                    : 0;
                const float rawDamage = player_->JustGreatswordSlam()
                    ? static_cast<float>(kSlamBaseDamage + slamBonus)
                    : baseDamage * damageMult / Tune().meleeDamageDivisor;
                const int damage = (std::max)(1, static_cast<int>(std::round(rawDamage * CurrentDamageMult())));
                constexpr float kGameplayKnockbackScale = 0.72f;
                const float knockbackMult = (wm->HasEquippedWeapon()
                                                ? wm->GetCurrent().knockbackMult
                                                : 1.0f)
                    * player_->GetAwakenedKnockbackMult() * kGameplayKnockbackScale;
                entry.enemy->TakeDamage(damage);
                constexpr float kDefaultKnockY = 0.05f;
                const float knockY = (attack != nullptr ? attack->knockY : kDefaultKnockY) * knockbackMult;
                entry.enemy->ApplyComboReaction(player_->GetLastDirX() * knockbackMult, knockY,
                    player_->JustWeaponSwitchHit(), playerPos.x);
                if (wm->HasEquippedWeapon() && wm->GetCurrent().type == WeaponType::Dagger) {
                    constexpr float kDaggerSlowSeconds = 0.8f;
                    entry.enemy->ApplySlow(kDaggerSlowSeconds);
                }
                constexpr Vector4 kUnarmedHitColor = { 1.0f, 0.8f, 0.25f, 1.0f };
                const Vector4 hitColor = wm->HasEquippedWeapon()
                    ? Vector4 { wm->GetCurrent().effectColor[0], wm->GetCurrent().effectColor[1],
                          wm->GetCurrent().effectColor[2], wm->GetCurrent().effectColor[3] }
                    : kUnarmedHitColor;
                const int extraBurstCount = wm->HasEquippedWeapon() ? wm->GetCurrent().effectBurstCount : 0;
                const float ringRadius = wm->HasEquippedWeapon() ? wm->GetCurrent().effectRingRadius : 0.0f;
                EmitEnemyHitEffect(enemyPos, hitColor, 1.0f, extraBurstCount, ringRadius);

                // 段ごとのhitStop（MeleeCombo.cppで武器・段別に調整済み）をヒットストップとカメラ揺れへ反映する
                constexpr float kHitStopShakeScale = 0.028f;
                constexpr float kHitStopShakeDurationBase = 0.06f;
                constexpr float kHitStopShakeDurationScale = 0.01f;
                constexpr int kDefaultHitStopFrames = 3;
                const int hitStopFrames = attack != nullptr
                    ? (attack->launcher ? GameConstants::kHitStopLaunch : attack->hitStop)
                    : kDefaultHitStopFrames;
                TimeManager::GetInstance()->RequestHitStop(hitStopFrames);
                cameraShaker_.Request(kHitStopShakeScale * static_cast<float>(hitStopFrames),
                    kHitStopShakeDurationBase + kHitStopShakeDurationScale * static_cast<float>(hitStopFrames));
            }
        }

        if (!entry.weaponAcquired && entry.enemy->IsDefeated()) {
            const Vector3 enemyPos = entry.enemy->GetPosition();
            const float dx = playerPos.x - enemyPos.x;
            const float dy = playerPos.y - enemyPos.y;
            constexpr float kAbsorbRange = 2.0f;
            constexpr float kAbsorbDuration = 0.5f;
            constexpr float kAbsorbRingSpeed = 3.0f;
            constexpr Vector4 kAbsorbRingColor = { 0.5f, 0.9f, 1.0f, 1.0f };
            constexpr int kAbsorbRingCount = 18;
            constexpr float kAbsorbRingLifetime = 0.4f;
            constexpr float kAbsorbRingSize = 0.3f;
            constexpr Vector4 kAbsorbFlashColor = { 0.6f, 0.9f, 1.0f, 0.35f };
            constexpr float kAbsorbFlashSeconds = 0.12f;
            constexpr float kAbsorbPullRate = 0.16f; // 1フレームでプレイヤーへ寄せる割合
            constexpr float kAbsorbTargetHeight = 0.5f;
            if (!entry.absorbing && dx * dx + dy * dy <= kAbsorbRange * kAbsorbRange
                && input_->TriggerKey(DIK_J)) {
                entry.absorbing = true;
                entry.absorbTimer = kAbsorbDuration;
                player_->PlayStealStab();
                pm_->EmitRing("weapon_orb", enemyPos, kAbsorbRingSpeed,
                    kAbsorbRingColor, kAbsorbRingCount, kAbsorbRingLifetime, kAbsorbRingSize);
                ScreenFlash::GetInstance()->Request(kAbsorbFlashColor, kAbsorbFlashSeconds);
            }
            if (entry.absorbing) {
                entry.absorbTimer -= GameConstants::kFrameDeltaTime;
                Vector3& absorbPos = entry.enemy->GetPositionRef();
                absorbPos.x += (playerPos.x - absorbPos.x) * kAbsorbPullRate;
                absorbPos.y += (playerPos.y + kAbsorbTargetHeight - absorbPos.y) * kAbsorbPullRate;
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
    constexpr float kBreakableSparkSpreadX = 5.0f;
    constexpr float kBreakableSparkRiseMin = 2.5f;
    constexpr float kBreakableSparkRiseMax = 7.0f;
    constexpr float kBreakableSparkLifetime = 0.9f;
    constexpr float kBreakableSparkSize = 0.2f;
    constexpr Vector4 kBreakableRingColor = { 1.0f, 0.55f, 0.15f, 1.0f };
    constexpr Vector4 kBreakableSparkColor = { 1.0f, 0.4f, 0.1f, 1.0f };
    constexpr Vector4 kDeflectStarColor = { 0.6f, 0.6f, 0.7f, 1.0f };
    constexpr Vector4 kDamageStarColor = { 1.0f, 0.6f, 0.15f, 1.0f };

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
            const Vector3 skillCenter = player_->JustSwordDash() ? player_->GetSwordSkillImpactPosition() : playerPos;
            const AABB range = {
                { skillCenter.x - Tune().weaponEnemySkillRadius, skillCenter.y - Tune().skillRangeHalfHeight, -kBreakableHalfDepth },
                { skillCenter.x + Tune().weaponEnemySkillRadius, skillCenter.y + Tune().skillRangeHalfHeight, kBreakableHalfDepth }
            };
            hit = Collision::CheckCollision(range, barrelBounds);
        }

        // 武器指定つき（壊せる壁など）は、その武器の近接攻撃・固有技でしか壊れない。銃では壊れない
        if (hit && !desc.breakableWeapon.empty()) {
            const bool weaponMatches = wm->HasEquippedWeapon()
                && wm->GetCurrent().type == ParseWeaponTypeName(desc.breakableWeapon);
            const bool meleeOrSkillHit = player_->JustComboHit() || !player_->JustFired();
            if (!weaponMatches || !meleeOrSkillHit) {
                pm_->EmitHitStar("hit_spark", barrel.position, kDeflectStarColor); // 弾かれた手応え
                hit = false;
            }
        }
        if (!hit) {
            continue;
        }

        (*barrel.hp)--;
        pm_->EmitHitStar("hit_spark", barrel.position, kDamageStarColor);
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
            kBreakableRingColor, kBreakableRingCount, kBreakableRingLifetime, kBreakableRingSize);
        std::uniform_real_distribution<float> vxB(-kBreakableSparkSpreadX, kBreakableSparkSpreadX);
        std::uniform_real_distribution<float> vyB(kBreakableSparkRiseMin, kBreakableSparkRiseMax);
        for (int i = 0; i < kBreakableSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", barrel.position,
                { vxB(rng_), vyB(rng_), 0.0f },
                kBreakableSparkColor, kBreakableSparkLifetime, kBreakableSparkSize);
        }
    }
}

void GamePlayScene::UpdatePlayerEnemyContactHit(float dt)
{
    constexpr float kContactHitCooldown = 0.5f;
    constexpr int kContactHitStopFrames = 5;
    constexpr float kContactShakeAmount = 0.18f;
    constexpr float kContactShakeSeconds = 0.15f;
    constexpr Vector4 kContactRingColor = { 1.0f, 0.85f, 0.2f, 1.0f };
    constexpr Vector4 kContactSparkColor = { 1.0f, 0.55f, 0.1f, 1.0f };

    auto* tm = TimeManager::GetInstance();
    const Vector3& ppos = player_->GetPosition();

    // 敵との当たり判定
    hitCooldown_ -= dt;
    {
        Collider playerCol = player_->GetCollider();
        const Vector3& epos = enemy_->GetPosition();
        AABB enemyAABB = { { epos.x - Tune().enemyHitBoxHalfExtent, epos.y - Tune().enemyHitBoxHalfExtent, -kStageHalfDepth },
            { epos.x + Tune().enemyHitBoxHalfExtent, epos.y + Tune().enemyHitBoxHalfExtent, kStageHalfDepth } };
        if (Collision::CheckCollision(playerCol.aabb, enemyAABB) && hitCooldown_ <= 0.0f) {
            hitCooldown_ = kContactHitCooldown;
            enemy_->TakeDamage(1);
            tm->RequestHitStop(kContactHitStopFrames);
            cameraShaker_.Request(kContactShakeAmount, kContactShakeSeconds);

            Vector3 hitPos = { (ppos.x + epos.x) * 0.5f,
                (ppos.y + epos.y) * 0.5f, 0.0f };
            pm_->EmitRing("hit_ring", hitPos, kHitRingSpeed, kContactRingColor, kHitRingCount, kHitRingLifetime, kHitRingSize);
            std::uniform_real_distribution<float> vxD(-kHitSparkSpreadX, kHitSparkSpreadX);
            std::uniform_real_distribution<float> vyD(kHitSparkRiseMin, kHitSparkRiseMax);
            for (int i = 0; i < kHitSparkCount; ++i) {
                pm_->EmitGravity("hit_spark", hitPos,
                    { vxD(rng_), vyD(rng_), 0.0f },
                    kContactSparkColor, kHitSparkLifetime, kHitSparkSize);
            }
        }
    }
}

void GamePlayScene::UpdateEnemyAttackOnPlayer(float dt)
{
    constexpr float kMinAimLength = 0.001f; // これ未満の距離では狙いの方向を正規化しない
    constexpr float kMuzzleRingSpeed = 1.2f;
    constexpr Vector4 kMuzzleRingColor = { 1.0f, 0.35f, 0.25f, 0.8f };
    constexpr int kMuzzleRingCount = 8;
    constexpr float kMuzzleRingLifetime = 0.15f;
    constexpr float kMuzzleRingSize = 0.1f;
    constexpr Vector4 kTracerColor = { 1.0f, 0.3f, 0.2f, 1.0f };
    constexpr float kTracerLifetime = 0.12f;
    constexpr float kTracerSize = 0.28f;
    constexpr float kBulletHalfExtent = 0.15f;
    constexpr int kBulletHitStopFrames = 7;
    constexpr float kBulletShakeAmount = 0.22f;
    constexpr float kBulletShakeSeconds = 0.18f;
    constexpr Vector4 kBulletHitRingColor = { 1.0f, 0.2f, 0.2f, 1.0f };

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
        if (len > kMinAimLength) {
            dir.x /= len;
            dir.y /= len;
        }
        EnemyBullet bullet;
        bullet.pos = epos;
        bullet.vel = { dir.x * EnemyTuning::GetInstance()->Bullet().speed, dir.y * EnemyTuning::GetInstance()->Bullet().speed, 0.0f };
        bullet.timer = EnemyTuning::GetInstance()->Bullet().lifetime;
        bullet.damage = shooter->GetAttackDamage();
        enemyBullets_.push_back(bullet);
        pm_->EmitRing("hit_ring", epos, kMuzzleRingSpeed, kMuzzleRingColor, kMuzzleRingCount, kMuzzleRingLifetime, kMuzzleRingSize);
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
            kTracerColor, kTracerLifetime, kTracerSize);

        if (bullet.timer <= 0.0f) {
            it = enemyBullets_.erase(it);
            continue;
        }

        AABB bulletAABB = { { bullet.pos.x - kBulletHalfExtent, bullet.pos.y - kBulletHalfExtent, -kStageHalfDepth },
            { bullet.pos.x + kBulletHalfExtent, bullet.pos.y + kBulletHalfExtent, kStageHalfDepth } };
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
        tm->RequestHitStop(kBulletHitStopFrames);
        cameraShaker_.Request(kBulletShakeAmount, kBulletShakeSeconds);

        pm_->EmitRing("hit_ring", bullet.pos, kHitRingSpeed, kBulletHitRingColor, kHitRingCount, kHitRingLifetime, kHitRingSize);
        std::uniform_real_distribution<float> vxD(-kHitSparkSpreadX, kHitSparkSpreadX);
        std::uniform_real_distribution<float> vyD(kHitSparkRiseMin, kHitSparkRiseMax);
        for (int i = 0; i < kHitSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", bullet.pos,
                { vxD(rng_), vyD(rng_), 0.0f },
                kPlayerHitSparkColor, kHitSparkLifetime, kHitSparkSize);
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
    constexpr float kSwingCenterHeight = 0.4f;

    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    const Vector3& epos = attacker->GetPosition();
    const Vector3& ppos = player_->GetPosition();
    const float dirX = ppos.x >= epos.x ? 1.0f : -1.0f;

    // 振った軌跡を見せる（当たらなくても振ったことが分かるように）
    const Vector3 swingCenter = { epos.x + dirX * tuning.meleeReach * 0.5f, epos.y + kSwingCenterHeight, 0.0f };
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
    std::uniform_real_distribution<float> vxD(-kHitSparkSpreadX, kHitSparkSpreadX);
    std::uniform_real_distribution<float> vyD(kHitSparkRiseMin, kHitSparkRiseMax);
    for (int i = 0; i < kSwingSparkCount; ++i) {
        pm_->EmitGravity("hit_spark", ppos, { vxD(rng_), vyD(rng_), 0.0f }, kPlayerHitSparkColor, kHitSparkLifetime, kHitSparkSize);
    }
}

void GamePlayScene::UpdateBossSlamAttack(float dt)
{
    // 予告円
    constexpr float kWarningMinScale = 0.6f; // 予告開始時の円の大きさ（着弾範囲比）。進行に応じて1.0まで広げる
    constexpr float kWarningBlinkSpeed = 22.0f; // 素早く点滅させて視線を引く
    constexpr float kWarningGreenBase = 0.15f;
    constexpr float kWarningGreenGrowth = 0.1f;
    constexpr float kWarningBlue = 0.1f;
    constexpr float kWarningAlphaBase = 0.35f;
    constexpr float kWarningAlphaBlink = 0.4f;
    constexpr float kWarningRingGreenBase = 0.3f;
    constexpr float kWarningRingGreenFade = 0.15f;
    constexpr float kWarningRingBlueBase = 0.15f;
    constexpr float kWarningRingBlueFade = 0.1f;
    constexpr int kWarningRingCount = 6;
    constexpr float kWarningRingLifetime = 0.15f;
    constexpr float kWarningRingSize = 0.06f;
    // 着弾
    constexpr int kSlamHitStopFrames = 9;
    constexpr float kSlamShakeAmount = 0.3f;
    constexpr float kSlamShakeSeconds = 0.22f;
    constexpr Vector4 kSlamInnerRingColor = { 1.0f, 0.6f, 0.2f, 1.0f };
    constexpr int kSlamInnerRingCount = 32;
    constexpr float kSlamInnerRingLifetime = 0.55f;
    constexpr float kSlamInnerRingSize = 0.34f;
    constexpr float kSlamOuterRingScale = 1.4f;
    constexpr Vector4 kSlamOuterRingColor = { 1.0f, 0.85f, 0.4f, 0.6f };
    constexpr int kSlamOuterRingCount = 20;
    constexpr float kSlamOuterRingLifetime = 0.4f;
    constexpr float kSlamOuterRingSize = 0.24f;
    constexpr float kSlamSparkSpreadX = 4.0f;
    constexpr float kSlamSparkRiseMin = 2.5f;
    constexpr float kSlamSparkRiseMax = 6.0f;
    constexpr int kSlamSparkCount = 20;
    constexpr Vector4 kSlamSparkColor = { 1.0f, 0.45f, 0.1f, 1.0f };
    constexpr float kSlamSparkLifetime = 0.85f;
    constexpr float kSlamSparkSize = 0.18f;

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
    const float diameterPx = EnemyTuning::GetInstance()->BossSlam().radius * 2.0f * pxPerWorldUnit
        * (kWarningMinScale + (1.0f - kWarningMinScale) * progress);
    const float blink = 0.5f + 0.5f * std::sin(bossSlamWarningTimer_ * kWarningBlinkSpeed);
    bossSlamWarningSprite_->SetColor({ 1.0f, kWarningGreenBase + progress * kWarningGreenGrowth, kWarningBlue,
        kWarningAlphaBase + blink * kWarningAlphaBlink });
    bossSlamWarningSprite_->SetPosition({ sx - diameterPx * 0.5f, sy - diameterPx * 0.5f });
    bossSlamWarningSprite_->SetSize({ diameterPx, diameterPx });
    bossSlamWarningSprite_->Update();

    pm_->EmitRing("hit_ring", bossSlamTargetPos_, EnemyTuning::GetInstance()->BossSlam().radius,
        { 1.0f, kWarningRingGreenBase - progress * kWarningRingGreenFade, kWarningRingBlueBase - progress * kWarningRingBlueFade, 0.5f },
        kWarningRingCount, kWarningRingLifetime, kWarningRingSize);

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
        tm->RequestHitStop(kSlamHitStopFrames);
        cameraShaker_.Request(kSlamShakeAmount, kSlamShakeSeconds);
    }

    pm_->EmitRing("hit_ring", bossSlamTargetPos_, EnemyTuning::GetInstance()->BossSlam().radius,
        kSlamInnerRingColor, kSlamInnerRingCount, kSlamInnerRingLifetime, kSlamInnerRingSize);
    pm_->EmitRing("hit_ring", bossSlamTargetPos_, EnemyTuning::GetInstance()->BossSlam().radius * kSlamOuterRingScale,
        kSlamOuterRingColor, kSlamOuterRingCount, kSlamOuterRingLifetime, kSlamOuterRingSize);
    std::uniform_real_distribution<float> vxS(-kSlamSparkSpreadX, kSlamSparkSpreadX);
    std::uniform_real_distribution<float> vyS(kSlamSparkRiseMin, kSlamSparkRiseMax);
    for (int i = 0; i < kSlamSparkCount; ++i) {
        pm_->EmitGravity("hit_spark", bossSlamTargetPos_,
            { vxS(rng_), vyS(rng_), 0.0f },
            kSlamSparkColor, kSlamSparkLifetime, kSlamSparkSize);
    }
}
