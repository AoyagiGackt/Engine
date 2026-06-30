#include "SceneManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#ifdef _DEBUG
#include "TrainingScene.h"
#endif
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

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

    // 最初のシーン（デバッグ時はトレーニングから直接開始）
#ifdef _DEBUG
    currentScene_ = std::make_unique<TrainingScene>();
#else
    currentScene_ = std::make_unique<TitleScene>();
#endif
    currentScene_->Initialize(dxCommon_, input_, audio_);
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

        // 前のシーンを終了処理してからリソースを解放
        if (currentScene_) {
            currentScene_->Finalize();
        }

        if (preloadedScene_ && nextSceneName_ == loadingTargetScene_) {
            // バックグラウンドで Initialize 済み → FlushUploads だけ呼んでそのまま使う
            currentScene_ = std::move(preloadedScene_);
            TextureManager::GetInstance()->FlushUploads();
            loadingTargetScene_.clear();
        } else {
            // 工場を使って新しいシーンを作成・初期化
            currentScene_ = sceneFactory_->CreateScene(nextSceneName_);
            currentScene_->Initialize(dxCommon_, input_, audio_);
            // シーン切り替え時にロードされたテクスチャを一括転送・同期する
            TextureManager::GetInstance()->FlushUploads();

            // LOADINGシーンへの切り替え時にバックグラウンドロードを開始する
            if (nextSceneName_ == "LOADING" && !loadingTargetScene_.empty()) {
                std::string target = loadingTargetScene_;
                loadingThread_ = std::thread([this, target]() {
                    auto scene = sceneFactory_->CreateScene(target);
                    scene->Initialize(dxCommon_, input_, audio_);
                    preloadedScene_ = std::move(scene);
                    asyncLoadReady_.store(true);
                });
                loadingThread_.detach();
            }
        }

        // ImGuiのセット
        currentScene_->SetImGuiManager(imguiManager_);

        // シーンが切り替わったので、画面を明るくし始める
        fade_.Start(Fade::Status::FadeIn, fadeInDuration_);
        isChanging_ = false;
    }

    // フェード中であっても、今のシーンの更新は続ける
    if (currentScene_) {
        currentScene_->Update();
    }
}

void SceneManager::Draw()
{
    if (currentScene_) {
        currentScene_->Draw();
    }

    fade_.Draw();
}

void SceneManager::Finalize()
{
    if (currentScene_) {
        currentScene_->Finalize();
    }

    currentScene_.reset();
    nextScene_.reset();

    // Fade内のSpriteが持つD3D12リソースを解放する
    fade_ = Fade{};
    // SpriteCommonのPSO・ルートシグネチャ・バッファを解放する
    spriteCommon_.reset();
}

// ロード画面経由でシーン切り替え
void SceneManager::ChangeSceneWithLoading(const std::string& targetScene)
{
    loadingTargetScene_ = targetScene;
    asyncLoadReady_.store(false);
    preloadedScene_.reset();
    ChangeScene("LOADING");
}

// シーン切り替え予約
void SceneManager::ChangeScene(const std::string& sceneName, float fadeOut, float fadeIn)
{
    if (isChanging_) {
        return;
    }

    nextSceneName_   = sceneName;
    isChanging_      = true;
    fadeInDuration_  = fadeIn;

    // 暗転開始
    fade_.Start(Fade::Status::FadeOut, fadeOut);
}
