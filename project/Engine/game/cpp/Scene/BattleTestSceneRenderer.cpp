/**
 * @file BattleTestSceneRenderer.cpp
 * @brief 訓練シーンの3D描画と画面演出を順序付けて実行する
 */
#include "BattleTestSceneRenderer.h"
#include "HitEffectLibrary.h"

#include "BattleTestScene.h"
#include "SceneShared.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "PipelineStateGuard.h"
#include "SlashMark.h"
#include "StageEditor.h"

using namespace engine::graphics;

namespace engine::game {

/** @brief 訓練シーンの現在状態を描画する */
void BattleTestSceneRenderer::Draw(BattleTestScene& scene)
{
    // キャプチャ済みのガラス割れ演出中は固定背景と破片だけを描画する
    if (scene.glassShatter_.IsActive() && !scene.glassShatter_.NeedCapture())
    {
        scene.spriteCommon_->CommonDrawSettings();
        scene.glassShatterBgSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        scene.glassShatterBgSprite_->Update();
        scene.glassShatterBgSprite_->Draw();
        scene.glassShatter_.Apply();
        return;
    }

    // 合成系の演出はバックバッファへ直接描いている時だけ行う（オフスクリーン描画中はポストエフェクト側が合成する）
    const bool drawingToBackBuffer = scene.GetActiveRTVHandle().ptr == scene.dxCommon_->GetCurrentBackBufferHandle().ptr;

    // 影パスを完了してからメイン描画先と3D共通状態を設定する
    scene.shadowManager_->RunShadowPass(scene.dxCommon_->GetCommandList(), scene.modelCommon_.get());
    ID3D12GraphicsCommandList* commandList = scene.dxCommon_->GetCommandList();
    scene.SetupMainRenderTarget();
    scene.modelCommon_->CommonDrawSettings();
    scene.objectCommon_->SetDefaultLight(commandList);
    scene.shadowManager_->SetShadowMap(commandList, scene.srvManager_);

    // 3D: ポータル・ダミー（斬られた物は破片の側で描く）・弾・プレイヤー・破片
    for (auto& portal : scene.warpPortalBlocks_)
    {
        portal->Draw();
    }
    for (auto& dummy : scene.dummies_)
    {
        if (!dummy.sliced)
        {
            dummy.object->Draw();
        }
    }
    scene.bulletPool_.Draw();
    scene.player_->Draw();
    scene.dummySlice_.Draw();
    // パーティクル・刃の光・軌跡・配置物
    scene.pm_->Update(scene.camera_.get());
    scene.pm_->Draw(scene.camera_.get());
    scene.bladeFlash_.Draw();
    scene.comboFeel_.DrawTrail();
    scene.GetStageEditor().DrawObjects();

    // バックバッファへ直接描画する場合だけUIより先に空間歪みを合成する
    if (scene.spaceWarp_.IsActive() && drawingToBackBuffer)
    {
        PipelineStateGuard restoreTarget([&scene] {
            scene.SetupMainRenderTarget();
        });
        scene.spaceWarp_.CaptureAndApply();
    }
    if (scene.comboFeel_.HasScreenEffects() && drawingToBackBuffer)
    {
        PipelineStateGuard restoreTarget([&scene] {
            scene.SetupMainRenderTarget();
        });
        scene.comboFeel_.ApplyScreenEffects();
    }

    // 画面座標で管理する訓練HUDをまとめて描画する
    scene.spriteCommon_->CommonDrawSettings();
    const bool paused = scene.pauseMenu_.IsPaused();
    if (scene.showHud_ && !paused)
    {
        for (auto& dummy : scene.dummies_)
        {
            if (dummy.hp > 0.0f)
            {
                dummy.hpBarBg->Draw();
                dummy.hpBarFg->Draw();
            }
        }
        scene.styleMeter_.DrawHud();
        scene.DrawWeaponSlotHud();
    }

    // フィニッシャーの暗転と凍結画面をHUDより手前へ合成する
    const bool captureFrame = scene.finisherShatter_.IsActive() && scene.finisherShatter_.NeedCapture();
    if (scene.finisherActive_ || captureFrame)
    {
        constexpr Vector3 kFinisherOverlayTint = { 0.0f, 0.0f, 0.05f };
        scene.finisherOverlay_->SetColor({ kFinisherOverlayTint.x, kFinisherOverlayTint.y, kFinisherOverlayTint.z,
            HitEffectLibrary::GetInstance()->SceneParticles().finisherOverlayAlpha });
        scene.finisherOverlay_->Update();
        scene.finisherOverlay_->Draw();
    }
    // 斬撃線を重ねてから、凍結画面の割れ演出を合成する
    SlashMark::GetInstance()->Draw();
    if (scene.finisherShatter_.IsActive() && drawingToBackBuffer)
    {
        if (scene.finisherShatter_.NeedCapture())
        {
            scene.finisherShatter_.CaptureFrame();
        }
        PipelineStateGuard restoreTarget([&scene] {
            scene.SetupMainRenderTarget();
            scene.spriteCommon_->CommonDrawSettings();
        });
        scene.finisherShatter_.Apply();
    }

    // 一時停止中はHUDを隠してポーズメニューだけを見せる
    if (paused)
    {
        scene.pauseMenu_.Draw(scene.input_);
        scene.fontRenderer_.Draw();
    }
    else if (scene.showHud_)
    {
        scene.GetStageEditor().DrawUIText(scene.fontRenderer_);
        scene.fontRenderer_.Draw();
    }

    // バックバッファが描画先の場合だけ最終画面をガラス割れ演出へ取り込む
    if (scene.glassShatter_.IsActive() && drawingToBackBuffer)
    {
        if (scene.glassShatter_.NeedCapture())
        {
            scene.glassShatter_.CaptureFrame();
        }
        scene.glassShatter_.Apply();
    }
}

/** @brief 有効なポストエフェクトに応じたオフスクリーンRTV（未使用時はバックバッファ）を返す */
D3D12_CPU_DESCRIPTOR_HANDLE BattleTestScene::GetActiveRTVHandle() const
{
    return SceneShared::GetActiveRTVHandle(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

/** @brief メイン描画先（GetActiveRTVHandle）とビューポート/シザーを設定する */
void BattleTestScene::SetupMainRenderTarget()
{
    SceneShared::SetupMainRenderTarget(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}


/** @brief ガラス割れ演出を手動テストとして開始する */
void BattleTestScene::TriggerGlassShatterTest()
{
    if (glassShatter_.IsActive())
    {
        return;
    }
    glassShatter_.Start();
}

} // namespace engine::game
