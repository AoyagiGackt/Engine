/**
 * @file GamePlaySceneEnemyAttacks.cpp
 * @brief 敵・ギミックの更新とプレイヤーへの被弾処理
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "GraphicsTuning.h"
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
constexpr float kStageHalfDepth = 0.5f; // 2.5Dステージの奥行き半分
constexpr float kUnitHalfExtent = 0.5f; // 拡縮1の箱の半分の大きさ

// 壊せる設置物は赤く脈動させて、攻撃で壊せると分かりやすくする
/** @brief 壊せる設置物の点滅に使う描画設定 */
struct BreakablePulseStyle {
    float speed = GraphicsTuning::Float("breakable.pulseSpeed", 4.0f);
    float amplitude = GraphicsTuning::Float("breakable.pulseAmplitude", 0.25f, 0.0f, 0.5f);
};

/** @brief 描画設定を初回だけ読み込み、フレーム間で共有する */
const BreakablePulseStyle& BreakablePulse()
{
    static const BreakablePulseStyle kStyle;
    return kStyle;
}

// 敵の近接攻撃は体の少し上を薙ぐ（演出とジャスト回避の判定位置）
constexpr float kMinAimLength = 0.001f; // これ未満の距離では狙いの方向を正規化しない

// ボスの叩きつけの予告円（進行度で円を広げ、点滅で視線を引く）

/** @brief 武器の命中時に付与する減速時間を定義表から取得する */
float WeaponHitSlowSeconds(WeaponType type)
{
    using Duration = float CombatTuningData::*;
    static constexpr Duration kDurations[] = {
        nullptr, nullptr, nullptr, &CombatTuningData::daggerSlowSeconds,
        nullptr, nullptr, nullptr, nullptr,
    };
    static_assert(std::size(kDurations) == kWeaponTypeCount);
    const size_t index = static_cast<size_t>(type);
    const Duration duration = index < std::size(kDurations) ? kDurations[index] : nullptr;
    return duration ? CombatTune().*duration : 0.0f;
}

/** @brief 壊せる物の当たり判定の箱をワールド座標で返す */
AABB BreakableBounds(const BreakableRef& barrel)
{
    // 攻撃判定と射撃の的選びで同じ大きさを使う。見た目の箱よりわずかに広く当たりを取り、かすった攻撃も拾う
    const BreakableSettings desc = barrel.Settings();
    const float halfExtentX = kUnitHalfExtent * std::abs(desc.scale.x) + CombatTune().breakableHitPadding;
    const float halfExtentY = kUnitHalfExtent * std::abs(desc.scale.y) + CombatTune().breakableHitPadding;
    return {
        { barrel.GetPosition().x - halfExtentX, barrel.GetPosition().y - halfExtentY, -kStageHalfDepth },
        { barrel.GetPosition().x + halfExtentX, barrel.GetPosition().y + halfExtentY, kStageHalfDepth }
    };
}

}

/** @brief 撃ったフレームに、ボス・道中の敵・壊せる物から銃口に一番近い1体を射撃の的に選ぶ */
void GamePlayScene::BuildGunShotTarget()
{
    if (!player_->JustFired())
    {
        gunShotPicker_.Clear();
        return;
    }
    // 銃の射程（射撃コンボの段の倍率込み）で前方と少しの後方を候補範囲にする
    const auto* wm = WeaponManager::GetInstance();
    const GunShotDef* shot = player_->GetActiveGunShot();
    const Vector3& playerPos = player_->GetPosition();
    const float rangeX = wm->GetRanged().range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    gunShotPicker_.Begin(playerPos, player_->GetLastDirX(),
        SceneShared::MakeDirectionalShotRange(playerPos, player_->GetLastDirX(), rangeX, CombatTune().gunBackRange));

    // 弾は最初に当たった相手で止まる（ボス・道中の敵・壊せる物の中から銃口に一番近い1つだけが的）
    if (!enemy_->IsDefeated() && enemy_->IsVisible())
    {
        gunShotPicker_.Consider(enemy_, GetEnemyHitBox());
    }
    for (const auto& entry : weaponEnemies_)
    {
        if (entry.enemy->IsDefeated())
        {
            continue;
        }
        gunShotPicker_.Consider(entry.enemy, EnemyHitBoxAt(entry.enemy->GetPosition()));
    }
    for (const BreakableRef& barrel : GetStageEditor().GetBreakables())
    {
        gunShotPicker_.Consider(barrel.TargetIdentity(), BreakableBounds(barrel));
    }
}

/** @brief 道中の武器敵を更新し、攻撃と武器奪取を処理する */
void GamePlayScene::UpdateWeaponEnemies()
{
    // entry.enemyの物理/アニメーション更新自体はStageEditor所有のためGetStageEditor().UpdateObjects()
    // （BaseScene::Tick()がUpdate()の直後に呼ぶ）が担う。ここではヒット判定・武器奪取だけを行う
    for (auto& entry : weaponEnemies_)
    {
        // 生きている敵には、このフレームの攻撃が当たったかを調べて反映する
        if (!entry.enemy->IsDefeated())
        {
            WeaponEnemyHit hit = DetectWeaponEnemyHit(entry, EnemyHitBoxAt(entry.enemy->GetPosition()));
            if (hit.hit)
            {
                ApplyWeaponEnemyHit(entry, hit);
            }
        }
        // 倒れた後は、モンスターなら消えて覚醒ゲージに、武器持ちなら武器の奪取を待つ
        if (!entry.weaponAcquired && entry.enemy->IsDefeated() && !entry.hasWeapon)
        {
            UpdateMonsterVanish(entry);
            continue;
        }
        if (!entry.weaponAcquired && entry.enemy->IsDefeated())
        {
            UpdateWeaponSteal(entry);
        }
    }
}

/** @brief 道中の武器敵に、このフレームの近接・射撃・固有技のどれが当たったかを調べる */
GamePlayScene::WeaponEnemyHit GamePlayScene::DetectWeaponEnemyHit(const WeaponEnemyEntry& entry, const AABB& enemyBounds) const
{
    const Vector3& playerPos = player_->GetPosition();
    const auto* wm = WeaponManager::GetInstance();
    WeaponEnemyHit result;
    // 近接: 武器の届く範囲の箱と敵の箱が重なったか
    if (wm->HasEquippedWeapon() && player_->JustComboHit())
    {
        const AABB range = SceneShared::MakeDirectionalShotRange(
            playerPos, player_->GetLastDirX(), wm->GetCurrent().range,
            wm->GetCurrent().range * CombatTune().skillRearReachMult);
        AABB airFriendlyRange = range;
        if (!player_->IsOnGround())
        {
            // 空中では上下と横を少し広げ、高さが合わずに空振りするのを減らす
            WidenAirMeleeRange(airFriendlyRange, playerPos,
                CombatTune().weaponEnemyAirRangeHalfHeight, CombatTune().weaponEnemyAirRangeWidthPadding);
        }
        result.hit = Collision::CheckCollision(airFriendlyRange, enemyBounds);
        result.melee = result.hit;
    }
    if (!result.hit && player_->JustFired())
    {
        // 弾は手前の相手で止まるので、射撃の的に選ばれた時だけ当たる
        result.hit = gunShotPicker_.IsTarget(entry.enemy);
        result.gun = result.hit;
    }
    // 固有技: プレイヤーを中心にした横長の範囲に入っていたか
    if (!result.hit && player_->JustWeaponSkillHit())
    {
        const float skillRadius = SkillRadiusFor(CombatTune().weaponEnemySkillRadius, player_->JustGreatswordSlam());
        result.hit = Collision::CheckCollision(SkillRangeAround(playerPos, skillRadius), enemyBounds);
        // スピン連射は手数の多い射撃扱い、それ以外の固有技は必殺の一撃として扱う
        result.skill = result.hit && !player_->JustSpinShot();
        result.gun = result.hit && player_->JustSpinShot();
    }
    return result;
}

/** @brief 命中時のダメージ量を武器・段・固有技から計算する */
int GamePlayScene::ComputeWeaponEnemyDamage(const WeaponEnemyHit& hit) const
{
    const auto* wm = WeaponManager::GetInstance();
    // 段の倍率は近接の段定義から、素手なら素手の基礎ダメージを使う
    const float damageMult = hit.attack != nullptr ? hit.attack->damageMult : 1.0f;
    const float baseDamage = wm->HasEquippedWeapon() ? wm->GetCurrent().damage : CombatTune().unarmedBaseDamage;
    // 大剣の叩きつけは固定ダメージ（ボスから技を奪っていれば上乗せ）
    const int slamBonus = (player_->JustGreatswordSlam() && HasBossSlamTechnique())
        ? GameRules::GetInstance()->Get().bossTechniqueBonusDamage
        : 0;
    const float rawDamage = player_->JustGreatswordSlam()
        ? static_cast<float>(CombatTune().weaponEnemySlamBaseDamage + slamBonus)
        : baseDamage * damageMult / CombatTune().meleeDamageDivisor * (hit.skill ? CombatTune().weaponEnemySkillDamageMult : 1.0f);
    // 覚醒などの全体倍率をかけ、最低でも1は入れる
    return (std::max)(1, static_cast<int>(std::round(rawDamage * CurrentDamageMult())));
}

/** @brief 命中した武器敵にダメージ・吹き飛び・のけぞりを与え、演出を出す */
void GamePlayScene::ApplyWeaponEnemyHit(WeaponEnemyEntry& entry, WeaponEnemyHit& hit)
{
    const Vector3& playerPos = player_->GetPosition();
    const auto* wm = WeaponManager::GetInstance();
    hit.attack = player_->GetActiveMeleeAttack();
    const int damage = ComputeWeaponEnemyDamage(hit);
    // 覚醒中の多段ヒットの2発目以降は、押し出し・止め・締め演出を軽くして刻みの速さを見せる
    hit.extraHit = hit.melee && player_->JustComboExtraHit();
    const float knockbackMult = (wm->HasEquippedWeapon()
                                    ? wm->GetCurrent().knockbackMult
                                    : 1.0f)
        * player_->GetAwakenedKnockbackMult() * CombatTune().knockbackScale
        * (hit.extraHit ? CombatTune().awakenExtraHitKnockScale : 1.0f);
    hit.comboFinisher = hit.melee && !hit.extraHit && IsMeleeFinisher(wm->GetCurrent().type, hit.attack);
    // 固有技は締めと同じく大きく吹き飛ばす
    hit.blowAway = hit.comboFinisher || hit.skill;
    // 近接と銃は武器ごとの疲労でダメージが落ちる（固有技はクールタイムがあるので対象外）
    hit.fired = hit.gun && player_->JustFired();
    const bool skillSequence = hit.melee && player_->IsSkillSequenceActive();
    const int fatigueIndex = skillSequence ? -1 : hit.melee ? HeldMeleeFatigueIndex() : (hit.fired ? GunFatigueIndex() : -1);
    hit.fatigueMult = WeaponFatigueDamageMult(fatigueIndex);
    entry.enemy->TakeDamageScaled(static_cast<float>(damage) * hit.fatigueMult);
    RegisterFatigueHit(fatigueIndex, hit.melee ? CombatTune().weaponFatiguePerHit : CombatTune().gunFatiguePerHit);

    // 吹き飛び: 固有技の連続技の締めは場外まで飛ばし、それ以外は段ごとの高さで押し出す
    const float knockY = (hit.skill ? CombatTune().skillKnockY : (hit.attack != nullptr ? hit.attack->knockY : CombatTune().defaultKnockY)) * knockbackMult;
    if (skillSequence && hit.attack != nullptr && hit.attack->finisher)
    {
        entry.enemy->ApplyHomeRun(player_->GetLastDirX());
    }
    else
    {
        entry.enemy->ApplyComboReaction(player_->GetLastDirX() * knockbackMult, knockY,
            player_->JustWeaponSwitchHit(), playerPos.x, hit.blowAway);
    }
    // のけぞり: 近接は段ごとの長さ、銃と固有技は調整値の長さ
    if (hit.melee)
    {
        entry.enemy->ApplyHitstun(MeleeHitstunSeconds(hit.attack), true);
    }
    else
    {
        entry.enemy->ApplyHitstun(hit.gun ? CombatTune().gunHitstunSeconds : CombatTune().skillHitstunSeconds, !hit.gun);
    }
    // 武器によっては当てた敵の動きを遅くする
    const float slowSeconds = wm->HasEquippedWeapon() ? WeaponHitSlowSeconds(wm->GetCurrent().type) : 0.0f;
    if (slowSeconds > 0.0f)
    {
        entry.enemy->ApplySlow(slowSeconds);
    }
    EmitWeaponEnemyHitEffects(entry.enemy->GetPosition(), hit);
}

/** @brief 命中時のパーティクル・属性演出・ヒットストップを出す */
void GamePlayScene::EmitWeaponEnemyHitEffects(const Vector3& enemyPos, const WeaponEnemyHit& hit)
{
    const auto* wm = WeaponManager::GetInstance();
    // 命中の火花は武器の色と大きさで出し、疲労している武器では色と強さを落とす
    Vector4 hitColor = wm->HasEquippedWeapon()
        ? ColorFromArray(wm->GetCurrent().effectColor)
        : HitEffectLibrary::GetInstance()->Color("unarmed_hit");
    const int extraBurstCount = wm->HasEquippedWeapon() ? wm->GetCurrent().effectBurstCount : 0;
    const float ringRadius = wm->HasEquippedWeapon() ? wm->GetCurrent().effectRingRadius : 0.0f;
    float hitStrength = hit.skill ? CombatTune().weaponEnemySkillHitStrength : 1.0f;
    const Vector4 elementColor = hitColor;
    ApplyFatigueToHitEffect(hit.fatigueMult, hitColor, hitStrength);
    EmitEnemyHitEffect(enemyPos, hitColor, hitStrength, extraBurstCount, ringRadius);
    if (hit.melee)
    {
        comboFeel_.OnMeleeHit(hit.attack, hit.comboFinisher, enemyPos, elementColor, player_->GetLastDirX(), camera_.get());
    }
    // 属性ごとの追加演出（銃は銃の属性、それ以外は手持ち武器の属性）。効きが落ちた武器では出さない
    // 何で当てたかごとの属性演出（上の行から順に見て、当てはまった1つだけを出す）
    /** @brief 命中の種類ごとに適用する属性演出の規則 */
    struct ElementalHitRule {
        bool (*applies)(const WeaponEnemyHit&);
        void (*emit)(GamePlayScene&, const Vector3& position, const Vector4& weaponColor);
    };
    static constexpr ElementalHitRule kElementalHitRules[] = {
        { [](const WeaponEnemyHit& h) {
            return h.fired;
        },
            [](GamePlayScene& scene, const Vector3& position, const Vector4&) {
                const RangedWeaponData& gun = WeaponManager::GetInstance()->GetRanged();
                scene.EmitElementalHitEffect(gun.element, ColorFromArray(gun.effectColor), position, CombatTune().gunElementScale);
            } },
        { [](const WeaponEnemyHit& h) {
            return h.skill;
        },
            [](GamePlayScene& scene, const Vector3& position, const Vector4& weaponColor) {
                scene.EmitElementalHitEffect(WeaponManager::GetInstance()->GetCurrent().element, weaponColor, position, CombatTune().skillElementScale);
            } },
        { [](const WeaponEnemyHit& h) {
            return h.melee;
        },
            [](GamePlayScene& scene, const Vector3& position, const Vector4&) {
                scene.EmitElementalHitEffect(WeaponManager::GetInstance()->GetCurrent(), position, scene.player_->GetComboStep());
            } },
    };
    // 効きが落ちた武器では属性演出を出さない
    if (hit.fatigueMult >= 1.0f)
    {
        for (const ElementalHitRule& rule : kElementalHitRules)
        {
            if (rule.applies(hit))
            {
                rule.emit(*this, enemyPos, elementColor);
                break;
            }
        }
    }

    // 段ごとのhitStop（MeleeCombo.cppで武器・段別に調整済み）をヒットストップとカメラ揺れへ反映する
    // 固有技は段定義を持たないため、締め扱いの大きな停止と揺れにする
    if (hit.extraHit)
    {
        RequestMeleeHitStopAndShake(nullptr, false, HitEffectLibrary::GetInstance()->SceneParticles().hitStopAwakenExtra);
    }
    else
    {
        RequestMeleeHitStopAndShake(hit.skill ? nullptr : hit.attack, hit.blowAway, CombatTune().defaultHitStopFrames);
    }
}

/** @brief 武器を持たないモンスターが倒れたら、少し待って弾けて消し、覚醒ゲージにする */
void GamePlayScene::UpdateMonsterVanish(WeaponEnemyEntry& entry)
{
    // モンスターは奪う武器が無いので、倒れた姿を少し見せてから弾けて消え、覚醒ゲージになる
    if (entry.vanishTimer <= 0.0f)
    {
        entry.vanishTimer = CombatTune().monsterVanishDelay;
    }
    entry.vanishTimer -= GameConstants::kFrameDeltaTime;
    if (entry.vanishTimer > 0.0f)
    {
        return;
    }
    // 消える瞬間に輪と星を出し、以後は奪取済みとして扱う
    const Vector3 vanishPos = entry.enemy->GetPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("monster_vanish", vanishPos);
    fx.Star("monster_vanish", vanishPos);
    entry.enemy->SetVisible(false);
    player_->ChargeAwakenGauge(CombatTune().monsterVanishGauge);
    entry.weaponAcquired = true;
}

/** @brief 倒れた武器持ち敵の武器を、奪取ボタンで吸い寄せて手に入れる */
void GamePlayScene::UpdateWeaponSteal(WeaponEnemyEntry& entry)
{
    const Vector3& playerPos = player_->GetPosition();
    // 打ち飛ばされて武器を落としていれば、本体ではなく落ちた武器の場所で奪う
    const Vector3 enemyPos = entry.enemy->GetWeaponPickupPosition();
    const float dx = playerPos.x - enemyPos.x;
    const float dy = playerPos.y - enemyPos.y;
    const float absorbRange = CombatTune().absorbRange;
    // 吸収範囲の中で奪取ボタンを押したら、突き刺し動作と演出を出して吸い寄せ始める
    if (!entry.absorbing && dx * dx + dy * dy <= absorbRange * absorbRange
        && input_->TriggerAction(Input::Action::Steal))
    {
        entry.absorbing = true;
        entry.absorbTimer = CombatTune().absorbDuration;
        player_->PlayStealStab();
        const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
        fx.Ring("absorb", enemyPos);
        fx.Flash("absorb");
    }
    if (!entry.absorbing)
    {
        return;
    }
    // 吸い寄せ中: 落ちた武器か敵本体をプレイヤーの少し上へ引き寄せる
    entry.absorbTimer -= GameConstants::kFrameDeltaTime;
    const float pullRate = CombatTune().absorbPullRate;
    const Vector3 absorbTarget = { playerPos.x, playerPos.y + CombatTune().absorbTargetHeight, playerPos.z };
    if (entry.enemy->HasDroppedWeapon())
    {
        entry.enemy->PullDroppedWeaponToward(absorbTarget, pullRate);
    }
    else
    {
        entry.enemy->MoveTowardXY(absorbTarget, pullRate);
        entry.enemy->RefreshVisualTransforms();
    }
    // 吸い寄せ終わったら武器を手に入れる（満杯なら交換待ちになる）
    if (entry.absorbTimer <= 0.0f)
    {
        entry.weaponAcquired = true;
        entry.enemy->SetVisible(false);
        const WeaponManager::AcquireResult acquired = WeaponManager::GetInstance()->Acquire(entry.weaponType);
        ApplyWeaponAcquireResult(acquired);
    }
}

/** @brief 武器を奪った結果に応じた後処理（新しく増えたら通知、持っている武器なら覚醒ゲージへ） */
void GamePlayScene::ApplyWeaponAcquireResult(WeaponManager::AcquireResult result)
{
    // AcquireResult ごとの後処理（交換が要る場合は呼び出し側が交換画面を出す）
    struct AcquireOutcome {
        WeaponManager::AcquireResult result;
        void (*apply)(GamePlayScene&);
    };
    static constexpr AcquireOutcome kOutcomes[] = {
        {
            WeaponManager::AcquireResult::Added, [](GamePlayScene& scene) {
                scene.hud_.Notify(HudEvent::WeaponAcquired);
            }
        },
        { WeaponManager::AcquireResult::Duplicate, [](GamePlayScene& scene) {
             scene.player_->ChargeAwakenGauge(CombatTune().duplicateWeaponAwakenBonus);
         } },
    };
    for (const AcquireOutcome& outcome : kOutcomes)
    {
        if (outcome.result == result)
        {
            outcome.apply(*this);
        }
    }
}

/** @brief レベルに配置された壊せる物（kind=="breakable"）のヒット判定・爆発ダメージ・演出を処理する */
void GamePlayScene::UpdateExplosiveBarrels()
{
    // 収集物（pickup）の回収はStageEditor側が行う。ここでは壊せる物（breakable）のヒット判定と爆風ダメージだけを担当する
    const Vector3& playerPos = player_->GetPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const auto* wm = WeaponManager::GetInstance();
    explosiveBarrelPulse_ += GameConstants::kFrameDeltaTime;

    for (BreakableRef barrel : GetStageEditor().GetBreakables())
    {
        const BreakableSettings desc = barrel.Settings();

        // 赤く脈動させて攻撃で壊せる設置物だと分かりやすくする（表示行列の更新はStageEditor側が毎フレーム行う）
        const auto& pulseStyle = BreakablePulse();
        const float pulse = 1.0f - pulseStyle.amplitude
            + std::sin(explosiveBarrelPulse_ * pulseStyle.speed) * pulseStyle.amplitude;
        barrel.SetColor({ desc.color.x, desc.color.y * pulse, desc.color.z, desc.color.w });

        const AABB barrelBounds = BreakableBounds(barrel);

        // ヒット判定は道中の武器敵(UpdateWeaponEnemies)と同じ3つの攻撃窓（近接コンボ/射撃/固有技）を流用する
        bool hit = false;
        if (wm->HasEquippedWeapon() && player_->JustComboHit())
        {
            const AABB range = SceneShared::MakeDirectionalRange(
                playerPos, player_->GetLastDirX(), wm->GetCurrent().range,
                wm->GetCurrent().range * CombatTune().skillRearReachMult);
            hit = Collision::CheckCollision(range, barrelBounds);
        }
        if (!hit && player_->JustFired())
        {
            // 弾は手前の相手で止まるので、射撃の的に選ばれた時だけ当たる
            hit = gunShotPicker_.IsTarget(barrel.TargetIdentity());
        }
        if (!hit && player_->JustWeaponSkillHit())
        {
            const AABB range = {
                { playerPos.x - CombatTune().weaponEnemySkillRadius, playerPos.y - CombatTune().skillRangeHalfHeight, -kStageHalfDepth },
                { playerPos.x + CombatTune().weaponEnemySkillRadius, playerPos.y + CombatTune().skillRangeHalfHeight, kStageHalfDepth }
            };
            hit = Collision::CheckCollision(range, barrelBounds);
        }

        // 武器指定つき（壊せる壁など）は、その武器の近接攻撃・固有技でしか壊れない。銃では壊れない
        if (hit && !desc.requiredWeapon.empty())
        {
            const bool weaponMatches = wm->HasEquippedWeapon()
                && wm->GetCurrent().type == ParseWeaponTypeName(std::string(desc.requiredWeapon));
            const bool meleeOrSkillHit = player_->JustComboHit() || !player_->JustFired();
            if (!weaponMatches || !meleeOrSkillHit)
            {
                fx.Star("breakable_deflect", barrel.GetPosition()); // 弾かれた手応え
                hit = false;
            }
        }
        if (!hit)
        {
            continue;
        }

        const bool destroyed = barrel.TakeHit();
        fx.Star("breakable_damage", barrel.GetPosition());
        if (!destroyed)
        {
            continue;
        }

        // 破壊: 爆風範囲内のプレイヤー/敵にまとめてダメージを与える（環境を利用した攻撃手段）
        barrel.Destroy();
        GameFlags::GetInstance()->SetFlag("broken_" + std::string(desc.name), true);

        const float radiusSq = desc.radius * desc.radius;
        if (DistanceSquaredXY(playerPos, barrel.GetPosition()) <= radiusSq && !TryJustDodge(barrel.GetPosition()) && !player_->IsInvincible())
        {
            RunData::GetInstance()->TakeDamage(desc.playerDamage);
            player_->OnHit();
        }
        if (enemy_ && !enemy_->IsDefeated())
        {
            const Vector3& epos = enemy_->GetPosition();
            if (DistanceSquaredXY(epos, barrel.GetPosition()) <= radiusSq)
            {
                enemy_->TakeDamage(desc.enemyDamage);
            }
        }
        for (auto& entry : weaponEnemies_)
        {
            if (entry.enemy->IsDefeated())
            {
                continue;
            }
            const Vector3& wepos = entry.enemy->GetPosition();
            if (DistanceSquaredXY(wepos, barrel.GetPosition()) <= radiusSq)
            {
                entry.enemy->TakeDamage(desc.enemyDamage);
            }
        }

        fx.Impact("breakable");
        fx.Ring("breakable", barrel.GetPosition(), { .speed = desc.radius });
        fx.Sparks("breakable", barrel.GetPosition());
    }
}

/** @brief UpdateParticles()の下請け プレイヤーと敵の接触ヒット判定・ダメージ・演出を処理する */
void GamePlayScene::UpdatePlayerEnemyContactHit(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    // 敵との当たり判定（倒れた敵・消えた敵の体には当たらない。倒した後も判定が残って止めと揺れが出続けないように）
    hitCooldown_ -= dt;
    if (enemy_->IsDefeated() || !enemy_->IsVisible())
    {
        return;
    }
    {
        Collider playerCol = player_->GetCollider();
        const Vector3& epos = enemy_->GetPosition();
        const AABB enemyAABB = GetEnemyHitBox();
        if (Collision::CheckCollision(playerCol.aabb, enemyAABB) && hitCooldown_ <= 0.0f)
        {
            hitCooldown_ = CombatTune().contactHitCooldown;
            enemy_->TakeDamage(CombatTune().contactDamage);

            // 演出はプレイヤーと敵の中間に出す
            const Vector3 hitPos = { (ppos.x + epos.x) * 0.5f, (ppos.y + epos.y) * 0.5f, 0.0f };
            const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
            fx.Impact("contact");
            fx.Ring("contact", hitPos);
            fx.Sparks("contact", hitPos);
        }
    }
}

/** @brief UpdateParticles()の下請け 敵の弾の発射・飛翔・プレイヤーへの命中時のダメージ・無敵開始・演出を処理する */
void GamePlayScene::UpdateEnemyAttackOnPlayer(float dt)
{
    const Vector3& ppos = player_->GetPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

    // 予備動作に入った敵には警告リングを出す（攻撃が来る前に知らせ、回避を狙えるようにする）
    EmitEnemyTelegraphCue(enemy_);
    for (const auto& entry : weaponEnemies_)
    {
        EmitEnemyTelegraphCue(entry.enemy);
    }

    // 予備動作明けの瞬間 近接の敵は前方を薙ぎ払い、遠隔の敵（槍・ボール）は狙いを一度だけ計算して実弾を撃つ
    // ボスは間合いなら薙ぎ払い、遠ければ弾（近づいても離れても攻撃が来る）
    // （発射の瞬間にプレイヤーが射程外にいる敵は撃たない遠くの敵に一方的に狙撃されないように）
    auto fireBulletFrom = [&](EnemyEntity* shooter) {
        if (!shooter->JustFiredAttack())
        {
            return;
        }
        if (shooter->IsMeleeAttacker())
        {
            const bool isBoss = shooter == enemy_;
            const float reach = EnemyTuning::GetInstance()->Basic().meleeReach;
            const bool inReach = std::abs(ppos.x - shooter->GetPosition().x) <= reach;
            if (!isBoss || inReach)
            {
                ApplyEnemyMeleeSwing(shooter);
                return;
            }
        }
        const Vector3& epos = shooter->GetPosition();
        // pos_ はAABB中心（当たり判定の基準点）そのものなので、狙い・発射位置ともにオフセットを足さずここから直接計算する
        Vector3 dir = { ppos.x - epos.x, ppos.y - epos.y, 0.0f };
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > EnemyTuning::GetInstance()->Bullet().fireRange)
        {
            return;
        }
        if (len > kMinAimLength)
        {
            dir.x /= len;
            dir.y /= len;
        }
        // 狙った向きに弾を作り、銃口の輪を出す
        EnemyBullet bullet;
        bullet.pos = epos;
        bullet.vel = { dir.x * EnemyTuning::GetInstance()->Bullet().speed, dir.y * EnemyTuning::GetInstance()->Bullet().speed, 0.0f };
        bullet.timer = EnemyTuning::GetInstance()->Bullet().lifetime;
        bullet.damage = shooter->GetAttackDamage();
        enemyBullets_.push_back(bullet);
        fx.Ring("enemy_muzzle", epos);
    };
    fireBulletFrom(enemy_);
    for (auto& entry : weaponEnemies_)
    {
        fireBulletFrom(entry.enemy);
    }

    // 飛んでいる弾を進め、寿命が尽きたら消す
    for (auto it = enemyBullets_.begin(); it != enemyBullets_.end();)
    {
        EnemyBullet& bullet = *it;
        bullet.pos.x += bullet.vel.x * dt;
        bullet.pos.y += bullet.vel.y * dt;
        bullet.timer -= dt;
        // 曳光弾の見た目実体（Object3d）は持たず、毎フレーム現在位置に粒を撒いて弾の軌跡に見せる
        fx.Particle("enemy_tracer", bullet.pos);

        if (bullet.timer <= 0.0f)
        {
            it = enemyBullets_.erase(it);
            continue;
        }

        // プレイヤーに重なったら、回避中ならジャスト回避、無敵中なら素通り、それ以外は被弾
        const float bulletHalf = CombatTune().enemyBulletHalfExtent;
        AABB bulletAABB = { { bullet.pos.x - bulletHalf, bullet.pos.y - bulletHalf, -kStageHalfDepth },
            { bullet.pos.x + bulletHalf, bullet.pos.y + bulletHalf, kStageHalfDepth } };
        Collider playerCol = player_->GetCollider();
        if (!Collision::CheckCollision(playerCol.aabb, bulletAABB))
        {
            ++it;
            continue;
        }

        // 回避中に弾が体を通過したらジャスト回避。弾はそのまま飛び続ける（避けた感を残す）
        if (TryJustDodge(bullet.pos))
        {
            ++it;
            continue;
        }
        if (player_->IsInvincible())
        {
            ++it;
            continue;
        }

        // 被弾: HPを減らし、プレイヤーをのけぞらせて弾を消す
        RunData::GetInstance()->TakeDamage(bullet.damage);
        player_->OnHit();

        fx.Impact("bullet_hit");
        fx.Ring("bullet_hit", bullet.pos);
        fx.Sparks("player_hit", bullet.pos);
        it = enemyBullets_.erase(it);
    }
}

/** @brief 近接敵の攻撃発生フレームに前方判定を出し、命中ならプレイヤーへダメージを与える */
void GamePlayScene::ApplyEnemyMeleeSwing(EnemyEntity* attacker)
{
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    const Vector3& epos = attacker->GetPosition();
    const Vector3& ppos = player_->GetPosition();
    const float dirX = ppos.x >= epos.x ? 1.0f : -1.0f;

    // 振った軌跡を見せる（当たらなくても振ったことが分かるように）
    const Vector3 swingCenter = { epos.x + dirX * tuning.meleeReach * 0.5f, epos.y + HitEffectLibrary::GetInstance()->SceneParticles().swingCenterHeight, 0.0f };
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Slash("enemy_swing", swingCenter, dirX);

    // 前方だけに判定を出す（背後にいるプレイヤーには当たらない＝回り込みが有効）
    const AABB swingRange = {
        { (std::min)(epos.x, epos.x + dirX * tuning.meleeReach), epos.y - tuning.meleeHitHalfHeight, -kStageHalfDepth },
        { (std::max)(epos.x, epos.x + dirX * tuning.meleeReach), epos.y + tuning.meleeHitHalfHeight, kStageHalfDepth }
    };
    if (!Collision::CheckCollision(player_->GetCollider().aabb, swingRange))
    {
        return;
    }
    if (TryJustDodge(swingCenter) || player_->IsInvincible())
    {
        return;
    }

    RunData::GetInstance()->TakeDamage(attacker->GetAttackDamage());
    player_->OnHit();
    fx.Impact("enemy_swing");
    fx.Sparks("player_hit", ppos);
}

} // namespace engine::game
