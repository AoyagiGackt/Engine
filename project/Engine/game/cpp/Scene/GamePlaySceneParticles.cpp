/**
 * @file GamePlaySceneParticles.cpp
 * @brief 戦闘と武器のパーティクル演出
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "ElementEffect.h"
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

void GamePlayScene::UpdateStyleTechniqueParticles(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    EmitComboHitParticles(ppos);
    EmitGunFireParticles(ppos);
    EmitBlinkAndGaugeParticles(ppos);
    EmitWeaponSkillCastParticles(ppos);
    EmitAwakenParticles(ppos, dt);
    EmitStyleRankUpParticles(ppos);
}

void GamePlayScene::EmitEnemyHitEffect(const Vector3& enemyPos, const Vector4& color, float strength,
    int extraBurstCount, float ringRadius)
{
    constexpr float kSurfaceLerp = 0.3f; // 敵の中心からプレイヤー側へ寄せる割合（体の表面で弾けたように見せる）
    constexpr Vector4 kCoreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    constexpr float kCoreLifetime = 0.08f;
    constexpr float kCoreScale = 0.48f;
    constexpr float kRingSpeed = 2.4f;
    constexpr int kRingCount = 7;
    constexpr float kRingLifetime = 0.16f;
    constexpr float kRingSize = 0.09f;
    constexpr int kSparkCount = 9;
    constexpr float kSparkSpreadX = 2.5f;
    constexpr float kSparkRiseMin = 1.5f;
    constexpr float kSparkRiseMax = 4.5f;
    constexpr float kSparkLifetime = 0.32f;
    constexpr float kSparkSize = 0.075f;
    // 武器固有の追加演出（weapons.jsonのburstCount/ringRadius）
    constexpr float kExtraSparkSpreadScale = 1.4f;
    constexpr float kExtraSparkRiseScale = 1.3f;
    constexpr float kExtraRingAlphaScale = 0.55f;
    constexpr int kExtraRingBaseCount = 8;
    constexpr int kExtraRingCountDivisor = 2; // 追加バースト数の何分の1をリングの粒数へ足すか
    constexpr float kExtraRingLifetimeScale = 1.5f;
    constexpr float kExtraRingSizeScale = 1.15f;

    // 当て続けるほど盛る（途切れずに当てた手応えを命中演出の量で返す）
    constexpr float kHitEscalationKeepSeconds = 2.0f; // この秒数ヒットが無いと盛り度合いが初撃に戻る
    constexpr float kEscalationStrengthGain = 0.6f; // 上限時に大きさへ足す割合
    constexpr int kEscalationExtraSparks = 18; // 上限時に足す火花の数
    constexpr float kEscalationExtraBurstGain = 1.0f; // 上限時に武器固有の追加バーストへ足す割合
    constexpr float kEscalationOuterRingThreshold = 0.4f; // この盛り度合い以上で外側のリングを重ねる
    constexpr float kEscalationOuterRingSpeed = 4.2f;
    constexpr int kEscalationOuterRingCount = 14;
    constexpr float kEscalationOuterRingLifetime = 0.26f;
    constexpr float kEscalationOuterRingSize = 0.11f;
    constexpr float kEscalationMaxThreshold = 1.0f; // 上限に達したら星をもう1つ重ねる

    ++hitEscalationCount_;
    hitEscalationTimer_ = kHitEscalationKeepSeconds;
    const float escalation = HitEscalationRatio();
    strength *= 1.0f + kEscalationStrengthGain * escalation;
    extraBurstCount += static_cast<int>(static_cast<float>(extraBurstCount) * kEscalationExtraBurstGain * escalation);
    const int sparkCount = kSparkCount + static_cast<int>(static_cast<float>(kEscalationExtraSparks) * escalation);

    const Vector3& ppos = player_->GetPosition();
    const Vector3 hitPos = {
        enemyPos.x + (ppos.x - enemyPos.x) * kSurfaceLerp,
        enemyPos.y + (ppos.y - enemyPos.y) * kSurfaceLerp,
        0.0f
    };

    // 白い芯→属性色の星→広がるリング→散る火花の順に重ねて、当たった一点をはっきり見せる
    pm_->EmitWithColor("hit_spark", hitPos, { 0.0f, 0.0f, 0.0f }, kCoreColor, kCoreLifetime, kCoreScale * strength);
    pm_->EmitHitStar("hit_spark", hitPos, color);
    if (escalation >= kEscalationMaxThreshold) {
        pm_->EmitHitStar("hit_spark", hitPos, kCoreColor);
    }
    pm_->EmitRing("hit_ring", hitPos, kRingSpeed * strength, color, kRingCount, kRingLifetime, kRingSize * strength);
    if (escalation >= kEscalationOuterRingThreshold) {
        pm_->EmitRing("hit_ring", hitPos, kEscalationOuterRingSpeed * strength, color, kEscalationOuterRingCount,
            kEscalationOuterRingLifetime, kEscalationOuterRingSize * strength);
    }
    std::uniform_real_distribution<float> vxD(-kSparkSpreadX, kSparkSpreadX);
    std::uniform_real_distribution<float> vyD(kSparkRiseMin, kSparkRiseMax);
    for (int i = 0; i < sparkCount; ++i) {
        pm_->EmitGravity("hit_spark", hitPos, { vxD(rng_) * strength, vyD(rng_) * strength, 0.0f },
            color, kSparkLifetime, kSparkSize * strength);
    }

    // 武器ごとのweapons.json effect設定（burstCount/ringRadius）を追加の弾け・リングとして重ね、
    // 武器が変わると当たった瞬間の見た目量も変わるようにする
    if (extraBurstCount > 0) {
        std::uniform_real_distribution<float> vxExtra(-kSparkSpreadX * kExtraSparkSpreadScale, kSparkSpreadX * kExtraSparkSpreadScale);
        std::uniform_real_distribution<float> vyExtra(kSparkRiseMin, kSparkRiseMax * kExtraSparkRiseScale);
        for (int i = 0; i < extraBurstCount; ++i) {
            pm_->EmitGravity("hit_spark", hitPos, { vxExtra(rng_) * strength, vyExtra(rng_) * strength, 0.0f },
                color, kSparkLifetime, kSparkSize * strength);
        }
    }
    if (ringRadius > 0.0f) {
        Vector4 softColor = { color.x, color.y, color.z, color.w * kExtraRingAlphaScale };
        pm_->EmitRing("hit_ring", hitPos, ringRadius * strength, softColor,
            kExtraRingBaseCount + extraBurstCount / kExtraRingCountDivisor, kRingLifetime * kExtraRingLifetimeScale,
            kRingSize * kExtraRingSizeScale * strength);
    }
}

float GamePlayScene::HitEscalationRatio() const
{
    constexpr int kEscalationMaxHits = 20; // この連続ヒット数で盛り度合いが上限になる
    const int hits = (std::max)(hitEscalationCount_ - 1, 0);
    return (std::min)(static_cast<float>(hits) / static_cast<float>(kEscalationMaxHits), 1.0f);
}

void GamePlayScene::EmitElementalHitEffect(const WeaponData& weapon, const Vector3& enemyPos, int comboStep)
{
    constexpr float kStepScalePerCombo = 0.12f; // コンボ段が1上がるごとの演出拡大率
    const Vector4 color = { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] };
    EmitElementalHitEffect(weapon.element, color, enemyPos,
        1.0f + kStepScalePerCombo * static_cast<float>((std::max)(comboStep - 1, 0)));
}

void GamePlayScene::EmitElementalHitEffect(const std::string& element, const Vector4& color, const Vector3& enemyPos, float scale)
{
    constexpr float kEscalationScaleGain = 0.8f; // 連続ヒットが上限の時に足す拡大率

    const float stepScale = scale * (1.0f + kEscalationScaleGain * HitEscalationRatio());
    if (const IElementEffect* effect = IElementEffect::Find(element)) {
        effect->EmitHit(ElementContext(), enemyPos, color, stepScale);
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
    constexpr float kSlashRadiusBase = 0.8f;
    constexpr float kSlashRadiusPerStep = 0.45f;
    constexpr int kFinisherStep = 3; // リングを追加で出す締めの段
    constexpr float kFinisherRingSpeed = 3.5f;
    constexpr int kFinisherRingCount = 10;
    constexpr float kFinisherRingLifetime = 0.3f;
    constexpr float kFinisherRingSize = 0.22f;
    // weapons.jsonのeffect設定から出す属性バースト
    constexpr int kBurstColumns = 5;
    constexpr int kBurstCenterColumn = 2;
    constexpr float kBurstSpreadZ = 0.45f;
    constexpr float kBurstSpeedBase = 2.0f;
    constexpr float kBurstSpeedStep = 0.3f;
    constexpr float kBurstRiseBase = 1.8f;
    constexpr int kBurstRiseSteps = 4;
    constexpr float kBurstRiseStep = 0.65f;
    constexpr float kBurstLifetime = 0.5f;
    constexpr float kBurstSize = 0.16f;
    constexpr int kEffectRingBaseCount = 12;
    constexpr float kEffectRingLifetime = 0.28f;
    constexpr float kEffectRingSize = 0.16f;
    // 武器切り替えヒット
    constexpr float kSwitchRingSpeed = 4.8f;
    constexpr int kSwitchRingCount = 18;
    constexpr float kSwitchRingLifetime = 0.38f;
    constexpr float kSwitchRingSize = 0.28f;
    constexpr int kSwitchHitStopFrames = 5;
    constexpr float kSwitchShakeAmount = 0.35f;
    constexpr float kSwitchShakeSeconds = 0.16f;
    // 通常ヒット
    constexpr int kHitStopFrames = 3;
    constexpr float kShakeAmountPerStep = 0.10f;
    constexpr float kShakeSeconds = 0.10f;

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
    float rad = kSlashRadiusBase + (step - 1) * kSlashRadiusPerStep;
    pm_->EmitSlash("sword_slash", ppos, ang, col, rad);
    if (step == kFinisherStep) {
        pm_->EmitRing("sword_slash", ppos, kFinisherRingSpeed, col, kFinisherRingCount, kFinisherRingLifetime, kFinisherRingSize);
    }

    // 属性演出はConfig/weapons.jsonの共通プリセットから生成する
    // 武器追加時にシーン側へtype分岐を増やさず色と密度を調整できるようにする
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 effectColor = { weapon.effectColor[0], weapon.effectColor[1],
        weapon.effectColor[2], weapon.effectColor[3] };
    for (int i = 0; i < weapon.effectBurstCount; ++i) {
        const float spread = static_cast<float>((i % kBurstColumns) - kBurstCenterColumn) * kBurstSpreadZ;
        pm_->EmitGravity("hit_spark", ppos,
            { dir * (kBurstSpeedBase + i * kBurstSpeedStep), kBurstRiseBase + (i % kBurstRiseSteps) * kBurstRiseStep, spread },
            effectColor, kBurstLifetime, kBurstSize);
    }
    if (weapon.effectRingRadius > 0.0f) {
        pm_->EmitRing("sword_slash", ppos, weapon.effectRingRadius,
            effectColor, kEffectRingBaseCount + weapon.effectBurstCount, kEffectRingLifetime, kEffectRingSize);
    }

    if (player_->JustWeaponSwitchHit()) {
        pm_->EmitRing("sword_slash", ppos, kSwitchRingSpeed, col, kSwitchRingCount, kSwitchRingLifetime, kSwitchRingSize);
        tm->RequestHitStop(kSwitchHitStopFrames);
        cameraShaker_.Request(kSwitchShakeAmount, kSwitchShakeSeconds);
    }

    tm->RequestHitStop(kHitStopFrames);
    cameraShaker_.Request(kShakeAmountPerStep * step, kShakeSeconds);
}

void GamePlayScene::EmitGunFireParticles(const Vector3& ppos)
{
    constexpr int kMinBullets = 2;
    constexpr int kFallbackBullets = 4; // 段定義がない時の見た目の弾数
    constexpr float kFallbackSpread = 0.15f;
    // 命中判定は着弾を待たない即着弾（ApplyGunShotStyleHit参照）なので、見た目の弾も
    // 同じ射程まで短時間で届かせる（届く前に消えると、画面奥の敵に当たった時だけ
    // 貫通したように見えてしまう）
    constexpr float kGunShotVisualLifetime = 0.12f;
    constexpr float kFanSpeedBase = 7.0f; // 扇状の広がり方（縦方向の速度成分）
    constexpr float kFanSpeedStep = 1.0f;
    constexpr float kGunShotSize = 0.14f;

    if (!player_->JustFired()) {
        return;
    }
    // 弾数・拡散・色は選択中の銃と段の定義に従う（散弾は扇状に、単発は直線に飛ぶ）
    auto* wm = WeaponManager::GetInstance();
    const GunShotDef* shot = player_->GetActiveGunShot();
    const RangedWeaponData& gun = wm->GetRanged();
    float dir = player_->GetLastDirX();
    Vector4 col = { gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
    int n = (shot != nullptr) ? (std::max)(shot->bullets, kMinBullets) : kFallbackBullets;
    float spread = (shot != nullptr) ? shot->spreadDeg * GameConstants::kDegToRad : kFallbackSpread;
    const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    const float travelSpeed = rangeX / kGunShotVisualLifetime;
    for (int i = 0; i < n; ++i) {
        float t = (n > 1) ? (i / (n - 1.0f) - 0.5f) : 0.0f; // -0.5〜+0.5
        float speed = kFanSpeedBase + i * kFanSpeedStep;
        pm_->EmitWithColor("gun_shot", ppos,
            { dir * travelSpeed, speed * spread * t, 0.0f },
            col, kGunShotVisualLifetime, kGunShotSize);
    }
}

void GamePlayScene::EmitBlinkAndGaugeParticles(const Vector3& ppos)
{
    constexpr int kDaggerStyleIndex = 2; // スタイル一覧上のダガーの位置
    constexpr float kStingerAlpha = 0.75f;
    constexpr float kStingerRingSpeed = 2.8f;
    constexpr int kStingerRingCount = 10;
    constexpr float kStingerRingLifetime = 0.28f;
    constexpr float kStingerRingSize = 0.17f;
    constexpr float kGaugeRingSpeed = 1.6f;
    constexpr Vector4 kGaugeRingColor = { 0.75f, 0.25f, 1.0f, 0.9f };
    constexpr int kGaugeRingCount = 8;
    constexpr float kGaugeRingLifetime = 0.38f;
    constexpr float kGaugeRingSize = 0.2f;

    auto* wm = WeaponManager::GetInstance();
    const auto& styles = wm->GetList();

    if (player_->JustDaggerStingerHit()) {
        const auto& sc = styles[kDaggerStyleIndex].styleColor;
        Vector4 col = { sc[0], sc[1], sc[2], kStingerAlpha };
        pm_->EmitRing("blink_trail", ppos, kStingerRingSpeed, col, kStingerRingCount, kStingerRingLifetime, kStingerRingSize);
    }

    if (player_->JustChargedGauge()) {
        pm_->EmitRing("awaken_aura", ppos, kGaugeRingSpeed, kGaugeRingColor, kGaugeRingCount, kGaugeRingLifetime, kGaugeRingSize);
    }
}

void GamePlayScene::EmitWeaponSkillCastParticles(const Vector3& ppos)
{
    // 固有技は発動モーションに合わせた見た目をここで技ごとに出す
    // （命中時の共通バーストはApplyWeaponSkillStyleHit側、ダガーのスティンガーはEmitBlinkAndGaugeParticles側）
    const auto* wm = WeaponManager::GetInstance();
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 color = { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] };
    const float dir = player_->GetLastDirX();

    if (player_->IsSpearSpinningCharge()) {
        constexpr float kSpinCenterHeight = 0.35f;
        constexpr float kSpinTrailScale = 0.72f;
        constexpr float kSpinTrailLifetime = 0.16f;
        constexpr float kSpinSlashAlpha = 0.55f;
        constexpr float kSpinSlashRadius = 1.65f;
        const Vector3 center = { ppos.x, ppos.y + kSpinCenterHeight, ppos.z };
        pm_->EmitTrail("weapon_trail", center, color, kSpinTrailScale, kSpinTrailLifetime);
        pm_->EmitSlash("sword_slash", center,
            player_->GetSpinAngle() * GameConstants::kDegToRad,
            { color.x, color.y, color.z, kSpinSlashAlpha }, kSpinSlashRadius);
    }

    if (player_->JustSwordDash()) {
        // 高速移動の終了地点へ、時間差で重なる三本の斬線を置く。
        constexpr float kDashSlashRadius = 3.4f;
        constexpr Vector3 kSecondSlashOffset = { 0.55f, 0.35f, 0.0f }; // xは進行方向の後ろ側へずらす量
        constexpr float kSecondSlashAngle = 0.42f;
        constexpr float kSecondSlashAlpha = 0.72f;
        constexpr float kSecondSlashRadiusScale = 0.82f;
        constexpr Vector3 kThirdSlashOffset = { 1.05f, -0.18f, 0.0f };
        constexpr float kThirdSlashAngle = -0.38f;
        constexpr float kThirdSlashAlpha = 0.58f;
        constexpr float kThirdSlashRadiusScale = 0.68f;
        constexpr float kImpactRingSpeed = 4.4f;
        constexpr int kImpactRingCount = 18;
        constexpr float kImpactRingLifetime = 0.24f;
        constexpr float kImpactRingSize = 0.12f;
        const float angle = (dir >= 0.0f) ? 0.0f : GameConstants::kPi;
        const Vector3 impact = player_->GetSwordSkillImpactPosition();
        pm_->EmitSlash("sword_slash", impact, angle, color, kDashSlashRadius);
        pm_->EmitSlash("sword_slash", { impact.x - dir * kSecondSlashOffset.x, impact.y + kSecondSlashOffset.y, impact.z },
            angle + kSecondSlashAngle, { color.x, color.y, color.z, kSecondSlashAlpha }, kDashSlashRadius * kSecondSlashRadiusScale);
        pm_->EmitSlash("sword_slash", { impact.x - dir * kThirdSlashOffset.x, impact.y + kThirdSlashOffset.y, impact.z },
            angle + kThirdSlashAngle, { color.x, color.y, color.z, kThirdSlashAlpha }, kDashSlashRadius * kThirdSlashRadiusScale);
        pm_->EmitRing("hit_ring", impact, kImpactRingSpeed, color, kImpactRingCount, kImpactRingLifetime, kImpactRingSize);
        EmitWeaponSkillImpact(impact, color, true);
    }
    if (player_->IsSkillSequenceActive() && player_->JustComboHit()) {
        // 固有技の連撃: 段ごとに向きを変えた斬線を重ね、回転斬りの段は輪を広げる。締めは大きな一閃で打ち飛ばしを見せる
        constexpr float kSequenceCenterHeight = 0.5f;
        constexpr float kSequenceSlashRadius = 1.9f;
        constexpr float kSequenceSlashAngleStep = 1.1f; // 段ごとに斬線の向きを散らす角度（ラジアン）
        constexpr float kTurnRingSpeed = 3.4f;
        constexpr int kTurnRingCount = 16;
        constexpr float kTurnRingLifetime = 0.22f;
        constexpr float kTurnRingSize = 0.12f;
        const MeleeAttackDef* step = player_->GetActiveMeleeAttack();
        const Vector3 center = { ppos.x, ppos.y + kSequenceCenterHeight, ppos.z };
        if (step != nullptr && step->finisher) {
            EmitWeaponSkillImpact(center, color, true);
        } else {
            const float angle = static_cast<float>(player_->GetComboStep()) * kSequenceSlashAngleStep;
            pm_->EmitSlash("sword_slash", center, angle, color, kSequenceSlashRadius);
            if (step != nullptr && step->bodyTurns != 0.0f) {
                pm_->EmitRing("hit_ring", center, kTurnRingSpeed, color, kTurnRingCount, kTurnRingLifetime, kTurnRingSize);
            }
        }
    }
    if (player_->JustSpearRetreat() || player_->JustGreatswordSlam() || player_->JustAxeCharge()
        || player_->JustScytheSpin() || player_->JustDaggerStingerHit()) {
        constexpr float kImpactCenterHeight = 0.5f; // 他の命中演出と同じ胸の高さ
        EmitWeaponSkillImpact({ ppos.x, ppos.y + kImpactCenterHeight, ppos.z }, color, false);
    }
    if (player_->JustSpearRetreat()) {
        // 回転突撃の終点で、螺旋の余韻と前方への衝撃をまとめて見せる
        constexpr int kChargeSparkCount = 12;
        constexpr float kChargeSparkRiseMin = 0.8f;
        constexpr float kChargeSparkRiseMax = 4.0f;
        constexpr float kChargeRingSpeed = 3.6f;
        constexpr int kChargeRingCount = 20;
        constexpr float kChargeRingLifetime = 0.32f;
        constexpr float kChargeRingSize = 0.18f;
        constexpr float kChargeSparkSpeedBase = 2.5f;
        constexpr float kChargeSparkSpeedStep = 0.18f;
        constexpr float kChargeSparkLifetime = 0.55f;
        constexpr float kChargeSparkSize = 0.15f;
        std::uniform_real_distribution<float> riseD(kChargeSparkRiseMin, kChargeSparkRiseMax);
        pm_->EmitRing("hit_ring", ppos, kChargeRingSpeed, color, kChargeRingCount, kChargeRingLifetime, kChargeRingSize);
        for (int i = 0; i < kChargeSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", ppos, { dir * (kChargeSparkSpeedBase + i * kChargeSparkSpeedStep), riseD(rng_), 0.0f },
                color, kChargeSparkLifetime, kChargeSparkSize);
        }
    }
    if (player_->JustGreatswordSlam()) {
        // 大地砕き（グレートソード/ハンマー共通）: ボス技習得前の基本状態でも足元に衝撃波を出す
        // （習得後はApplyWeaponSkillStyleHit側でさらに大きい輪が重なる）
        constexpr float kSlamBaseRadius = 2.2f;
        constexpr int kSlamRingCount = 16;
        constexpr float kSlamRingLifetime = 0.3f;
        constexpr float kSlamRingSize = 0.22f;
        pm_->EmitRing("hit_ring", ppos, kSlamBaseRadius, color, kSlamRingCount, kSlamRingLifetime, kSlamRingSize);
    }
    if (player_->JustSpinShot()) {
        // スピン連射: 全周へ銃口色の輪を広げる
        constexpr float kSpinShotRingSpeed = 3.0f;
        constexpr int kSpinShotRingCount = 20;
        constexpr float kSpinShotRingLifetime = 0.3f;
        constexpr float kSpinShotRingSize = 0.16f;
        pm_->EmitRing("gun_shot", ppos, kSpinShotRingSpeed, color, kSpinShotRingCount, kSpinShotRingLifetime, kSpinShotRingSize);
    }
    if (player_->JustScytheSpin()) {
        // 回転斬り: 前後2方向へ斬線を重ねて円を描くように見せる
        constexpr float kScytheSlashRadius = 2.6f;
        pm_->EmitSlash("sword_slash", ppos, 0.0f, color, kScytheSlashRadius);
        pm_->EmitSlash("sword_slash", ppos, GameConstants::kPi, color, kScytheSlashRadius);
    }
    if (player_->JustAxeCharge()) {
        // バーサーク突進: 武器の色で手元にトレイル残像を強めに残す
        constexpr float kChargeTrailScale = 0.42f;
        constexpr float kChargeTrailLifetime = 0.22f;
        pm_->EmitTrail("weapon_trail", ppos, color, kChargeTrailScale, kChargeTrailLifetime);
    }
}

void GamePlayScene::EmitWeaponSkillImpact(const Vector3& center, const Vector4& color, bool slashLine)
{
    constexpr float kFlashAlpha = 0.32f;
    constexpr float kFlashSeconds = 0.12f;
    constexpr int kImpactHitStopFrames = 5;
    constexpr float kImpactShakeAmount = 0.24f;
    constexpr float kImpactShakeSeconds = 0.2f;
    constexpr int kBladeCount = 12;
    constexpr float kBladeRadius = 2.6f;
    constexpr float kBladeSpeedMin = 1.5f;
    constexpr float kBladeSpeedMax = 3.5f;
    constexpr Vector4 kCoreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    constexpr float kCoreLifetime = 0.14f;
    constexpr float kCoreScale = 1.4f;
    constexpr float kWhiteRingSpeed = 6.5f;
    constexpr int kWhiteRingCount = 28;
    constexpr float kWhiteRingLifetime = 0.22f;
    constexpr float kWhiteRingSize = 0.14f;
    constexpr float kColorRingSpeed = 3.2f;
    constexpr int kColorRingCount = 24;
    constexpr float kColorRingLifetime = 0.42f;
    constexpr float kColorRingSize = 0.24f;
    constexpr int kSparkCount = 26;
    constexpr float kSparkSpreadX = 6.0f;
    constexpr float kSparkRiseMin = 1.0f;
    constexpr float kSparkRiseMax = 7.0f;
    constexpr float kSparkLifetime = 0.55f;
    constexpr float kSparkSize = 0.12f;
    constexpr float kSlashLineHalfLength = 3.2f; // 刀の突進距離の半分強（通過した跡を丸ごと一閃で覆う）
    constexpr Vector4 kSlashLineColor = { 0.85f, 0.97f, 1.0f, 1.0f };
    constexpr float kSlashLineThickness = 8.0f;
    constexpr float kSlashLineSeconds = 0.32f;

    ScreenFlash::GetInstance()->Request({ color.x, color.y, color.z, kFlashAlpha }, kFlashSeconds);
    TimeManager::GetInstance()->RequestHitStop(kImpactHitStopFrames);
    cameraShaker_.Request(kImpactShakeAmount, kImpactShakeSeconds);
    bladeFlash_.Emit(center, kBladeCount, kBladeRadius, kBladeSpeedMin, kBladeSpeedMax);

    pm_->EmitWithColor("hit_spark", center, { 0.0f, 0.0f, 0.0f }, kCoreColor, kCoreLifetime, kCoreScale);
    pm_->EmitHitStar("hit_spark", center, kCoreColor);
    pm_->EmitHitStar("hit_spark", center, color);
    pm_->EmitRing("hit_ring", center, kWhiteRingSpeed, kCoreColor, kWhiteRingCount, kWhiteRingLifetime, kWhiteRingSize);
    pm_->EmitRing("hit_ring", center, kColorRingSpeed, color, kColorRingCount, kColorRingLifetime, kColorRingSize);
    std::uniform_real_distribution<float> vxD(-kSparkSpreadX, kSparkSpreadX);
    std::uniform_real_distribution<float> vyD(kSparkRiseMin, kSparkRiseMax);
    for (int i = 0; i < kSparkCount; ++i) {
        pm_->EmitGravity("hit_spark", center, { vxD(rng_), vyD(rng_), 0.0f }, color, kSparkLifetime, kSparkSize);
    }

    if (slashLine) {
        const Vector3& cam = camera_->GetTranslate();
        SceneShared::SpawnSlashMarkWorld({ center.x - kSlashLineHalfLength, center.y },
            { center.x + kSlashLineHalfLength, center.y },
            cam.x, cam.y, kSlashLineColor, kSlashLineThickness, kSlashLineSeconds);
    }
}

void GamePlayScene::EmitAwakenParticles(const Vector3& ppos, float dt)
{
    constexpr float kAuraInterval = 0.07f; // 覚醒中オーラを出す間隔（秒）
    constexpr float kAuraRiseSpeed = 0.4f;
    constexpr Vector4 kAuraColor = { 1.0f, 0.88f, 0.25f, 0.45f };
    constexpr float kAuraLifetime = 0.45f;
    constexpr float kAuraSize = 0.28f;
    constexpr float kBurstOuterRingSpeed = 5.0f;
    constexpr Vector4 kBurstOuterRingColor = { 1.0f, 0.85f, 0.15f, 1.0f };
    constexpr int kBurstOuterRingCount = 24;
    constexpr float kBurstOuterRingLifetime = 0.5f;
    constexpr float kBurstOuterRingSize = 0.5f;
    constexpr float kBurstInnerRingSpeed = 2.5f;
    constexpr Vector4 kBurstInnerRingColor = { 1.0f, 1.0f, 0.9f, 1.0f };
    constexpr int kBurstInnerRingCount = 16;
    constexpr float kBurstInnerRingLifetime = 0.35f;
    constexpr float kBurstInnerRingSize = 0.3f;
    constexpr Vector4 kBurstStarColor = { 1.0f, 0.9f, 0.3f, 1.0f };
    constexpr int kBurstHitStopFrames = 4;
    constexpr float kBurstShakeAmount = 0.18f;
    constexpr float kBurstShakeSeconds = 0.15f;

    auto* tm = TimeManager::GetInstance();

    // 覚醒中オーラ（連続）
    if (player_->IsAwakened()) {
        auraTimer_ += dt;
        if (auraTimer_ >= kAuraInterval) {
            auraTimer_ = 0.0f;
            pm_->EmitWithColor("awaken_aura", ppos,
                { 0.0f, kAuraRiseSpeed, 0.0f },
                kAuraColor, kAuraLifetime, kAuraSize, true);
        }
    } else {
        auraTimer_ = 0.0f;
    }

    // 覚醒発動の瞬間（衝撃波バースト）
    if (player_->JustAwakened()) {
        pm_->EmitRing("awaken_aura", ppos, kBurstOuterRingSpeed, kBurstOuterRingColor,
            kBurstOuterRingCount, kBurstOuterRingLifetime, kBurstOuterRingSize);
        pm_->EmitRing("awaken_aura", ppos, kBurstInnerRingSpeed, kBurstInnerRingColor,
            kBurstInnerRingCount, kBurstInnerRingLifetime, kBurstInnerRingSize);
        pm_->EmitHitStar("awaken_aura", ppos, kBurstStarColor);
        tm->RequestHitStop(kBurstHitStopFrames);
        cameraShaker_.Request(kBurstShakeAmount, kBurstShakeSeconds);
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
    const Vector3 weaponPos = player_->GetActiveWeaponWorldPosition();
    pm_->EmitTrail("weapon_trail", weaponPos, color, kWeaponTrailScale, kWeaponTrailLifetime);
    EmitWeaponElementAura(weapon.element, color, weaponPos);
}

void GamePlayScene::EmitWeaponElementAura(const std::string& element, const Vector4& color, const Vector3& weaponPos)
{
    // 毎フレーム1〜2粒だけ零し、振りの軌跡に属性の手触りを添える（命中演出の邪魔をしない量に抑える）
    constexpr float kJitter = 0.15f; // 武器位置からのばらつき
    std::uniform_real_distribution<float> jitterD(-kJitter, kJitter);
    const IElementEffect* effect = IElementEffect::Find(element);
    if (effect == nullptr) {
        return;
    }
    const Vector3 pos = { weaponPos.x + jitterD(rng_), weaponPos.y + jitterD(rng_), weaponPos.z };
    effect->EmitAura(ElementContext(), pos, color);
}

ElementEffectContext GamePlayScene::ElementContext()
{
    return { *pm_, rng_, player_->GetLastDirX() };
}
