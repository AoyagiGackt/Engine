/**
 * @file SceneManager.cpp
 * @brief SceneManagerが担当する処理を実装するファイル
 */
#include "SceneManager.h"
#include "CrashHandler.h"
#include "StageEditor.h"
#include "TextureManager.h"
#include "Logger.h"
#include "TitleScene.h"
#include <stdexcept>
#ifdef _DEBUG
#include "TrainingScene.h"
#endif
#ifdef USE_IMGUI
#include "EditorUI.h"
#include "GraphEditor.h"
#endif
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

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

    // 最初のシーン（デバッグ時はテストしやすいようTrainingSceneへ直行、それ以外はタイトルから）
    currentScene_ = std::make_unique<TitleScene>();
    currentScene_->Init(dxCommon_, input_, audio_);
    // シーン初期化中にロードされたテクスチャを一括転送・同期する
    TextureManager::GetInstance()->FlushUploads();

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    // フェードの初期化
    fade_.Initialize(spriteCommon_.get());

    currentScene_->SetImGuiManager(imguiManager_);
}

void SceneManager::Update()
{

    fade_.Update(); // フェードのタイマー更新

    // シーン切り替えの予約（FadeOutが終わったタイミング）
    if (isChanging_ && fade_.IsFinished()) {
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
            asyncLoadProgress_.store(0.5f);
            try {
                preloadedScene_->Init(dxCommon_, input_, audio_);
                asyncLoadProgress_.store(0.9f);
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
                // 前回のロードスレッドが残っていれば先に片付ける（本来は起こらないはずの安全策）
                if (loadingThread_.joinable()) {
                    loadingThread_.join();
                }

                std::string target = loadingTargetScene_;
                loadingThread_ = std::thread([this, target]() {
                    try {
                        asyncLoadProgress_.store(0.1f);
                        auto scene = sceneFactory_->CreateScene(target);
                        if (!scene) {
                            throw std::runtime_error("Unknown scene: " + target);
                        }
                        // GPUリソース生成を含むInitはメインスレッド側で実行する
                        // ワーカーは共有描画状態へ触れず、生成済みシーンの受け渡しだけを担当する
                        asyncLoadProgress_.store(0.4f);
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
        }

        // ImGuiのセット
        currentScene_->SetImGuiManager(imguiManager_);
        dxCommon_->SetDiagnosticContext(nextSceneName_);
        CrashHandler::SetContext(nextSceneName_);

        // シーンが切り替わったので、画面を明るくし始める
        fade_.Start(Fade::Status::FadeIn, fadeInDuration_);
        isChanging_ = false;
    }

    // フェード中であっても、今のシーンの更新は続ける
    if (currentScene_) {
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
    EditorUI::ShowHotkeyOverlay(
        GraphEditor::GetInstance()->IsVisible(),
        currentScene_ && currentScene_->GetStageEditor().IsVisible(),
        currentScene_ ? currentScene_->GetHotkeyOverlayExtra() : nullptr);
#endif
}

void SceneManager::Draw()
{
    if (currentScene_) {
        // D3D12デバッグメッセージへ現在のシーンを添えて原因箇所を絞り込む
        dxCommon_->SetDiagnosticContext(nextSceneName_.empty() ? "TitleScene" : nextSceneName_);
        CrashHandler::SetContext(nextSceneName_.empty() ? "TitleScene" : nextSceneName_);
        currentScene_->Render();
    }

    fade_.Draw();
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

    if (currentScene_) {
        currentScene_->Shutdown();
    }

    currentScene_.reset();
    nextScene_.reset();

    // Fade内のSpriteが持つD3D12リソースを解放する
    fade_ = Fade { };
    // SpriteCommonのPSO・ルートシグネチャ・バッファを解放する
    spriteCommon_.reset();
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
    if (isChanging_) {
        return;
    }

    nextSceneName_ = sceneName;
    isChanging_ = true;
    fadeInDuration_ = fadeIn;

    // 暗転開始
    fade_.Start(Fade::Status::FadeOut, fadeOut);
}
