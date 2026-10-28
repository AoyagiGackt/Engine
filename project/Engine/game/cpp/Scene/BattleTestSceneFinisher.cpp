/**
 * @file BattleTestSceneFinisher.cpp
 * @brief バトルテストのフィニッシャー演出と判定
 */
#include "BattleTestScene.h"
#include "GameSounds.h"
#include "SceneVisualTuning.h"
#include "HitEffectLibrary.h"
#include "CombatTuning.h"
#include "BattleTestTuning.h"
#include "AudioBridge.h"
#include "BattleTestSceneRenderer.h"
#include "Collision.h"
#include "DiagnosticsDraw.h"
#include "FinisherEffects.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControlPanel.h"
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

namespace engine::game {

/** @brief フィニッシャースラッシュの発動演出をトリガーする */
void BattleTestScene::TriggerFinisherSlash() {
    // ── フィニッシャースラッシュ 発動の合図（斬撃線の表示は UpdateFinisherSlash に委譲）──
    if (!player_->JustFinisherSlash()) {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    finisherActive_ = true;
    finisherLineIdx_ = 0;
    finisherBeatTimer_ = CombatTune().finisherChargeDelay;
    tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopJuggle);
    ScreenFlash::GetInstance()->Request(SceneVisualTuning::BattleFinisher().chargeFlashColor, SceneVisualTuning::BattleFinisher().chargeFlashSeconds);
    SpawnHitEffect({ pp.x, pp.y + SceneVisualTuning::BattleFinisher().effectHeight, 0.0f });
    sceneShared::EmitFinisherCharge(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + SceneVisualTuning::BattleFinisher().effectHeight, 0.0f });
    spaceWarp_.AddImpulse(BattleTestTune().chargeWarpImpulse);
}

/** @brief フィニッシャースラッシュ演出（斬撃線を1本ずつ表示→本命ヒット）を更新する本命ヒットの瞬間なら true を返す */
bool BattleTestScene::UpdateFinisherSlash() {
    if (!finisherActive_) {
        return false;
    }

    finisherBeatTimer_ -= gameConstants::kFrameDeltaTime;
    if (finisherBeatTimer_ > 0.0f) {
        return false;
    }

    if (finisherLineIdx_ < CombatTune().finisherSlashLines) {
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

/** @brief UpdateFinisherSlash()の下請け 斬撃線を1本表示し、全マネキンに拘束ヒットを与える */
void BattleTestScene::UpdateFinisherSlashLine() {
    auto* tm = TimeManager::GetInstance();
    // カメラ視界全体にランダムな位置を高速で斬り刻む
    static std::mt19937 sRng { std::random_device { }() };
    const finisherEffects::BeatSlash slash = finisherEffects::SpawnBeatSlash(sRng, camera_->GetTranslate(), finisherLineIdx_);
    const Vector3 slashCenter = { slash.center.x, slash.center.y, 0.0f };
    sceneShared::EmitFinisherSlashLine(pm_, "bt_sword_slash", "bt_hit_spark", slashCenter, slash.angle, slash.length);
    gameSounds::Play(SoundCue::kFinisherBeat);

    // 空間にガラス質の刃を明滅させ、歪みを脈動させる
    finisherEffects::EmitBeatBlades(bladeFlash_, slashCenter);
    spaceWarp_.AddImpulse(BattleTestTune().beatWarpImpulse);

    tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopFinisherBeat);

    // 斬撃線が出るたびに実際にヒットさせ、マネキンを浮かせ続ける
    ApplyFinisherHits(BattleTestTune().beatHitFlash, BattleTestTune().beatKnockX, BattleTestTune().beatKnockY);

    styleMeter_.RegisterHit("finisher_line", BattleTestTune().beatStyleScore);

    finisherLineIdx_++;
    finisherBeatTimer_ = CombatTune().FinisherBeatDelayAfter(finisherLineIdx_);
}

/** @brief UpdateFinisherSlash()の下請け 解放の瞬間に距離を問わず全マネキンへヒットを適用する */
void BattleTestScene::ApplyFinisherReleaseHits() {
    ApplyFinisherHits(BattleTestTune().releaseHitFlash, BattleTestTune().releaseKnockX, BattleTestTune().releaseKnockY);
}

/** @brief ダミーを回復可能な状態に保ちながら、段階ごとの強さでヒットと吹き飛びを加える */
void BattleTestScene::ApplyFinisherHits(float hitFlash, float knockX, float knockY) {
    const Vector3& pp = player_->GetPosition();
    for (auto& d : dummies_) {
        d.hp = d.maxHp;
        d.hitFlash = hitFlash;
        d.hpDisplay = 0.0f;
        d.returnTimer = BattleTestTune().dummyReturnDelay;
        d.knockVelX += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * knockX;
        d.knockVelY += knockY;
        SpawnHitEffect({ d.pos.x, d.pos.y + SceneVisualTuning::BattleFinisher().effectHeight, 0.0f });
    }
}

/** @brief UpdateFinisherSlash()の下請け 解放演出（斬撃線・画面フラッシュ・刃の放出・ダミー切断）を再生する */
void BattleTestScene::PlayFinisherReleaseEffects() {
    auto* tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    static std::mt19937 sRngRelease { std::random_device { }() };
    finisherEffects::SpawnReleaseSlashes(sRngRelease, pp, camera_->GetTranslate());
    gameSounds::Play(SoundCue::kFinisherRelease);

    tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopFinisherSlash);
    ScreenFlash::GetInstance()->Request(SceneVisualTuning::BattleFinisher().releaseFlashColor, HitEffectLibrary::GetInstance()->SceneParticles().shakeFinisherSlashDur);
    sceneShared::EmitFinisherRelease(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + SceneVisualTuning::BattleFinisher().effectHeight, 0.0f });
    styleMeter_.RegisterHit("finisher_release", BattleTestTune().releaseStyleScore);

    // 解放の瞬間 刃の一斉放出と空間歪みの最大化、最も近いダミーを切断破片に差し替える
    finisherEffects::EmitReleaseBlades(bladeFlash_, spaceWarp_, { pp.x, pp.y + SceneVisualTuning::BattleFinisher().effectHeight, 0.0f });

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
        static std::mt19937 sRngSlice { std::random_device { }() };
        const Vector3 slicePos = { nearest->pos.x, nearest->pos.y + kDummyModelFootOffsetY, nearest->pos.z };
        dummySlice_.Start(modelDummy_, slicePos, { kDummyModelScale, kDummyModelScale, kDummyModelScale }, sRngSlice());
        nearest->sliced = true;
    }
}

/** @brief UpdateFinisherSlash()の下請け プレイヤー位置を画面UVへ投影し世界割れ演出を開始する */
void BattleTestScene::StartFinisherShatterImpact() {
    // 暗転+斬撃線ごと凍った画面をプレイヤー位置から砕き、素の世界を見せる
    finisherEffects::StartShatterAt(finisherShatter_, *camera_, player_->GetPosition());
    gameSounds::Play(SoundCue::kGlassShatter);
}

} // namespace engine::game
