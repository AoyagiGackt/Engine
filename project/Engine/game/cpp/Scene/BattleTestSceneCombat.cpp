/**
 * @file BattleTestSceneCombat.cpp
 * @brief BattleTestSceneの戦闘判定（近接/固有技/銃/乱舞/フィニッシャー/ダミー/ロックオン/配置ナイト）を実装するファイル
 * @note BattleTestScene.cppからの分割ファイルクラス自体はBattleTestSceneのまま、定義の置き場所だけを分けている
 */
#include "BattleTestScene.h"
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
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// 固有技（スペースキー）のダミー用ヒット定義。ApplyMeleeHitToDummy が参照するのは
// id/damageMult/knockX/knockY/launcher/hitStop のみなので、コンボ制御用フィールドは0で埋める
static constexpr MeleeAttackDef kSwordDashSkill = { "swd_dash", 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.30f, 0.05f, false, 5, 0.0f, false, { }, { }, { }, { } };
static constexpr MeleeAttackDef kSpearRetreatSkill = { "spr_retreat", 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.10f, 0.02f, false, 4, 0.0f, false, { }, { }, { }, { } };
static constexpr MeleeAttackDef kGreatswordSlamSkill = { "gs_slam", 1.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.40f, 0.30f, true, 10, 0.0f, false, { }, { }, { }, { } };
static constexpr MeleeAttackDef kAxeChargeSkill = { "axe_charge", 1.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.35f, 0.08f, false, 6, 0.0f, false, { }, { }, { }, { } };
// ダガー スティンガーの多段突き。1-2段目は軽い刺突、3段目だけ少し重くしてフィニッシュ感を出す
static constexpr MeleeAttackDef kDaggerStingerHits[3] = {
    { "dag_stg1", 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.08f, 0.02f, false, 1, 0.0f, false, { }, { }, { }, { } },
    { "dag_stg2", 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.08f, 0.02f, false, 1, 0.0f, false, { }, { }, { }, { } },
    { "dag_stg3", 0.85f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.30f, 0.08f, false, 4, 0.0f, false, { }, { }, { }, { } },
};
static constexpr float kDaggerStingerReachMult = 0.75f; ///< weapon.range に掛ける射程係数

// グレートソード 投げ回転斬りの渦。knockXは0にして吸い込みと喧嘩させず、代わりに小さく打ち上げて多段ヒット感を出す
static constexpr MeleeAttackDef kGreatswordSpinSkill = { "gsw_spin", 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.05f, false, 2, 0.0f, false, { }, { }, { }, { } };
static constexpr float kGreatswordSpinSuctionRadius = 4.0f; ///< この半径内のダミーを渦へ引き寄せる
static constexpr float kGreatswordSpinHitRadius = 1.5f; ///< この半径内なら実際にヒットする
static constexpr float kGreatswordSpinSuctionSpeed = 0.10f; ///< 毎フレームの吸い込み速度

// 近接攻撃・固有技の判定ボックス関連
static constexpr float kLockAssistReachMult = 1.2f; ///< ロックオン中に前方リーチへ掛ける補正
static constexpr float kLockAssistRearMult = 0.5f; ///< ロックオン中の背面リーチ（前方リーチ比）
static constexpr float kSlamRangeBelowY = 1.0f; ///< 設置型AoEの足元方向の厚み
static constexpr float kSlamRangeAboveY = 1.5f; ///< 設置型AoEの頭上方向の厚み
static constexpr float kStageHalfDepth = 0.5f; ///< 2.5Dステージの奥行き半分
static constexpr Vector4 kSlamFlashColor = { 1.0f, 0.6f, 0.3f, 0.30f };
static constexpr float kSlamFlashDuration = 0.10f;

// 乱舞ダミー（dummies_）の物理・演出調整値
static constexpr float kDummyKnockDragX = 0.84f; ///< 水平ノックバック速度の毎フレーム減衰率
static constexpr float kDummyKnockDragY = 0.88f; ///< 垂直ノックバック速度の毎フレーム減衰率
static constexpr float kDummyHpRecoverTime = 0.8f; ///< 被弾後、HPバー表示が満タンに戻るまでの秒数
static constexpr float kDummyReturnLerpRate = 0.05f; ///< 帰還タイマー経過後、定位置へ戻る補間率（毎フレーム）
static constexpr float kRampageRushKnockXFinisher = 0.45f; ///< 乱舞ラッシュ命中時の水平ノックバック（フィニッシュ段）
static constexpr float kRampageRushKnockXNormal = 0.12f; ///< 乱舞ラッシュ命中時の水平ノックバック（通常段）
static constexpr float kRampageRushKnockYFinisher = 0.18f; ///< 乱舞ラッシュ命中時の垂直ノックバック（フィニッシュ段）
static constexpr float kRampageRushKnockYNormal = 0.03f; ///< 乱舞ラッシュ命中時の垂直ノックバック（通常段）
static constexpr int kRampageRushHitStopFinisher = 6; ///< 乱舞ラッシュ命中時のヒットストップ（フィニッシュ段）
static constexpr int kRampageRushHitStopNormal = 2; ///< 乱舞ラッシュ命中時のヒットストップ（通常段）
static constexpr float kRampageRushStyleFinisher = 60.0f; ///< 乱舞ラッシュ命中時のスタイル加点（フィニッシュ段）
static constexpr float kRampageRushStyleNormal = 20.0f; ///< 乱舞ラッシュ命中時のスタイル加点（通常段）

// 射撃コンボのマズルフラッシュ演出
static constexpr float kMuzzleSpeedStep = 1.0f; ///< 1粒ごとの速度加算
static constexpr float kMuzzleLifeTime = 0.35f; ///< パーティクル寿命（秒）
static constexpr float kMuzzleScale = 0.14f; ///< パーティクルの大きさ

// 攻撃倍率・判定
static constexpr float kAwakenedAttackMult = 1.5f; ///< 覚醒中の攻撃倍率
static constexpr float kMinMeleeReachRatio = 0.35f; ///< 踏み込みを差し引いた後も最低限残すリーチ（基準リーチ比）
static constexpr float kGunShotRearReach = 0.8f; ///< 射撃判定の背面側の余裕（銃身ぶん）
static constexpr Vector2 kRampageRushHalfExtent = { 2.5f, 1.5f }; ///< 乱舞ラッシュの判定半径（横・縦）
static constexpr float kBulletHalfExtent = 0.12f; ///< スピン連射弾の判定半径
static constexpr float kMinBulletSpeed = 0.001f; ///< これ未満の弾速はノックバック方向を決めない
static constexpr float kBulletKnockX = 0.09f;
static constexpr float kBulletKnockY = 0.04f;
static constexpr int kBulletHitStopFrames = 2;
static constexpr float kBulletStyleScore = 3.0f;
static constexpr float kMeleeStyleScoreScale = 1.5f; ///< 近接ヒットのスタイル加点（与ダメージ比）

// 覚醒ゲージの加算量
static constexpr float kMeleeAwakenGain = 0.08f;
static constexpr float kGunAwakenGain = 0.04f;
static constexpr float kBulletAwakenGain = 0.02f;

// ダミーの被弾リアクション
static constexpr float kGunHitFlash = 0.10f;
static constexpr float kRampageFinisherHitFlash = 0.20f;
static constexpr float kRampageHitFlash = 0.08f;
static constexpr float kBulletHitFlash = 0.08f;
static constexpr float kLauncherHitFlash = 0.20f;
static constexpr float kMeleeHitFlash = 0.14f;
static constexpr float kDummyReturnDelay = 1.5f; ///< 被弾から定位置へ戻り始めるまでの秒数
static constexpr float kHitEffectHeight = 0.5f; ///< ダミー中心からヒット演出を出す高さ
static constexpr float kKnightHitEffectHeight = 0.7f;
static constexpr Vector4 kLauncherFlashColor = { 1.0f, 0.95f, 0.7f, 0.25f };
static constexpr float kLauncherFlashDuration = 0.08f;
static constexpr Vector4 kDummyFlashColor = { 1.5f, 1.5f, 1.5f, 1.0f };
static constexpr Vector4 kDummyNormalColor = { 1.0f, 1.0f, 1.0f, 1.0f };

// 近接ヒットの属性バースト（weapons.jsonのeffect設定）
static constexpr int kBurstColumns = 5;
static constexpr int kBurstCenterColumn = 2;
static constexpr float kBurstSpreadZ = 0.5f;
static constexpr float kBurstSpeedBase = 2.0f;
static constexpr float kBurstSpeedStep = 0.25f;
static constexpr float kBurstRiseBase = 2.0f;
static constexpr int kBurstRiseSteps = 4;
static constexpr float kBurstRiseStep = 0.7f;
static constexpr float kBurstLifetime = 0.5f;
static constexpr float kBurstSize = 0.16f;
static constexpr int kEffectRingBaseCount = 12;
static constexpr float kEffectRingLifetime = 0.28f;
static constexpr float kEffectRingSize = 0.16f;

// グレートソード投げ
static constexpr float kThrowEffectHeight = 0.5f;
static constexpr float kThrowRingSpeed = 1.2f;
static constexpr int kThrowRingCount = 10;
static constexpr float kThrowRingLifetime = 0.22f;
static constexpr float kThrowRingSize = 0.16f;
static constexpr int kThrowSparkCount = 6;
static constexpr float kThrowSparkSpeedBase = 6.0f;
static constexpr float kThrowSparkSpeedStep = 1.2f;
static constexpr float kThrowSparkRiseBase = 1.0f;
static constexpr int kThrowSparkRiseSteps = 3;
static constexpr float kThrowSparkRiseStep = 0.6f;
static constexpr float kThrowSparkLifetime = 0.35f;
static constexpr float kThrowSparkSize = 0.14f;
static constexpr float kSpinSuctionMinDistance = 0.05f; ///< これより近いダミーは吸い込まない（方向が定まらないため）
static constexpr int kSpinHitRingCount = 14;
static constexpr float kSpinHitRingLifetime = 0.30f;
static constexpr float kSpinHitRingSize = 0.18f;

// ダミーの物理
static constexpr float kDummyGravity = 0.012f;
static constexpr float kDummyGroundY = 0.4f;
static constexpr float kDummyMinX = 3.0f;
static constexpr float kDummyMaxX = 35.0f;
static constexpr float kDummyWallEpsilon = 0.01f; ///< 壁に接したとみなす距離

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
    { &Player::JustSwordDash, &kSwordDashSkill, 0.80f, false, false },
    { &Player::JustSpearRetreat, &kSpearRetreatSkill, 0.90f, false, false },
    { &Player::JustGreatswordSlam, &kGreatswordSlamSkill, 1.00f, true, true },
    { &Player::JustAxeCharge, &kAxeChargeSkill, 0.85f, false, false },
};
AABB BattleTestScene::DummyBounds(const Dummy& d)
{
    // ダミーは 1×1×1 の正方形として扱う
    return { { d.pos.x - 0.5f, d.pos.y - 0.5f, -kStageHalfDepth },
        { d.pos.x + 0.5f, d.pos.y + 0.5f, kStageHalfDepth } };
}

float BattleTestScene::ComputeAttackMult() const
{
    return (player_->IsAwakened() ? kAwakenedAttackMult : 1.0f) * player_->GetAxeRageMult();
}

// ══════════════════════════════════════════════════════
// 戦闘判定
// ══════════════════════════════════════════════════════

bool BattleTestScene::UpdateMeleeComboHit()
{
    // 格闘コンボ（L キー）
    // ヒットはボタン押下の瞬間ではなく、モーション中の hitTime で発生する（MeleeComboController 管理）。
    // 連打間隔・段ごとの威力/リーチ/打ち上げは全て武器タイプ別の MeleeAttackDef が持つ
    if (!player_->JustComboHit()) {
        return false;
    }

    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();

    bool hitConfirmed = false;
    const MeleeAttackDef* atk = player_->GetActiveMeleeAttack();
    const float rangeMult = (atk != nullptr) ? atk->rangeMult : 1.0f;
    const float baseReach = weapon.range * rangeMult;
    // ヒット判定時点のpp(プレイヤー座標)は既にlungeDistぶん敵へ踏み込み済みなので、
    // ここでweapon.rangeをそのまま足すと踏み込み+リーチの二重計上になり、間合い外から当たって見える。
    // 踏み込みぶんを差し引いて、始点からの合計到達距離がbaseReachに収まるようにする
    const float lungeDist = (atk != nullptr) ? atk->lungeDist : 0.0f;
    const float meleeReach = (std::max)(baseReach - lungeDist, baseReach * kMinMeleeReachRatio);
    const float dirX = player_->GetLastDirX();
    // 前方に厚く、背後は振り抜きぶんだけ（左右対称だと背後の遠い敵にまで当たってしまう）
    AABB meleeRange = SceneShared::MakeDirectionalRange(pp, dirX, meleeReach, meleeReach * GameConstants::kSkillRearReachMult);
    // ロック中は判定を広げてロックしたのに届かないを減らす（距離無制限ヒットはやめる）
    AABB assistRange = SceneShared::MakeDirectionalRange(pp, dirX, meleeReach * kLockAssistReachMult, meleeReach * kLockAssistRearMult);
    for (size_t di = 0; di < dummies_.size(); ++di) {
        auto& d = dummies_[di];
        if (d.hp <= 0.0f) {
            continue;
        }
        bool isLocked = (lockedDummyIndex_ == di);
        bool hit = Collision::CheckCollision(isLocked ? assistRange : meleeRange, DummyBounds(d));
        if (hit && atk != nullptr) {
            hitConfirmed = true;
            ApplyMeleeHitToDummy(d, atk, atkMult);
        }
    }
    if (hitConfirmed) {
        player_->ChargeAwakenGauge(kMeleeAwakenGain);
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateWeaponSkillHits()
{
    // SPACE固有技の攻撃判定と演出を武器ごとの定義から適用する
    // 発生条件・射程係数・判定形状は kWeaponSkills テーブルが持つ
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();

    bool hitConfirmed = false;
    for (const auto& skill : kWeaponSkills) {
        if (!((*player_).*skill.justTriggered)()) {
            continue;
        }
        const float reach = weapon.range * skill.reachMult;
        // 通常は前方に厚い指向性判定、設置型AoEのみ前後対称に叩きつける
        const AABB skillRange = skill.symmetricAoE
            ? AABB { { pp.x - reach, pp.y - kSlamRangeBelowY, -kStageHalfDepth },
                  { pp.x + reach, pp.y + kSlamRangeAboveY, kStageHalfDepth } }
            : SceneShared::MakeDirectionalRange(pp, player_->GetLastDirX(), reach, reach * GameConstants::kSkillRearReachMult);
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) {
                continue;
            }
            if (Collision::CheckCollision(skillRange, DummyBounds(d))) {
                hitConfirmed = true;
                ApplyMeleeHitToDummy(d, skill.def, atkMult);
            }
        }
        if (skill.screenImpact) {
            TimeManager::GetInstance()->RequestHitStop(GameConstants::kHitStopLaunch);
            ScreenFlash::GetInstance()->Request(kSlamFlashColor, kSlamFlashDuration);
        }
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateDaggerStingerHit()
{
    // ダガー スティンガー: 踏み込み直後から1段ずつ発生するヒットを、刺突番号ごとの定義で個別に適用する
    if (!player_->JustDaggerStingerHit()) {
        return false;
    }

    const int idx = std::clamp(player_->GetDaggerStingerHitIndex(), 0, 2);
    const MeleeAttackDef* atk = &kDaggerStingerHits[idx];
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();
    const float reach = weapon.range * kDaggerStingerReachMult;
    const AABB skillRange = SceneShared::MakeDirectionalRange(pp, player_->GetLastDirX(), reach, reach * GameConstants::kSkillRearReachMult);

    bool hitConfirmed = false;
    for (auto& d : dummies_) {
        if (d.hp <= 0.0f) {
            continue;
        }
        if (Collision::CheckCollision(skillRange, DummyBounds(d))) {
            hitConfirmed = true;
            ApplyMeleeHitToDummy(d, atk, atkMult);
        }
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateGreatswordSpin()
{
    // 投げた瞬間（まだ飛行中でIsGreatswordSpinning()はfalse）に発射エフェクトを出す
    if (player_->JustGreatswordThrown()) {
        const WeaponData& weapon = weaponManager_->GetCurrent();
        const Vector4 effectColor = { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] };
        const Vector3& pp = player_->GetPosition();
        const Vector3 launchPos = { pp.x, pp.y + kThrowEffectHeight, 0.0f };
        pm_->EmitRing("bt_hit_ring", launchPos, kThrowRingSpeed, effectColor, kThrowRingCount, kThrowRingLifetime, kThrowRingSize);
        for (int i = 0; i < kThrowSparkCount; ++i) {
            const float speed = kThrowSparkSpeedBase + i * kThrowSparkSpeedStep;
            pm_->EmitGravity("bt_hit_spark", launchPos,
                { player_->GetLastDirX() * speed, kThrowSparkRiseBase + (i % kThrowSparkRiseSteps) * kThrowSparkRiseStep, 0.0f },
                effectColor, kThrowSparkLifetime, kThrowSparkSize);
        }
    }

    // グレートソード 投げ回転斬り: 静止した大剣が渦になっている間、周囲のダミーを毎フレーム引き寄せ、
    // 一定間隔で範囲内のダミーへまとめてヒットさせる（吸い込みと着弾を別々に判定する）
    if (!player_->IsGreatswordSpinning()) {
        return false;
    }

    const Vector3& center = player_->GetGreatswordThrowPos();
    for (auto& d : dummies_) {
        if (d.hp <= 0.0f) {
            continue;
        }
        const float dx = center.x - d.pos.x;
        const float dist = std::abs(dx);
        if (dist < kGreatswordSpinSuctionRadius && dist > kSpinSuctionMinDistance) {
            d.knockVelX += (dx / dist) * kGreatswordSpinSuctionSpeed;
        }
    }

    if (!player_->JustGreatswordSpinHit()) {
        return false;
    }

    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector4 effectColor = { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], weapon.effectColor[3] };
    pm_->EmitRing("bt_hit_ring", center, kGreatswordSpinHitRadius, effectColor, kSpinHitRingCount, kSpinHitRingLifetime, kSpinHitRingSize);

    const float atkMult = ComputeAttackMult();
    bool hitConfirmed = false;
    for (auto& d : dummies_) {
        if (d.hp <= 0.0f) {
            continue;
        }
        if (std::abs(d.pos.x - center.x) <= kGreatswordSpinHitRadius) {
            hitConfirmed = true;
            ApplyMeleeHitToDummy(d, &kGreatswordSpinSkill, atkMult);
        }
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateGunShotHit()
{
    // 射撃コンボ（K キー）
    // 発砲はボタン押下の瞬間ではなく、段の shotTime で発生する（GunComboController 管理）。
    // 弾数・射程倍率・ノックバック・打ち上げ・ヒットストップは全て銃種別の GunShotDef が持つ
    if (!player_->JustFired()) {
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
    AABB shotRange = SceneShared::MakeDirectionalShotRange(pp, player_->GetLastDirX(), rangeX, kGunShotRearReach);
    for (auto& d : dummies_) {
        if (d.hp <= 0.0f) {
            continue;
        }
        if (shot != nullptr && Collision::CheckCollision(shotRange, DummyBounds(d))) {
            hitConfirmed = true;
            d.hp = d.maxHp;
            d.hitFlash = kGunHitFlash;
            d.hpDisplay = 0.0f;
            d.returnTimer = kDummyReturnDelay;
            d.knockVelX += player_->GetLastDirX() * shot->knockX * atkMult;
            d.knockVelY += shot->knockY * atkMult;
            SpawnHitEffect({ d.pos.x, d.pos.y + kHitEffectHeight, 0.0f });
            tm->RequestHitStop(shot->launcher ? GameConstants::kHitStopLaunch : shot->hitStop);
            styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult * atkMult);
        }
    }
    if (hitConfirmed) {
        player_->ChargeAwakenGauge(kGunAwakenGain);
    }
    // マズルフラッシュ: 段の弾数ぶん扇状にばらまく（ダメージは上のヒットスキャンが担当。
    // BulletPool の弾はダミーに当たると二重ヒットになるため、射撃コンボの弾道は視覚専用のパーティクルにする）
    if (shot != nullptr) {
        const float dir = player_->GetLastDirX();
        const Vector3 firePos = { pp.x, pp.y, 0.0f }; // 銃口高さ＝手の高さ付近（頭から出ているように見えないよう低めに）
        const Vector4 col = { gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
        const int n = (std::max)(shot->bullets, 2);
        // ヒット判定はrangeXの距離まで即座に通るのに、演出弾が固定速度だと寿命内にその距離まで
        // 届かず敵の手前で消えてしまい「当たっていない」ように見える。実際の射程まで届く速度にする
        const float reachSpeed = rangeX / kMuzzleLifeTime;
        for (int i = 0; i < n; ++i) {
            float t = (n > 1) ? (i / (n - 1.0f) - 0.5f) : 0.0f; // -0.5〜+0.5
            float speed = reachSpeed + i * kMuzzleSpeedStep;
            pm_->EmitWithColor("bt_gun_shot", firePos,
                { dir * speed, speed * shot->spreadDeg * GameConstants::kDegToRad * t, 0.0f },
                col, kMuzzleLifeTime, kMuzzleScale);
        }
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateRampageHit()
{
    // ── 覚醒乱舞ヒット ───────────────────────────────────────────────
    if (!player_->JustRampageHit()) {
        return false;
    }

    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3& pp = player_->GetPosition();
    const float atkMult = ComputeAttackMult();

    bool hitConfirmed = false;
    const bool isFinisher = player_->JustRampageFinish();
    AABB rushRange = {
        { pp.x - kRampageRushHalfExtent.x, pp.y - kRampageRushHalfExtent.y, -kStageHalfDepth },
        { pp.x + kRampageRushHalfExtent.x, pp.y + kRampageRushHalfExtent.y, kStageHalfDepth }
    };
    for (auto& d : dummies_) {
        if (Collision::CheckCollision(rushRange, DummyBounds(d))) {
            hitConfirmed = true;
            d.hp = d.maxHp;
            d.hitFlash = isFinisher ? kRampageFinisherHitFlash : kRampageHitFlash;
            d.hpDisplay = 0.0f;
            d.returnTimer = kDummyReturnDelay;
            float kb = (isFinisher ? kRampageRushKnockXFinisher : kRampageRushKnockXNormal) * weapon.knockbackMult;
            d.knockVelX += player_->GetLastDirX() * kb * atkMult;
            d.knockVelY += (isFinisher ? kRampageRushKnockYFinisher : kRampageRushKnockYNormal) * atkMult * weapon.knockbackMult;
            SpawnHitEffect({ d.pos.x, d.pos.y + kHitEffectHeight, 0.0f });
            tm->RequestHitStop(isFinisher ? kRampageRushHitStopFinisher : kRampageRushHitStopNormal);
            styleMeter_.RegisterHit("rampage", isFinisher ? kRampageRushStyleFinisher : kRampageRushStyleNormal);
        }
    }
    return hitConfirmed;
}

// ══════════════════════════════════════════════════════
// フィニッシャー演出
// ══════════════════════════════════════════════════════

void BattleTestScene::UpdateSpinShotFire()
{
    SceneShared::UpdateSpinShotFire(player_.get(), bulletPool_);
}

bool BattleTestScene::UpdateBulletHits()
{
    // ── 弾丸の移動・衝突判定 ────────────────────────────────────────
    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();

    bool hitConfirmed = false;
    bulletPool_.Update();
    for (int bi = 0; bi < BulletPool::kMaxBullets; ++bi) {
        if (!bulletPool_.IsActive(bi)) {
            continue;
        }
        const Vector3& bpos = bulletPool_.GetPos(bi);
        const Vector3& bvel = bulletPool_.GetVel(bi);
        AABB bulletAABB = { { bpos.x - kBulletHalfExtent, bpos.y - kBulletHalfExtent, -kStageHalfDepth },
            { bpos.x + kBulletHalfExtent, bpos.y + kBulletHalfExtent, kStageHalfDepth } };
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) {
                continue;
            }
            if (Collision::CheckCollision(bulletAABB, DummyBounds(d))) {
                hitConfirmed = true;
                d.hp = d.maxHp;
                d.hitFlash = kBulletHitFlash;
                d.hpDisplay = 0.0f;
                d.returnTimer = kDummyReturnDelay;
                float bspd = std::sqrt(bvel.x * bvel.x + bvel.y * bvel.y);
                if (bspd > kMinBulletSpeed) {
                    d.knockVelX += bvel.x / bspd * kBulletKnockX * weapon.knockbackMult;
                    d.knockVelY += bvel.y / bspd * kBulletKnockY * weapon.knockbackMult;
                }
                SpawnHitEffect({ d.pos.x, d.pos.y + kHitEffectHeight, 0.0f });
                tm->RequestHitStop(kBulletHitStopFrames);
                styleMeter_.RegisterHit("spin_bullet", kBulletStyleScore);
                player_->ChargeAwakenGauge(kBulletAwakenGain);
                bulletPool_.Kill(bi);
                break;
            }
        }
    }
    return hitConfirmed;
}

bool BattleTestScene::UpdateCombat()
{
    bool hitConfirmed = false;
    hitConfirmed |= UpdateMeleeComboHit();
    hitConfirmed |= UpdateWeaponSkillHits();
    hitConfirmed |= UpdateDaggerStingerHit();
    hitConfirmed |= UpdateGreatswordSpin();
    hitConfirmed |= UpdateGunShotHit();
    hitConfirmed |= UpdateRampageHit();
    TriggerFinisherSlash();
    UpdateSpinShotFire();
    hitConfirmed |= UpdateBulletHits();
    return hitConfirmed;
}

void BattleTestScene::ApplyMeleeHitToDummy(Dummy& d, const MeleeAttackDef* atk, float atkMult)
{
    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();

    d.hp = d.maxHp;
    d.hitFlash = atk->launcher ? kLauncherHitFlash : kMeleeHitFlash;
    d.hpDisplay = 0.0f;
    d.returnTimer = kDummyReturnDelay;

    // ノックバックは段の定義 × 武器の重さ × 覚醒倍率
    const float kb = weapon.knockbackMult * atkMult;
    d.knockVelX += player_->GetLastDirX() * atk->knockX * kb;
    d.knockVelY += atk->knockY * kb;

    const Vector3 hitPosition = { d.pos.x, d.pos.y + kHitEffectHeight, 0.0f };
    SpawnHitEffect(hitPosition);

    // 本編と同じ属性プリセットを使い、テストシーンで色と密度を調整できるようにする
    const Vector4 effectColor = { weapon.effectColor[0], weapon.effectColor[1],
        weapon.effectColor[2], weapon.effectColor[3] };
    for (int i = 0; i < weapon.effectBurstCount; ++i) {
        const float side = static_cast<float>((i % kBurstColumns) - kBurstCenterColumn) * kBurstSpreadZ;
        pm_->EmitGravity("bt_hit_spark", hitPosition,
            { player_->GetLastDirX() * (kBurstSpeedBase + i * kBurstSpeedStep), kBurstRiseBase + (i % kBurstRiseSteps) * kBurstRiseStep, side },
            effectColor, kBurstLifetime, kBurstSize);
    }
    if (weapon.effectRingRadius > 0.0f) {
        pm_->EmitRing("bt_hit_ring", hitPosition, weapon.effectRingRadius,
            effectColor, kEffectRingBaseCount + weapon.effectBurstCount, kEffectRingLifetime, kEffectRingSize);
    }

    if (atk->launcher) {
        // 打ち上げ: 長めのヒットストップ + 画面フラッシュで浮かせた手応えを出す
        tm->RequestHitStop(GameConstants::kHitStopLaunch);
        ScreenFlash::GetInstance()->Request(kLauncherFlashColor, kLauncherFlashDuration);
    } else {
        tm->RequestHitStop(atk->hitStop);
    }

    // スタイル加点はおおよそ与ダメージに比例（同じ技の連発は StyleMeter 側で減衰する）
    styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * kMeleeStyleScoreScale * atkMult);
}

// ══════════════════════════════════════════════════════
// 敵とターゲットの更新
// ══════════════════════════════════════════════════════

void BattleTestScene::UpdateDummies()
{
    for (auto& d : dummies_) {
        // ノックバック物理
        d.knockVelY -= kDummyGravity;
        d.pos.x += d.knockVelX;
        d.pos.y += d.knockVelY;

        if (d.pos.y <= kDummyGroundY) {
            d.pos.y = kDummyGroundY;
            d.knockVelY = 0.0f;
        }
        d.pos.x = std::clamp(d.pos.x, kDummyMinX, kDummyMaxX);
        if (d.pos.x <= kDummyMinX + kDummyWallEpsilon || d.pos.x >= kDummyMaxX - kDummyWallEpsilon) {
            d.knockVelX = 0.0f;
        }
        d.knockVelX *= kDummyKnockDragX;
        d.knockVelY *= kDummyKnockDragY;

        // HP バー表示値を回復（被弾後 kDummyHpRecoverTime 秒で満タンに戻る）
        d.hpDisplay = (std::min)(d.hpDisplay + GameConstants::kFrameDeltaTime / kDummyHpRecoverTime, 1.0f);

        // 帰還タイマー（被弾から kDummyReturnDelay 秒後に中央へ戻る）
        d.returnTimer -= GameConstants::kFrameDeltaTime;
        if (d.returnTimer <= 0.0f) {
            d.returnTimer = 0.0f;
            d.pos.x += (d.homePos.x - d.pos.x) * kDummyReturnLerpRate;
            d.pos.y += (d.homePos.y - d.pos.y) * kDummyReturnLerpRate;
        }

        d.object->SetPosition({ d.pos.x, d.pos.y + kDummyModelFootOffsetY, d.pos.z });

        d.hitFlash -= GameConstants::kFrameDeltaTime;
        if (d.hitFlash > 0) {
            d.object->SetColor(kDummyFlashColor);
        } else {
            d.object->SetColor(kDummyNormalColor);
        }
        d.object->Update();
    }

    UpdateHpBars();
}

void BattleTestScene::UpdatePlacedKnights()
{
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
                AABB meleeRange = SceneShared::MakeDirectionalRange(pp, dirX, meleeReach, meleeReach * GameConstants::kSkillRearReachMult);
                if (Collision::CheckCollision(meleeRange, knight->GetAABB())) {
                    knight->TakeDamage(1, dirX, atk->knockY);
                    SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + kKnightHitEffectHeight, 0.0f });
                    tm->RequestHitStop(atk->launcher ? GameConstants::kHitStopLaunch : atk->hitStop);
                    styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * kMeleeStyleScoreScale);
                    player_->ChargeAwakenGauge(kMeleeAwakenGain);
                }
            }
        }
        if (player_->JustFired()) {
            const GunShotDef* shot = player_->GetActiveGunShot();
            const RangedWeaponData& gun = weaponManager_->GetRanged();
            if (shot != nullptr) {
                const float rangeX = gun.range * shot->rangeMult;
                AABB shotRange = SceneShared::MakeDirectionalShotRange(pp, player_->GetLastDirX(), rangeX, kGunShotRearReach);
                if (Collision::CheckCollision(shotRange, knight->GetAABB())) {
                    knight->TakeDamage(1, player_->GetLastDirX(), shot->knockY);
                    SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + kKnightHitEffectHeight, 0.0f });
                    tm->RequestHitStop(shot->launcher ? GameConstants::kHitStopLaunch : shot->hitStop);
                    styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult);
                    player_->ChargeAwakenGauge(kGunAwakenGain);
                }
            }
        }
        if (player_->JustRampageHit()) {
            AABB rushRange = {
                { pp.x - kRampageRushHalfExtent.x, pp.y - kRampageRushHalfExtent.y, -kStageHalfDepth },
                { pp.x + kRampageRushHalfExtent.x, pp.y + kRampageRushHalfExtent.y, kStageHalfDepth }
            };
            if (Collision::CheckCollision(rushRange, knight->GetAABB())) {
                bool isFinisher = player_->JustRampageFinish();
                float knockY = (isFinisher ? kRampageRushKnockYFinisher : kRampageRushKnockYNormal) * weapon.knockbackMult;
                knight->TakeDamage(1, player_->GetLastDirX(), knockY);
                SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + kKnightHitEffectHeight, 0.0f });
                tm->RequestHitStop(isFinisher ? kRampageRushHitStopFinisher : kRampageRushHitStopNormal);
                styleMeter_.RegisterHit("rampage", isFinisher ? kRampageRushStyleFinisher : kRampageRushStyleNormal);
            }
        }
    }
}

