/**
 * @file Framework.cpp
 * @brief アプリケーションのメインループ・基盤システム初期化と終了処理（Framework）の実装
 */
#include "Framework.h"
#include "AssetPack.h"
#include "FrameProfiler.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImageFilter.h"
#include "LightingMode.h"
#include "Logger.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "ModelManager.h"
#include "OutlineEffect.h"
#include "ParticleManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "VignetteEffect.h"
using namespace engine;
using namespace engine::graphics;

void Framework::Run()
{
    const std::wstring commandLine = GetCommandLineW();
    const bool forceCook = commandLine.find(L"--cook-assets") != std::wstring::npos;
    const bool prepareAssets = commandLine.find(L"--prepare-assets") != std::wstring::npos;
    auto* assetPack = AssetPack::GetInstance();
    assetPack->Initialize(forceCook, prepareAssets);
    // 変更がなければ、ビルド後の素材準備はウィンドウやGPUを作らず終了する。
    if (prepareAssets && !assetPack->IsCooking()) {
        assetPack->Finalize();
        return;
    }
    Initialize();
    // 素材準備専用の起動では、全シーンを収集してpakを書き出したら終了する。
    if (forceCook || prepareAssets) {
        Finalize();
        return;
    }
    while (true) {
        FrameProfiler::GetInstance()->BeginFrame();
        Update();

        if (IsEndRequest()) {
            break;
        }

        Draw();
        FrameProfiler::GetInstance()->EndFrame();
    }

    Finalize();
}

void Framework::Initialize()
{
    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize();

    dxCommon_ = std::make_unique<DirectXCommon>();
    dxCommon_->Initialize(winApp_.get());

    // ウィンドウがリサイズされたらスワップチェーンを追従させる
    winApp_->SetResizeCallback([this](int32_t width, int32_t height) {
        dxCommon_->OnResize(width, height);
    });

    SrvManager::GetInstance()->Initialize(dxCommon_.get());
    GrayscaleEffect::GetInstance()->Initialize(dxCommon_.get(), SrvManager::GetInstance());
    ImageFilter::GetInstance()->Initialize(dxCommon_.get(), SrvManager::GetInstance());
    VignetteEffect::GetInstance()->Initialize(dxCommon_.get());
    HsvFilter::GetInstance()->Initialize(dxCommon_.get(), SrvManager::GetInstance());
    OutlineEffect::GetInstance()->Initialize(dxCommon_.get());
    TextureManager::GetInstance()->Initialize(dxCommon_.get());
    ParticleManager::GetInstance()->Initialize(dxCommon_.get());

    input_ = std::make_unique<Input>();
    input_->Initialize(winApp_.get());

    audio_ = std::make_unique<Audio>();
    audio_->Initialize();

    imguiManager_ = std::make_unique<ImGuiManager>();
    imguiManager_->Initialize(winApp_.get(), dxCommon_.get());
}

void Framework::Update()
{
    input_->Update();
    imguiManager_->Begin();

    // F11 でフルスクリーン/ウィンドウ切り替え
    if (input_->TriggerKey(DIK_F11)) {
        winApp_->ToggleFullscreen();
    }

    // F10 でVSyncのON/OFF切り替え
    if (input_->TriggerKey(DIK_F10)) {
        dxCommon_->ToggleVSync();
        Logger::LogInfo(dxCommon_->IsVSyncEnabled() ? "VSync: ON" : "VSync: OFF");
    }
}

void Framework::Finalize()
{
    // ImGuiの終了
    if (imguiManager_) {
        imguiManager_->Finalize();
    }

    if (videoPlayer_) {
        videoPlayer_->Finalize();
    }

    // 各種マネージャーのGPUリソースを解放する
    HsvFilter::GetInstance()->Finalize();
    VignetteEffect::GetInstance()->Finalize();
    ImageFilter::GetInstance()->Finalize();
    GrayscaleEffect::GetInstance()->Finalize();
    ParticleManager::GetInstance()->Finalize();
    MeshManager::GetInstance()->Finalize();
    MaterialManager::GetInstance()->Finalize();
    TextureManager::GetInstance()->Finalize();
    SrvManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();

    // Audioの終了
    if (audio_) {
        audio_->Finalize();
    }

    // クラス自体の削除
    imguiManager_.reset();
    audio_.reset();
    input_.reset();

    // 最後にデバイスとウィンドウを削除（Finalizeでまずフルスクリーン解除とGPU完了待ち）
    dxCommon_->Finalize();
    dxCommon_.reset();
    winApp_.reset();

    // cookモードなら、このプレイ中に読み込んだ素材を変換済みの形でpakへ書き出す
    AssetPack::GetInstance()->Finalize();
}
