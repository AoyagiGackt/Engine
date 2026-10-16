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
constexpr float kStageHalfDepth = 0.5f; // 2.5Dステージの奥行き半分
constexpr float kUnitHalfExtent = 0.5f; // 拡縮1の箱の半分の大きさ

// 壊せる設置物は赤く脈動させて、攻撃で壊せると分かりやすくする
constexpr float kBreakablePulseSpeed = 4.0f;
constexpr float kBreakablePulseAmplitude = 0.25f;

// 敵の近接攻撃は体の少し上を薙ぐ（演出とジャスト回避の判定位置）
constexpr float kSwingCenterHeight = 0.4f;
constexpr float kMinAimLength = 0.001f; // これ未満の距離では狙いの方向を正規化しない

// ボスの叩きつけの予告円（進行度で円を広げ、点滅で視線を引く）
constexpr float kWarningMinScale = 0.6f; // 予告開始時の円の大きさ（着弾範囲比）。進行に応じて1.0まで広げる
constexpr float kWarningBlinkSpeed = 22.0f;
constexpr float kWarningGreenBase = 0.15f;
constexpr float kWarningGreenGrowth = 0.1f;
constexpr float kWarningBlue = 0.1f;
constexpr float kWarningAlphaBase = 0.35f;
constexpr float kWarningAlphaBlink = 0.4f;
constexpr float kWarningRingGreenBase = 0.3f;
constexpr float kWarningRingGreenFade = 0.15f;
constexpr float kWarningRingBlueBase = 0.15f;
constexpr float kWarningRingBlueFade = 0.1f;

AABB BreakableBounds(const BreakableRef& barrel)
{
    // 攻撃判定と射撃の的選びで同じ大きさを使う。見た目の箱よりわずかに広く当たりを取り、かすった攻撃も拾う
    const ObjectDesc& desc = *barrel.desc;
    const float halfExtentX = kUnitHalfExtent * std::abs(desc.scale.x) + Tune().breakableHitPadding;
    const float halfExtentY = kUnitHalfExtent * std::abs(desc.scale.y) + Tune().breakableHitPadding;
    return {
        { barrel.position.x - halfExtentX, barrel.position.y - halfExtentY, -kStageHalfDepth },
        { barrel.position.x + halfExtentX, barrel.position.y + halfExtentY, kStageHalfDepth }
    };
}

Vector4 Rgba(const float (&color)[4])
{
    return { color[0], color[1], color[2], color[3] };
}
}

void GamePlayScene::BuildGunShotTarget()
{
    if (!player_->JustFired()) {
        gunShotPicker_.Clear();
        return;
    }
    const auto* wm = WeaponManager::GetInstance();
    const GunShotDef* shot = player_->GetActiveGunShot();
    const Vector3& playerPos = player_->GetPosition();
    const float rangeX = wm->GetRanged().range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    gunShotPicker_.Begin(playerPos, player_->GetLastDirX(),
        SceneShared::MakeDirectionalShotRange(playerPos, player_->GetLastDirX(), rangeX, Tune().gunBackRange));

    // 弾は最初に当たった相手で止まる（ボス・道中の敵・壊せる物の中から銃口に一番近い1つだけが的）
    if (!enemy_->IsDefeated() && enemy_->IsVisible()) {
        gunShotPicker_.Consider(enemy_, GetEnemyHitBox());
    }
    for (const auto& entry : weaponEnemies_) {
        if (entry.enemy->IsDefeated()) {
            continue;
        }
        const Vector3& enemyPos = entry.enemy->GetPosition();
        const AABB enemyBounds = {
            { enemyPos.x - Tune().enemyHitBoxHalfExtent, enemyPos.y - Tune().enemyHitBoxHalfExtent, -kStageHalfDepth },
            { enemyPos.x + Tune().enemyHitBoxHalfExtent, enemyPos.y + Tune().enemyHitBoxHalfExtent, kStageHalfDepth }
        };
        gunShotPicker_.Consider(entry.enemy, enemyBounds);
    }
    for (const BreakableRef& barrel : GetStageEditor().GetBreakables()) {
        gunShotPicker_.Consider(barrel.desc, BreakableBounds(barrel));
    }
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
            bool meleeHit = false;
            bool gunHit = false;
            bool skillHit = false;
            if (wm->HasEquippedWeapon() && player_->JustComboHit()) {
                const AABB range = SceneShared::MakeDirectionalShotRange(
                    playerPos, player_->GetLastDirX(), wm->GetCurrent().range,
                    wm->GetCurrent().range * GameConstants::kSkillRearReachMult);
                AABB airFriendlyRange = range;
                if (!player_->IsOnGround()) {
                    // 空中では上下と横を少し広げ、高さが合わずに空振りするのを減らす
                    airFriendlyRange.min.y = playerPos.y - Tune().weaponEnemyAirRangeHalfHeight;
                    airFriendlyRange.max.y = playerPos.y + Tune().weaponEnemyAirRangeHalfHeight;
                    airFriendlyRange.min.x -= Tune().weaponEnemyAirRangeWidthPadding;
                    airFriendlyRange.max.x += Tune().weaponEnemyAirRangeWidthPadding;
                }
                hit = Collision::CheckCollision(airFriendlyRange, enemyBounds);
                meleeHit = hit;
            }
            if (!hit && player_->JustFired()) {
                // 弾は手前の相手で止まるので、射撃の的に選ばれた時だけ当たる
                hit = gunShotPicker_.IsTarget(entry.enemy);
                gunHit = hit;
            }
            if (!hit && player_->JustWeaponSkillHit()) {
                const float skillRadius = SkillRadiusFor(Tune().weaponEnemySkillRadius, player_->JustGreatswordSlam());
                const AABB range = {
                    { playerPos.x - skillRadius, playerPos.y - Tune().skillRangeHalfHeight, -kStageHalfDepth },
                    { playerPos.x + skillRadius, playerPos.y + Tune().skillRangeHalfHeight, kStageHalfDepth }
                };
                hit = Collision::CheckCollision(range, enemyBounds);
                // スピン連射は手数の多い射撃扱い、それ以外の固有技は必殺の一撃として扱う
                skillHit = hit && !player_->JustSpinShot();
                gunHit = hit && player_->JustSpinShot();
            }
            if (hit) {
                const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
                const float damageMult = attack != nullptr ? attack->damageMult : 1.0f;
                const float baseDamage = wm->HasEquippedWeapon() ? wm->GetCurrent().damage : Tune().unarmedBaseDamage;
                const int slamBonus = (player_->JustGreatswordSlam() && HasBossSlamTechnique())
                    ? GameRules::GetInstance()->Get().bossTechniqueBonusDamage
                    : 0;
                const float rawDamage = player_->JustGreatswordSlam()
                    ? static_cast<float>(Tune().weaponEnemySlamBaseDamage + slamBonus)
                    : baseDamage * damageMult / Tune().meleeDamageDivisor * (skillHit ? Tune().weaponEnemySkillDamageMult : 1.0f);
                const int damage = (std::max)(1, static_cast<int>(std::round(rawDamage * CurrentDamageMult())));
                // 覚醒中の多段ヒットの2発目以降は、押し出し・止め・締め演出を軽くして刻みの速さを見せる
                const bool extraHit = meleeHit && player_->JustComboExtraHit();
                const float knockbackMult = (wm->HasEquippedWeapon()
                                                ? wm->GetCurrent().knockbackMult
                                                : 1.0f)
                    * player_->GetAwakenedKnockbackMult() * Tune().knockbackScale
                    * (extraHit ? GameConstants::kAwakenExtraHitKnockScale : 1.0f);
                const bool comboFinisher = meleeHit && !extraHit && IsMeleeFinisher(wm->GetCurrent().type, attack);
                // 固有技は締めと同じく大きく吹き飛ばす
                const bool blowAway = comboFinisher || skillHit;
                // 近接と銃は武器ごとの疲労でダメージが落ちる（固有技はクールタイムがあるので対象外）
                const bool firedHit = gunHit && player_->JustFired();
                const bool skillSequence = meleeHit && player_->IsSkillSequenceActive();
                const int fatigueIndex = skillSequence ? -1 : meleeHit ? HeldMeleeFatigueIndex() : (firedHit ? GunFatigueIndex() : -1);
                const float fatigueMult = WeaponFatigueDamageMult(fatigueIndex);
                entry.enemy->TakeDamageScaled(static_cast<float>(damage) * fatigueMult);
                RegisterFatigueHit(fatigueIndex, meleeHit ? Tune().weaponFatiguePerHit : Tune().gunFatiguePerHit);
                const float knockY = (skillHit ? Tune().skillKnockY : (attack != nullptr ? attack->knockY : Tune().defaultKnockY)) * knockbackMult;
                if (skillSequence && attack != nullptr && attack->finisher) {
                    entry.enemy->ApplyHomeRun(player_->GetLastDirX());
                } else {
                    entry.enemy->ApplyComboReaction(player_->GetLastDirX() * knockbackMult, knockY,
                        player_->JustWeaponSwitchHit(), playerPos.x, blowAway);
                }
                if (meleeHit) {
                    entry.enemy->ApplyHitstun(MeleeHitstunSeconds(attack), true);
                } else {
                    entry.enemy->ApplyHitstun(gunHit ? Tune().gunHitstunSeconds : Tune().skillHitstunSeconds, !gunHit);
                }
                if (wm->HasEquippedWeapon() && wm->GetCurrent().type == WeaponType::Dagger) {
                    entry.enemy->ApplySlow(Tune().daggerSlowSeconds);
                }
                Vector4 hitColor = wm->HasEquippedWeapon()
                    ? Rgba(wm->GetCurrent().effectColor)
                    : HitEffectLibrary::GetInstance()->Color("unarmed_hit");
                const int extraBurstCount = wm->HasEquippedWeapon() ? wm->GetCurrent().effectBurstCount : 0;
                const float ringRadius = wm->HasEquippedWeapon() ? wm->GetCurrent().effectRingRadius : 0.0f;
                float hitStrength = skillHit ? Tune().weaponEnemySkillHitStrength : 1.0f;
                const Vector4 elementColor = hitColor;
                ApplyFatigueToHitEffect(fatigueMult, hitColor, hitStrength);
                EmitEnemyHitEffect(enemyPos, hitColor, hitStrength, extraBurstCount, ringRadius);
                if (meleeHit) {
                    comboFeel_.OnMeleeHit(attack, comboFinisher, enemyPos, elementColor, player_->GetLastDirX(), camera_.get());
                }
                // 属性ごとの追加演出（銃は銃の属性、それ以外は手持ち武器の属性）。効きが落ちた武器では出さない
                // 何で当てたかごとの属性演出（上の行から順に見て、当てはまった1つだけを出す）
                struct HitSource {
                    bool fired;
                    bool skill;
                    bool melee;
                };
                struct ElementalHitRule {
                    bool (*applies)(const HitSource&);
                    void (*emit)(GamePlayScene&, const Vector3& position, const Vector4& weaponColor);
                };
                static constexpr ElementalHitRule kElementalHitRules[] = {
                    { [](const HitSource& h) { return h.fired; },
                        [](GamePlayScene& scene, const Vector3& position, const Vector4&) {
                            const RangedWeaponData& gun = WeaponManager::GetInstance()->GetRanged();
                            scene.EmitElementalHitEffect(gun.element, Rgba(gun.effectColor), position, Tune().gunElementScale);
                        } },
                    { [](const HitSource& h) { return h.skill; },
                        [](GamePlayScene& scene, const Vector3& position, const Vector4& weaponColor) {
                            scene.EmitElementalHitEffect(WeaponManager::GetInstance()->GetCurrent().element, weaponColor, position, Tune().skillElementScale);
                        } },
                    { [](const HitSource& h) { return h.melee; },
                        [](GamePlayScene& scene, const Vector3& position, const Vector4&) {
                            scene.EmitElementalHitEffect(WeaponManager::GetInstance()->GetCurrent(), position, scene.player_->GetComboStep());
                        } },
                };
                // 効きが落ちた武器では属性演出を出さない
                if (fatigueMult >= 1.0f) {
                    const HitSource source = { firedHit, skillHit, meleeHit };
                    for (const ElementalHitRule& rule : kElementalHitRules) {
                        if (rule.applies(source)) {
                            rule.emit(*this, enemyPos, elementColor);
                            break;
                        }
                    }
                }

                // 段ごとのhitStop（MeleeCombo.cppで武器・段別に調整済み）をヒットストップとカメラ揺れへ反映する
                // 固有技は段定義を持たないため、締め扱いの大きな停止と揺れにする
                if (extraHit) {
                    RequestMeleeHitStopAndShake(nullptr, false, GameConstants::kHitStopAwakenExtra);
                } else {
                    RequestMeleeHitStopAndShake(skillHit ? nullptr : attack, blowAway, Tune().defaultHitStopFrames);
                }
            }
        }

        if (!entry.weaponAcquired && entry.enemy->IsDefeated() && !entry.hasWeapon) {
            // モンスターは奪う武器が無いので、倒れた姿を少し見せてから弾けて消え、覚醒ゲージになる
            if (entry.vanishTimer <= 0.0f) {
                entry.vanishTimer = Tune().monsterVanishDelay;
            }
            entry.vanishTimer -= GameConstants::kFrameDeltaTime;
            if (entry.vanishTimer <= 0.0f) {
                const Vector3 vanishPos = entry.enemy->GetPosition();
                const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
                fx.Ring("monster_vanish", vanishPos);
                fx.Star("monster_vanish", vanishPos);
                entry.enemy->SetVisible(false);
                player_->ChargeAwakenGauge(Tune().monsterVanishGauge);
                entry.weaponAcquired = true;
            }
            continue;
        }
        if (!entry.weaponAcquired && entry.enemy->IsDefeated()) {
            // 打ち飛ばされて武器を落としていれば、本体ではなく落ちた武器の場所で奪う
            const Vector3 enemyPos = entry.enemy->GetWeaponPickupPosition();
            const float dx = playerPos.x - enemyPos.x;
            const float dy = playerPos.y - enemyPos.y;
            const float absorbRange = Tune().absorbRange;
            if (!entry.absorbing && dx * dx + dy * dy <= absorbRange * absorbRange
                && input_->TriggerAction(Input::Action::Steal)) {
                entry.absorbing = true;
                entry.absorbTimer = Tune().absorbDuration;
                player_->PlayStealStab();
                const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
                fx.Ring("absorb", enemyPos);
                fx.Flash("absorb");
            }
            if (entry.absorbing) {
                entry.absorbTimer -= GameConstants::kFrameDeltaTime;
                const float pullRate = Tune().absorbPullRate;
                const Vector3 absorbTarget = { playerPos.x, playerPos.y + Tune().absorbTargetHeight, playerPos.z };
                if (entry.enemy->HasDroppedWeapon()) {
                    entry.enemy->PullDroppedWeaponToward(absorbTarget, pullRate);
                } else {
                    Vector3& absorbPos = entry.enemy->GetPositionRef();
                    absorbPos.x += (absorbTarget.x - absorbPos.x) * pullRate;
                    absorbPos.y += (absorbTarget.y - absorbPos.y) * pullRate;
                    entry.enemy->RefreshVisualTransforms();
                }
                if (entry.absorbTimer <= 0.0f) {
                    entry.weaponAcquired = true;
                    entry.enemy->SetVisible(false);
                    const WeaponManager::AcquireResult acquired = WeaponManager::GetInstance()->Acquire(entry.weaponType);
                    ApplyWeaponAcquireResult(acquired);
                }
            }
        }
    }
}

void GamePlayScene::ApplyWeaponAcquireResult(WeaponManager::AcquireResult result)
{
    // AcquireResult ごとの後処理（交換が要る場合は呼び出し側が交換画面を出す）
    struct AcquireOutcome {
        WeaponManager::AcquireResult result;
        void (*apply)(GamePlayScene&);
    };
    static constexpr AcquireOutcome kOutcomes[] = {
        { WeaponManager::AcquireResult::Added, [](GamePlayScene& scene) { scene.hud_.Notify(HudEvent::WeaponAcquired); } },
        { WeaponManager::AcquireResult::Duplicate, [](GamePlayScene& scene) {
             scene.player_->ChargeAwakenGauge(Tune().duplicateWeaponAwakenBonus);
         } },
    };
    for (const AcquireOutcome& outcome : kOutcomes) {
        if (outcome.result == result) {
            outcome.apply(*this);
        }
    }
}

void GamePlayScene::UpdateExplosiveBarrels()
{
    // 収集物（pickup）の回収はStageEditor側が行う。ここでは壊せる物（breakable）のヒット判定と爆風ダメージだけを担当する
    const Vector3& playerPos = player_->GetPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const auto* wm = WeaponManager::GetInstance();
    explosiveBarrelPulse_ += GameConstants::kFrameDeltaTime;

    for (BreakableRef barrel : GetStageEditor().GetBreakables()) {
        const ObjectDesc& desc = *barrel.desc;

        // 赤く脈動させて攻撃で壊せる設置物だと分かりやすくする（表示行列の更新はStageEditor側が毎フレーム行う）
        const float pulse = 1.0f - kBreakablePulseAmplitude + std::sin(explosiveBarrelPulse_ * kBreakablePulseSpeed) * kBreakablePulseAmplitude;
        barrel.object->SetColor({ desc.breakableColor.x, desc.breakableColor.y * pulse, desc.breakableColor.z, desc.breakableColor.w });
        barrel.object->Update();

        const AABB barrelBounds = BreakableBounds(barrel);

        // ヒット判定は道中の武器敵(UpdateWeaponEnemies)と同じ3つの攻撃窓（近接コンボ/射撃/固有技）を流用する
        bool hit = false;
        if (wm->HasEquippedWeapon() && player_->JustComboHit()) {
            const AABB range = SceneShared::MakeDirectionalRange(
                playerPos, player_->GetLastDirX(), wm->GetCurrent().range,
                wm->GetCurrent().range * GameConstants::kSkillRearReachMult);
            hit = Collision::CheckCollision(range, barrelBounds);
        }
        if (!hit && player_->JustFired()) {
            // 弾は手前の相手で止まるので、射撃の的に選ばれた時だけ当たる
            hit = gunShotPicker_.IsTarget(barrel.desc);
        }
        if (!hit && player_->JustWeaponSkillHit()) {
            const AABB range = {
                { playerPos.x - Tune().weaponEnemySkillRadius, playerPos.y - Tune().skillRangeHalfHeight, -kStageHalfDepth },
                { playerPos.x + Tune().weaponEnemySkillRadius, playerPos.y + Tune().skillRangeHalfHeight, kStageHalfDepth }
            };
            hit = Collision::CheckCollision(range, barrelBounds);
        }

        // 武器指定つき（壊せる壁など）は、その武器の近接攻撃・固有技でしか壊れない。銃では壊れない
        if (hit && !desc.breakableWeapon.empty()) {
            const bool weaponMatches = wm->HasEquippedWeapon()
                && wm->GetCurrent().type == ParseWeaponTypeName(desc.breakableWeapon);
            const bool meleeOrSkillHit = player_->JustComboHit() || !player_->JustFired();
            if (!weaponMatches || !meleeOrSkillHit) {
                fx.Star("breakable_deflect", barrel.position); // 弾かれた手応え
                hit = false;
            }
        }
        if (!hit) {
            continue;
        }

        (*barrel.hp)--;
        fx.Star("breakable_damage", barrel.position);
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

        fx.Impact("breakable");
        fx.Ring("breakable", barrel.position, { .speed = desc.breakableRadius });
        fx.Sparks("breakable", barrel.position);
    }
}

void GamePlayScene::UpdatePlayerEnemyContactHit(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    // 敵との当たり判定（倒れた敵・消えた敵の体には当たらない。倒した後も判定が残って止めと揺れが出続けないように）
    hitCooldown_ -= dt;
    if (enemy_->IsDefeated() || !enemy_->IsVisible()) {
        return;
    }
    {
        Collider playerCol = player_->GetCollider();
        const Vector3& epos = enemy_->GetPosition();
        AABB enemyAABB = { { epos.x - Tune().enemyHitBoxHalfExtent, epos.y - Tune().enemyHitBoxHalfExtent, -kStageHalfDepth },
            { epos.x + Tune().enemyHitBoxHalfExtent, epos.y + Tune().enemyHitBoxHalfExtent, kStageHalfDepth } };
        if (Collision::CheckCollision(playerCol.aabb, enemyAABB) && hitCooldown_ <= 0.0f) {
            hitCooldown_ = Tune().contactHitCooldown;
            enemy_->TakeDamage(Tune().contactDamage);

            // 演出はプレイヤーと敵の中間に出す
            const Vector3 hitPos = { (ppos.x + epos.x) * 0.5f, (ppos.y + epos.y) * 0.5f, 0.0f };
            const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
            fx.Impact("contact");
            fx.Ring("contact", hitPos);
            fx.Sparks("contact", hitPos);
        }
    }
}

void GamePlayScene::UpdateEnemyAttackOnPlayer(float dt)
{
    const Vector3& ppos = player_->GetPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

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
        fx.Ring("enemy_muzzle", epos);
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
        fx.Particle("enemy_tracer", bullet.pos);

        if (bullet.timer <= 0.0f) {
            it = enemyBullets_.erase(it);
            continue;
        }

        const float bulletHalf = Tune().enemyBulletHalfExtent;
        AABB bulletAABB = { { bullet.pos.x - bulletHalf, bullet.pos.y - bulletHalf, -kStageHalfDepth },
            { bullet.pos.x + bulletHalf, bullet.pos.y + bulletHalf, kStageHalfDepth } };
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

        fx.Impact("bullet_hit");
        fx.Ring("bullet_hit", bullet.pos);
        fx.Sparks("player_hit", bullet.pos);
        it = enemyBullets_.erase(it);
    }
}

void GamePlayScene::ApplyEnemyMeleeSwing(EnemyEntity* attacker)
{
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    const Vector3& epos = attacker->GetPosition();
    const Vector3& ppos = player_->GetPosition();
    const float dirX = ppos.x >= epos.x ? 1.0f : -1.0f;

    // 振った軌跡を見せる（当たらなくても振ったことが分かるように）
    const Vector3 swingCenter = { epos.x + dirX * tuning.meleeReach * 0.5f, epos.y + kSwingCenterHeight, 0.0f };
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Slash("enemy_swing", swingCenter, dirX);

    // 前方だけに判定を出す（背後にいるプレイヤーには当たらない＝回り込みが有効）
    const AABB swingRange = {
        { (std::min)(epos.x, epos.x + dirX * tuning.meleeReach), epos.y - tuning.meleeHitHalfHeight, -kStageHalfDepth },
        { (std::max)(epos.x, epos.x + dirX * tuning.meleeReach), epos.y + tuning.meleeHitHalfHeight, kStageHalfDepth }
    };
    if (!Collision::CheckCollision(player_->GetCollider().aabb, swingRange)) {
        return;
    }
    if (TryJustDodge(swingCenter) || player_->IsInvincible()) {
        return;
    }

    RunData::GetInstance()->TakeDamage(attacker->GetAttackDamage());
    player_->OnHit();
    fx.Impact("enemy_swing");
    fx.Sparks("player_hit", ppos);
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
    const float diameterPx = EnemyTuning::GetInstance()->BossSlam().radius * 2.0f * pxPerWorldUnit
        * (kWarningMinScale + (1.0f - kWarningMinScale) * progress);
    const float blink = 0.5f + 0.5f * std::sin(bossSlamWarningTimer_ * kWarningBlinkSpeed);
    bossSlamWarningSprite_->SetColor({ 1.0f, kWarningGreenBase + progress * kWarningGreenGrowth, kWarningBlue,
        kWarningAlphaBase + blink * kWarningAlphaBlink });
    bossSlamWarningSprite_->SetPosition({ sx - diameterPx * 0.5f, sy - diameterPx * 0.5f });
    bossSlamWarningSprite_->SetSize({ diameterPx, diameterPx });
    bossSlamWarningSprite_->Update();

    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("slam_warning", bossSlamTargetPos_,
        { .speed = EnemyTuning::GetInstance()->BossSlam().radius,
            .tint = Vector3 { 1.0f, kWarningRingGreenBase - progress * kWarningRingGreenFade, kWarningRingBlueBase - progress * kWarningRingBlueFade } });

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
        fx.Impact("boss_slam");
    }

    // 内側と外側（半径の倍率はレシピの speed）の2重のリングと火花
    fx.Ring("boss_slam_inner", bossSlamTargetPos_, { .speed = slamRadius });
    fx.Ring("boss_slam_outer", bossSlamTargetPos_, { .speed = slamRadius });
    fx.Sparks("boss_slam", bossSlamTargetPos_);
}
