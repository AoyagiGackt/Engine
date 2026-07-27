/**
 * @file DirectXCommonInit.cpp
 * @brief DirectXCommonのデバイス・コマンド・スワップチェーン・深度バッファ・フェンス等の生成処理を実装するファイル
 * @note DirectXCommon.cppからの分割ファイルクラス自体はDirectXCommonのまま、定義の置き場所だけを分けている
 */
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include "GameConstants.h"
#include "Input.h"
#include "Logger.h"
#include "StringUtility.h"
#include "WinApp.h"
#include <format>
#include <string>
using namespace engine;

using Microsoft::WRL::ComPtr;


// 内部関数

// ══════════════════════════════════════════════════════
// DirectXリソース生成
// ══════════════════════════════════════════════════════

void DirectXCommon::InitializeDevice()
{
    HRESULT hr;
#ifdef _DEBUG
    // GPU停止時に直前のコマンド履歴とページフォルト情報を取得できるようにする
    // DREDはデバイス生成後には有効化できないため、デバッグレイヤーと同時に設定する
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }

    ComPtr<ID3D12Debug1> validationController = nullptr;

    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&validationController)))) {
        validationController->EnableDebugLayer();

        // GPU検証は負荷が大きいため通常のDebug実行では無効にする
        // 詳細なシェーダー検証が必要な場合だけENGINE_GPU_VALIDATIONを設定して有効にする
        wchar_t gpuValidationValue[2] = { };
        const bool enableGpuValidation = GetEnvironmentVariableW(L"ENGINE_GPU_VALIDATION", gpuValidationValue, 2) > 0
            && gpuValidationValue[0] != L'0';
        validationController->SetEnableGPUBasedValidation(enableGpuValidation ? TRUE : FALSE);
        Logger::LogInfo(enableGpuValidation
                ? "D3D12 GPU-based validation: ON"
                : "D3D12 GPU-based validation: OFF");
    }
#endif

    hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    ComPtr<IDXGIAdapter4> useAdapter = nullptr;
    for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC3 adapterDesc { };
        hr = useAdapter->GetDesc3(&adapterDesc);
        ENGINE_ASSERT(SUCCEEDED(hr));

        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            Logger::Log(StringUtility::ConvertString(std::format(L"USE Adapter:{}", adapterDesc.Description)));
            break;
        }

        useAdapter = nullptr;
    }

    ENGINE_ASSERT(useAdapter != nullptr);

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 };
    const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };
    for (size_t i = 0; i < _countof(featureLevels); ++i) {
        hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device_));

        if (SUCCEEDED(hr)) {
            Logger::Log((std::format("Feature Level: {}", featureLevelStrings[i])));
            break;
        }
    }

    ENGINE_ASSERT(device_ != nullptr);

#ifdef _DEBUG
    // ERROR/CORRUPTIONメッセージが出た瞬間にその場でブレークさせる
    // （出力ウィンドウに流れるだけだと見逃し、原因のドローコールから離れた場所で気づくことになるため）
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue_)))) {
        infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        infoQueue_->RegisterMessageCallback(
            &DirectXCommon::DiagnosticsMessageCallback,
            D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &diagnosticsCallbackCookie_);
    }
#endif
}

#ifdef _DEBUG
void CALLBACK DirectXCommon::DiagnosticsMessageCallback(
    D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
    D3D12_MESSAGE_ID id, LPCSTR description, void* context)
{
    auto* self = static_cast<DirectXCommon*>(context);
    const std::string message = std::format(
        "D3D12 message id={} context={} detail={}", static_cast<int>(id),
        self ? self->diagnosticContext_ : "unknown", description ? description : "no description");

    if (severity == D3D12_MESSAGE_SEVERITY_ERROR
        || severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
        Logger::LogError(message);
    } else if (severity == D3D12_MESSAGE_SEVERITY_WARNING) {
        Logger::LogWarning(message);
    }
}
#endif

void DirectXCommon::CreateCommand()
{
    HRESULT hr;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { };
    hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    for (UINT i = 0; i < kFrameCount; ++i) {
        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocators_[i]));
        ENGINE_ASSERT(SUCCEEDED(hr));
    }

    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators_[0].Get(), nullptr, IID_PPV_ARGS(&commandList_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    commandList_->Close();
}

void DirectXCommon::CreateDescriptorHeaps()
{
    rtvDescriptorHeap_ = CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
}

void DirectXCommon::CreateSwapChain()
{
    HRESULT hr;

    // VSyncオフ時にちぎれ表示（ティアリング）を許可できるかを確認する
    // （対応していない環境ではVSyncオフでもPresent間隔0のまま=実質VSyncオンと同じ挙動になる）
    BOOL allowTearing = FALSE;
    if (SUCCEEDED(dxgiFactory_->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))) {
        tearingSupported_ = (allowTearing != FALSE);
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { };
    swapChainDesc.Width = winApp_->kClientWidth;
    swapChainDesc.Height = winApp_->kClientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2; // ダブルバッファ
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    hr = dxgiFactory_->CreateSwapChainForHwnd(
        commandQueue_.Get(),
        winApp_->GetHwnd(),
        &swapChainDesc,
        nullptr, nullptr,
        reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

void DirectXCommon::CreateDepthBuffer()
{
    // 深度ステンシルテクスチャの設定
    D3D12_RESOURCE_DESC resourceDesc = { };
    resourceDesc.Width = winApp_->kClientWidth;
    resourceDesc.Height = winApp_->kClientHeight;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS; // SRV用にTYPELESSで作成
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties = { };
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE depthClearValue { };
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClearValue,
        IID_PPV_ARGS(&depthStencilResource_)); // depthStencilResource_ に保存
    ENGINE_ASSERT(SUCCEEDED(hr));

    // DSVヒープの作成
    dsvDescriptorHeap_ = CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

    // DSVの作成
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = { };
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(
        depthStencilResource_.Get(),
        &dsvDesc,
        dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
}

void DirectXCommon::CreateRTV()
{
    HRESULT hr;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&swapChainResources_[0]));
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = swapChain_->GetBuffer(1, IID_PPV_ARGS(&swapChainResources_[1]));
    ENGINE_ASSERT(SUCCEEDED(hr));

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = { };
    rtvDesc.Format = GetBackBufferFormat();
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

    rtvHandles_[0] = rtvStartHandle;
    device_->CreateRenderTargetView(swapChainResources_[0].Get(), &rtvDesc, rtvHandles_[0]);

    rtvHandles_[1].ptr = rtvHandles_[0].ptr + device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    device_->CreateRenderTargetView(swapChainResources_[1].Get(), &rtvDesc, rtvHandles_[1]);
}

void DirectXCommon::CreateFence()
{
    HRESULT hr;
    fenceValue_ = 0;
    hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    ENGINE_ASSERT(fenceEvent_ != nullptr);
}

void DirectXCommon::InitializeDXC()
{
    HRESULT hr;
    // dxcUtilsの初期化
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // dxcCompilerの初期化
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // includeHandlerの初期化
    hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
    ENGINE_ASSERT(SUCCEEDED(hr));
}

void DirectXCommon::InitializeFixFPS()
{
    // 現在時間を記録する
    reference_ = std::chrono::steady_clock::now();
}

void DirectXCommon::UpdateFixFPS()
{

    static constexpr float kFpsMargin = 65.0f; // 60fps未達のチェック閾値
    const std::chrono::microseconds kMinTime(uint64_t(1000000.0f / GameConstants::kTargetFps));
    const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / kFpsMargin));
    // 現在時間を取得
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    // 前回記録からの経過時間を取得する
    std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

    // 60フレーム立っていない場合
    if (elapsed < kMinCheckTime) {
        // 60フレームになるまで微小なスリープを繰り返す
        while (std::chrono::steady_clock::now() - reference_ < kMinTime) {
            // 1マイクロ秒スリープ
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }

    // 現在時間を記録する
    reference_ = std::chrono::steady_clock::now();
}
