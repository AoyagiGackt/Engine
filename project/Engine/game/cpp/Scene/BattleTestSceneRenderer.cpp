/**
 * @file BattleTestSceneRenderer.cpp
 * @brief 訓練シーンの3D描画と画面演出を順序付けて実行する
 */
#include "BattleTestSceneRenderer.h"

#include "BattleTestScene.h"
#include "GameConstants.h"
#include "PipelineStateGuard.h"
#include "SlashMark.h"
#include "StageEditor.h"

using namespace engine::graphics;

namespace engine::game {

void BattleTestSceneRenderer::Draw(BattleTestScene& scene)
{
    // キャプチャ済みのガラス割れ演出中は固定背景と破片だけを描画する
    if (scene.glassShatter_.IsActive() && !scene.glassShatter_.NeedCapture()) {
        scene.spriteCommon_->CommonDrawSettings();
        scene.glassShatterBgSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        scene.glassShatterBgSprite_->Update();
        scene.glassShatterBgSprite_->Draw();
        scene.glassShatter_.Apply();
        return;
    }

    // 影パスを完了してからメイン描画先と3D共通状態を設定する
    scene.shadowManager_->BeginShadowPass(scene.dxCommon_->GetCommandList());
    scene.modelCommon_->BeginShadowPass();
    scene.shadowManager_->EndShadowPass(scene.dxCommon_->GetCommandList());
    ID3D12GraphicsCommandList* commandList = scene.dxCommon_->GetCommandList();
    scene.SetupMainRenderTarget();
    scene.modelCommon_->CommonDrawSettings();
    scene.objectCommon_->SetDefaultLight(commandList);
    scene.shadowManager_->SetShadowMap(commandList, scene.srvManager_);

    // 背景、訓練対象、プレイヤーの順で3Dワールドを描画する
    for (auto& city : scene.cityBackgroundObjects_) {
        city->Draw();
    }
    for (auto& portal : scene.warpPortalBlocks_) {
        portal->Draw();
    }
    for (auto& dummy : scene.dummies_) {
        if (!dummy.sliced) {
            dummy.object->Draw();
        }
    }
    scene.bulletPool_.Draw();
    if (scene.knight_) {
        scene.knight_->Draw();
    }
    scene.player_->Draw();
    scene.dummySlice_.Draw();
    scene.pm_->Update(scene.camera_.get());
    scene.pm_->Draw(scene.camera_.get());
    scene.bladeFlash_.Draw();
    scene.GetStageEditor().DrawObjects();

    // バックバッファへ直接描画する場合だけUIより先に空間歪みを合成する
    if (scene.spaceWarp_.IsActive()
        && scene.GetActiveRTVHandle().ptr == scene.dxCommon_->GetCurrentBackBufferHandle().ptr) {
        PipelineStateGuard restoreTarget([&scene] { scene.SetupMainRenderTarget(); });
        scene.spaceWarp_.CaptureAndApply();
    }

    // 画面座標で管理する訓練HUDをまとめて描画する
    scene.spriteCommon_->CommonDrawSettings();
    for (auto& dummy : scene.dummies_) {
        if (dummy.hp > 0.0f) {
            dummy.hpBarBg->Draw();
            dummy.hpBarFg->Draw();
        }
    }
    scene.awakenGaugeBg_->Draw();
    if (scene.player_->GetAwakenGauge() > 0.0f) {
        scene.awakenGaugeFg_->Draw();
    }
    scene.styleMeter_.DrawHud();
    scene.DrawWeaponSlotHud();

    // フィニッシャーの暗転と凍結画面をHUDより手前へ合成する
    const bool captureFrame = scene.finisherShatter_.IsActive() && scene.finisherShatter_.NeedCapture();
    if (scene.finisherActive_ || captureFrame) {
        scene.finisherOverlay_->SetColor({ 0.0f, 0.0f, 0.05f, GameConstants::kFinisherOverlayAlpha });
        scene.finisherOverlay_->Update();
        scene.finisherOverlay_->Draw();
    }
    SlashMark::GetInstance()->Draw();
    if (scene.finisherShatter_.IsActive()
        && scene.GetActiveRTVHandle().ptr == scene.dxCommon_->GetCurrentBackBufferHandle().ptr) {
        if (scene.finisherShatter_.NeedCapture()) {
            scene.finisherShatter_.CaptureFrame();
        }
        PipelineStateGuard restoreTarget([&scene] {
            scene.SetupMainRenderTarget();
            scene.spriteCommon_->CommonDrawSettings();
        });
        scene.finisherShatter_.Apply();
    }

    scene.fontRenderer_.Draw();

    // バックバッファが描画先の場合だけ最終画面をガラス割れ演出へ取り込む
    if (scene.glassShatter_.IsActive()
        && scene.GetActiveRTVHandle().ptr == scene.dxCommon_->GetCurrentBackBufferHandle().ptr) {
        if (scene.glassShatter_.NeedCapture()) {
            scene.glassShatter_.CaptureFrame();
        }
        scene.glassShatter_.Apply();
    }
}

} // namespace engine::game
