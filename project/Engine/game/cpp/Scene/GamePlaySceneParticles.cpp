/**
 * @file GamePlaySceneParticles.cpp
 * @brief 戦闘と武器のパーティクル演出
 * @note 数・色・寿命などの見た目は Resources/Config/hit_effects.json のレシピ、
 *       連続ヒットで盛る度合いなどの手触りは combat_tuning.json が持つ
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
#include "HitEffectLibrary.h"
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

namespace {
constexpr float kSurfaceLerp = 0.3f; // 敵の中心からプレイヤー側へ寄せる割合（体の表面で弾けたように見せる）
constexpr int kExtraRingCountDivisor = 2; // 武器固有の追加バースト数の何分の1をリングの粒数へ足すか
constexpr float kChestHeight = 0.5f; // 足元の pos_ から胸の高さ（他の命中演出と揃える）

// 近接ヒットの斬線は段が進むほど大きくし、締めの段ではリングも重ねる
constexpr float kComboSlashGrowthPerStep = 0.5625f;
constexpr int kComboFinisherStep = 3;

// weapons.jsonのeffect設定から出す属性バースト（列ごとに奥行きと速さをずらして扇状に散らす）
constexpr int kBurstColumns = 5;
constexpr int kBurstCenterColumn = 2;
constexpr float kBurstSpreadZ = 0.45f;
constexpr float kBurstSpeedBase = 2.0f;
constexpr float kBurstSpeedStep = 0.3f;
constexpr float kBurstRiseBase = 1.8f;
constexpr int kBurstRiseSteps = 4;
constexpr float kBurstRiseStep = 0.65f;

// 銃の見た目の弾（命中は即着弾なので、着弾点まで短時間で届かせる）
constexpr int kMinBullets = 2;
constexpr int kFallbackBullets = 4; // 段定義がない時の見た目の弾数
constexpr float kFallbackSpread = 0.15f;
constexpr float kFanSpeedBase = 7.0f; // 扇状の広がり方（縦方向の速度成分）
constexpr float kFanSpeedStep = 1.0f;
constexpr float kHalfRange = 0.5f; // 弾の並びを -0.5〜+0.5 に割り振る

// 固有技の連撃の斬線を段ごとに散らす角度（ラジアン）
constexpr float kSequenceSlashAngleStep = 1.1f;

// 固有技の命中の大きな一撃（刃の閃光と、突進の跡を覆う一閃）
constexpr int kBladeCount = 12;
constexpr float kBladeRadius = 2.6f;
constexpr float kBladeSpeedMin = 1.5f;
constexpr float kBladeSpeedMax = 3.5f;
constexpr float kSlashLineHalfLength = 3.2f;
constexpr Vector4 kSlashLineColor = { 0.85f, 0.97f, 1.0f, 1.0f };
constexpr float kSlashLineThickness = 8.0f;
constexpr float kSlashLineSeconds = 0.32f;

constexpr float kAuraInterval = 0.07f; // 覚醒中オーラを出す間隔（秒）
constexpr float kAuraJitter = 0.15f; // 武器の属性オーラを散らす幅

/** @brief 固有技の発動ごとに出す見た目（どの出来事で、どのレシピを出すか） */
struct SkillCastEffect {
    bool (Player::*happened)() const;
    bool impact; ///< 胸の高さで大きな一撃を出すか
    const char* ring; ///< 足元に広げるリング（無ければ nullptr）
    const char* slash; ///< 前後2方向へ重ねる斬線（無ければ nullptr）
    const char* trail; ///< 手元に残す残像（無ければ nullptr）
};
constexpr SkillCastEffect kSkillCastEffects[] = {
    { &Player::JustGreatswordSlam, true, "skill_slam_base", nullptr, nullptr }, // 大地砕き: 足元に衝撃波
    { &Player::JustSpinShot, false, "spin_shot", nullptr, nullptr }, // スピン連射: 全周へ銃口色の輪
    { &Player::JustScytheSpin, true, nullptr, "scythe_spin", nullptr }, // 回転斬り: 前後の斬線で円を描く
    { &Player::JustAxeCharge, true, nullptr, nullptr, "axe_charge" }, // バーサーク突進: 強めの残像
};

Vector3 Rgb(const Vector4& color)
{
    return { color.x, color.y, color.z };
}

Vector4 Rgba(const float (&color)[4])
{
    return { color[0], color[1], color[2], color[3] };
}
} // namespace

void GamePlayScene::UpdateStyleTechniqueParticles(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    EmitComboHitParticles(ppos);
    EmitGunFireParticles(ppos);
    EmitWeaponSkillCastParticles(ppos);
    EmitAwakenParticles(ppos, dt);
    EmitStyleRankUpParticles(ppos);
}

void GamePlayScene::EmitEnemyHitEffect(const Vector3& enemyPos, const Vector4& color, float strength,
    int extraBurstCount, float ringRadius)
{
    // 当て続けるほど盛る（途切れずに当てた手応えを命中演出の量で返す）
    const CombatTuningData& tune = Tune();
    ++hitEscalationCount_;
    hitEscalationTimer_ = tune.hitEscalationKeepSeconds;
    const float escalation = HitEscalationRatio();
    strength *= 1.0f + tune.hitEscalationStrengthGain * escalation;
    extraBurstCount += static_cast<int>(static_cast<float>(extraBurstCount) * tune.hitEscalationExtraBurstGain * escalation);
    const int extraSparks = static_cast<int>(static_cast<float>(tune.hitEscalationExtraSparks) * escalation);

    const Vector3& ppos = player_->GetPosition();
    const Vector3 hitPos = {
        enemyPos.x + (ppos.x - enemyPos.x) * kSurfaceLerp,
        enemyPos.y + (ppos.y - enemyPos.y) * kSurfaceLerp,
        0.0f
    };

    // 白い芯→属性色の星→広がるリング→散る火花の順に重ねて、当たった一点をはっきり見せる
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Particle("enemy_hit_core", hitPos, { .scale = strength });
    fx.Star("enemy_hit", hitPos, { .color = color });
    if (escalation >= tune.hitEscalationMaxThreshold) {
        fx.Star("enemy_hit_core", hitPos);
    }
    fx.Ring("enemy_hit", hitPos, { .scale = strength, .color = color });
    if (escalation >= tune.hitEscalationOuterRingThreshold) {
        fx.Ring("enemy_hit_outer", hitPos, { .scale = strength, .color = color });
    }
    fx.Sparks("enemy_hit", hitPos, { .scale = strength, .color = color, .extraCount = extraSparks });

    // 武器ごとのweapons.json effect設定（burstCount/ringRadius）を追加の弾け・リングとして重ね、
    // 武器が変わると当たった瞬間の見た目量も変わるようにする
    fx.Sparks("enemy_hit_extra", hitPos, { .scale = strength, .color = color, .extraCount = extraBurstCount });
    if (ringRadius > 0.0f) {
        fx.Ring("enemy_hit_extra", hitPos,
            { .scale = strength, .speed = ringRadius, .color = color, .extraCount = extraBurstCount / kExtraRingCountDivisor });
    }
}

float GamePlayScene::HitEscalationRatio() const
{
    const int hits = (std::max)(hitEscalationCount_ - 1, 0);
    return (std::min)(static_cast<float>(hits) / static_cast<float>((std::max)(Tune().hitEscalationMaxHits, 1)), 1.0f);
}

void GamePlayScene::EmitElementalHitEffect(const WeaponData& weapon, const Vector3& enemyPos, int comboStep)
{
    EmitElementalHitEffect(weapon.element, Rgba(weapon.effectColor), enemyPos,
        1.0f + Tune().elementScalePerComboStep * static_cast<float>((std::max)(comboStep - 1, 0)));
}

void GamePlayScene::EmitElementalHitEffect(const std::string& element, const Vector4& color, const Vector3& enemyPos, float scale)
{
    const float stepScale = scale * (1.0f + Tune().elementEscalationScaleGain * HitEscalationRatio());
    if (const IElementEffect* effect = IElementEffect::Find(element)) {
        effect->EmitHit(ElementContext(), enemyPos, color, stepScale);
    }
}

void GamePlayScene::EmitEnemyTelegraphCue(const EnemyEntity* enemy)
{
    if (enemy == nullptr || !enemy->JustStartedTelegraph()) {
        return;
    }
    // 予備動作に入った瞬間、足元に輪と頭上に明滅する印を出して攻撃が来ることを知らせる
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("enemy_telegraph", enemy->GetPosition());
    fx.Particle("enemy_telegraph_mark", enemy->GetPosition());
}

void GamePlayScene::EmitComboHitParticles(const Vector3& ppos)
{
    if (!player_->JustComboHit()) {
        return;
    }
    auto* wm = WeaponManager::GetInstance();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

    const int step = player_->GetComboStep();
    const float dir = player_->GetLastDirX();
    const auto& styleColor = wm->GetList()[wm->GetIndex()].styleColor;
    const Vector3 styleRgb = { styleColor[0], styleColor[1], styleColor[2] };
    fx.Slash("combo_step", ppos, dir,
        { .scale = 1.0f + static_cast<float>(step - 1) * kComboSlashGrowthPerStep, .tint = styleRgb });
    if (step == kComboFinisherStep) {
        fx.Ring("combo_step_finisher", ppos, { .tint = styleRgb });
    }

    // 属性演出はConfig/weapons.jsonの共通プリセットから生成する
    // 武器追加時にシーン側へtype分岐を増やさず色と密度を調整できるようにする
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 effectColor = Rgba(weapon.effectColor);
    for (int i = 0; i < weapon.effectBurstCount; ++i) {
        const float spread = static_cast<float>((i % kBurstColumns) - kBurstCenterColumn) * kBurstSpreadZ;
        const Vector3 velocity = { dir * (kBurstSpeedBase + i * kBurstSpeedStep), kBurstRiseBase + (i % kBurstRiseSteps) * kBurstRiseStep, spread };
        fx.Particle("weapon_burst", ppos, { .color = effectColor, .velocity = velocity });
    }
    if (weapon.effectRingRadius > 0.0f) {
        fx.Ring("weapon_effect", ppos, { .speed = weapon.effectRingRadius, .color = effectColor, .extraCount = weapon.effectBurstCount });
    }

    if (player_->JustWeaponSwitchHit()) {
        fx.Ring("weapon_switch", ppos, { .tint = styleRgb });
        fx.Impact("weapon_switch");
    }
    // 段が進むほど揺れを大きくする
    fx.Impact("combo_step", { .scale = static_cast<float>(step) });
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
    const float dir = player_->GetLastDirX();
    const int bullets = (shot != nullptr) ? (std::max)(shot->bullets, kMinBullets) : kFallbackBullets;
    const float spread = (shot != nullptr) ? shot->spreadDeg * GameConstants::kDegToRad : kFallbackSpread;
    const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    // 弾は的（銃口に一番近い相手）で止まる。的が無ければ射程の端まで飛ぶ。寿命のうちに届く速さにする
    const float travel = gunShotPicker_.HasTarget() ? gunShotPicker_.GetHitDistance() : rangeX;
    const float lifetime = HitEffectLibrary::GetInstance()->Particle("gun_shot").lifetime;
    const float travelSpeed = lifetime > 0.0f ? travel / lifetime : 0.0f;
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    for (int i = 0; i < bullets; ++i) {
        const float t = (bullets > 1) ? (i / (bullets - 1.0f) - kHalfRange) : 0.0f;
        const float speed = kFanSpeedBase + i * kFanSpeedStep;
        fx.Particle("gun_shot", ppos,
            { .color = Rgba(gun.color), .velocity = Vector3 { dir * travelSpeed, speed * spread * t, 0.0f } });
    }
}

void GamePlayScene::EmitWeaponSkillCastParticles(const Vector3& ppos)
{
    // 固有技は発動モーションに合わせた見た目をここで技ごとに出す（命中時の共通バーストはApplyWeaponSkillStyleHit側）
    const auto* wm = WeaponManager::GetInstance();
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 color = Rgba(weapon.effectColor);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const Vector3 chest = { ppos.x, ppos.y + kChestHeight, ppos.z };

    if (player_->IsSkillSequenceActive() && player_->JustComboHit()) {
        // 固有技の連撃: 段ごとに向きを変えた斬線を重ね、回転斬りの段は輪を広げる。締めは大きな一閃で打ち飛ばしを見せる
        const MeleeAttackDef* step = player_->GetActiveMeleeAttack();
        if (step != nullptr && step->finisher) {
            EmitWeaponSkillImpact(chest, color, true);
        } else {
            const float angle = static_cast<float>(player_->GetComboStep()) * kSequenceSlashAngleStep;
            fx.Slash("skill_sequence", ppos, 0.0f, { .angle = angle, .color = color });
            if (step != nullptr && step->bodyTurns != 0.0f) {
                fx.Ring("skill_sequence_turn", ppos, { .color = color });
            }
        }
    }

    for (const SkillCastEffect& cast : kSkillCastEffects) {
        if (!((*player_).*cast.happened)()) {
            continue;
        }
        if (cast.impact) {
            EmitWeaponSkillImpact(chest, color, false);
        }
        if (cast.ring) {
            fx.Ring(cast.ring, ppos, { .color = color });
        }
        if (cast.slash) {
            fx.Slash(cast.slash, ppos, 1.0f, { .color = color });
            fx.Slash(cast.slash, ppos, -1.0f, { .color = color });
        }
        if (cast.trail) {
            fx.Trail(cast.trail, ppos, color);
        }
    }
}

void GamePlayScene::EmitWeaponSkillImpact(const Vector3& center, const Vector4& color, bool slashLine)
{
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Flash("skill_impact", { .tint = Rgb(color) });
    fx.Impact("skill_impact");
    bladeFlash_.Emit(center, kBladeCount, kBladeRadius, kBladeSpeedMin, kBladeSpeedMax);

    fx.Particle("skill_impact_core", center);
    fx.Star("skill_impact_core", center);
    fx.Star("skill_impact", center, { .color = color });
    fx.Ring("skill_impact_white", center);
    fx.Ring("skill_impact_color", center, { .color = color });
    fx.Sparks("skill_impact", center, { .color = color });

    if (slashLine) {
        // 突進の跡を丸ごと一閃で覆う
        const Vector3& cam = camera_->GetTranslate();
        SceneShared::SpawnSlashMarkWorld({ center.x - kSlashLineHalfLength, center.y },
            { center.x + kSlashLineHalfLength, center.y },
            cam.x, cam.y, kSlashLineColor, kSlashLineThickness, kSlashLineSeconds);
    }
}

void GamePlayScene::EmitAwakenParticles(const Vector3& ppos, float dt)
{
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

    // 覚醒中オーラ（連続）
    if (player_->IsAwakened()) {
        auraTimer_ += dt;
        if (auraTimer_ >= kAuraInterval) {
            auraTimer_ = 0.0f;
            fx.Particle("awaken_aura", ppos);
        }
    } else {
        auraTimer_ = 0.0f;
    }

    // 覚醒発動の瞬間（衝撃波バースト）
    if (player_->JustAwakened()) {
        fx.Ring("awaken_outer", ppos);
        fx.Ring("awaken_inner", ppos);
        fx.Star("awaken", ppos);
        fx.Impact("awaken");
    }
}

void GamePlayScene::EmitStyleRankUpParticles(const Vector3& ppos)
{
    // ランク閾値をここで別管理すると表示側(styleRankHud_)とズレるため、判定はHUD自身に問い合わせる
    if (!styleRankHud_.JustRankedUp()) {
        return;
    }
    const Vector4 rankColor = styleRankHud_.GetRankColor();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("rank_up", ppos, { .color = rankColor });
    fx.Star("rank_up", ppos, { .color = rankColor });
    fx.Impact("rank_up");
    fx.Flash("rank_up", { .tint = Rgb(rankColor) });
}

void GamePlayScene::UpdateWeaponTrail()
{
    // 近接コンボのモーション中、装備武器の手元に色付きの残像を撒いて振りの軌跡を見せる
    const auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon() || !player_->IsMeleeAttacking()) {
        return;
    }
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 color = Rgba(weapon.effectColor);
    const Vector3 weaponPos = player_->GetActiveWeaponWorldPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Trail("weapon_swing", weaponPos, color);
    EmitWeaponElementAura(weapon.element, color, weaponPos);
}

void GamePlayScene::UpdateComboFeel(bool gameAdvanced)
{
    const auto* wm = WeaponManager::GetInstance();
    const Vector4 weaponColor = wm->HasEquippedWeapon()
        ? Rgba(wm->GetCurrent().effectColor)
        : HitEffectLibrary::GetInstance()->Color("unarmed_trail");
    comboFeel_.SetAwakened(player_->IsAwakened());
    comboFeel_.Update(*player_, weaponColor, camera_.get(), gameAdvanced);
    comboFeel_.UpdateDebugPanel(input_);
}

void GamePlayScene::EmitWeaponElementAura(const std::string& element, const Vector4& color, const Vector3& weaponPos)
{
    // 毎フレーム1〜2粒だけ零し、振りの軌跡に属性の手触りを添える（命中演出の邪魔をしない量に抑える）
    std::uniform_real_distribution<float> jitterD(-kAuraJitter, kAuraJitter);
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
