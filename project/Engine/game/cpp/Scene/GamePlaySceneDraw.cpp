/**
 * @file GamePlaySceneDraw.cpp
 * @brief GamePlaySceneのフィニッシャー演出・クリア判定・メイン描画パスを実装するファイル
 * @note GamePlayScene.cppからの分割ファイルクラス自体はGamePlaySceneのまま、定義の置き場所だけを分けている
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
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
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "UILayout.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
// フィニッシャーの斬り刻み（1本ごと）
constexpr float kSlashLineLengthMin = 4.0f;
constexpr float kSlashLineLengthMax = 9.0f;
constexpr float kSlashLineThicknessMin = 3.0f;
constexpr float kSlashLineThicknessMax = 7.0f;
constexpr float kSlashLineLingerSeconds = 0.25f; // 解放の瞬間からさらに残す時間
constexpr Vector4 kSlashLineColor = { 0.75f, 0.95f, 1.0f, 1.0f };
constexpr float kBeatLaunchSpeed = 0.10f; // 1本ごとに敵を浮かせ続ける打ち上げ量
constexpr float kBeatStyleGain = 0.02f;
constexpr float kBeatShakeAmount = 0.06f;
constexpr float kBeatShakeSeconds = 0.05f;
constexpr int kBeatBladeCount = 3;
constexpr float kBeatBladeRadius = 4.0f;
constexpr float kBeatBladeSpeedMin = 1.2f;
constexpr float kBeatBladeSpeedMax = 2.8f;
constexpr float kBeatWarpImpulse = 0.12f;

// フィニッシャーの解放
constexpr float kReleaseStyleGain = 0.35f;
constexpr int kReleaseBladeCount = 30;
constexpr float kReleaseBladeSpeedMin = 2.0f;
constexpr float kReleaseBladeSpeedMax = 5.0f;
constexpr float kReleaseWarpImpulse = 1.0f;
constexpr Vector4 kReleaseFlashColor = { 0.75f, 0.95f, 1.0f, 0.5f };
constexpr float kReleaseFlashSeconds = 0.15f;
constexpr float kMinClipW = 0.0001f; // これ以下のw成分ではスクリーン座標へ変換しない（カメラの背後）
constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr float kSlashFlashSeconds = 0.22f;
constexpr int kReleaseSlashCount = 8;
constexpr float kReleaseSlashThickness = 9.0f;
constexpr float kReleaseSlashSeconds = 0.15f;

// ボスの武器吸収
constexpr float kAbsorbTargetHeight = 0.5f; // プレイヤーの足元からこの高さへ吸い寄せる
constexpr float kMinAbsorbDirectionLength = 0.001f;
constexpr Vector4 kAbsorbGlowColor = { 0.5f, 0.85f, 1.0f, 1.0f };
constexpr int kAbsorbOrbCount = 10;
constexpr float kAbsorbOrbSpeedBase = 4.0f;
constexpr float kAbsorbOrbSpeedStep = 0.3f;
constexpr float kAbsorbOrbLifetime = 0.35f;
constexpr float kAbsorbOrbSize = 0.22f;
constexpr float kAbsorbPullRate = 0.16f; // 1フレームでプレイヤーへ寄せる割合

// クリア結果画面
constexpr Vector2 kClearTitlePosition = { 490.0f, 200.0f };
constexpr float kClearTitleScale = 4.0f;
constexpr Vector4 kClearTitleColor = { 1.0f, 1.0f, 0.3f, 1.0f };
constexpr Vector2 kStyleLabelPosition = { 420.0f, 310.0f };
constexpr float kStyleLabelScale = 3.0f;
constexpr Vector4 kStyleLabelColor = { 0.8f, 0.8f, 0.8f, 1.0f };
constexpr Vector2 kRankPosition = { 580.0f, 305.0f };
constexpr float kRankScale = 4.0f;
constexpr Vector4 kRankColor = { 1.0f, 0.5f, 0.1f, 1.0f };
constexpr Vector2 kGoldPosition = { 540.0f, 400.0f };
constexpr float kGoldScale = 3.0f;
constexpr Vector4 kGoldColor = { 0.9f, 0.85f, 0.2f, 1.0f };
constexpr Vector2 kScorePosition = { 500.0f, 470.0f };
constexpr float kScoreScale = 2.5f;
constexpr Vector4 kScoreColor = { 0.85f, 0.95f, 1.0f, 1.0f };

// 覚醒中の残像
constexpr float kGhostMaxAlpha = 0.5f;
constexpr Vector3 kGhostColor = { 0.4f, 0.75f, 1.0f };
}

// ══════════════════════════════════════════════════════

void GamePlayScene::UpdateFinisherSlash(float dt)
{
    if (!finisherActive_) {
        return;
    }

    finisherBeatTimer_ -= dt;
    if (finisherBeatTimer_ > 0.0f) {
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const Vector3& epos = enemy_->GetPosition();

    if (finisherLineIdx_ < GameConstants::kFinisherSlashLines) {
        // カメラ視界全体にランダムな位置を高速で斬り刻む
        const Vector3& cam = camera_->GetTranslate();
        std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
        std::uniform_real_distribution<float> offXDist(-GameConstants::kCameraHalfW, GameConstants::kCameraHalfW);
        std::uniform_real_distribution<float> offYDist(-GameConstants::kCameraHalfH, GameConstants::kCameraHalfH);
        std::uniform_real_distribution<float> lenDist(kSlashLineLengthMin, kSlashLineLengthMax);
        std::uniform_real_distribution<float> thickDist(kSlashLineThicknessMin, kSlashLineThicknessMax);
        const float ang = angleDist(rng_);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        const Vector2 center = { cam.x + offXDist(rng_), cam.y + offYDist(rng_) };
        const float len = lenDist(rng_);

        // 解放の瞬間まで全ての斬撃線を画面に残す
        const float duration = (GameConstants::kFinisherSlashLines - 1 - finisherLineIdx_) * GameConstants::kFinisherLineInterval
            + GameConstants::kFinisherImpactDelay + kSlashLineLingerSeconds;
        SceneShared::SpawnSlashMarkWorld(
            { center.x - dir.x * len, center.y - dir.y * len },
            { center.x + dir.x * len, center.y + dir.y * len },
            cam.x, cam.y, kSlashLineColor, thickDist(rng_), duration);

        // 1本ごとに実際にヒットさせ、敵を空中に拘束し続ける
        enemy_->TakeDamage(GameConstants::kFinisherLineDamage);
        enemy_->Launch(kBeatLaunchSpeed);
        styleMeter_ = std::clamp(styleMeter_ + kBeatStyleGain, 0.0f, 1.0f);

        tm->RequestHitStop(GameConstants::kHitStopFinisherBeat);
        cameraShaker_.Request(kBeatShakeAmount, kBeatShakeSeconds);
        SceneShared::EmitFinisherSlashLine(pm_, "sword_slash", "hit_spark",
            { center.x, center.y, 0.0f }, ang, len);

        // 空間にガラス質の刃を明滅させ、歪みを脈動させる
        bladeFlash_.Emit({ center.x, center.y, epos.z }, kBeatBladeCount, kBeatBladeRadius, kBeatBladeSpeedMin, kBeatBladeSpeedMax);
        spaceWarp_.AddImpulse(kBeatWarpImpulse);

        finisherLineIdx_++;
        finisherBeatTimer_ = (finisherLineIdx_ < GameConstants::kFinisherSlashLines)
            ? GameConstants::kFinisherLineInterval
            : GameConstants::kFinisherImpactDelay;
        return;
    }

    // 解放 溜めた斬撃が一斉に炸裂する
    finisherActive_ = false;
    tm->RequestHitStop(GameConstants::kHitStopFinisherSlash);
    cameraShaker_.Request(GameConstants::kShakeFinisherSlashAmt, GameConstants::kShakeFinisherSlashDur);
    styleMeter_ = std::clamp(styleMeter_ + kReleaseStyleGain, 0.0f, 1.0f);
    enemy_->TakeDamage(GameConstants::kFinisherSlashDamage);
    enemy_->Launch(GameConstants::kLaunchSpeed);

    // 敵本体を切断破片に差し替える（演出が飛散に移るまで本体は非表示）
    enemySlice_.Start(enemy_->GetModel(), epos, { 1.0f, 1.0f, 1.0f }, rng_());
    enemy_->SetVisible(false);

    // 解放の瞬間 刃の一斉放出と空間歪みの最大化
    bladeFlash_.Emit(epos, kReleaseBladeCount, GameConstants::kFinisherSlashRadius, kReleaseBladeSpeedMin, kReleaseBladeSpeedMax);
    spaceWarp_.AddImpulse(kReleaseWarpImpulse);

    // 白閃光とともに暗転+斬撃線ごと凍った画面を敵位置から砕き、素の世界を見せる
    ScreenFlash::GetInstance()->Request(kReleaseFlashColor, kReleaseFlashSeconds);
    {
        const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
        const float cx = epos.x * vp.m[0][0] + epos.y * vp.m[1][0] + epos.z * vp.m[2][0] + vp.m[3][0];
        const float cy = epos.x * vp.m[0][1] + epos.y * vp.m[1][1] + epos.z * vp.m[2][1] + vp.m[3][1];
        const float cw = epos.x * vp.m[0][3] + epos.y * vp.m[1][3] + epos.z * vp.m[2][3] + vp.m[3][3];
        if (cw > kMinClipW) {
            finisherShatter_.SetImpactUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
        }
    }
    finisherShatter_.Reset();
    finisherShatter_.Start();

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    SlashMark::GetInstance()->FlashAll(kWhite, kSlashFlashSeconds);
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    const Vector3& cam = camera_->GetTranslate();
    for (int i = 0; i < kReleaseSlashCount; ++i) {
        const float ang = angleDist(rng_);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        SceneShared::SpawnSlashMarkWorld(
            { epos.x - dir.x * GameConstants::kFinisherSlashRadius,
                epos.y - dir.y * GameConstants::kFinisherSlashRadius },
            { epos.x + dir.x * GameConstants::kFinisherSlashRadius,
                epos.y + dir.y * GameConstants::kFinisherSlashRadius },
            cam.x, cam.y, kWhite, kReleaseSlashThickness, kReleaseSlashSeconds);
    }

    SceneShared::EmitFinisherRelease(pm_, "hit_ring", "hit_spark", epos);
}

void GamePlayScene::CheckClearCondition()
{
    const GameRulesData& rules = GameRules::GetInstance()->Get();

    // 最終敵の撃破後にgame_rules.jsonで指定した武器を奪い、スロットを完成させる
    if (!weaponStealTriggered_ && enemy_->IsDefeated()
        && !finisherActive_ && !enemySlice_.IsActive()) {
        // 打ち飛ばされて武器を落としていれば、本体ではなく落ちた武器の場所で奪う
        const Vector3 epos = enemy_->GetWeaponPickupPosition();
        const Vector3& ppos = player_->GetPosition();
        const float dx = ppos.x - epos.x;
        const float dy = ppos.y - epos.y;
        constexpr float kAbsorbRange = 2.0f;
        constexpr float kAbsorbDuration = 0.5f;
        if (!mainWeaponAbsorbing_ && dx * dx + dy * dy <= kAbsorbRange * kAbsorbRange
            && input_->TriggerAction(Input::Action::Steal)) {
            mainWeaponAbsorbing_ = true;
            mainWeaponAbsorbTimer_ = kAbsorbDuration;
            player_->PlayStealStab();
            Vector3 toPlayer = { ppos.x - epos.x, ppos.y + kAbsorbTargetHeight - epos.y, 0.0f };
            float len = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
            if (len > kMinAbsorbDirectionLength) {
                toPlayer.x /= len;
                toPlayer.y /= len;
            }
            for (int i = 0; i < kAbsorbOrbCount; ++i) {
                float speed = kAbsorbOrbSpeedBase + static_cast<float>(i) * kAbsorbOrbSpeedStep;
                pm_->EmitGravity("weapon_orb",
                    { epos.x, epos.y + kAbsorbTargetHeight, 0.0f },
                    { toPlayer.x * speed, toPlayer.y * speed, 0.0f },
                    kAbsorbGlowColor, kAbsorbOrbLifetime, kAbsorbOrbSize);
            }
        }
        if (mainWeaponAbsorbing_) {
            mainWeaponAbsorbTimer_ -= GameConstants::kFrameDeltaTime;
            const Vector3 absorbTarget = { ppos.x, ppos.y + kAbsorbTargetHeight, ppos.z };
            if (enemy_->HasDroppedWeapon()) {
                enemy_->PullDroppedWeaponToward(absorbTarget, kAbsorbPullRate);
            } else {
                Vector3& absorbPos = enemy_->GetPositionRef();
                absorbPos.x += (absorbTarget.x - absorbPos.x) * kAbsorbPullRate;
                absorbPos.y += (absorbTarget.y - absorbPos.y) * kAbsorbPullRate;
                enemy_->RefreshVisualTransforms();
            }
            if (mainWeaponAbsorbTimer_ <= 0.0f) {
                weaponStealTriggered_ = true;
                enemy_->SetVisible(false);
                // ボスの配置物に設定された武器を奪う（未設定ならgame_rules.jsonの既定）。あわせてボス技を習得する
                const WeaponManager::AcquireResult acquired = WeaponManager::GetInstance()->Acquire(bossWeaponType_);
                if (acquired == WeaponManager::AcquireResult::Duplicate) {
                    player_->ChargeAwakenGauge(CombatTuning::GetInstance()->Get().duplicateWeaponAwakenBonus);
                } else if (acquired == WeaponManager::AcquireResult::Added) {
                    hud_.Notify(HudEvent::WeaponAcquired);
                }
                // ボスの武器は次のステージの専用の壁を壊す鍵になるため、満杯でも破棄させず必ずどれかと交換させる
                pendingWeaponMandatory_ = acquired == WeaponManager::AcquireResult::NeedsReplacement;
                RunData::GetInstance()->AddBossTechnique(rules.bossTechnique);
            }
        }
    }

    // ローグライト: 敵撃破でクリア（大技・切断演出は見せ切ってから遷移する）
    if (rules.requireBossWeaponSteal && !clearTriggered_ && enemy_->IsDefeated() && weaponStealTriggered_
        && !finisherActive_ && !enemySlice_.IsActive()
        && RunData::GetInstance()->IsRunActive()) {
        requestClear_ = true;
    }

    // ノードグラフ（RequestStageClear/SetFlag）やステージのトリガーがクリアフラグを立てた場合もクリアへ進む
    if (!clearTriggered_ && !rules.clearFlag.empty() && GameFlags::GetInstance()->GetFlag(rules.clearFlag)
        && !finisherActive_ && !enemySlice_.IsActive()) {
        GameFlags::GetInstance()->SetFlag(rules.clearFlag, false); // 次のステージへ持ち越さない
        requestClear_ = true;
    }

    if (requestClear_) {
        requestClear_ = false;
        clearTriggered_ = true;
        if (!RunData::GetInstance()->IsRunActive()) {
            glassShatter_.Start();
        }
    }
}

// ══════════════════════════════════════════════════════
// レンダリング
// ══════════════════════════════════════════════════════

D3D12_CPU_DESCRIPTOR_HANDLE GamePlayScene::GetActiveRTVHandle() const
{
    return SceneShared::GetActiveRTVHandle(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

void GamePlayScene::SetupMainRenderTarget()
{
    SceneShared::SetupMainRenderTarget(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

void GamePlayScene::SetupModelRenderState()
{
    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(dxCommon_->GetCommandList());
    shadowManager_->SetShadowMap(dxCommon_->GetCommandList(), srvManager_);
}

void GamePlayScene::DrawShadowPass()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    shadowManager_->BeginShadowPass(commandList);
    modelCommon_->BeginShadowPass();
    shadowManager_->EndShadowPass(commandList);
}

void GamePlayScene::Draw()
{
    if (DrawClearOverlayIfNeeded()) {
        return;
    }

    DrawWorldAndActors();
    DrawOverlaysAndUI();
}

bool GamePlayScene::DrawClearOverlayIfNeeded()
{
    // クリア演出中（かつキャプチャ済み）はシーン描画をスキップ
    if (clearTriggered_ && RunData::GetInstance()->IsRunActive() && showResult_) {
        GetStageEditor().DrawObjects();
        spriteCommon_->CommonDrawSettings();
        clearBgSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        clearBgSprite_->Update();
        clearBgSprite_->Draw();
        const char* rank = RunData::CalcRank(peakStyle_);
        fontRenderer_.Reset();
        // 本編HUDと同じレイアウト（Resources/Config/UI/gameplay.json）の result グループで調整する
        UILayout& layout = UILayout::Get("gameplay");
        auto drawResultText = [&](const char* text, const char* key, const Vector2& position, float scale, const Vector4& color) {
            const std::string group = std::string("result.") + key;
            const Vector2 p = layout.Pos(group + "_pos", position);
            fontRenderer_.DrawString(text, p.x, p.y, layout.Float(group + "_scale", scale), layout.Color(group + "_color", color));
        };
        drawResultText("CLEAR!", "title", kClearTitlePosition, kClearTitleScale, kClearTitleColor);
        drawResultText("Style:", "style_label", kStyleLabelPosition, kStyleLabelScale, kStyleLabelColor);
        drawResultText(rank, "rank", kRankPosition, kRankScale, kRankColor);
        char goldBuf[32];
        snprintf(goldBuf, sizeof(goldBuf), "+%dG", lastGold_);
        drawResultText(goldBuf, "gold", kGoldPosition, kGoldScale, kGoldColor);
        char scoreBuf[32];
        snprintf(scoreBuf, sizeof(scoreBuf), "+%d pts", lastScore_);
        drawResultText(scoreBuf, "score", kScorePosition, kScoreScale, kScoreColor);
        fontRenderer_.Draw();
        return true;
    }
    if (clearTriggered_ && IsGlassShatterFlow() && !glassShatter_.NeedCapture()) {
        GetStageEditor().DrawObjects();
        spriteCommon_->CommonDrawSettings();
        clearBgSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        clearBgSprite_->Update();
        clearBgSprite_->Draw();
        glassShatter_.Apply();
        return true;
    }
    return false;
}

void GamePlayScene::DrawWorldAndActors()
{
    renderTexture_->BeginRendering();
    renderTexture_->EndRendering();

    DrawShadowPass();
    SetupMainRenderTarget();

    spriteCommon_->CommonDrawSettings();
    renderTextureSprite_->Update();
    renderTextureSprite_->Draw();

    spriteCommon_->CommonDrawSettings();
    if (IsWaterFloor()) {
        waterPool_->Draw(camera_.get());
    }

    SetupModelRenderState();
    skydome_->Draw();

    SetupModelRenderState();

    // HUDより前にエディタ管理の配置物（収集物・壊せる物を含む）を描画し、BaseScene側の二重描画を抑止する
    GetStageEditor().DrawObjects();
    if (!ghostTrail_.empty()) {
        SetupModelRenderState();
        ghostObject_->SetModel(player_->GetModel()); // 覚醒フォーム切り替えに残像の見た目を追従させる
        for (const auto& g : ghostTrail_) {
            float alpha = (1.0f - g.age / kGhostLifetime) * kGhostMaxAlpha;
            ghostObject_->SetPosition(g.pos);
            ghostObject_->SetColor({ kGhostColor.x, kGhostColor.y, kGhostColor.z, alpha });
            ghostObject_->Update();
            ghostObject_->Draw();
        }
    }

    player_->Draw();
    // weaponEnemies_/enemy_の描画自体はStageEditor所有のためGetStageEditor().DrawObjects()
    // （このすぐ上で呼び済み）が担う。ここで二重に呼ぶとブロックが二重描画されてしまう
    enemySlice_.Draw();

    pm_->Update(camera_.get());
    pm_->Draw(camera_.get());

    bladeFlash_.Draw();

    // 空間歪み（バックバッファ直描き時のみUIより先に画面をキャプチャして歪ませる）
    if (spaceWarp_.IsActive()
        && GetActiveRTVHandle().ptr == dxCommon_->GetCurrentBackBufferHandle().ptr) {
        // スコープを抜けた瞬間に必ずレンダーターゲット設定を戻す（歪み描画がRTを変えるため）
        PipelineStateGuard restoreGuard([this] { SetupMainRenderTarget(); });
        spaceWarp_.CaptureAndApply();
    }
}

