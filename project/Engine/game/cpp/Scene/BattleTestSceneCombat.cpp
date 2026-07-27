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

// 近接攻撃・固有技の判定ボックス関連
static constexpr float kLockAssistReachMult = 1.6f; ///< ロックオン中に前方リーチへ掛ける補正
static constexpr float kLockAssistRearMult = 0.6f; ///< ロックオン中の背面リーチ（前方リーチ比）
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
static constexpr float kMuzzleBaseSpeed = 8.0f; ///< 扇状パーティクルの基本速度
static constexpr float kMuzzleSpeedStep = 1.0f; ///< 1粒ごとの速度加算
static constexpr float kMuzzleLifeTime = 0.35f; ///< パーティクル寿命（秒）
static constexpr float kMuzzleScale = 0.14f; ///< パーティクルの大きさ

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
    return { { d.pos.x - 0.5f, d.pos.y - 0.5f, -0.5f },
        { d.pos.x + 0.5f, d.pos.y + 0.5f, 0.5f } };
}

float BattleTestScene::ComputeAttackMult() const
{
    return (player_->IsAwakened() ? 1.5f : 1.0f) * player_->GetAxeRageMult();
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
    const float meleeReach = weapon.range * rangeMult;
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
        bool isLocked = (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ == di);
        bool hit = Collision::CheckCollision(isLocked ? assistRange : meleeRange, DummyBounds(d));
        if (hit && atk != nullptr) {
            hitConfirmed = true;
            ApplyMeleeHitToDummy(d, atk, atkMult);
        }
    }
    if (hitConfirmed) {
        player_->ChargeAwakenGauge(0.08f);
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
    AABB shotRange = SceneShared::MakeDirectionalShotRange(pp, player_->GetLastDirX(), rangeX, 0.8f);
    for (auto& d : dummies_) {
        if (d.hp <= 0.0f) {
            continue;
        }
        if (shot != nullptr && Collision::CheckCollision(shotRange, DummyBounds(d))) {
            hitConfirmed = true;
            d.hp = d.maxHp;
            d.hitFlash = 0.10f;
            d.hpDisplay = 0.0f;
            d.returnTimer = 1.5f;
            d.knockVelX += player_->GetLastDirX() * shot->knockX * atkMult;
            d.knockVelY += shot->knockY * atkMult;
            SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
            tm->RequestHitStop(shot->launcher ? GameConstants::kHitStopLaunch : shot->hitStop);
            styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult * atkMult);
        }
    }
    if (hitConfirmed) {
        player_->ChargeAwakenGauge(0.04f);
    }
    // マズルフラッシュ: 段の弾数ぶん扇状にばらまく（ダメージは上のヒットスキャンが担当。
    // BulletPool の弾はダミーに当たると二重ヒットになるため、射撃コンボの弾道は視覚専用のパーティクルにする）
    if (shot != nullptr) {
        const float dir = player_->GetLastDirX();
        const Vector3 firePos = { pp.x, pp.y, 0.0f }; // 銃口高さ＝手の高さ付近（頭から出ているように見えないよう低めに）
        const Vector4 col = { gun.color[0], gun.color[1], gun.color[2], gun.color[3] };
        const int n = (std::max)(shot->bullets, 2);
        for (int i = 0; i < n; ++i) {
            float t = (n > 1) ? (i / (n - 1.0f) - 0.5f) : 0.0f; // -0.5〜+0.5
            float speed = kMuzzleBaseSpeed + i * kMuzzleSpeedStep;
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
        { pp.x - 2.5f, pp.y - 1.5f, -0.5f },
        { pp.x + 2.5f, pp.y + 1.5f, 0.5f }
    };
    for (auto& d : dummies_) {
        if (Collision::CheckCollision(rushRange, DummyBounds(d))) {
            hitConfirmed = true;
            d.hp = d.maxHp;
            d.hitFlash = isFinisher ? 0.20f : 0.08f;
            d.hpDisplay = 0.0f;
            d.returnTimer = 1.5f;
            float kb = (isFinisher ? kRampageRushKnockXFinisher : kRampageRushKnockXNormal) * weapon.knockbackMult;
            d.knockVelX += player_->GetLastDirX() * kb * atkMult;
            d.knockVelY += (isFinisher ? kRampageRushKnockYFinisher : kRampageRushKnockYNormal) * atkMult * weapon.knockbackMult;
            SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
            tm->RequestHitStop(isFinisher ? kRampageRushHitStopFinisher : kRampageRushHitStopNormal);
            styleMeter_.RegisterHit("rampage", isFinisher ? kRampageRushStyleFinisher : kRampageRushStyleNormal);
        }
    }
    return hitConfirmed;
}

// ══════════════════════════════════════════════════════
// フィニッシャー演出
// ══════════════════════════════════════════════════════

void BattleTestScene::TriggerFinisherSlash()
{
    // ── フィニッシャースラッシュ 発動の合図（斬撃線の表示は UpdateFinisherSlash に委譲）──
    if (!player_->JustFinisherSlash()) {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    finisherActive_ = true;
    finisherLineIdx_ = 0;
    finisherBeatTimer_ = GameConstants::kFinisherChargeDelay;
    tm->RequestHitStop(GameConstants::kHitStopJuggle);
    ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.35f }, 0.10f);
    SpawnHitEffect({ pp.x, pp.y + 0.5f, 0.0f });
    SceneShared::EmitFinisherCharge(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + 0.5f, 0.0f });
    spaceWarp_.AddImpulse(0.4f);
}

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
        AABB bulletAABB = { { bpos.x - 0.12f, bpos.y - 0.12f, -0.5f },
            { bpos.x + 0.12f, bpos.y + 0.12f, 0.5f } };
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) {
                continue;
            }
            if (Collision::CheckCollision(bulletAABB, DummyBounds(d))) {
                hitConfirmed = true;
                d.hp = d.maxHp;
                d.hitFlash = 0.08f;
                d.hpDisplay = 0.0f;
                d.returnTimer = 1.5f;
                float bspd = std::sqrt(bvel.x * bvel.x + bvel.y * bvel.y);
                if (bspd > 0.001f) {
                    d.knockVelX += bvel.x / bspd * 0.09f * weapon.knockbackMult;
                    d.knockVelY += bvel.y / bspd * 0.04f * weapon.knockbackMult;
                }
                SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
                tm->RequestHitStop(2);
                styleMeter_.RegisterHit("spin_bullet", 3.0f);
                player_->ChargeAwakenGauge(0.02f);
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
    hitConfirmed |= UpdateGunShotHit();
    hitConfirmed |= UpdateRampageHit();
    TriggerFinisherSlash();
    UpdateSpinShotFire();
    hitConfirmed |= UpdateBulletHits();
    return hitConfirmed;
}

bool BattleTestScene::UpdateFinisherSlash()
{
    if (!finisherActive_) {
        return false;
    }

    finisherBeatTimer_ -= GameConstants::kFrameDeltaTime;
    if (finisherBeatTimer_ > 0.0f) {
        return false;
    }

    if (finisherLineIdx_ < GameConstants::kFinisherSlashLines) {
        UpdateFinisherSlashLine();
        return true;
    }

    // 解放 溜めた斬撃が一斉に炸裂し、距離を問わず全マネキンに命中
    finisherActive_ = false;
    ApplyFinisherReleaseHits();
    PlayFinisherReleaseEffects();
    StartFinisherShatterImpact();
    return true;
}

void BattleTestScene::UpdateFinisherSlashLine()
{
    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    // カメラ視界全体にランダムな位置を高速で斬り刻む
    const Vector3& cam = camera_->GetTranslate();
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    std::uniform_real_distribution<float> offXDist(-GameConstants::kCameraHalfW, GameConstants::kCameraHalfW);
    std::uniform_real_distribution<float> offYDist(-GameConstants::kCameraHalfH, GameConstants::kCameraHalfH);
    std::uniform_real_distribution<float> lenDist(4.0f, 9.0f);
    std::uniform_real_distribution<float> thickDist(3.0f, 7.0f);
    const float ang = angleDist(rng);
    const Vector2 dir = { std::cos(ang), std::sin(ang) };
    const Vector2 center = { cam.x + offXDist(rng), cam.y + offYDist(rng) };
    const float len = lenDist(rng);

    // 解放の瞬間まで全ての斬撃線を画面に残す
    const float duration = (GameConstants::kFinisherSlashLines - 1 - finisherLineIdx_) * GameConstants::kFinisherLineInterval
        + GameConstants::kFinisherImpactDelay + 0.25f;
    SceneShared::SpawnSlashMarkWorld(
        { center.x - dir.x * len, center.y - dir.y * len },
        { center.x + dir.x * len, center.y + dir.y * len },
        cam.x, cam.y, { 0.75f, 0.95f, 1.0f, 1.0f }, thickDist(rng), duration);

    SceneShared::EmitFinisherSlashLine(pm_, "bt_sword_slash", "bt_hit_spark",
        { center.x, center.y, 0.0f }, ang, len);

    // 空間にガラス質の刃を明滅させ、歪みを脈動させる
    bladeFlash_.Emit({ center.x, center.y, 0.0f }, 3, 4.0f, 1.2f, 2.8f);
    spaceWarp_.AddImpulse(0.12f);

    tm->RequestHitStop(GameConstants::kHitStopFinisherBeat);

    // 斬撃線が出るたびに実際にヒットさせ、マネキンを浮かせ続ける
    for (auto& d : dummies_) {
        d.hp = d.maxHp;
        d.hitFlash = 0.10f;
        d.hpDisplay = 0.0f;
        d.returnTimer = 1.5f;
        d.knockVelX += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * 0.06f;
        d.knockVelY += 0.06f;
        SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
    }

    styleMeter_.RegisterHit("finisher_line", 6.0f);

    finisherLineIdx_++;
    finisherBeatTimer_ = (finisherLineIdx_ < GameConstants::kFinisherSlashLines)
        ? GameConstants::kFinisherLineInterval
        : GameConstants::kFinisherImpactDelay;
}

void BattleTestScene::ApplyFinisherReleaseHits()
{
    const Vector3& pp = player_->GetPosition();
    for (auto& d : dummies_) {
        d.hp = d.maxHp;
        d.hitFlash = 0.22f;
        d.hpDisplay = 0.0f;
        d.returnTimer = 1.5f;
        d.knockVelX += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * 0.5f;
        d.knockVelY += 0.20f;
        SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
    }
}

void BattleTestScene::PlayFinisherReleaseEffects()
{
    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    SlashMark::GetInstance()->FlashAll({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.22f);
    static std::mt19937 rngRelease { std::random_device { }() };
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    const Vector3& cam = camera_->GetTranslate();
    for (int i = 0; i < 8; ++i) {
        const float ang = angleDist(rngRelease);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        SceneShared::SpawnSlashMarkWorld(
            { pp.x - dir.x * GameConstants::kFinisherSlashRadius,
                pp.y - dir.y * GameConstants::kFinisherSlashRadius },
            { pp.x + dir.x * GameConstants::kFinisherSlashRadius,
                pp.y + dir.y * GameConstants::kFinisherSlashRadius },
            cam.x, cam.y, { 1.0f, 1.0f, 1.0f, 1.0f }, 9.0f, 0.15f);
    }

    tm->RequestHitStop(GameConstants::kHitStopFinisherSlash);
    ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.65f }, GameConstants::kShakeFinisherSlashDur);
    SceneShared::EmitFinisherRelease(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + 0.5f, 0.0f });
    styleMeter_.RegisterHit("finisher_release", 120.0f);

    // 解放の瞬間 刃の一斉放出と空間歪みの最大化、最も近いダミーを切断破片に差し替える
    bladeFlash_.Emit({ pp.x, pp.y + 0.5f, 0.0f }, 30, GameConstants::kFinisherSlashRadius, 2.0f, 5.0f);
    spaceWarp_.AddImpulse(1.0f);

    Dummy* nearest = nullptr;
    float minDist = FLT_MAX;
    for (auto& d : dummies_) {
        float dist = std::abs(d.pos.x - pp.x);
        if (dist < minDist) {
            minDist = dist;
            nearest = &d;
        }
    }
    if (nearest != nullptr) {
        static std::mt19937 rngSlice { std::random_device { }() };
        dummySlice_.Start(modelDummy_.get(), nearest->pos, { 1.0f, 1.0f, 1.0f }, rngSlice());
        nearest->sliced = true;
    }
}

void BattleTestScene::StartFinisherShatterImpact()
{
    // 暗転+斬撃線ごと凍った画面をプレイヤー位置から砕き、素の世界を見せる
    const Vector3& pp = player_->GetPosition();
    const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
    const float cx = pp.x * vp.m[0][0] + pp.y * vp.m[1][0] + pp.z * vp.m[2][0] + vp.m[3][0];
    const float cy = pp.x * vp.m[0][1] + pp.y * vp.m[1][1] + pp.z * vp.m[2][1] + vp.m[3][1];
    const float cw = pp.x * vp.m[0][3] + pp.y * vp.m[1][3] + pp.z * vp.m[2][3] + vp.m[3][3];
    if (cw > 0.0001f) {
        finisherShatter_.SetImpactUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
    }

    finisherShatter_.Reset();
    finisherShatter_.Start();
}

void BattleTestScene::ApplyMeleeHitToDummy(Dummy& d, const MeleeAttackDef* atk, float atkMult)
{
    auto* tm = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();

    d.hp = d.maxHp;
    d.hitFlash = atk->launcher ? 0.20f : 0.14f;
    d.hpDisplay = 0.0f;
    d.returnTimer = 1.5f;

    // ノックバックは段の定義 × 武器の重さ × 覚醒倍率
    const float kb = weapon.knockbackMult * atkMult;
    d.knockVelX += player_->GetLastDirX() * atk->knockX * kb;
    d.knockVelY += atk->knockY * kb;

    const Vector3 hitPosition = { d.pos.x, d.pos.y + 0.5f, 0.0f };
    SpawnHitEffect(hitPosition);

    // 本編と同じ属性プリセットを使い、テストシーンで色と密度を調整できるようにする
    const Vector4 effectColor = { weapon.effectColor[0], weapon.effectColor[1],
        weapon.effectColor[2], weapon.effectColor[3] };
    for (int i = 0; i < weapon.effectBurstCount; ++i) {
        const float side = static_cast<float>((i % 5) - 2) * 0.5f;
        pm_->EmitGravity("bt_hit_spark", hitPosition,
            { player_->GetLastDirX() * (2.0f + i * 0.25f), 2.0f + (i % 4) * 0.7f, side },
            effectColor, 0.5f, 0.16f);
    }
    if (weapon.effectRingRadius > 0.0f) {
        pm_->EmitRing("bt_hit_ring", hitPosition, weapon.effectRingRadius,
            effectColor, 12 + weapon.effectBurstCount, 0.28f, 0.16f);
    }

    if (atk->launcher) {
        // 打ち上げ: 長めのヒットストップ + 画面フラッシュで浮かせた手応えを出す
        tm->RequestHitStop(GameConstants::kHitStopLaunch);
        ScreenFlash::GetInstance()->Request({ 1.0f, 0.95f, 0.7f, 0.25f }, 0.08f);
    } else {
        tm->RequestHitStop(atk->hitStop);
    }

    // スタイル加点はおおよそ与ダメージに比例（同じ技の連発は StyleMeter 側で減衰する）
    styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * 1.5f * atkMult);
}

// ══════════════════════════════════════════════════════
// 敵とターゲットの更新
// ══════════════════════════════════════════════════════

void BattleTestScene::UpdateDummies()
{
    for (auto& d : dummies_) {
        // ノックバック物理
        d.knockVelY -= 0.012f;
        d.pos.x += d.knockVelX;
        d.pos.y += d.knockVelY;

        if (d.pos.y <= 0.4f) {
            d.pos.y = 0.4f;
            d.knockVelY = 0.0f;
        }
        d.pos.x = std::clamp(d.pos.x, 3.0f, 35.0f);
        if (d.pos.x <= 3.01f || d.pos.x >= 34.99f) {
            d.knockVelX = 0.0f;
        }
        d.knockVelX *= kDummyKnockDragX;
        d.knockVelY *= kDummyKnockDragY;

        // HP バー表示値を回復（被弾後 kDummyHpRecoverTime 秒で満タンに戻る）
        d.hpDisplay = (std::min)(d.hpDisplay + GameConstants::kFrameDeltaTime / kDummyHpRecoverTime, 1.0f);

        // 帰還タイマー（被弾から 1.5 秒後に中央へ戻る）
        d.returnTimer -= GameConstants::kFrameDeltaTime;
        if (d.returnTimer <= 0.0f) {
            d.returnTimer = 0.0f;
            d.pos.x += (d.homePos.x - d.pos.x) * kDummyReturnLerpRate;
            d.pos.y += (d.homePos.y - d.pos.y) * kDummyReturnLerpRate;
        }

        d.object->SetPosition(d.pos);

        d.hitFlash -= GameConstants::kFrameDeltaTime;
        if (d.hitFlash > 0) {
            d.object->SetColor({ 1.5f, 1.5f, 1.5f, 1.0f });
        } else {
            d.object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
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
                    SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + 0.7f, 0.0f });
                    tm->RequestHitStop(atk->launcher ? GameConstants::kHitStopLaunch : atk->hitStop);
                    styleMeter_.RegisterHit(atk->id, weapon.damage * atk->damageMult * 1.5f);
                    player_->ChargeAwakenGauge(0.08f);
                }
            }
        }
        if (player_->JustFired()) {
            const GunShotDef* shot = player_->GetActiveGunShot();
            const RangedWeaponData& gun = weaponManager_->GetRanged();
            if (shot != nullptr) {
                const float rangeX = gun.range * shot->rangeMult;
                AABB shotRange = SceneShared::MakeDirectionalShotRange(pp, player_->GetLastDirX(), rangeX, 0.8f);
                if (Collision::CheckCollision(shotRange, knight->GetAABB())) {
                    knight->TakeDamage(1, player_->GetLastDirX(), shot->knockY);
                    SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + 0.7f, 0.0f });
                    tm->RequestHitStop(shot->launcher ? GameConstants::kHitStopLaunch : shot->hitStop);
                    styleMeter_.RegisterHit(shot->id, gun.damage * shot->damageMult);
                    player_->ChargeAwakenGauge(0.04f);
                }
            }
        }
        if (player_->JustRampageHit()) {
            AABB rushRange = {
                { pp.x - 2.5f, pp.y - 1.5f, -0.5f },
                { pp.x + 2.5f, pp.y + 1.5f, 0.5f }
            };
            if (Collision::CheckCollision(rushRange, knight->GetAABB())) {
                bool isFinisher = player_->JustRampageFinish();
                float knockY = (isFinisher ? kRampageRushKnockYFinisher : kRampageRushKnockYNormal) * weapon.knockbackMult;
                knight->TakeDamage(1, player_->GetLastDirX(), knockY);
                SpawnHitEffect({ knight->GetPosition().x, knight->GetPosition().y + 0.7f, 0.0f });
                tm->RequestHitStop(isFinisher ? kRampageRushHitStopFinisher : kRampageRushHitStopNormal);
                styleMeter_.RegisterHit("rampage", isFinisher ? kRampageRushStyleFinisher : kRampageRushStyleNormal);
            }
        }
    }
}

