/**
 * @file GamePlaySceneParticles.cpp
 * @brief 戦闘と武器のパーティクル演出
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

void GamePlayScene::EmitElementalHitEffect(const WeaponData& weapon, const Vector3& enemyPos, int comboStep)
{
    constexpr float kStepScalePerCombo = 0.12f; // コンボ段が1上がるごとの演出拡大率

    const Vector4 color = { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] };
    const float stepScale = 1.0f + kStepScalePerCombo * static_cast<float>((std::max)(comboStep - 1, 0));
    const float dir = player_->GetLastDirX();

    if (weapon.element == "Fire") {
        // 炎: 命中点から赤橙色の火の粉が上へ噴き上がる。
        constexpr int kEmberCount = 10;
        constexpr int kEmberColumns = 5; // 横方向に並べる列数
        constexpr int kEmberCenterColumn = 2;
        constexpr float kEmberColumnSpacing = 0.65f;
        constexpr float kEmberRiseBase = 3.2f;
        constexpr int kEmberRiseSteps = 3;
        constexpr float kEmberRiseStep = 0.9f;
        constexpr Vector4 kEmberSubColor = { 1.0f, 0.55f, 0.05f, 1.0f };
        constexpr float kEmberLifetime = 0.5f;
        constexpr float kEmberSize = 0.18f;
        for (int i = 0; i < kEmberCount; ++i) {
            const float side = static_cast<float>((i % kEmberColumns) - kEmberCenterColumn) * kEmberColumnSpacing;
            pm_->EmitGravity("hit_spark", enemyPos, { side, kEmberRiseBase + (i % kEmberRiseSteps) * kEmberRiseStep, 0.0f },
                i % 2 == 0 ? color : kEmberSubColor, kEmberLifetime, kEmberSize * stepScale);
        }
    } else if (weapon.element == "Lightning") {
        // 雷: 白い芯を持つ高速の十字放電。
        constexpr float kBoltSpeedX = 2.0f;
        constexpr float kHorizontalBoltLifetime = 0.16f;
        constexpr float kHorizontalBoltLength = 3.4f;
        constexpr float kHorizontalBoltThickness = 0.12f;
        constexpr Vector4 kBoltCoreColor = { 1.0f, 1.0f, 0.85f, 1.0f };
        constexpr float kVerticalBoltLifetime = 0.12f;
        constexpr float kVerticalBoltThickness = 0.14f;
        constexpr float kVerticalBoltLength = 3.0f;
        constexpr float kBoltRingSpeed = 4.5f;
        constexpr int kBoltRingCount = 8;
        constexpr float kBoltRingLifetime = 0.14f;
        constexpr float kBoltRingSize = 0.1f;
        pm_->EmitEllipse("hit_spark", enemyPos, { dir * kBoltSpeedX, 0.0f, 0.0f }, color,
            kHorizontalBoltLifetime, kHorizontalBoltLength * stepScale, kHorizontalBoltThickness);
        pm_->EmitEllipse("hit_spark", enemyPos, { 0.0f, 1.0f, 0.0f }, kBoltCoreColor,
            kVerticalBoltLifetime, kVerticalBoltThickness, kVerticalBoltLength * stepScale);
        pm_->EmitRing("hit_ring", enemyPos, kBoltRingSpeed, color, kBoltRingCount, kBoltRingLifetime, kBoltRingSize);
    } else if (weapon.element == "Ice") {
        // 氷: 水色の破片が扇状に飛び、薄い冷気の輪が残る。
        constexpr int kShardCount = 12;
        constexpr int kShardColumns = 7;
        constexpr int kShardCenterColumn = 3;
        constexpr float kShardColumnSpacing = 0.75f;
        constexpr float kShardRiseBase = 2.0f;
        constexpr int kShardRiseSteps = 4;
        constexpr float kShardRiseStep = 0.8f;
        constexpr float kShardLifetime = 0.65f;
        constexpr float kShardSize = 0.14f;
        constexpr float kFrostRingSpeed = 1.3f;
        constexpr Vector4 kFrostRingColor = { 0.65f, 0.95f, 1.0f, 0.75f };
        constexpr int kFrostRingCount = 20;
        constexpr float kFrostRingLifetime = 0.5f;
        constexpr float kFrostRingSize = 0.12f;
        for (int i = 0; i < kShardCount; ++i) {
            const float vx = static_cast<float>((i % kShardColumns) - kShardCenterColumn) * kShardColumnSpacing;
            pm_->EmitGravity("hit_spark", enemyPos, { vx, kShardRiseBase + (i % kShardRiseSteps) * kShardRiseStep, 0.0f },
                color, kShardLifetime, kShardSize * stepScale);
        }
        pm_->EmitRing("hit_ring", enemyPos, kFrostRingSpeed, kFrostRingColor, kFrostRingCount, kFrostRingLifetime, kFrostRingSize);
    } else if (weapon.element == "Gravity") {
        // 重力: 密度の違う紫の同心円と明滅する重い核。
        constexpr float kInnerRingSpeed = 0.8f;
        constexpr int kInnerRingCount = 24;
        constexpr float kInnerRingLifetime = 0.55f;
        constexpr float kInnerRingSize = 0.28f;
        constexpr float kOuterRingSpeed = 2.0f;
        constexpr Vector4 kOuterRingColor = { 0.35f, 0.08f, 0.6f, 0.8f };
        constexpr int kOuterRingCount = 18;
        constexpr float kOuterRingLifetime = 0.38f;
        constexpr float kOuterRingSize = 0.18f;
        constexpr Vector4 kCoreColor = { 0.95f, 0.7f, 1.0f, 1.0f };
        constexpr float kCoreLifetime = 0.35f;
        constexpr float kCoreSize = 0.8f;
        pm_->EmitRing("hit_ring", enemyPos, kInnerRingSpeed, color, kInnerRingCount, kInnerRingLifetime, kInnerRingSize * stepScale);
        pm_->EmitRing("hit_ring", enemyPos, kOuterRingSpeed, kOuterRingColor, kOuterRingCount, kOuterRingLifetime, kOuterRingSize);
        pm_->EmitWithColor("hit_spark", enemyPos, { 0.0f, 0.0f, 0.0f },
            kCoreColor, kCoreLifetime, kCoreSize * stepScale, true);
    } else if (weapon.element == "Blood") {
        // 血: 深紅の斬線と重い飛沫。
        constexpr float kSlashAngleRight = 0.45f;
        constexpr float kSlashAngleLeft = 2.69f;
        constexpr float kSlashRadius = 1.8f;
        constexpr int kSplatterCount = 9;
        constexpr float kSplatterSpeedBase = 0.5f;
        constexpr float kSplatterSpeedStep = 0.25f;
        constexpr float kSplatterRiseBase = 1.0f;
        constexpr int kSplatterRiseSteps = 4;
        constexpr float kSplatterRiseStep = 0.7f;
        constexpr float kSplatterLifetime = 0.55f;
        constexpr float kSplatterSize = 0.2f;
        pm_->EmitSlash("sword_slash", enemyPos, dir > 0.0f ? kSlashAngleRight : kSlashAngleLeft, color, kSlashRadius * stepScale);
        for (int i = 0; i < kSplatterCount; ++i) {
            pm_->EmitGravity("hit_spark", enemyPos,
                { -dir * (kSplatterSpeedBase + i * kSplatterSpeedStep), kSplatterRiseBase + (i % kSplatterRiseSteps) * kSplatterRiseStep, 0.0f },
                color, kSplatterLifetime, kSplatterSize);
        }
    } else if (weapon.element == "Void") {
        // 虚無: 暗紫の二重リングと不規則に明滅する粒子。
        constexpr float kInnerRingSpeed = 1.4f;
        constexpr int kInnerRingCount = 28;
        constexpr float kInnerRingLifetime = 0.65f;
        constexpr float kInnerRingSize = 0.2f;
        constexpr float kOuterRingSpeed = 3.0f;
        constexpr Vector4 kOuterRingColor = { 0.12f, 0.02f, 0.24f, 0.9f };
        constexpr int kOuterRingCount = 16;
        constexpr float kOuterRingLifetime = 0.3f;
        constexpr float kOuterRingSize = 0.15f;
        constexpr int kMoteCount = 7;
        constexpr int kMoteColumnsX = 3;
        constexpr int kMoteColumnsY = 4;
        constexpr float kMoteSpeedX = 1.4f;
        constexpr float kMoteSpeedY = 1.1f;
        constexpr float kMoteLifetime = 0.45f;
        constexpr float kMoteSize = 0.2f;
        pm_->EmitRing("hit_ring", enemyPos, kInnerRingSpeed, color, kInnerRingCount, kInnerRingLifetime, kInnerRingSize * stepScale);
        pm_->EmitRing("hit_ring", enemyPos, kOuterRingSpeed, kOuterRingColor, kOuterRingCount, kOuterRingLifetime, kOuterRingSize);
        for (int i = 0; i < kMoteCount; ++i) {
            pm_->EmitWithColor("hit_spark", enemyPos,
                { static_cast<float>((i % kMoteColumnsX) - 1) * kMoteSpeedX, static_cast<float>((i % kMoteColumnsY) - 1) * kMoteSpeedY, 0.0f },
                color, kMoteLifetime, kMoteSize, true);
        }
    } else if (weapon.element == "Wind") {
        // 風: 緑の交差する風刃と外へ抜ける軽い渦。
        constexpr float kBladeAngle = 0.55f;
        constexpr float kMainBladeRadius = 2.0f;
        constexpr Vector4 kSubBladeColor = { 0.75f, 1.0f, 0.65f, 0.8f };
        constexpr float kSubBladeRadius = 1.7f;
        constexpr float kVortexRingSpeed = 3.8f;
        constexpr int kVortexRingCount = 18;
        constexpr float kVortexRingLifetime = 0.28f;
        constexpr float kVortexRingSize = 0.1f;
        pm_->EmitSlash("sword_slash", enemyPos, kBladeAngle, color, kMainBladeRadius * stepScale);
        pm_->EmitSlash("sword_slash", enemyPos, -kBladeAngle, kSubBladeColor, kSubBladeRadius * stepScale);
        pm_->EmitRing("hit_ring", enemyPos, kVortexRingSpeed, color, kVortexRingCount, kVortexRingLifetime, kVortexRingSize);
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
    pm_->EmitTrail("weapon_trail", player_->GetActiveWeaponWorldPosition(), color,
        kWeaponTrailScale, kWeaponTrailLifetime);
}
