/**
 * @file BattleTestSceneCombat.cpp
 * @brief BattleTestSceneの戦闘判定（近接/固有技/銃/乱舞/フィニッシャー/ダミー/ロックオン/配置ナイト）を実装するファイル
 * @note BattleTestScene の戦闘判定だけを BattleTestScene.cpp から分けて置いている（クラスは BattleTestScene のまま）
 */
#include "BattleTestScene.h"
#include "SceneVisualTuning.h"
#include "HitEffectLibrary.h"
#include "CombatTuning.h"
#include "BattleTestTuning.h"
#include "AudioBridge.h"
#include "BattleTestSceneRenderer.h"
#include "Collision.h"
#include "DiagnosticsDraw.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
using namespace engine::graphics;

// 固有技（スペースキー）のダミー用ヒット定義。ApplyMeleeHitToDummy が参照するのは
// id/damageMult/knockX/knockY/launcher/hitStop のみなので、コンボ制御用フィールドは0で埋める
namespace engine::game {
static constexpr MeleeAttackDef kGreatswordSlamSkill = { "gs_slam", 1.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.40f, 0.30f, true, 10, 0.0f, false, { }, { }, { }, { } };
static constexpr MeleeAttackDef kAxeChargeSkill = { "axe_charge", 1.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.08f, false, 6, 0.0f, false, { }, { }, { }, { } };

// グレートソード 投げ回転斬りの渦。knockXは0にして吸い込みと喧嘩させず、代わりに小さく打ち上げて多段ヒット感を出す
static constexpr MeleeAttackDef kGreatswordSpinSkill = { "gsw_spin", 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.05f, false, 2, 0.0f, false, { }, { }, { }, { } };

// 近接攻撃・固有技の判定ボックス関連
static constexpr float kStageHalfDepth = 0.5f; ///< 2.5Dステージの奥行き半分
static constexpr float kDummyHalfExtent = 0.5f; ///< ダミーは 1×1×1 の箱として扱う

// 攻撃倍率・判定
static constexpr float kMinBulletSpeed = 0.001f; ///< これ未満の弾速はノックバック方向を決めない

// グレートソード投げ
static constexpr float kSpinSuctionMinDistance = 0.05f; ///< これより近いダミーは吸い込まない（方向が定まらないため）

// ダミーの物理
static constexpr float kDummyWallEpsilon = 0.01f; ///< 壁に接したとみなす距離
static constexpr float kDummyYawOffset = -gameConstants::kHalfPi; ///< スライムのモデルの正面を右向きに合わせる補正（enemy_params.json の Slime と同じ）
static constexpr float kDummyTurnRate = 0.2f; ///< 1フレームで目標の向きへ寄せる割合

/**
 * @brief 固有技（スペースキー）1件ぶんの発生条件と判定パラメータ
 * @note 新しい武器固有技はこの表に1行足すだけで追加できる
 */
struct WeaponSkillEntry {
    bool (Player::*justTriggered)() const; ///< 発生フレームを返すPlayerのゲッター
    const MeleeAttackDef* def; ///< ダメージ・ノックバック定義
    float reachMult; ///< weapon.range に掛ける射程係数
    bool symmetricAoE; ///< true=前後対称の設置型AoE / false=前方指向性
    bool screenImpact; ///< true=ヒットストップ＋画面フラッシュの大技演出つき
};
static constexpr WeaponSkillEntry kWeaponSkills[] = {
    { &Player::JustGreatswordSlam, &kGreatswordSlamSkill, 1.00f, true, true },
    { &Player::JustAxeCharge, &kAxeChargeSkill, 0.85f, false, false },
};
/** @brief ダミー1体のAABBを返す（当たり判定用） */
AABB BattleTestScene::DummyBounds(const Dummy& d) {
    return { { d.pos.x - kDummyHalfExtent, d.pos.y - kDummyHalfExtent, -kStageHalfDepth },
        { d.pos.x + kDummyHalfExtent, d.pos.y + kDummyHalfExtent, kStageHalfDepth } };
}

/** @brief ダミーに命中した時の演出を出す位置（胴の高さ）を返す */
Vector3 BattleTestScene::DummyHitEffectPosition(const Dummy& d) {
    return { d.pos.x, d.pos.y + SceneVisualTuning::BattleCombat().hitEffectHeight, 0.0f };
}

/** @brief 覚醒/アックス怒り状態を反映した現在の攻撃力倍率を返す */
float BattleTestScene::ComputeAttackMult() const {
    return (player_->IsAwakened() ? BattleTestTune().awakenedAttackMult : 1.0f) * player_->GetAxeRageMult();
}

/** @brief 命中判定の材料を今の状態からまとめて取る */
BattleTestScene::AttackFrame BattleTestScene::CaptureAttackFrame() const {
    return { weaponManager_->GetCurrent(), player_->GetPosition(), ComputeAttackMult() };
}

// ══════════════════════════════════════════════════════
// 戦闘判定
// ══════════════════════════════════════════════════════

/** @brief 格闘コンボのヒット判定を処理し、命中があれば true を返す */
bool BattleTestScene::UpdateMeleeComboHit() {
    // 格闘コンボ（L キー）
    // ヒットはボタン押下の瞬間ではなく、モーション中の hitTime で発生する（MeleeComboController 管理）。
    // 連打間隔・段ごとの威力/リーチ/打ち上げは全て武器タイプ別の MeleeAttackDef が持つ
    if (!player_->JustComboHit()) {
        return false;
    }

    const AttackFrame attack = CaptureAttackFrame();

    bool hitConfirmed = false;
    const MeleeAttackDef* atk = player_->GetActiveMeleeAttack();
    const float rangeMult = (atk != nullptr) ? atk->rangeMult : 1.0f;
    const float baseReach = attack.weapon.range * rangeMult;
    // ヒット判定時点のプレイヤー座標は既にlungeDistぶん敵へ踏み込み済みなので、
    // ここで武器のリーチ（weapon.range）をそのまま足すと踏み込み+リーチの二重計上になり、間合い外から当たって見える。
    // 踏み込みぶんを差し引いて、始点からの合計到達距離がbaseReachに収まるようにする
    const float lungeDist = (atk != nullptr) ? atk->lungeDist : 0.0f;
    const float meleeReach = (std::max)(baseReach - lungeDist, baseReach * BattleTestTune().minMeleeReachRatio);
    const float dirX = player_->GetLastDirX();
    // 前方に厚く、背後は振り抜きぶんだけ（左右対称だと背後の遠い敵にまで当たってしまう）
    AABB meleeRange = sceneShared::MakeDirectionalRange(attack.playerPos, dirX, meleeReach, meleeReach * CombatTune().skillRearReachMult);
    // ロック中は判定を広げてロックしたのに届かないを減らす（距離無制限ヒットはやめる）
    AABB assistRange = sceneShared::MakeDirectionalRange(attack.playerPos, dirX, meleeReach * BattleTestTune().lockAssistReachMult, meleeReach * BattleTestTune().lockAssistRearMult);
    for (size_t di = 0; di < dummies_.size(); ++di) {
        auto& d = dummies_[di];
        if (d.hp <= 0.0f) {
            continue;
        }
        bool isLocked = (lockedDummyIndex_ == di);
        bool hit = Collision::CheckCollision(isLocked ? assistRange : meleeRange, DummyBounds(d));
        if (hit && atk != nullptr) {
            hitConfirmed = true;
            ApplyMeleeHitToDummy(d, atk, attack.attackMult);
        }
    }
    if (hitConfirmed) {
        player_->ChargeAwakenGauge(BattleTestTune().meleeAwakenGain);
    }
    return hitConfirmed;
}

/** @brief 武器固有技（Space キー）のヒット判定を処理し、命中があれば true を返す */
bool BattleTestScene::UpdateWeaponSkillHits() {
    // SPACE固有技の攻撃判定と演出を武器ごとの定義から適用する
    // 発生条件・射程係数・判定形状は kWeaponSkills テーブルが持つ
    const AttackFrame attack = CaptureAttackFrame();

    bool hitConfirmed = false;
    for (const auto& skill : kWeaponSkills) {
        if (!((*player_).*skill.justTriggered)()) {
            continue;
        }
        const float reach = attack.weapon.range * skill.reachMult;
        // 通常は前方に厚い指向性判定、設置型AoEのみ前後対称に叩きつける
        const AABB skillRange = skill.symmetricAoE
            ? AABB { { attack.playerPos.x - reach, attack.playerPos.y - BattleTestTune().slamRangeBelowY, -kStageHalfDepth },
                  { attack.playerPos.x + reach, attack.playerPos.y + BattleTestTune().slamRangeAboveY, kStageHalfDepth } }
            : sceneShared::MakeDirectionalRange(attack.playerPos, player_->GetLastDirX(), reach, reach * CombatTune().skillRearReachMult);
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) {
                continue;
            }
            if (Collision::CheckCollision(skillRange, DummyBounds(d))) {
                hitConfirmed = true;
                ApplyMeleeHitToDummy(d, skill.def, attack.attackMult);
            }
        }
        if (skill.screenImpact) {
            TimeManager::GetInstance()->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopLaunch);
            ScreenFlash::GetInstance()->Request(SceneVisualTuning::BattleCombat().slamFlashColor, SceneVisualTuning::BattleCombat().slamFlashDuration);
        }
    }
    return hitConfirmed;
}

/** @brief グレートソード 投げ回転斬りの渦（吸い込み+多段ヒット）を処理する命中があれば true を返す */
bool BattleTestScene::UpdateGreatswordSpin() {
    // 投げた瞬間（まだ飛行中でIsGreatswordSpinning()はfalse）に発射エフェクトを出す
    if (player_->JustGreatswordThrown()) {
        const WeaponData& weapon = weaponManager_->GetCurrent();
        const Vector4 effectColor = ColorFromArray(weapon.effectColor);
        const Vector3& pp = player_->GetPosition();
        const Vector3 launchPos = { pp.x, pp.y + SceneVisualTuning::BattleCombat().throwEffectHeight, 0.0f };
        pm_->EmitRing("bt_hit_ring", launchPos, SceneVisualTuning::BattleCombat().throwRingSpeed, effectColor, SceneVisualTuning::BattleCombat().throwRingCount, SceneVisualTuning::BattleCombat().throwRingLifetime, SceneVisualTuning::BattleCombat().throwRingSize);
        for (int i = 0; i < SceneVisualTuning::BattleCombat().throwSparkCount; ++i) {
            const float speed = SceneVisualTuning::BattleCombat().throwSparkSpeedBase + i * SceneVisualTuning::BattleCombat().throwSparkSpeedStep;
            pm_->EmitGravity("bt_hit_spark", launchPos,
                { player_->GetLastDirX() * speed, SceneVisualTuning::BattleCombat().throwSparkRiseBase + (i % SceneVisualTuning::BattleCombat().throwSparkRiseSteps) * SceneVisualTuning::BattleCombat().throwSparkRiseStep, 0.0f },
                effectColor, SceneVisualTuning::BattleCombat().throwSparkLifetime, SceneVisualTuning::BattleCombat().throwSparkSize);
        }
    }

    // グレートソード 投げ回転斬り: 静止した大剣が渦になっている間、周囲のダミーを毎フレーム引き寄せ、
    // 一定間隔で範囲内のダミーへまとめてヒットさせる（吸い込みと着弾を別々に判定する）
    if (!player_->IsGreatswordSpinning()) {
        return false;
    }

    // 渦の吸い込み範囲にいるダミーを、渦の中心へ向けて引き寄せる
    const Vector3& center = player_->GetGreatswordThrowPos();
    for (auto& d : dummies_) {
        if (d.hp <= 0.0f) {
            continue;
        }
        const float dx = center.x - d.pos.x;
        const float dist = std::abs(dx);
        if (dist < BattleTestTune().greatswordSpinSuctionRadius && dist > kSpinSuctionMinDistance) {
            d.knockVelX += (dx / dist) * BattleTestTune().greatswordSpinSuctionSpeed;
        }
    }

    if (!player_->JustGreatswordSpinHit()) {
        return false;
    }

    // ヒットの瞬間: 渦の位置に輪を出し、当たり範囲内のダミー全員に当てる
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector4 effectColor = ColorFromArray(weapon.effectColor);
    pm_->EmitRing("bt_hit_ring", center, BattleTestTune().greatswordSpinHitRadius, effectColor, SceneVisualTuning::BattleCombat().spinHitRingCount, SceneVisualTuning::BattleCombat().spinHitRingLifetime, SceneVisualTuning::BattleCombat().spinHitRingSize);

    const float atkMult = ComputeAttackMult();
    bool hitConfirmed = false;
    for (auto& d : dummies_) {
        if (d.hp <= 0.0f) {
            continue;
        }
        if (std::abs(d.pos.x - center.x) <= BattleTestTune().greatswordSpinHitRadius) {
            hitConfirmed = true;
            ApplyMeleeHitToDummy(d, &kGreatswordSpinSkill, atkMult);
        }
    }
    return hitConfirmed;
}

/** @brief 射撃コンボのヒットスキャン判定を処理する命中があれば true を返す */
bool BattleTestScene::UpdateGunShotHit() {
    // 射撃コンボ（K キー）
    // 発砲はボタン押下の瞬間ではなく、段の shotTime で発生する（GunComboController 管理）。
    // 弾数・射程倍率・ノックバック・打ち上げ・ヒットストップは全て銃種別の GunShotDef が持つ
    if (!player_->JustFired()) {
        gunShotPicker_.Clear();
        return false;
    }

    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();

    bool hitConfirmed = false;
    const GunShotDef* shot = player_->GetActiveGunShot();
    const RangedWeaponData& gun = weaponManager_->GetRanged();
    const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    // 銃口の向きにだけ飛ぶ（背後は銃身ぶんの余裕のみ）
    AABB shotRange = sceneShared::MakeDirectionalShotRange(pp, player_->GetLastDirX(), rangeX, BattleTestTune().gunShotRearReach);

    // 弾は最初に当たった相手で止まる（ダミーと配置ナイトの中から銃口に一番近い1体だけに当てる）
    gunShotPicker_.Begin(pp, player_->GetLastDirX(), shotRange);
    for (const auto& d : dummies_) {
        if (d.hp > 0.0f) {
            gunShotPicker_.Consider(&d, DummyBounds(d));
        }
    }
    for (KnightEnemy* knight : GetStageEditor().GetKnights()) {
        if (knight->IsAlive()) {
            gunShotPicker_.Consider(knight, knight->GetAABB());
        }
    }

    for (auto& d : dummies_) {
        if (shot != nullptr && gunShotPicker_.IsTarget(&d)) {
            hitConfirmed = true;
            d.hp = d.maxHp;
            d.hitFlash = BattleTestTune().gunHitFlash;
            d.hpDisplay = 0.0f;
            d.returnTimer = BattleTestTune().dummyReturnDelay;
            d.knockVelX += player_->GetLastDirX() * shot->knockX * atkMult;
            d.knockVelY += shot->knockY * atkMult;
            SpawnHitEffect(DummyHitEffectPosition(d));
            tm->RequestHitStop(shot->launcher ? HitEffectLibrary::GetInstance()->SceneParticles().hitStopLaunch : shot->hitStop);
            styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult * atkMult);
        }
    }
    if (hitConfirmed) {
        player_->ChargeAwakenGauge(BattleTestTune().gunAwakenGain);
    }
    // マズルフラッシュ: 段の弾数ぶん扇状にばらまく（ダメージは上のヒットスキャンが担当。
    // BulletPool の弾はダミーに当たると二重ヒットになるため、射撃コンボの弾道は視覚専用のパーティクルにする）
    if (shot != nullptr) {
        const float dir = player_->GetLastDirX();
        const Vector3 firePos = { pp.x, pp.y, 0.0f }; // 銃口高さ＝手の高さ付近（頭から出ているように見えないよう低めに）
        const Vector4 col = { gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
        const int n = (std::max)(shot->bullets, SceneVisualTuning::BattleCombat().minMuzzleBullets);
        // 演出弾は寿命の間に着弾点まで届く速さで飛ばす（的がいればそこで止め、いなければ射程の端まで）
        const float travel = gunShotPicker_.HasTarget() ? gunShotPicker_.GetHitDistance() : rangeX;
        const float reachSpeed = travel / SceneVisualTuning::BattleCombat().muzzleLifeTime;
        for (int i = 0; i < n; ++i) {
            float t = (n > 1) ? (i / (n - 1.0f) - SceneVisualTuning::BattleCombat().muzzleHalfRange) : 0.0f;
            float speed = reachSpeed + i * SceneVisualTuning::BattleCombat().muzzleSpeedStep;
            pm_->EmitWithColor("bt_gun_shot", firePos,
                { dir * speed, speed * shot->spreadDeg * gameConstants::kDegToRad * t, 0.0f },
                col, SceneVisualTuning::BattleCombat().muzzleLifeTime, SceneVisualTuning::BattleCombat().muzzleScale);
        }
    }
    return hitConfirmed;
}

/** @brief 覚醒乱舞のヒット判定を処理し、命中があれば true を返す */
bool BattleTestScene::UpdateRampageHit() {
    // ── 覚醒乱舞ヒット ───────────────────────────────────────────────
    if (!player_->JustRampageHit()) {
        return false;
    }

    auto* tm = TimeManager::GetInstance();
    const AttackFrame attack = CaptureAttackFrame();

    bool hitConfirmed = false;
    const bool isFinisher = player_->JustRampageFinish();
    AABB rushRange = {
        { attack.playerPos.x - BattleTestTune().rampageRushHalfWidth, attack.playerPos.y - BattleTestTune().rampageRushHalfHeight, -kStageHalfDepth },
        { attack.playerPos.x + BattleTestTune().rampageRushHalfWidth, attack.playerPos.y + BattleTestTune().rampageRushHalfHeight, kStageHalfDepth }
    };
    for (auto& d : dummies_) {
        if (Collision::CheckCollision(rushRange, DummyBounds(d))) {
            hitConfirmed = true;
            d.hp = d.maxHp;
            d.hitFlash = isFinisher ? BattleTestTune().rampageFinisherHitFlash : BattleTestTune().rampageHitFlash;
            d.hpDisplay = 0.0f;
            d.returnTimer = BattleTestTune().dummyReturnDelay;
            float kb = (isFinisher ? BattleTestTune().rampageRushKnockXFinisher : BattleTestTune().rampageRushKnockXNormal) * attack.weapon.knockbackMult;
            d.knockVelX += player_->GetLastDirX() * kb * attack.attackMult;
            d.knockVelY += (isFinisher ? BattleTestTune().rampageRushKnockYFinisher : BattleTestTune().rampageRushKnockYNormal) * attack.attackMult * attack.weapon.knockbackMult;
            SpawnHitEffect(DummyHitEffectPosition(d));
            tm->RequestHitStop(isFinisher ? BattleTestTune().rampageRushHitStopFinisher : BattleTestTune().rampageRushHitStopNormal);
            styleMeter_.RegisterHit("rampage", isFinisher ? BattleTestTune().rampageRushStyleFinisher : BattleTestTune().rampageRushStyleNormal);
        }
    }
    return hitConfirmed;
}

// ══════════════════════════════════════════════════════
// フィニッシャー演出
// ══════════════════════════════════════════════════════

/** @brief スピン連射の弾を発射する（処理はシーン共通の関数に任せる） */
void BattleTestScene::UpdateSpinShotFire() {
    sceneShared::UpdateSpinShotFire(player_.get(), bulletPool_);
}

/** @brief 弾丸の移動と衝突判定を処理する命中があれば true を返す */
bool BattleTestScene::UpdateBulletHits() {
    // ── 弾丸の移動・衝突判定 ────────────────────────────────────────
    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();

    bool hitConfirmed = false;
    // 飛んでいる弾ごとに、生きているダミーと重なったら当てて弾を消す（1発で当たるのは1体だけ）
    bulletPool_.Update();
    for (int bi = 0; bi < BulletPool::kMaxBullets; ++bi) {
        if (!bulletPool_.IsActive(bi)) {
            continue;
        }
        const Vector3& bpos = bulletPool_.GetPos(bi);
        const Vector3& bvel = bulletPool_.GetVel(bi);
        AABB bulletAABB = { { bpos.x - BattleTestTune().bulletHalfExtent, bpos.y - BattleTestTune().bulletHalfExtent, -kStageHalfDepth },
            { bpos.x + BattleTestTune().bulletHalfExtent, bpos.y + BattleTestTune().bulletHalfExtent, kStageHalfDepth } };
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) {
                continue;
            }
            if (Collision::CheckCollision(bulletAABB, DummyBounds(d))) {
                hitConfirmed = true;
                // 訓練用のダミーなので HP は減らさず、被弾表示と戻るまでの時間だけ更新する
                d.hp = d.maxHp;
                d.hitFlash = BattleTestTune().bulletHitFlash;
                d.hpDisplay = 0.0f;
                d.returnTimer = BattleTestTune().dummyReturnDelay;
                // 弾の飛ぶ向きへ押す（速さが0に近い弾は向きが決まらないので押さない）
                float bspd = std::sqrt(bvel.x * bvel.x + bvel.y * bvel.y);
                if (bspd > kMinBulletSpeed) {
                    d.knockVelX += bvel.x / bspd * BattleTestTune().bulletKnockX * weapon.knockbackMult;
                    d.knockVelY += bvel.y / bspd * BattleTestTune().bulletKnockY * weapon.knockbackMult;
                }
                SpawnHitEffect(DummyHitEffectPosition(d));
                tm->RequestHitStop(BattleTestTune().bulletHitStopFrames);
                styleMeter_.RegisterHit("spin_bullet", BattleTestTune().bulletStyleScore);
                player_->ChargeAwakenGauge(BattleTestTune().bulletAwakenGain);
                bulletPool_.Kill(bi);
                break;
            }
        }
    }
    return hitConfirmed;
}

/** @brief 格闘/射撃/乱舞/弾丸の当たり判定を処理する命中があれば true を返す */
bool BattleTestScene::UpdateCombat() {
    bool hitConfirmed = false;
    hitConfirmed |= UpdateMeleeComboHit();
    hitConfirmed |= UpdateWeaponSkillHits();
    hitConfirmed |= UpdateGreatswordSpin();
    hitConfirmed |= UpdateGunShotHit();
    hitConfirmed |= UpdateRampageHit();
    TriggerFinisherSlash();
    UpdateSpinShotFire();
    hitConfirmed |= UpdateBulletHits();
    return hitConfirmed;
}

/** @brief ダミー1体への近接ヒット処理（ノックバック・打ち上げ・演出・スタイル加点） */
void BattleTestScene::ApplyMeleeHitToDummy(Dummy& d, const MeleeAttackDef* atk, float atkMult) {
    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();

    d.hp = d.maxHp;
    d.hitFlash = atk->launcher ? BattleTestTune().launcherHitFlash : BattleTestTune().meleeHitFlash;
    d.hpDisplay = 0.0f;
    d.returnTimer = BattleTestTune().dummyReturnDelay;

    // 覚醒中の多段ヒットの2発目以降は、押し出しと止めを小さくして刻みの速さを見せる
    const bool extraHit = player_->JustComboExtraHit();

    // ノックバックは段の定義 × 武器の重さ × 覚醒倍率
    const float kb = weapon.knockbackMult * atkMult * (extraHit ? CombatTune().awakenExtraHitKnockScale : 1.0f);
    d.knockVelX += player_->GetLastDirX() * atk->knockX * kb;
    d.knockVelY += atk->knockY * kb;

    const Vector3 hitPosition = DummyHitEffectPosition(d);
    SpawnHitEffect(hitPosition);

    // 本編と同じ属性プリセットを使い、テストシーンで色と密度を調整できるようにする
    const Vector4 effectColor = ColorFromArray(weapon.effectColor);
    for (int i = 0; i < weapon.effectBurstCount; ++i) {
        const float side = static_cast<float>((i % SceneVisualTuning::BattleCombat().burstColumns) - SceneVisualTuning::BattleCombat().burstCenterColumn) * SceneVisualTuning::BattleCombat().burstSpreadZ;
        pm_->EmitGravity("bt_hit_spark", hitPosition,
            { player_->GetLastDirX() * (SceneVisualTuning::BattleCombat().burstSpeedBase + i * SceneVisualTuning::BattleCombat().burstSpeedStep), SceneVisualTuning::BattleCombat().burstRiseBase + (i % SceneVisualTuning::BattleCombat().burstRiseSteps) * SceneVisualTuning::BattleCombat().burstRiseStep, side },
            effectColor, SceneVisualTuning::BattleCombat().burstLifetime, SceneVisualTuning::BattleCombat().burstSize);
    }
    if (weapon.effectRingRadius > 0.0f) {
        pm_->EmitRing("bt_hit_ring", hitPosition, weapon.effectRingRadius,
            effectColor, SceneVisualTuning::BattleCombat().effectRingBaseCount + weapon.effectBurstCount, SceneVisualTuning::BattleCombat().effectRingLifetime, SceneVisualTuning::BattleCombat().effectRingSize);
    }
    comboFeel_.OnMeleeHit(atk, !extraHit && IsMeleeFinisher(weapon.type, atk), hitPosition, effectColor,
        player_->GetLastDirX(), camera_.get());

    if (extraHit) {
        tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopAwakenExtra);
    } else if (atk->launcher) {
        // 打ち上げ: 長めのヒットストップ + 画面フラッシュで浮かせた手応えを出す
        tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopLaunch);
        ScreenFlash::GetInstance()->Request(SceneVisualTuning::BattleCombat().launcherFlashColor, SceneVisualTuning::BattleCombat().launcherFlashDuration);
    } else {
        tm->RequestHitStop(atk->hitStop);
    }

    // スタイル加点はおおよそ与ダメージに比例（同じ技の連発は StyleMeter 側で減衰する）
    styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * BattleTestTune().meleeStyleScoreScale * atkMult);
}

// ══════════════════════════════════════════════════════
// 敵とターゲットの更新
// ══════════════════════════════════════════════════════

/** @brief ダミー敵のノックバック物理と HP バー表示を更新する */
void BattleTestScene::UpdateDummies() {
    for (auto& d : dummies_) {
        // ノックバック物理
        d.knockVelY -= BattleTestTune().dummyGravity;
        d.pos.x += d.knockVelX;
        d.pos.y += d.knockVelY;

        if (d.pos.y <= BattleTestTune().dummyGroundY) {
            d.pos.y = BattleTestTune().dummyGroundY;
            d.knockVelY = 0.0f;
        }
        // 訓練場の左右の端で止め、壁に当たったら横の勢いを消す
        d.pos.x = std::clamp(d.pos.x, BattleTestTune().dummyMinX, BattleTestTune().dummyMaxX);
        if (d.pos.x <= BattleTestTune().dummyMinX + kDummyWallEpsilon || d.pos.x >= BattleTestTune().dummyMaxX - kDummyWallEpsilon) {
            d.knockVelX = 0.0f;
        }
        d.knockVelX *= BattleTestTune().dummyKnockDragX;
        d.knockVelY *= BattleTestTune().dummyKnockDragY;

        // HP バー表示値を回復（被弾後 BattleTestTune().dummyHpRecoverTime 秒で満タンに戻る）
        d.hpDisplay = (std::min)(d.hpDisplay + gameConstants::kFrameDeltaTime / BattleTestTune().dummyHpRecoverTime, 1.0f);

        // 帰還タイマー（被弾から BattleTestTune().dummyReturnDelay 秒後に中央へ戻る）
        d.returnTimer -= gameConstants::kFrameDeltaTime;
        if (d.returnTimer <= 0.0f) {
            d.returnTimer = 0.0f;
            d.pos.x += (d.homePos.x - d.pos.x) * BattleTestTune().dummyReturnLerpRate;
            d.pos.y += (d.homePos.y - d.pos.y) * BattleTestTune().dummyReturnLerpRate;
        }

        // モデルは足元が原点なので、当たり判定の中心から下げて置く
        d.object->SetPosition({ d.pos.x, d.pos.y + kDummyModelFootOffsetY, d.pos.z });

        // いつもプレイヤーの方を向く（敵と同じく左右は ±90 度、モデルの正面の補正を足す）
        const float facingYaw = player_->GetPosition().x >= d.pos.x ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
        d.yaw += (facingYaw + kDummyYawOffset - d.yaw) * kDummyTurnRate;
        d.object->SetRotation({ 0.0f, d.yaw, 0.0f });

        // 被弾直後だけ色を変える
        d.hitFlash -= gameConstants::kFrameDeltaTime;
        if (d.hitFlash > 0) {
            d.object->SetColor(SceneVisualTuning::BattleCombat().dummyFlashColor);
        } else {
            d.object->SetColor(SceneVisualTuning::BattleCombat().dummyNormalColor);
        }
        d.object->Update();
    }

    UpdateHpBars();
}

/** @brief StageEditorで新規配置したナイト（GetStageEditor().GetKnights()）への攻撃判定 */
void BattleTestScene::UpdatePlacedKnights() {
    // AI/重力自体はBaseScene::Tick()がUpdate()の後にGetStageEditor().UpdateObjects()で回す
    // （1フレーム遅れで前フレームの位置に対して判定する形になるが60fpsなら誤差程度）
    // ここでは当たり判定・ダメージ処理だけを行う
    std::vector<KnightEnemy*> knights = GetStageEditor().GetKnights();
    if (knights.empty()) {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    std::vector<AABB> solids = GetStageEditor().GetSolidColliders();
    // 命中の火花はナイトの胴の高さに出す
    auto spawnKnightHitEffect = [this](const KnightEnemy& knight) {
        SpawnHitEffect({ knight.GetPosition().x, knight.GetPosition().y + SceneVisualTuning::BattleCombat().knightHitEffectHeight, 0.0f });
    };

    // 地形との当たりを解いてから、近接・射撃・乱舞のそれぞれが当たったかを見る
    for (KnightEnemy* knight : knights) {
        knight->ResolveBlockCollision(solids);
        if (!knight->IsAlive()) {
            continue;
        }

        if (player_->JustComboHit()) {
            const MeleeAttackDef* atk = player_->GetActiveMeleeAttack();
            if (atk != nullptr) {
                const float meleeReach = weapon.range * atk->rangeMult;
                const float dirX = player_->GetLastDirX();
                AABB meleeRange = sceneShared::MakeDirectionalRange(pp, dirX, meleeReach, meleeReach * CombatTune().skillRearReachMult);
                if (Collision::CheckCollision(meleeRange, knight->GetAABB())) {
                    knight->TakeDamage(1, dirX, atk->knockY);
                    spawnKnightHitEffect(*knight);
                    const int hitStop = player_->JustComboExtraHit() ? HitEffectLibrary::GetInstance()->SceneParticles().hitStopAwakenExtra
                        : (atk->launcher ? HitEffectLibrary::GetInstance()->SceneParticles().hitStopLaunch : atk->hitStop);
                    tm->RequestHitStop(hitStop);
                    styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * BattleTestTune().meleeStyleScoreScale);
                    player_->ChargeAwakenGauge(BattleTestTune().meleeAwakenGain);
                }
            }
        }
        if (player_->JustFired()) {
            const GunShotDef* shot = player_->GetActiveGunShot();
            const RangedWeaponData& gun = weaponManager_->GetRanged();
            if (shot != nullptr) {
                // 的選びは UpdateGunShotHit() で済んでいる（ダミーとナイトのうち銃口に一番近い1体だけに当てる）
                if (gunShotPicker_.IsTarget(knight)) {
                    knight->TakeDamage(1, player_->GetLastDirX(), shot->knockY);
                    spawnKnightHitEffect(*knight);
                    tm->RequestHitStop(shot->launcher ? HitEffectLibrary::GetInstance()->SceneParticles().hitStopLaunch : shot->hitStop);
                    styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult);
                    player_->ChargeAwakenGauge(BattleTestTune().gunAwakenGain);
                }
            }
        }
        if (player_->JustRampageHit()) {
            AABB rushRange = {
                { pp.x - BattleTestTune().rampageRushHalfWidth, pp.y - BattleTestTune().rampageRushHalfHeight, -kStageHalfDepth },
                { pp.x + BattleTestTune().rampageRushHalfWidth, pp.y + BattleTestTune().rampageRushHalfHeight, kStageHalfDepth }
            };
            if (Collision::CheckCollision(rushRange, knight->GetAABB())) {
                bool isFinisher = player_->JustRampageFinish();
                float knockY = (isFinisher ? BattleTestTune().rampageRushKnockYFinisher : BattleTestTune().rampageRushKnockYNormal) * weapon.knockbackMult;
                knight->TakeDamage(1, player_->GetLastDirX(), knockY);
                spawnKnightHitEffect(*knight);
                tm->RequestHitStop(isFinisher ? BattleTestTune().rampageRushHitStopFinisher : BattleTestTune().rampageRushHitStopNormal);
                styleMeter_.RegisterHit("rampage", isFinisher ? BattleTestTune().rampageRushStyleFinisher : BattleTestTune().rampageRushStyleNormal);
            }
        }
    }
}

} // namespace engine::game
