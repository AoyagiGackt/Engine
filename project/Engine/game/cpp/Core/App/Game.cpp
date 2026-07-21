/**
 * @file Game.cpp
 * @brief Gameが担当する処理を実装するファイル
 */
#include "Game.h"
#include "DelayTimer.h"
#include "FrameProfiler.h"
#include "GameConstants.h"
#include "GamePlayScene.h"
#include "GameSettings.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ImageFilter.h"
#include "InputBuffer.h"
#include "RenderPassGraph.h"
#include "SaveData.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "TextureManager.h"
#include "TimeManager.h"
#include "TitleScene.h"
#include "VignetteEffect.h"
#include <SrvManager.h>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

void MyGame::Initialize()
{
    // 基盤の初期化
    Framework::Initialize();

    // 工場を作る
    sceneFactory_ = std::make_unique<SceneFactory>();

    // SceneManagerに工場を教える
    SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());

    // 最初のシーンを工場経由でセットする
    SceneManager::GetInstance()->Initialize(
        dxCommon_.get(),
        input_.get(),
        audio_.get(),
        imguiManager_.get());

    // ゲーム設定を読み込んで音量に反映する
    GameSettingsManager::GetInstance()->Load();
    const GameSettings& s = GameSettingsManager::GetInstance()->Get();
    audio_->SetBGMVolume(s.bgmVolume);
    audio_->SetSEVolume(s.seVolume);

    // コンティニューデータ・通算記録を読み込む
    SaveDataManager::GetInstance()->Load();

    // スクリーンフラッシュ初期化
    ScreenFlash::GetInstance()->Initialize(dxCommon_.get());
}

void MyGame::Update()
{
    // 基盤の更新
    Framework::Update();

    // 入力バッファ更新（Input::Update() の直後）
    InputBuffer::GetInstance()->Update(input_.get());

    // 時間管理・オーディオフェードを毎フレーム更新
    TimeManager::GetInstance()->Update();
    audio_->Update(GameConstants::kFrameDeltaTime);

    // 遅延コールバック・スクリーンフラッシュを毎フレーム更新
    DelayTimer::GetInstance()->Update(GameConstants::kFrameDeltaTime);
    ScreenFlash::GetInstance()->Update(GameConstants::kFrameDeltaTime);

#ifdef _DEBUG
    // テクスチャのホットリロード（保存したら即座に反映、開発時のみ）
    TextureManager::GetInstance()->CheckHotReload();
#endif

    // シーンマネージャー更新
    SceneManager::GetInstance()->Update();

#ifdef USE_IMGUI
    // F8でエンジン共通の実行時診断を開閉する
    static bool showEngineDebug = false;
    if (input_->TriggerKey(DIK_F8)) {
        showEngineDebug = !showEngineDebug;
    }
    if (showEngineDebug) {
        const bool panelOpen = ImGui::Begin("Engine Debug", &showEngineDebug);
        if (panelOpen) {
        auto* profiler = FrameProfiler::GetInstance();
        ImGui::Text("CPU %.2f ms  %.1f FPS", profiler->GetMs(), profiler->GetFPS());
        ImGui::Text("Gamepad %s", input_->IsGamepadConnected() ? "Connected" : "Disconnected");
        bool hotReloadEnabled = TextureManager::GetInstance()->IsHotReloadEnabled();
        if (ImGui::Checkbox("ゲーム中のテクスチャホットリロード", &hotReloadEnabled)) {
            TextureManager::GetInstance()->SetHotReloadEnabled(hotReloadEnabled);
        }

        bool vsync = dxCommon_->IsVSyncEnabled();
        if (ImGui::Checkbox("VSync", &vsync)) {
            dxCommon_->SetVSyncEnabled(vsync);
        }

        GameSettings& settings = GameSettingsManager::GetInstance()->Get();
        bool settingsChanged = false;
        settingsChanged |= ImGui::SliderFloat("BGM", &settings.bgmVolume, 0.0f, 1.0f);
        settingsChanged |= ImGui::SliderFloat("SE", &settings.seVolume, 0.0f, 1.0f);
        if (settingsChanged) {
            audio_->SetBGMVolume(settings.bgmVolume);
            audio_->SetSEVolume(settings.seVolume);
            GameSettingsManager::GetInstance()->Save();
        }

        ImGui::Separator();
        if (ImGui::Button("Title")) {
            SceneManager::GetInstance()->ChangeScene("TITLE");
        }
        ImGui::SameLine();
        if (ImGui::Button("Stage Select")) {
            SceneManager::GetInstance()->ChangeScene("MAP");
        }
        if (ImGui::Button("Training")) {
            SceneManager::GetInstance()->ChangeScene("TRAINING");
        }
        ImGui::SameLine();
        if (ImGui::Button("Battle Test")) {
            SceneManager::GetInstance()->ChangeScene("BATTLETEST");
        }
        }
        ImGui::End();
    }
#endif

    ImGuiControlPanel::ShowControls();

    // ImGui終了処理
    imguiManager_->End();
}

void MyGame::Draw()
{
    dxCommon_->PreDraw();
    SrvManager::GetInstance()->PreDraw();

    auto* gs = GrayscaleEffect::GetInstance();
    auto* imgFilter = ImageFilter::GetInstance();
    auto* hsv = HsvFilter::GetInstance();

    // オフスクリーンRTVリダイレクトに対応していないシーン（GamePlayScene以外）では
    // BeginScene/EndScene/Apply を呼ばない呼んでしまうと、シーン側が何も描き込まない
    // オフスクリーンテクスチャでバックバッファが上書きされ、画面から絵が消えてしまう
    const bool postEffectsSupported = SceneManager::GetInstance()->CurrentScenePostEffectsSupported();

    RenderPassGraph graph(dxCommon_.get());

    graph.AddPass("Scene Capture Begin", [=] {
        if (imgFilter->IsEnabled()) {
            imgFilter->BeginScene();
        } else if (gs->IsEnabled()) {
            gs->BeginScene();
        } else if (hsv->IsEnabled()) {
            hsv->BeginScene();
        }
    }, postEffectsSupported);

    graph.AddPass("Scene", [] { SceneManager::GetInstance()->Draw(); });

    graph.AddPass("Post Effects", [=] {
        if (imgFilter->IsEnabled()) {
            imgFilter->EndScene();
            imgFilter->Apply(SrvManager::GetInstance());
        } else if (gs->IsEnabled()) {
            gs->EndScene();
            gs->Apply(SrvManager::GetInstance());
        } else if (hsv->IsEnabled()) {
            hsv->EndScene();
            hsv->Apply(SrvManager::GetInstance());
        }
    }, postEffectsSupported);

    graph.AddPass("Vignette", [] { VignetteEffect::GetInstance()->Apply(); });
    graph.AddPass("Screen Flash", [] { ScreenFlash::GetInstance()->Draw(); });
    graph.AddPass("Debug UI", [this] { imguiManager_->Draw(dxCommon_.get()); });
    graph.Execute();

    dxCommon_->PostDraw();
}

void MyGame::Finalize()
{
    SceneManager::GetInstance()->Finalize();

    Framework::Finalize();
}
