/**
 * @file SceneManager.cpp
 * @brief シーンの切替・更新・描画を統括するマネージャー（SceneManager）の実装
 */
#include "SceneManager.h"
#include "AssetPack.h"
#include "GameRules.h"
#include "WeaponManager.h"
#include "CrashHandler.h"
#include "Logger.h"
#include "StageEditor.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "SrvManager.h"
#include "UILayout.h"
#include "EngineAssert.h"
#include "JsonHelper.h"
#include "RunData.h"
#include <fstream>
#include <array>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#ifdef USE_IMGUI
#include "EditorUI.h"
#include "DiagnosticsDraw.h"
#include "GraphEditor.h"
#endif
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr const char* kDebugStartupPath = "Resources/Config/debug_startup.json";
// cook時に素材を記録するため一度ずつ開く画面（ランを始めずに開けるもの。MAP/GAMEPLAYはフロアごとに別途開く）
constexpr const char* kCookScenesWithoutRun[] = { "TITLE", "OPTIONS", "TRAINING", "BATTLETEST", "GAMEOVER", "CLEAR" };
constexpr std::array<const char*, 6> kDebugStartScenes = { "TITLE", "MAP", "GAMEPLAY", "TRAINING", "BATTLETEST", "OPTIONS" };
bool IsDebugStartScene(const std::string& scene)
{
    return std::find(kDebugStartScenes.begin(), kDebugStartScenes.end(), scene) != kDebugStartScenes.end();
}
}

SceneManager* SceneManager::GetInstance()
{
    static SceneManager instance;
    return &instance;
}

void SceneManager::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio, ImGuiManager* imgui)
{
    dxCommon_ = dxCommon;
    input_ = input;
    audio_ = audio;
    imguiManager_ = imgui;
    dxCommon_->SetDiagnosticContext("TitleScene");
    CrashHandler::SetContext("TitleScene");

    // cook中は、遊ばなかった画面の素材もpakへ入るよう、起動シーンを作る前に全シーンを一度ずつ読み込む
    // （起動シーンより後に行うと、破棄したシーンのカメラ等が共通設定に残ってしまうため先に済ませる）
    if (AssetPack::GetInstance()->IsCooking()) {
        CookAllScenes();
    }

    // 通常はタイトルから開始し、Debugだけエディターで保存した開始シーンを使う。
    std::string startupScene = "TITLE";
#ifdef _DEBUG
    const auto settings = JsonHelper::Load(kDebugStartupPath);
    if (settings.is_object() && settings.contains("scene") && settings["scene"].is_string()) {
        const auto candidate = settings["scene"].get<std::string>();
        if (IsDebugStartScene(candidate)) { startupScene = candidate; }
    }
    debugStartScene_ = startupScene;
    if (startupScene == "MAP" || startupScene == "GAMEPLAY") {
        RunData::GetInstance()->StartNewRun();
    }
#endif
    currentScene_ = sceneFactory_ ? sceneFactory_->CreateScene(startupScene) : nullptr;
    if (!currentScene_) {
        startupScene = "TITLE";
        currentScene_ = std::make_unique<TitleScene>();
    }
    nextSceneName_ = startupScene;
    dxCommon_->SetDiagnosticContext(startupScene);
    CrashHandler::SetContext(startupScene);
    transition_.Initialize(dxCommon_);
    TextureManager::GetInstance()->FlushUploads();

    const auto loadingStarted = std::chrono::steady_clock::now();
    currentScene_->Init(dxCommon_, input_, audio_);
    TextureManager::GetInstance()->FlushUploads();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - loadingStarted).count();
    Logger::Log("Startup scene load " + startupScene + ": " + std::to_string(elapsed) + " ms");
    currentScene_->SetImGuiManager(imguiManager_);
}

void SceneManager::Update()
{
#ifdef USE_IMGUI
    DiagnosticsDraw::SetImageViewport();
#endif
    if (transition_.Update()) { PerformSceneSwitch(); }

    // フェード中であっても、今のシーンの更新は続ける
    if (currentScene_ && !transition_.IsLoading()) {
        PrepareEditorPreview();
        // F2トグル・パネル・トリガー判定は先に処理してから、Tick()内のIsVisible()分岐に反映させる
        currentScene_->GetStageEditor().Update(input_, currentScene_->GetEditorPlayerPos());
        // Tick()がUpdate()呼び出し・エディタ表示中の一時停止・UpdateObjects()を一括して面倒を見る
        // （各シーン側はDrawObjects()の呼び出し位置だけ自分のDraw()内で気にすればよい）
        currentScene_->Tick();
    }

#ifdef USE_IMGUI
    // ImGuiのウィジェット構築はUpdateフェーズ（imguiManager_->End()より前）で行う必要があるため、
    // Draw()ではなくここで呼ぶシーンに関係なく常に開けるようにする
    GraphEditor::GetInstance()->Update(input_);

    // 画面UIの位置・サイズ・色の調整パネル（各シーンがUILayoutで参照した項目が自動で並ぶ）
    if (currentScene_ && !transition_.IsLoading() && currentScene_->GetStageEditor().IsVisible()) {
        UILayout::DrawEditorPanel();
    }

    // エディタ起動キーの一覧を常に画面左下へ出す（開き方が画面のどこにも出ていないと気づけないため）
    if (!currentScene_ || currentScene_->ShouldShowHotkeyOverlay()) {
        EditorUI::ShowHotkeyOverlay(
            GraphEditor::GetInstance()->IsVisible(),
            currentScene_ && currentScene_->GetStageEditor().IsVisible(),
            currentScene_ ? currentScene_->GetHotkeyOverlayExtra() : nullptr);
    }
#endif
}

void SceneManager::CookAllScenes()
{
    if (!sceneFactory_) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    auto* runData = RunData::GetInstance();
    auto* weapons = WeaponManager::GetInstance();

    // 作って初期化し、読み込みが終わったらすぐ破棄する（素材の記録はAssetPack側で自動的に行われる）
    auto loadOnce = [&](const char* sceneName) {
        std::unique_ptr<BaseScene> scene = sceneFactory_->CreateScene(sceneName);
        if (!scene) {
            return;
        }
        scene->Init(dxCommon_, input_, audio_);
        TextureManager::GetInstance()->FlushUploads();
        dxCommon_->WaitForGpu();
        scene->Shutdown();
    };

    // ランを始めていない状態で開く画面（ゲームオーバーはランを始めていなければ記録を書き換えない）
    for (const char* sceneName : kCookScenesWithoutRun) {
        loadOnce(sceneName);
    }
    // ランが必要な画面。本編はフロアごとにレベルが違うため、全フロアぶん開く
    const int floorCount = (std::max)(1, static_cast<int>(GameRules::GetInstance()->Get().levelPaths.size()));
    for (int floor = 0; floor < floorCount; ++floor) {
        runData->StartNewRun();
        for (int i = 0; i < floor; ++i) {
            runData->AdvanceFloor();
        }
        weapons->Reset();
        loadOnce("MAP");
        loadOnce("GAMEPLAY");
    }

    // タイトルを開く前の状態（ラン無し・武器無し・無音）へ戻す
    runData->StartNewRun();
    runData->EndRun();
    weapons->Reset();
    audio_->StopWave();
    UILayout::ResetUsedLayouts();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    Logger::Log("[AssetPack] 全シーンの素材を読み込んで記録しました: " + std::to_string(elapsed) + " ms");
}

void SceneManager::PerformSceneSwitch()
{
    const auto loadingStarted = std::chrono::steady_clock::now();
    audio_->StopWave(); // 前のシーンの音を止める

    // PostDraw は複数フレームを並行実行するため、直前まで描画していた
    // シーンの GPU リソースを Finalize する前に使用完了を保証する。
    // ここで待たないと、解放済みのリソース／ディスクリプタ参照が次回の
    // ExecuteCommandLists で検出されることがある。
    dxCommon_->WaitForGpu();

    // 前のシーンを終了処理してからリソースを解放
    if (currentScene_) {
        currentScene_->Shutdown();
    }

    // UIレイアウトの編集パネルには、新しいシーンが参照した項目だけを並べる
    UILayout::ResetUsedLayouts();

    // 工場を使って新しいシーンを作成・初期化
    currentScene_ = sceneFactory_->CreateScene(nextSceneName_);
    currentScene_->Init(dxCommon_, input_, audio_);
    // シーン切り替え時にロードされたテクスチャを一括転送・同期する
    TextureManager::GetInstance()->FlushUploads();

    // ImGuiのセット
    currentScene_->SetImGuiManager(imguiManager_);
    dxCommon_->SetDiagnosticContext(nextSceneName_);
    CrashHandler::SetContext(nextSceneName_);

    // シーンが切り替わったので、画面を明るくし始める
    transition_.FinishSwitch();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - loadingStarted).count();
    Logger::Log("Scene load " + nextSceneName_ + ": " + std::to_string(elapsed) + " ms");
}

void SceneManager::Draw()
{
    if (currentScene_) {
        // D3D12デバッグメッセージへ現在のシーンを添えて原因箇所を絞り込む
        dxCommon_->SetDiagnosticContext(nextSceneName_.empty() ? "TitleScene" : nextSceneName_);
        CrashHandler::SetContext(nextSceneName_.empty() ? "TitleScene" : nextSceneName_);
        currentScene_->Render();
    }

}

void SceneManager::DrawTransition()
{
    transition_.Draw();
}

void SceneManager::PrepareEditorPreview()
{
    editorPreview_.Prepare(*dxCommon_, currentScene_->GetStageEditor());
}

void SceneManager::CaptureEditorPreview()
{
    if (currentScene_ && !transition_.IsLoading()) {
        editorPreview_.Capture(*dxCommon_, currentScene_->GetStageEditor());
    }
}

void SceneManager::Finalize()
{
    // 最終フレームで使用したシーンのGPUリソースを安全に破棄できるまで待機する
    // ステージエディタ表示中は配置モデルとギズモも描画するため、シーン破棄より前に同期する
    if (dxCommon_) {
        dxCommon_->WaitForGpu();
    }

    editorPreview_.Finalize();

    if (currentScene_) {
        currentScene_->Shutdown();
    }

    currentScene_.reset();
    nextScene_.reset();

    transition_ = SceneTransitionOverlay {};

}

// シーン切り替え予約
void SceneManager::ChangeScene(const std::string& sceneName, float fadeOut, float fadeIn)
{
    if (transition_.IsChanging()) { return; }
    nextSceneName_ = sceneName;
    transition_.Begin(fadeOut, fadeIn);
}

bool SceneManager::SetDebugStartScene(const std::string& sceneName)
{
#ifdef _DEBUG
    if (!IsDebugStartScene(sceneName)) { return false; }
    try {
        const nlohmann::json settings = { { "scene", sceneName } };
        std::ofstream file(kDebugStartupPath, std::ios::trunc);
        if (!file) { return false; }
        file << settings.dump(2) << '\n';
        file.flush();
        if (!file) { return false; }
        debugStartScene_ = sceneName;
        return true;
    } catch (const std::exception& error) {
        Logger::LogError("Debug startup settings: " + std::string(error.what()));
        return false;
    }
#else
    (void)sceneName;
    return false;
#endif
}
