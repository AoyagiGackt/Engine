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
using namespace engine::graphics;

// スタイルゲージ・覚醒ゲージ・ヒット判定範囲の調整値はResources/Config/combat_tuning.json（CombatTuning）で持つ
namespace engine::game {

namespace {

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

} // namespace

/** @brief UpdateParticles()の下請け 格闘/射撃/瞬歩/覚醒ゲージ・覚醒発動・ランク上昇のスタイル演出パーティクルを更新する */
void GamePlayScene::UpdateStyleTechniqueParticles(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    EmitComboHitParticles(ppos);
    EmitGunFireParticles(ppos);
    EmitWeaponSkillCastParticles(ppos);
    EmitAwakenParticles(ppos, dt);
    EmitStyleRankUpParticles(ppos);
}

/** @brief 敵に攻撃が当たった位置へ星・リング・火花を出す（どの攻撃が敵に入ったかを敵側で見せる） */
void GamePlayScene::EmitEnemyHitEffect(const Vector3& enemyPos, const Vector4& color, float strength,
    int extraBurstCount, float ringRadius)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    // 当て続けるほど盛る（途切れずに当てた手応えを命中演出の量で返す）
    const CombatTuningData& tune = CombatTune();
    ++hitEscalationCount_;
    hitEscalationTimer_ = tune.hitEscalationKeepSeconds;
    const float escalation = HitEscalationRatio();
    strength *= 1.0f + tune.hitEscalationStrengthGain * escalation;
    extraBurstCount += static_cast<int>(static_cast<float>(extraBurstCount) * tune.hitEscalationExtraBurstGain * escalation);
    const int extraSparks = static_cast<int>(static_cast<float>(tune.hitEscalationExtraSparks) * escalation);

    const Vector3& ppos = player_->GetPosition();
    const Vector3 hitPos = {
        enemyPos.x + (ppos.x - enemyPos.x) * visuals.surfaceLerp,
        enemyPos.y + (ppos.y - enemyPos.y) * visuals.surfaceLerp,
        0.0f
    };

    // 白い芯→属性色の星→広がるリング→散る火花の順に重ねて、当たった一点をはっきり見せる
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Particle("enemy_hit_core", hitPos, { .scale = strength });
    fx.Star("enemy_hit", hitPos, { .color = color });
    if (escalation >= tune.hitEscalationMaxThreshold)
    {
        fx.Star("enemy_hit_core", hitPos);
    }
    fx.Ring("enemy_hit", hitPos, { .scale = strength, .color = color });
    if (escalation >= tune.hitEscalationOuterRingThreshold)
    {
        fx.Ring("enemy_hit_outer", hitPos, { .scale = strength, .color = color });
    }
    fx.Sparks("enemy_hit", hitPos, { .scale = strength, .color = color, .extraCount = extraSparks });

    // 武器ごとのweapons.json effect設定（burstCount/ringRadius）を追加の弾け・リングとして重ね、
    // 武器が変わると当たった瞬間の見た目量も変わるようにする
    fx.Sparks("enemy_hit_extra", hitPos, { .scale = strength, .color = color, .extraCount = extraBurstCount });
    if (ringRadius > 0.0f)
    {
        fx.Ring("enemy_hit_extra", hitPos,
            { .scale = strength, .speed = ringRadius, .color = color, .extraCount = extraBurstCount / visuals.extraRingCountDivisor });
    }
}

/** @brief 連続ヒット数に応じた命中演出の盛り度合い（0.0=初撃〜1.0=上限） */
float GamePlayScene::HitEscalationRatio() const
{
    const int hits = (std::max)(hitEscalationCount_ - 1, 0);
    return (std::min)(static_cast<float>(hits) / static_cast<float>((std::max)(CombatTune().hitEscalationMaxHits, 1)), 1.0f);
}

/** @brief 属性名ごとの追加命中演出を出す（近接・固有技・銃で共通） */
void GamePlayScene::EmitElementalHitEffect(const WeaponData& weapon, const Vector3& enemyPos, int comboStep)
{
    EmitElementalHitEffect(weapon.element, ColorFromArray(weapon.effectColor), enemyPos,
        1.0f + CombatTune().elementScalePerComboStep * static_cast<float>((std::max)(comboStep - 1, 0)));
}

/** @brief 属性名ごとの追加命中演出を出す（近接・固有技・銃で共通） */
void GamePlayScene::EmitElementalHitEffect(const std::string& element, const Vector4& color, const Vector3& enemyPos, float scale)
{
    const float stepScale = scale * (1.0f + CombatTune().elementEscalationScaleGain * HitEscalationRatio());
    if (const IElementEffect* effect = IElementEffect::Find(element))
    {
        effect->EmitHit(ElementContext(), enemyPos, color, stepScale);
    }
}

/** @brief 敵が予備動作に入った瞬間に警告リングを出す（攻撃が来ることを事前に伝え、回避を狙えるようにする） */
void GamePlayScene::EmitEnemyTelegraphCue(const EnemyEntity* enemy)
{
    if (enemy == nullptr || !enemy->JustStartedTelegraph())
    {
        return;
    }
    // 予備動作に入った瞬間、足元に輪と頭上に明滅する印を出して攻撃が来ることを知らせる
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("enemy_telegraph", enemy->GetPosition());
    fx.Particle("enemy_telegraph_mark", enemy->GetPosition());
}

/** @brief UpdateStyleTechniqueParticles()の下請け 格闘コンボヒット時の斬撃・属性パーティクルを発生させる */
void GamePlayScene::EmitComboHitParticles(const Vector3& ppos)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    if (!player_->JustComboHit())
    {
        return;
    }
    auto* wm = WeaponManager::GetInstance();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

    // 段が進むほど大きな斬線をスタイル色で出し、締めの段では輪も出す
    const int step = player_->GetComboStep();
    const float dir = player_->GetLastDirX();
    const auto& styleColor = wm->GetList()[wm->GetIndex()].styleColor;
    const Vector3 styleRgb = { styleColor[0], styleColor[1], styleColor[2] };
    fx.Slash("combo_step", ppos, dir,
        { .scale = 1.0f + static_cast<float>(step - 1) * visuals.comboSlashGrowthPerStep, .tint = styleRgb });
    if (step == visuals.comboFinisherStep)
    {
        fx.Ring("combo_step_finisher", ppos, { .tint = styleRgb });
    }

    // 属性演出はConfig/weapons.jsonの共通プリセットから生成する
    // 武器追加時にシーン側へtype分岐を増やさず色と密度を調整できるようにする
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 effectColor = ColorFromArray(weapon.effectColor);
    for (int i = 0; i < weapon.effectBurstCount; ++i)
    {
        const float spread = static_cast<float>((i % visuals.burstColumns) - visuals.burstCenterColumn) * visuals.burstSpreadZ;
        const Vector3 velocity = { dir * (visuals.burstSpeedBase + i * visuals.burstSpeedStep), visuals.burstRiseBase + (i % visuals.burstRiseSteps) * visuals.burstRiseStep, spread };
        fx.Particle("weapon_burst", ppos, { .color = effectColor, .velocity = velocity });
    }
    if (weapon.effectRingRadius > 0.0f)
    {
        fx.Ring("weapon_effect", ppos, { .speed = weapon.effectRingRadius, .color = effectColor, .extraCount = weapon.effectBurstCount });
    }

    // 持ち替え直後の一撃は輪と揺れで強調する
    if (player_->JustWeaponSwitchHit())
    {
        fx.Ring("weapon_switch", ppos, { .tint = styleRgb });
        fx.Impact("weapon_switch");
    }
    // 段が進むほど揺れを大きくする
    fx.Impact("combo_step", { .scale = static_cast<float>(step) });
}

/** @brief UpdateStyleTechniqueParticles()の下請け 銃発射時の弾煙パーティクルを発生させる */
void GamePlayScene::EmitGunFireParticles(const Vector3& ppos)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    if (!player_->JustFired())
    {
        return;
    }
    // 弾数・拡散・色は選択中の銃と段の定義に従う（散弾は扇状に、単発は直線に飛ぶ）
    auto* wm = WeaponManager::GetInstance();
    const GunShotDef* shot = player_->GetActiveGunShot();
    const RangedWeaponData& gun = wm->GetRanged();
    const float dir = player_->GetLastDirX();
    const int bullets = (shot != nullptr) ? (std::max)(shot->bullets, visuals.minBullets) : visuals.fallbackBullets;
    const float spread = (shot != nullptr) ? shot->spreadDeg * GameConstants::kDegToRad : visuals.fallbackSpread;
    const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    // 弾は的（銃口に一番近い相手）で止まる。的が無ければ射程の端まで飛ぶ。寿命のうちに届く速さにする
    const float travel = gunShotPicker_.HasTarget() ? gunShotPicker_.GetHitDistance() : rangeX;
    const float lifetime = HitEffectLibrary::GetInstance()->Particle("gun_shot").lifetime;
    const float travelSpeed = lifetime > 0.0f ? travel / lifetime : 0.0f;
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    for (int i = 0; i < bullets; ++i)
    {
        const float t = (bullets > 1) ? (i / (bullets - 1.0f) - visuals.halfRange) : 0.0f;
        const float speed = visuals.fanSpeedBase + i * visuals.fanSpeedStep;
        fx.Particle("gun_shot", ppos,
            { .color = ColorFromArray(gun.color), .velocity = Vector3 { dir * travelSpeed, speed * spread * t, 0.0f } });
    }
}

/** @brief UpdateStyleTechniqueParticles()の下請け 武器固有技（ダガーのスティンガー以外）発動時に技ごとの見た目を出す */
void GamePlayScene::EmitWeaponSkillCastParticles(const Vector3& ppos)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    // 固有技は発動モーションに合わせた見た目をここで技ごとに出す（命中時の共通バーストはApplyWeaponSkillStyleHit側）
    const auto* wm = WeaponManager::GetInstance();
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 color = ColorFromArray(weapon.effectColor);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const Vector3 chest = { ppos.x, ppos.y + visuals.chestHeight, ppos.z };

    if (player_->IsSkillSequenceActive() && player_->JustComboHit())
    {
        // 固有技の連撃: 段ごとに向きを変えた斬線を重ね、回転斬りの段は輪を広げる。締めは大きな一閃で打ち飛ばしを見せる
        const MeleeAttackDef* step = player_->GetActiveMeleeAttack();
        if (step != nullptr && step->finisher)
        {
            EmitWeaponSkillImpact(chest, color, true);
        }
        else
        {
            const float angle = static_cast<float>(player_->GetComboStep()) * visuals.sequenceSlashAngleStep;
            fx.Slash("skill_sequence", ppos, 0.0f, { .angle = angle, .color = color });
            if (step != nullptr && step->bodyTurns != 0.0f)
            {
                fx.Ring("skill_sequence_turn", ppos, { .color = color });
            }
        }
    }

    // このフレームに出た固有技の、表に書かれた演出（衝撃・輪・左右の斬線・軌跡）を出す
    for (const SkillCastEffect& cast : kSkillCastEffects)
    {
        if (!((*player_).*cast.happened)())
        {
            continue;
        }
        if (cast.impact)
        {
            EmitWeaponSkillImpact(chest, color, false);
        }
        if (cast.ring)
        {
            fx.Ring(cast.ring, ppos, { .color = color });
        }
        if (cast.slash)
        {
            fx.Slash(cast.slash, ppos, 1.0f, { .color = color });
            fx.Slash(cast.slash, ppos, -1.0f, { .color = color });
        }
        if (cast.trail)
        {
            fx.Trail(cast.trail, ppos, color);
        }
    }
}

/** @brief 固有技が決まった瞬間の共通演出（閃光・停止・揺れ・刃の明滅・大きな弾け）を当たり外れに関係なく出す */
void GamePlayScene::EmitWeaponSkillImpact(const Vector3& center, const Vector4& color, bool slashLine)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Flash("skill_impact", { .tint = ColorRgb(color) });
    fx.Impact("skill_impact");
    bladeFlash_.Emit(center, visuals.bladeCount, visuals.bladeRadius, visuals.bladeSpeedMin, visuals.bladeSpeedMax);

    fx.Particle("skill_impact_core", center);
    fx.Star("skill_impact_core", center);
    fx.Star("skill_impact", center, { .color = color });
    fx.Ring("skill_impact_white", center);
    fx.Ring("skill_impact_color", center, { .color = color });
    fx.Sparks("skill_impact", center, { .color = color });

    if (slashLine)
    {
        // 突進の跡を丸ごと一閃で覆う
        const Vector3& cam = camera_->GetTranslate();
        SceneShared::SpawnSlashMarkWorld({ center.x - visuals.slashLineHalfLength, center.y },
            { center.x + visuals.slashLineHalfLength, center.y },
            cam.x, cam.y, visuals.slashLineColor, visuals.slashLineThickness, visuals.slashLineSeconds);
    }
}

/** @brief UpdateStyleTechniqueParticles()の下請け 覚醒中の継続オーラと発動瞬間の衝撃波を発生させる */
void GamePlayScene::EmitAwakenParticles(const Vector3& ppos, float dt)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

    // 覚醒中オーラ（連続）
    if (player_->IsAwakened())
    {
        auraTimer_ += dt;
        if (auraTimer_ >= visuals.auraInterval)
        {
            auraTimer_ = 0.0f;
            fx.Particle("awaken_aura", ppos);
        }
    }
    else
    {
        auraTimer_ = 0.0f;
    }

    // 覚醒発動の瞬間（衝撃波バースト）
    if (player_->JustAwakened())
    {
        fx.Ring("awaken_outer", ppos);
        fx.Ring("awaken_inner", ppos);
        fx.Star("awaken", ppos);
        fx.Impact("awaken");
    }
}

/** @brief UpdateStyleTechniqueParticles()の下請け styleRankHud_のランクが上がった瞬間にリング・火花・カメラシェイク・画面フラッシュを出す */
void GamePlayScene::EmitStyleRankUpParticles(const Vector3& ppos)
{
    // ランク閾値をここで別管理すると表示側(styleRankHud_)とズレるため、判定はHUD自身に問い合わせる
    if (!styleRankHud_.JustRankedUp())
    {
        return;
    }
    const Vector4 rankColor = styleRankHud_.GetRankColor();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("rank_up", ppos, { .color = rankColor });
    fx.Star("rank_up", ppos, { .color = rankColor });
    fx.Impact("rank_up");
    fx.Flash("rank_up", { .tint = ColorRgb(rankColor) });
}

/** @brief 近接コンボのモーション中、装備武器の色で手元にトレイル残像を発生させ続ける */
void GamePlayScene::UpdateWeaponTrail()
{
    // 近接コンボのモーション中、装備武器の手元に色付きの残像を撒いて振りの軌跡を見せる
    const auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon() || !player_->IsMeleeAttacking())
    {
        return;
    }
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 color = ColorFromArray(weapon.effectColor);
    const Vector3 weaponPos = player_->GetActiveWeaponWorldPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Trail("weapon_swing", weaponPos, color);
    EmitWeaponElementAura(weapon.element, color, weaponPos);
}

/** @brief 刃の軌跡・締め演出・F6調整パネルを更新する */
void GamePlayScene::UpdateComboFeel(bool gameAdvanced)
{
    const auto* wm = WeaponManager::GetInstance();
    const Vector4 weaponColor = wm->HasEquippedWeapon()
        ? ColorFromArray(wm->GetCurrent().effectColor)
        : HitEffectLibrary::GetInstance()->Color("unarmed_trail");
    comboFeel_.SetAwakened(player_->IsAwakened());
    comboFeel_.Update(*player_, weaponColor, camera_.get(), gameAdvanced);
    comboFeel_.UpdateDebugPanel(input_);
}

/** @brief 近接モーション中、武器の先から属性ごとの粒子を零す（火の粉・水滴・冷気など） */
void GamePlayScene::EmitWeaponElementAura(const std::string& element, const Vector4& color, const Vector3& weaponPos)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    // 毎フレーム1〜2粒だけ零し、振りの軌跡に属性の手触りを添える（命中演出の邪魔をしない量に抑える）
    std::uniform_real_distribution<float> jitterD(-visuals.auraJitter, visuals.auraJitter);
    const IElementEffect* effect = IElementEffect::Find(element);
    if (effect == nullptr)
    {
        return;
    }
    const Vector3 pos = { weaponPos.x + jitterD(rng_), weaponPos.y + jitterD(rng_), weaponPos.z };
    effect->EmitAura(ElementContext(), pos, color);
}

/** @brief 属性エフェクトに渡す粒子の発生先・乱数・プレイヤーの向きをまとめる */
ElementEffectContext GamePlayScene::ElementContext()
{
    return { *pm_, rng_, player_->GetLastDirX() };
}

/** @brief UpdateParticles()の下請け ボスの予告円→着弾ダメージのAoEスラム攻撃を処理する */
void GamePlayScene::UpdateBossSlamAttack(float dt)
{
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    if (!enemy_ || enemy_->IsDefeated() || !enemy_->IsVisible())
    {
        return;
    }

    if (!bossSlamWarningActive_)
    {
        const Vector3& ppos = player_->GetPosition();
        const Vector3& epos = enemy_->GetPosition();
        const float dx = ppos.x - epos.x;
        const float dy = ppos.y - epos.y;
        if (dx * dx + dy * dy > EnemyTuning::GetInstance()->BossSlam().engageRange * EnemyTuning::GetInstance()->BossSlam().engageRange)
        {
            return; // ボスと交戦中でなければタイマーを進めない（戦闘開始前に発動しないように）
        }
        bossSlamTimer_ -= dt;
        if (bossSlamTimer_ <= 0.0f)
        {
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
        * (visuals.warningMinScale + (1.0f - visuals.warningMinScale) * progress);
    const float blink = 0.5f + 0.5f * std::sin(bossSlamWarningTimer_ * visuals.warningBlinkSpeed);
    bossSlamWarningSprite_->SetColor({ 1.0f, visuals.warningGreenBase + progress * visuals.warningGreenGrowth, visuals.warningBlue,
        visuals.warningAlphaBase + blink * visuals.warningAlphaBlink });
    bossSlamWarningSprite_->SetPosition({ sx - diameterPx * 0.5f, sy - diameterPx * 0.5f });
    bossSlamWarningSprite_->SetSize({ diameterPx, diameterPx });
    bossSlamWarningSprite_->Update();

    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("slam_warning", bossSlamTargetPos_,
        { .speed = EnemyTuning::GetInstance()->BossSlam().radius,
            .tint = Vector3 { 1.0f, visuals.warningRingGreenBase - progress * visuals.warningRingGreenFade, visuals.warningRingBlueBase - progress * visuals.warningRingBlueFade } });

    if (bossSlamWarningTimer_ > 0.0f)
    {
        return;
    }

    // 着弾判定
    bossSlamWarningActive_ = false;
    bossSlamTimer_ = EnemyTuning::GetInstance()->BossSlam().interval;

    const Vector3& ppos = player_->GetPosition();
    const float dx = ppos.x - bossSlamTargetPos_.x;
    const float dy = ppos.y - bossSlamTargetPos_.y;
    const float slamRadius = EnemyTuning::GetInstance()->BossSlam().radius;
    if (dx * dx + dy * dy <= slamRadius * slamRadius && !TryJustDodge(bossSlamTargetPos_) && !player_->IsInvincible())
    {
        RunData::GetInstance()->TakeDamage(EnemyTuning::GetInstance()->BossSlam().damage);
        player_->OnHit();
        fx.Impact("boss_slam");
    }

    // 内側と外側（半径の倍率はレシピの speed）の2重のリングと火花
    fx.Ring("boss_slam_inner", bossSlamTargetPos_, { .speed = slamRadius });
    fx.Ring("boss_slam_outer", bossSlamTargetPos_, { .speed = slamRadius });
    fx.Sparks("boss_slam", bossSlamTargetPos_);
}

} // namespace engine::game
