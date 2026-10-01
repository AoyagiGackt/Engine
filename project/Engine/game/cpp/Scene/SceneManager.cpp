/**
 * @file SceneManager.cpp
 * @brief シーンの切替・更新・描画・非同期ロードを統括するマネージャー（SceneManager）の実装
 */
#include "SceneManager.h"
#include "CrashHandler.h"
#include "Logger.h"
#include "StageEditor.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "SrvManager.h"
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
// 非同期ロードの進捗表示に使う段階ごとの値（0〜1）
constexpr float kProgressCreating = 0.1f; // シーンの生成を始めた
constexpr float kProgressCreated = 0.4f; // シーンを生成し、メインスレッドでの初期化を待っている
constexpr float kProgressInitializing = 0.5f; // メインスレッドで初期化中
constexpr float kProgressInitialized = 0.9f; // 初期化が終わり、切り替えを待っている
constexpr const char* kDebugStartupPath = "Resources/Config/debug_startup.json";
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

SceneManager::~SceneManager()
{
    if (loadingThread_.joinable()) {
        loadingThread_.join();
    }
}

void SceneManager::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio, ImGuiManager* imgui)
{
    dxCommon_ = dxCommon;
    input_ = input;
    audio_ = audio;
    imguiManager_ = imgui;
    dxCommon_->SetDiagnosticContext("TitleScene");
    CrashHandler::SetContext("TitleScene");

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

    // エディタ起動キーの一覧を常に画面左下へ出す（開き方が画面のどこにも出ていないと気づけないため）
    if (!currentScene_ || currentScene_->ShouldShowHotkeyOverlay()) {
        EditorUI::ShowHotkeyOverlay(
            GraphEditor::GetInstance()->IsVisible(),
            currentScene_ && currentScene_->GetStageEditor().IsVisible(),
            currentScene_ ? currentScene_->GetHotkeyOverlayExtra() : nullptr);
    }
#endif
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

    if (preloadedScene_ && nextSceneName_ == loadingTargetScene_) {
        // ワーカー側はシーンオブジェクトの生成までに限定する
        // D3D12、SRV、TextureManager等の共有状態へ触れるInitは、ロード画面が暗転した後に
        // メインスレッドで実行して描画スレッドとの競合を防ぐ
        if (loadingThread_.joinable()) {
            loadingThread_.join();
        }
        asyncLoadProgress_.store(kProgressInitializing);
        try {
            preloadedScene_->Init(dxCommon_, input_, audio_);
            asyncLoadProgress_.store(kProgressInitialized);
            currentScene_ = std::move(preloadedScene_);
            TextureManager::GetInstance()->FlushUploads();
            asyncLoadProgress_.store(1.0f);
        } catch (const std::exception& error) {
            {
                std::scoped_lock lock(asyncLoadErrorMutex_);
                asyncLoadError_ = error.what();
            }
            asyncLoadFailed_.store(true);
            Logger::LogError("Scene GPU initialization failed: " + std::string(error.what()));
            preloadedScene_.reset();
            currentScene_ = std::make_unique<TitleScene>();
            currentScene_->Init(dxCommon_, input_, audio_);
            TextureManager::GetInstance()->FlushUploads();
            nextSceneName_ = "TITLE";
        }
        loadingTargetScene_.clear();
    } else {
        // 工場を使って新しいシーンを作成・初期化
        currentScene_ = sceneFactory_->CreateScene(nextSceneName_);
        currentScene_->Init(dxCommon_, input_, audio_);
        // シーン切り替え時にロードされたテクスチャを一括転送・同期する
        TextureManager::GetInstance()->FlushUploads();

        // LOADINGシーンへの切り替え時にバックグラウンドロードを開始する
        if (nextSceneName_ == "LOADING" && !loadingTargetScene_.empty()) {
            StartBackgroundLoad();
        }
    }

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

void SceneManager::StartBackgroundLoad()
{
    // 前回のロードスレッドが残っていれば先に片付ける（本来は起こらないはずの安全策）
    if (loadingThread_.joinable()) {
        loadingThread_.join();
    }

    std::string target = loadingTargetScene_;
    loadingThread_ = std::thread([this, target]() {
        try {
            asyncLoadProgress_.store(kProgressCreating);
            auto scene = sceneFactory_->CreateScene(target);
            if (!scene) {
                throw std::runtime_error("Unknown scene: " + target);
            }
            // GPUリソース生成を含むInitはメインスレッド側で実行する
            // ワーカーは共有描画状態へ触れず、生成済みシーンの受け渡しだけを担当する
            asyncLoadProgress_.store(kProgressCreated);
            preloadedScene_ = std::move(scene);
            asyncLoadReady_.store(true);
        } catch (const std::exception& error) {
            {
                std::scoped_lock lock(asyncLoadErrorMutex_);
                asyncLoadError_ = error.what();
            }
            asyncLoadFailed_.store(true);
            Logger::LogError("Async scene load failed: " + std::string(error.what()));
        } catch (...) {
            {
                std::scoped_lock lock(asyncLoadErrorMutex_);
                asyncLoadError_ = "Unknown exception";
            }
            asyncLoadFailed_.store(true);
            Logger::LogError("Async scene load failed: unknown exception");
        }
    });
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
    // バックグラウンドロードスレッドが dxCommon_ 等を参照し続けている間に
    // 破棄処理へ進まないよう、終了前に必ず合流させる
    if (loadingThread_.joinable()) {
        loadingThread_.join();
    }

    // 最終フレームで使用したシーンのGPUリソースを安全に破棄できるまで待機する
    // ステージエディタ表示中は配置モデルとギズモも描画するため、シーン破棄より前に同期する
    if (dxCommon_) {
        dxCommon_->WaitForGpu();
    }

    preloadedScene_.reset();

    editorPreview_.Finalize();

    if (currentScene_) {
        currentScene_->Shutdown();
    }

    currentScene_.reset();
    nextScene_.reset();

    transition_ = SceneTransitionOverlay {};

}

// ロード画面経由でシーン切り替え
void SceneManager::ChangeSceneWithLoading(const std::string& targetScene)
{
    loadingTargetScene_ = targetScene;
    asyncLoadReady_.store(false);
    asyncLoadProgress_.store(0.0f);
    asyncLoadFailed_.store(false);
    {
        std::scoped_lock lock(asyncLoadErrorMutex_);
        asyncLoadError_.clear();
    }
    preloadedScene_.reset();
    ChangeScene("LOADING");
}

std::string SceneManager::GetAsyncLoadError() const
{
    std::scoped_lock lock(asyncLoadErrorMutex_);
    return asyncLoadError_;
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
