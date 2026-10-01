/**
 * @file BattleTestSceneFinisher.cpp
 * @brief バトルテストのフィニッシャー演出と判定
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

namespace {
constexpr float kEffectHeight = 0.5f; // 足元から演出を出す高さ
constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };

// 発動
constexpr Vector4 kChargeFlashColor = { 0.75f, 0.95f, 1.0f, 0.35f };
constexpr float kChargeFlashSeconds = 0.10f;
constexpr float kChargeWarpImpulse = 0.4f;

// 1本ごとの斬り刻み
constexpr float kSlashLineLengthMin = 4.0f;
constexpr float kSlashLineLengthMax = 9.0f;
constexpr float kSlashLineThicknessMin = 3.0f;
constexpr float kSlashLineThicknessMax = 7.0f;
constexpr float kSlashLineLingerSeconds = 0.25f; // 解放の瞬間からさらに残す時間
constexpr Vector4 kSlashLineColor = { 0.75f, 0.95f, 1.0f, 1.0f };
constexpr int kBeatBladeCount = 3;
constexpr float kBeatBladeRadius = 4.0f;
constexpr float kBeatBladeSpeedMin = 1.2f;
constexpr float kBeatBladeSpeedMax = 2.8f;
constexpr float kBeatWarpImpulse = 0.12f;
constexpr float kBeatHitFlash = 0.10f;
constexpr float kBeatKnockX = 0.06f;
constexpr float kBeatKnockY = 0.06f;
constexpr float kBeatStyleScore = 6.0f;
constexpr float kDummyReturnDelay = 1.5f;

// 解放
constexpr float kReleaseHitFlash = 0.22f;
constexpr float kReleaseKnockX = 0.5f;
constexpr float kReleaseKnockY = 0.20f;
constexpr float kSlashFlashSeconds = 0.22f;
constexpr int kReleaseSlashCount = 8;
constexpr float kReleaseSlashThickness = 9.0f;
constexpr float kReleaseSlashSeconds = 0.15f;
constexpr Vector4 kReleaseFlashColor = { 0.75f, 0.95f, 1.0f, 0.65f };
constexpr float kReleaseStyleScore = 120.0f;
constexpr int kReleaseBladeCount = 30;
constexpr float kReleaseBladeSpeedMin = 2.0f;
constexpr float kReleaseBladeSpeedMax = 5.0f;
constexpr float kReleaseWarpImpulse = 1.0f;
constexpr float kMinClipW = 0.0001f; // これ以下のw成分ではスクリーン座標へ変換しない（カメラの背後）
}

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
    ScreenFlash::GetInstance()->Request(kChargeFlashColor, kChargeFlashSeconds);
    SpawnHitEffect({ pp.x, pp.y + kEffectHeight, 0.0f });
    SceneShared::EmitFinisherCharge(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + kEffectHeight, 0.0f });
    spaceWarp_.AddImpulse(kChargeWarpImpulse);
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
    std::uniform_real_distribution<float> lenDist(kSlashLineLengthMin, kSlashLineLengthMax);
    std::uniform_real_distribution<float> thickDist(kSlashLineThicknessMin, kSlashLineThicknessMax);
    const float ang = angleDist(rng);
    const Vector2 dir = { std::cos(ang), std::sin(ang) };
    const Vector2 center = { cam.x + offXDist(rng), cam.y + offYDist(rng) };
    const float len = lenDist(rng);

    // 解放の瞬間まで全ての斬撃線を画面に残す
    const float duration = (GameConstants::kFinisherSlashLines - 1 - finisherLineIdx_) * GameConstants::kFinisherLineInterval
        + GameConstants::kFinisherImpactDelay + kSlashLineLingerSeconds;
    SceneShared::SpawnSlashMarkWorld(
        { center.x - dir.x * len, center.y - dir.y * len },
        { center.x + dir.x * len, center.y + dir.y * len },
        cam.x, cam.y, kSlashLineColor, thickDist(rng), duration);

    SceneShared::EmitFinisherSlashLine(pm_, "bt_sword_slash", "bt_hit_spark",
        { center.x, center.y, 0.0f }, ang, len);

    // 空間にガラス質の刃を明滅させ、歪みを脈動させる
    bladeFlash_.Emit({ center.x, center.y, 0.0f }, kBeatBladeCount, kBeatBladeRadius, kBeatBladeSpeedMin, kBeatBladeSpeedMax);
    spaceWarp_.AddImpulse(kBeatWarpImpulse);

    tm->RequestHitStop(GameConstants::kHitStopFinisherBeat);

    // 斬撃線が出るたびに実際にヒットさせ、マネキンを浮かせ続ける
    for (auto& d : dummies_) {
        d.hp = d.maxHp;
        d.hitFlash = kBeatHitFlash;
        d.hpDisplay = 0.0f;
        d.returnTimer = kDummyReturnDelay;
        d.knockVelX += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * kBeatKnockX;
        d.knockVelY += kBeatKnockY;
        SpawnHitEffect({ d.pos.x, d.pos.y + kEffectHeight, 0.0f });
    }

    styleMeter_.RegisterHit("finisher_line", kBeatStyleScore);

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
        d.hitFlash = kReleaseHitFlash;
        d.hpDisplay = 0.0f;
        d.returnTimer = kDummyReturnDelay;
        d.knockVelX += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * kReleaseKnockX;
        d.knockVelY += kReleaseKnockY;
        SpawnHitEffect({ d.pos.x, d.pos.y + kEffectHeight, 0.0f });
    }
}

void BattleTestScene::PlayFinisherReleaseEffects()
{
    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    SlashMark::GetInstance()->FlashAll(kWhite, kSlashFlashSeconds);
    static std::mt19937 rngRelease { std::random_device { }() };
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    const Vector3& cam = camera_->GetTranslate();
    for (int i = 0; i < kReleaseSlashCount; ++i) {
        const float ang = angleDist(rngRelease);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        SceneShared::SpawnSlashMarkWorld(
            { pp.x - dir.x * GameConstants::kFinisherSlashRadius,
                pp.y - dir.y * GameConstants::kFinisherSlashRadius },
            { pp.x + dir.x * GameConstants::kFinisherSlashRadius,
                pp.y + dir.y * GameConstants::kFinisherSlashRadius },
            cam.x, cam.y, kWhite, kReleaseSlashThickness, kReleaseSlashSeconds);
    }

    tm->RequestHitStop(GameConstants::kHitStopFinisherSlash);
    ScreenFlash::GetInstance()->Request(kReleaseFlashColor, GameConstants::kShakeFinisherSlashDur);
    SceneShared::EmitFinisherRelease(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + kEffectHeight, 0.0f });
    styleMeter_.RegisterHit("finisher_release", kReleaseStyleScore);

    // 解放の瞬間 刃の一斉放出と空間歪みの最大化、最も近いダミーを切断破片に差し替える
    bladeFlash_.Emit({ pp.x, pp.y + kEffectHeight, 0.0f }, kReleaseBladeCount, GameConstants::kFinisherSlashRadius,
        kReleaseBladeSpeedMin, kReleaseBladeSpeedMax);
    spaceWarp_.AddImpulse(kReleaseWarpImpulse);

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
        const Vector3 slicePos = { nearest->pos.x, nearest->pos.y + kDummyModelFootOffsetY, nearest->pos.z };
        dummySlice_.Start(modelDummy_, slicePos, { kDummyModelScale, kDummyModelScale, kDummyModelScale }, rngSlice());
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
    if (cw > kMinClipW) {
        finisherShatter_.SetImpactUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
    }

    finisherShatter_.Reset();
    finisherShatter_.Start();
}
