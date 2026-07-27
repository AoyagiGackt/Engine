/**
 * @file GamePlaySceneCombat.cpp
 * @brief GamePlaySceneの戦闘関連更新処理（近接/銃コンボ・武器付き敵・ギミック・被弾・スタイル演出）を実装するファイル
 * @note GamePlayScene.cppからの分割ファイルクラス自体はGamePlaySceneのまま、定義の置き場所だけを分けている
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "GameConstants.h"
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
#include <cmath>
#include <cstring>
#include <random>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// スタイルゲージ関連の調整値
static constexpr float kMeleeDamageDivisor = 25.0f; ///< 武器ダメージ×倍率を敵HPスケールへ落とし込む除数
static constexpr float kStyleDecayRate = 0.12f; ///< スタイルゲージの毎秒減衰量（StylePersist未所持時）

// UpdateStyleAndUI: 近接コンボのヒット判定・スタイル加点調整値
static constexpr float kEnemyHitBoxHalfExtent = 0.5f; ///< 敵の当たり判定AABBの半径（X/Y共通）
static constexpr float kMeleeRepeatPenaltyPerHit = 0.035f; ///< 同じ技を連続ヒットさせるたびに加算される減点係数
static constexpr float kMeleeRepeatPenaltyCap = 0.10f; ///< 連続技ペナルティの上限
static constexpr float kMeleeWeaponSwitchBonus = 0.18f; ///< 武器切替直後のコンボヒットに乗るボーナス
static constexpr float kMeleeBaseStyleGain = 0.10f; ///< 近接ヒット1回あたりの基礎スタイル加点
static constexpr float kMeleeComboStepStyleGain = 0.04f; ///< コンボ段数1につき加算されるスタイル加点
static constexpr float kMeleeAwakenGaugeGain = 0.08f; ///< 近接ヒットで溜まる覚醒ゲージ量
static constexpr float kChainSkillRange = 5.0f; ///< 雷属性(Spear)の周囲武器敵への連鎖判定距離

// UpdateStyleAndUI: 武器固有技（SPACE）のスタイル加点調整値
static constexpr float kSkillSlamRadius = 3.5f; ///< 設置型AoE技(大剣叩きつけ等)の判定半径
static constexpr float kSkillDefaultRadius = 2.8f; ///< 通常の固有技の判定半径
static constexpr float kSkillRangeHalfHeight = 2.0f; ///< 固有技判定AABBの縦方向半径
static constexpr float kSkillVarietyBonusRepeat = 0.06f; ///< 同じ固有技を連続で当てた場合のスタイル加点
static constexpr float kSkillVarietyBonusFresh = 0.18f; ///< 直前と違う固有技を当てた場合のスタイル加点
static constexpr float kSkillAwakenGaugeGain = 0.10f; ///< 固有技ヒットで溜まる覚醒ゲージ量

// UpdateStyleAndUI: 銃コンボのスタイル加点調整値
static constexpr float kGunBackRange = 0.8f; ///< 銃口とは逆方向の判定の奥行き（銃身ぶんの余裕）
static constexpr float kGunBaseStyleGain = 0.04f; ///< 銃ヒット1回あたりの基礎スタイル加点
static constexpr float kGunComboStepStyleGain = 0.01f; ///< 銃コンボ段数1につき加算されるスタイル加点
static constexpr float kGunAwakenGaugeGain = 0.04f; ///< 銃ヒットで溜まる覚醒ゲージ量

// UpdateStyleAndUI: 瞬歩(ブリンク)・覚醒乱舞のスタイル加点調整値
static constexpr float kStingerStyleGain = 0.10f; ///< ダガー スティンガー刺突ごとのスタイル加点
static constexpr float kRampageRushRadiusX = 2.5f; ///< 覚醒乱舞ラッシュの判定半径(横)
static constexpr float kRampageRushRadiusY = 1.5f; ///< 覚醒乱舞ラッシュの判定半径(縦)
static constexpr float kRampageBaseStyleGain = 0.10f; ///< 覚醒乱舞ヒットの基礎スタイル加点
static constexpr float kRampageJuggleStyleGain = 0.02f; ///< 乱舞の連続ジャグル回数1につき加算されるスタイル加点

void GamePlayScene::UpdateWeaponEnemies()
{
    const Vector3& playerPos = player_->GetPosition();
    const auto* wm = WeaponManager::GetInstance();

    for (auto& entry : weaponEnemies_) {
        entry.enemy->Update();
        if (!entry.enemy->IsDefeated()) {
            const Vector3& enemyPos = entry.enemy->GetPosition();
            const AABB enemyBounds = {
                { enemyPos.x - 0.5f, enemyPos.y - 0.5f, -0.5f },
                { enemyPos.x + 0.5f, enemyPos.y + 0.5f, 0.5f }
            };

            bool hit = false;
            if (wm->HasEquippedWeapon() && player_->JustComboHit()) {
                const AABB range = SceneShared::MakeDirectionalShotRange(
                    playerPos, player_->GetLastDirX(), wm->GetCurrent().range,
                    wm->GetCurrent().range * 0.4f);
                hit = Collision::CheckCollision(range, enemyBounds);
            }
            if (!hit && player_->JustFired()) {
                const AABB range = SceneShared::MakeDirectionalRange(
                    playerPos, player_->GetLastDirX(), wm->GetRanged().range, 0.8f);
                hit = Collision::CheckCollision(range, enemyBounds);
            }
            if (!hit && (player_->JustSwordDash() || player_->JustSpearRetreat() || player_->JustDaggerStingerHit() || player_->JustGreatswordSlam() || player_->JustSpinShot() || player_->JustScytheSpin() || player_->JustAxeCharge())) {
                const AABB range = {
                    { playerPos.x - 3.0f, playerPos.y - 2.0f, -0.5f },
                    { playerPos.x + 3.0f, playerPos.y + 2.0f, 0.5f }
                };
                hit = Collision::CheckCollision(range, enemyBounds);
            }
            if (hit) {
                const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
                const float damageMult = attack != nullptr ? attack->damageMult : 1.0f;
                const float baseDamage = wm->HasEquippedWeapon() ? wm->GetCurrent().damage : 20.0f;
                const int damage = player_->JustGreatswordSlam()
                    ? 3
                    : (std::max)(1, static_cast<int>(std::round(baseDamage * damageMult / 25.0f)));
                const float knockbackMult = wm->HasEquippedWeapon()
                    ? wm->GetCurrent().knockbackMult
                    : 1.0f;
                entry.enemy->TakeDamage(damage);
                const float knockY = (attack != nullptr ? attack->knockY : 0.05f) * knockbackMult;
                entry.enemy->ApplyComboReaction(player_->GetLastDirX() * knockbackMult, knockY,
                    player_->JustWeaponSwitchHit(), playerPos.x);
                if (wm->HasEquippedWeapon() && wm->GetCurrent().type == WeaponType::Dagger) {
                    entry.enemy->ApplySlow(0.8f);
                }
                pm_->EmitHitStar("hit_spark", enemyPos, { 1.0f, 0.8f, 0.25f, 1.0f });
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
                    WeaponManager::GetInstance()->Acquire(entry.weaponType);
                }
            }
        }
    }
}

void GamePlayScene::UpdateWeaponGimmicks()
{
    const Vector3& pos = player_->GetPosition();
    const float gatePulse = 0.72f + 0.28f * std::sin(energyCorePulse_ * 6.0f);
    if (swordGateActive_) {
        swordGate_->SetColor({ 1.0f * gatePulse, 0.10f * gatePulse, 0.03f, 1.0f });
        swordGate_->Update();
    }
    if (spearGateActive_) {
        spearGate_->SetColor({ 0.04f, 0.45f * gatePulse, 1.0f * gatePulse, 1.0f });
        spearGate_->Update();
    }
    if (swordGateActive_ && player_->JustSwordDash() && std::abs(pos.x - 17.5f) <= 3.0f) {
        swordGateActive_ = false;
        pm_->EmitRing("sword_slash", { 17.5f, 1.5f, 0.0f }, 4.0f,
            { 1.0f, 0.3f, 0.15f, 1.0f }, 20, 0.5f, 0.35f);
        cameraShaker_.Request(0.18f, 0.15f);
    }
    if (spearGateActive_ && player_->JustSpearRetreat() && std::abs(pos.x - 25.5f) <= 3.0f) {
        spearGateActive_ = false;
        pm_->EmitRing("hit_ring", { 25.5f, 1.5f, 0.0f }, 4.0f,
            { 0.25f, 0.75f, 1.0f, 1.0f }, 20, 0.5f, 0.35f);
        cameraShaker_.Request(0.18f, 0.15f);
    }
}

void GamePlayScene::UpdateEnergyCores()
{
    energyCorePulse_ += GameConstants::kFrameDeltaTime;
    const Vector3& playerPos = player_->GetPosition();
    for (auto& core : energyCores_) {
        if (core.collected) {
            continue;
        }

        const float dx = playerPos.x - core.position.x;
        const float dy = playerPos.y - core.position.y;
        if (dx * dx + dy * dy <= 1.0f) {
            core.collected = true;
            collectedEnergyCores_++;
            player_->ChargeAwakenGauge(0.2f);
            pm_->EmitRing("awaken_aura", core.position, 3.0f,
                { 0.3f, 0.95f, 1.0f, 1.0f }, 18, 0.45f, 0.3f);
            ScreenFlash::GetInstance()->Request(
                { 0.4f, 0.9f, 1.0f, 0.2f }, 0.08f);
            continue;
        }

        const float pulse = 0.85f + std::sin(energyCorePulse_ * 5.0f) * 0.15f;
        core.object->SetRotation({ 0.0f, energyCorePulse_ * 1.5f, 0.0f });
        core.object->SetScale({ 0.35f * pulse, 0.35f * pulse, 0.35f * pulse });
        core.object->SetColor({ 0.3f, 0.9f * pulse, 1.0f, 1.0f });
        core.object->Update();
    }
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
    const float cameraX = cameraMinX <= cameraMaxX
        ? std::clamp(ppos.x, cameraMinX, cameraMaxX)
        : (stageLeft + stageRight) * 0.5f;
    cameraTargetPos_ = {
        cameraX,
        ppos.y + 3.0f,
        -24.0f
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

void GamePlayScene::UpdateStyleAndUI(float dt)
{
    if (dt <= 0.0f) {
        UpdateWeaponSlotHud();
        DrawStyleUI();
        return;
    }

    UpdateWeaponSlotHud();

    const auto* wm = WeaponManager::GetInstance();
    const Vector3& ppos = player_->GetPosition();
    const Vector3& epos = enemy_->GetPosition();
    AABB enemyAABB = { { epos.x - kEnemyHitBoxHalfExtent, epos.y - kEnemyHitBoxHalfExtent, -0.5f },
        { epos.x + kEnemyHitBoxHalfExtent, epos.y + kEnemyHitBoxHalfExtent, 0.5f } };

    if (wm->HasEquippedWeapon() && player_->JustComboHit()) {
        // 前方に厚く、背後は振り抜きぶんだけ（左右対称だと背後の遠い敵にまで当たってしまう）
        AABB meleeRange = SceneShared::MakeDirectionalRange(
            ppos, player_->GetLastDirX(), wm->GetCurrent().range,
            wm->GetCurrent().range * GameConstants::kSkillRearReachMult);
        if (Collision::CheckCollision(meleeRange, enemyAABB)) {
            const int techniqueId = static_cast<int>(wm->GetCurrent().type) * 16
                + player_->GetComboStep();
            if (techniqueId == lastTechniqueId_) {
                repeatedTechniqueCount_++;
            } else {
                lastTechniqueId_ = techniqueId;
                repeatedTechniqueCount_ = 0;
            }
            const float repeatPenalty = (std::min)(repeatedTechniqueCount_ * kMeleeRepeatPenaltyPerHit, kMeleeRepeatPenaltyCap);
            const float switchBonus = player_->JustWeaponSwitchHit() ? kMeleeWeaponSwitchBonus : 0.0f;
            styleMeter_ = std::clamp(styleMeter_ + kMeleeBaseStyleGain
                    + player_->GetComboStep() * kMeleeComboStepStyleGain + switchBonus - repeatPenalty,
                0.0f, 1.0f);
            player_->ChargeAwakenGauge(kMeleeAwakenGaugeGain);
            const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
            const WeaponData& weapon = wm->GetCurrent();
            const float damageMult = attack != nullptr ? attack->damageMult : 1.0f;
            const int damage = (std::max)(1,
                static_cast<int>(std::round(weapon.damage * damageMult / kMeleeDamageDivisor)));
            enemy_->TakeDamage(damage);
            enemy_->ApplyComboReaction(player_->GetLastDirX() * weapon.knockbackMult,
                (attack != nullptr ? attack->knockY : 0.05f) * weapon.knockbackMult,
                player_->JustWeaponSwitchHit(), ppos.x);

            const WeaponType element = wm->GetCurrent().type;
            if (element == WeaponType::Sword && player_->GetComboStep() >= 3) {
                enemy_->TakeDamage(1); // 炎: コンボ後半で追加ダメージ
            } else if (element == WeaponType::Dagger) {
                enemy_->ApplySlow(0.9f); // 氷: 行動速度を落とす
            } else if (element == WeaponType::Spear) {
                // 雷: 周囲の武器敵へ連鎖する。
                for (auto& entry : weaponEnemies_) {
                    if (!entry.enemy->IsDefeated()
                        && std::abs(entry.enemy->GetPosition().x - enemy_->GetPosition().x) < kChainSkillRange) {
                        entry.enemy->TakeDamage(1);
                    }
                }
            }
        }
    }
    if (player_->JustSwordDash() || player_->JustSpearRetreat()
        || player_->JustDaggerStingerHit() || player_->JustGreatswordSlam()
        || player_->JustSpinShot() || player_->JustScytheSpin()
        || player_->JustAxeCharge()) {
        const float radius = player_->JustGreatswordSlam() ? kSkillSlamRadius : kSkillDefaultRadius;
        const AABB skillRange = {
            { ppos.x - radius, ppos.y - kSkillRangeHalfHeight, -0.5f },
            { ppos.x + radius, ppos.y + kSkillRangeHalfHeight, 0.5f }
        };
        if (Collision::CheckCollision(skillRange, enemyAABB)) {
            const int techniqueId = 1000 + static_cast<int>(wm->GetCurrent().type);
            const float varietyBonus = techniqueId == lastTechniqueId_ ? kSkillVarietyBonusRepeat : kSkillVarietyBonusFresh;
            lastTechniqueId_ = techniqueId;
            styleMeter_ = std::clamp(styleMeter_ + varietyBonus, 0.0f, 1.0f);
            player_->ChargeAwakenGauge(kSkillAwakenGaugeGain);
            enemy_->TakeDamage(player_->JustGreatswordSlam() ? 3 : 2);
        }
    }
    if (player_->JustFired()) {
        const GunShotDef* shot = player_->GetActiveGunShot();
        const RangedWeaponData& gun = wm->GetRanged();
        const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
        // 銃口の向きにだけ飛ぶ（背後は銃身ぶんの余裕のみ）
        AABB shotRange = SceneShared::MakeDirectionalShotRange(
            ppos, player_->GetLastDirX(), rangeX, kGunBackRange);
        if (Collision::CheckCollision(shotRange, enemyAABB)) {
            // 段が進むほどスタイルが伸びる（銃コンボを回す動機付け）
            float gain = kGunBaseStyleGain + ((shot != nullptr) ? player_->GetGunComboStep() * kGunComboStepStyleGain : 0.0f);
            styleMeter_ = std::clamp(styleMeter_ + gain, 0.0f, 1.0f);
            player_->ChargeAwakenGauge(kGunAwakenGaugeGain);
            enemy_->TakeDamage(1);
        }
    }
    if (player_->JustDaggerStingerHit()) {
        styleMeter_ = std::clamp(styleMeter_ + kStingerStyleGain, 0.0f, 1.0f);
    }
    if (player_->JustRampageHit()) {
        AABB rushRange = { { ppos.x - kRampageRushRadiusX, ppos.y - kRampageRushRadiusY, -0.5f },
            { ppos.x + kRampageRushRadiusX, ppos.y + kRampageRushRadiusY, 0.5f } };
        if (Collision::CheckCollision(rushRange, enemyAABB)) {
            // 乱舞スラッシュ 回数が増えるほど多くゲージが溜まる
            styleMeter_ = std::clamp(
                styleMeter_ + kRampageBaseStyleGain + player_->GetJuggleCount() * kRampageJuggleStyleGain, 0.0f, 1.0f);
            enemy_->TakeDamage(1);
        }
    }
    // フィニッシャースラッシュのダメージは UpdateFinisherSlash の本命ヒットで適用する

    float decayMult = RunData::GetInstance()->HasSkill(RunData::Skill::StylePersist) ? 0.6f : 1.0f;
    styleMeter_ = std::clamp(styleMeter_ - kStyleDecayRate * dt * decayMult, 0.0f, 1.0f);
    peakStyle_ = (std::max)(peakStyle_, styleMeter_);

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
    if (awakenActive && (movingX || !player_->IsOnGround())) {
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
        AABB enemyAABB = { { epos.x - 0.5f, epos.y - 0.5f, -0.5f },
            { epos.x + 0.5f, epos.y + 0.5f, 0.5f } };
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
    // 予備動作明けの瞬間 狙いを一度だけ計算し、実弾を撃ち出す
    if (enemy_->JustFiredAttack()) {
        const Vector3& epos = enemy_->GetPosition();
        const Vector3& ppos = player_->GetPosition();
        // pos_ はAABB中心（当たり判定の基準点）そのものなので、狙い・発射位置ともにオフセットを足さずここから直接計算する
        Vector3 dir = { ppos.x - epos.x, ppos.y - epos.y, 0.0f };
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 0.001f) {
            dir.x /= len;
            dir.y /= len;
        }
        enemyBulletPos_ = epos;
        enemyBulletVel_ = { dir.x * kEnemyBulletSpeed, dir.y * kEnemyBulletSpeed, 0.0f };
        enemyBulletTimer_ = kEnemyBulletLifetime;
        enemyBulletActive_ = true;
        pm_->EmitRing("hit_ring", enemyBulletPos_, 1.2f, { 1.0f, 0.35f, 0.25f, 0.8f }, 8, 0.15f, 0.1f);
    }

    if (!enemyBulletActive_) {
        return;
    }

    enemyBulletPos_.x += enemyBulletVel_.x * dt;
    enemyBulletPos_.y += enemyBulletVel_.y * dt;
    enemyBulletTimer_ -= dt;
    // 曳光弾の見た目実体（Object3d）は持たず、毎フレーム現在位置に粒を撒いて弾の軌跡に見せる
    pm_->EmitWithColor("gun_shot", enemyBulletPos_, { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.3f, 0.2f, 1.0f }, 0.12f, 0.28f);

    if (enemyBulletTimer_ <= 0.0f) {
        enemyBulletActive_ = false;
        return;
    }

    if (player_->IsInvincible()) {
        return;
    }

    AABB bulletAABB = { { enemyBulletPos_.x - 0.15f, enemyBulletPos_.y - 0.15f, -0.5f },
        { enemyBulletPos_.x + 0.15f, enemyBulletPos_.y + 0.15f, 0.5f } };
    Collider playerCol = player_->GetCollider();
    if (!Collision::CheckCollision(playerCol.aabb, bulletAABB)) {
        return;
    }

    enemyBulletActive_ = false;
    RunData::GetInstance()->TakeDamage(enemy_->GetAttackDamage());
    player_->OnHit();

    auto* tm = TimeManager::GetInstance();
    tm->RequestHitStop(7);
    cameraShaker_.Request(0.22f, 0.18f);

    pm_->EmitRing("hit_ring", enemyBulletPos_, 4.0f, { 1.0f, 0.2f, 0.2f, 1.0f }, 16, 0.3f, 0.2f);
    std::uniform_real_distribution<float> vxD(-3.0f, 3.0f);
    std::uniform_real_distribution<float> vyD(2.0f, 5.5f);
    for (int i = 0; i < 8; ++i) {
        pm_->EmitGravity("hit_spark", enemyBulletPos_,
            { vxD(rng_), vyD(rng_), 0.0f },
            { 1.0f, 0.15f, 0.15f, 1.0f }, 0.7f, 0.15f);
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

    // 属性演出はweapons.jsonの共通プリセットから生成する
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
    // スタイルランクが上がった瞬間のバースト
    // しきい値はGameConstants::kStyleRankThresholds（DrawRankAndAwakenGauge()のkStyleRanksと共通）を使う
    int styleTier = 0;
    for (int i = 0; i < GameConstants::kStyleRankCount; ++i) {
        if (styleMeter_ >= GameConstants::kStyleRankThresholds[i]) {
            styleTier = i;
        }
    }
    if (styleTier > prevStyleTier_) {
        pm_->EmitRing("hit_ring", ppos, 3.5f, { 1.0f, 0.85f, 0.2f, 1.0f }, 20, 0.4f, 0.28f);
        pm_->EmitHitStar("hit_spark", ppos, { 1.0f, 0.9f, 0.3f, 1.0f });
    }
    prevStyleTier_ = styleTier;
}

// ══════════════════════════════════════════════════════
// フィニッシャーとクリア判定
