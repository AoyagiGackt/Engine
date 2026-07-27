/**
 * @file GamePlaySceneDraw.cpp
 * @brief GamePlaySceneのフィニッシャー演出・クリア判定・メイン描画パスを実装するファイル
 * @note GamePlayScene.cppからの分割ファイルクラス自体はGamePlaySceneのまま、定義の置き場所だけを分けている
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "GameConstants.h"
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
#include "ScoreManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

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
        std::uniform_real_distribution<float> lenDist(4.0f, 9.0f);
        std::uniform_real_distribution<float> thickDist(3.0f, 7.0f);
        const float ang = angleDist(rng_);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        const Vector2 center = { cam.x + offXDist(rng_), cam.y + offYDist(rng_) };
        const float len = lenDist(rng_);

        // 解放の瞬間まで全ての斬撃線を画面に残す
        const float duration = (GameConstants::kFinisherSlashLines - 1 - finisherLineIdx_) * GameConstants::kFinisherLineInterval
            + GameConstants::kFinisherImpactDelay + 0.25f;
        SceneShared::SpawnSlashMarkWorld(
            { center.x - dir.x * len, center.y - dir.y * len },
            { center.x + dir.x * len, center.y + dir.y * len },
            cam.x, cam.y, { 0.75f, 0.95f, 1.0f, 1.0f }, thickDist(rng_), duration);

        // 1本ごとに実際にヒットさせ、敵を空中に拘束し続ける
        enemy_->TakeDamage(GameConstants::kFinisherLineDamage);
        enemy_->Launch(0.10f);
        styleMeter_ = std::clamp(styleMeter_ + 0.02f, 0.0f, 1.0f);

        tm->RequestHitStop(GameConstants::kHitStopFinisherBeat);
        cameraShaker_.Request(0.06f, 0.05f);
        SceneShared::EmitFinisherSlashLine(pm_, "sword_slash", "hit_spark",
            { center.x, center.y, 0.0f }, ang, len);

        // 空間にガラス質の刃を明滅させ、歪みを脈動させる
        bladeFlash_.Emit({ center.x, center.y, epos.z }, 3, 4.0f, 1.2f, 2.8f);
        spaceWarp_.AddImpulse(0.12f);

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
    styleMeter_ = std::clamp(styleMeter_ + 0.35f, 0.0f, 1.0f);
    enemy_->TakeDamage(GameConstants::kFinisherSlashDamage);
    enemy_->Launch(GameConstants::kLaunchSpeed);

    // 敵本体を切断破片に差し替える（演出が飛散に移るまで本体は非表示）
    enemySlice_.Start(enemy_->GetModel(), epos, { 1.0f, 1.0f, 1.0f }, rng_());
    enemy_->SetVisible(false);

    // 解放の瞬間 刃の一斉放出と空間歪みの最大化
    bladeFlash_.Emit(epos, 30, GameConstants::kFinisherSlashRadius, 2.0f, 5.0f);
    spaceWarp_.AddImpulse(1.0f);

    // 白閃光とともに暗転+斬撃線ごと凍った画面を敵位置から砕き、素の世界を見せる
    ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.5f }, 0.15f);
    {
        const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
        const float cx = epos.x * vp.m[0][0] + epos.y * vp.m[1][0] + epos.z * vp.m[2][0] + vp.m[3][0];
        const float cy = epos.x * vp.m[0][1] + epos.y * vp.m[1][1] + epos.z * vp.m[2][1] + vp.m[3][1];
        const float cw = epos.x * vp.m[0][3] + epos.y * vp.m[1][3] + epos.z * vp.m[2][3] + vp.m[3][3];
        if (cw > 0.0001f) {
            finisherShatter_.SetImpactUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
        }
    }
    finisherShatter_.Reset();
    finisherShatter_.Start();

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    SlashMark::GetInstance()->FlashAll({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.22f);
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    const Vector3& cam = camera_->GetTranslate();
    for (int i = 0; i < 8; ++i) {
        const float ang = angleDist(rng_);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        SceneShared::SpawnSlashMarkWorld(
            { epos.x - dir.x * GameConstants::kFinisherSlashRadius,
                epos.y - dir.y * GameConstants::kFinisherSlashRadius },
            { epos.x + dir.x * GameConstants::kFinisherSlashRadius,
                epos.y + dir.y * GameConstants::kFinisherSlashRadius },
            cam.x, cam.y, { 1.0f, 1.0f, 1.0f, 1.0f }, 9.0f, 0.15f);
    }

    SceneShared::EmitFinisherRelease(pm_, "hit_ring", "hit_spark", epos);
}

void GamePlayScene::CheckClearCondition()
{
    // 最終敵の撃破後にハンマーを奪い、4つ目のスロットを完成させる
    if (!weaponStealTriggered_ && enemy_->IsDefeated()
        && !finisherActive_ && !enemySlice_.IsActive()) {
        const Vector3& epos = enemy_->GetPosition();
        const Vector3& ppos = player_->GetPosition();
        const float dx = ppos.x - epos.x;
        const float dy = ppos.y - epos.y;
        constexpr float kAbsorbRange = 2.0f;
        constexpr float kAbsorbDuration = 0.5f;
        if (!mainWeaponAbsorbing_ && dx * dx + dy * dy <= kAbsorbRange * kAbsorbRange
            && input_->TriggerKey(DIK_J)) {
            mainWeaponAbsorbing_ = true;
            mainWeaponAbsorbTimer_ = kAbsorbDuration;
            player_->PlayStealStab();
            Vector3 toPlayer = { ppos.x - epos.x, ppos.y + 0.5f - epos.y, 0.0f };
            float len = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
            if (len > 0.001f) {
                toPlayer.x /= len;
                toPlayer.y /= len;
            }
            Vector4 glowColor = { 0.5f, 0.85f, 1.0f, 1.0f };
            for (int i = 0; i < 10; ++i) {
                float speed = 4.0f + static_cast<float>(i) * 0.3f;
                pm_->EmitGravity("weapon_orb",
                    { epos.x, epos.y + 0.5f, 0.0f },
                    { toPlayer.x * speed, toPlayer.y * speed, 0.0f },
                    glowColor, 0.35f, 0.22f);
            }
        }
        if (mainWeaponAbsorbing_) {
            mainWeaponAbsorbTimer_ -= GameConstants::kFrameDeltaTime;
            Vector3& absorbPos = enemy_->GetPositionRef();
            absorbPos.x += (ppos.x - absorbPos.x) * 0.16f;
            absorbPos.y += (ppos.y + 0.5f - absorbPos.y) * 0.16f;
            enemy_->RefreshVisualTransforms();
            if (mainWeaponAbsorbTimer_ <= 0.0f) {
                weaponStealTriggered_ = true;
                enemy_->SetVisible(false);
                WeaponManager::GetInstance()->Acquire(WeaponType::Hammer);
            }
        }
    }

    // ローグライト: 敵撃破でクリア（大技・切断演出は見せ切ってから遷移する）
    if (!clearTriggered_ && enemy_->IsDefeated() && weaponStealTriggered_
        && !finisherActive_ && !enemySlice_.IsActive()
        && RunData::GetInstance()->IsRunActive()) {
        requestClear_ = true;
    }

    if (requestClear_ || gameTime_.IsCleared()) {
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
        fontRenderer_.DrawString("CLEAR!", 490.0f, 200.0f, 4.0f, { 1.0f, 1.0f, 0.3f, 1.0f });
        fontRenderer_.DrawString("Style:", 420.0f, 310.0f, 3.0f, { 0.8f, 0.8f, 0.8f, 1.0f });
        fontRenderer_.DrawString(rank, 580.0f, 305.0f, 4.0f, { 1.0f, 0.5f, 0.1f, 1.0f });
        char goldBuf[32];
        snprintf(goldBuf, sizeof(goldBuf), "+%dG", lastGold_);
        fontRenderer_.DrawString(goldBuf, 540.0f, 400.0f, 3.0f, { 0.9f, 0.85f, 0.2f, 1.0f });
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
    if (RunData::GetInstance()->GetFloor() == 3) {
        waterPool_->Draw(camera_.get());
    }

    SetupModelRenderState();
    skydome_->Draw();

    SetupModelRenderState();

    // HUDより前にエディタ管理の配置物を描画し、BaseScene側の二重描画を抑止する
    GetStageEditor().DrawObjects();
    for (auto& core : energyCores_) {
        if (!core.collected) {
            core.object->Draw();
        }
    }
    if (swordGateActive_) {
        swordGate_->Draw();
    }
    if (spearGateActive_) {
        spearGate_->Draw();
    }

    if (!ghostTrail_.empty()) {
        SetupModelRenderState();
        ghostObject_->SetModel(player_->GetModel()); // 覚醒フォーム切り替えに残像の見た目を追従させる
        for (const auto& g : ghostTrail_) {
            float alpha = (1.0f - g.age / kGhostLifetime) * 0.5f;
            ghostObject_->SetPosition(g.pos);
            ghostObject_->SetColor({ 0.4f, 0.75f, 1.0f, alpha });
            ghostObject_->Update();
            ghostObject_->Draw();
        }
    }

    player_->Draw();
    for (auto& entry : weaponEnemies_) {
        entry.enemy->Draw();
    }
    enemy_->Draw();
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

