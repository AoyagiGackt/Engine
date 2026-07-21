/**
 * @file DirectXCommon.cpp
 * @brief DirectXCommonのエンジン基盤の初期化と状態管理に関する具体的な処理を実装するファイル
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

namespace {

// 状態をリソース自身へ保存し、解放後に同じアドレスが再利用されても
// 古い追跡情報を引き継がないようにする
constexpr GUID kTrackedResourceStateGuid = {
    0x56f0ea61, 0x3f6b, 0x4f54, { 0xa8, 0x91, 0xc8, 0x15, 0x29, 0x6f, 0x6e, 0x31 }
};

bool ReadTrackedState(ID3D12Resource* resource, D3D12_RESOURCE_STATES& state)
{
    UINT size = sizeof(state);
    return resource
        && SUCCEEDED(resource->GetPrivateData(kTrackedResourceStateGuid, &size, &state))
        && size == sizeof(state);
}

void WriteTrackedState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
{
    if (resource) {
        resource->SetPrivateData(kTrackedResourceStateGuid, sizeof(state), &state);
    }
}

} // namespace

// Initialize関数
// ══════════════════════════════════════════════════════
// 初期化とフレーム制御
// ══════════════════════════════════════════════════════

void DirectXCommon::Initialize(WinApp* winApp)
{
    ENGINE_ASSERT(winApp);
    winApp_ = winApp;

    // FPS固定初期化
    InitializeFixFPS();
    // デバイス初期化
    InitializeDevice();
    // コマンド関連初期化
    CreateCommand();
    // ディスクリプタヒープ作成
    CreateDescriptorHeaps();
    // スワップチェーン作成
    CreateSwapChain();
    // レンダーターゲットビュー作成
    CreateRTV();
    // 深度バッファ作成
    CreateDepthBuffer();
    // フェンス作成
    CreateFence();
    // DXCコンパイラ初期化
    InitializeDXC();
}

void DirectXCommon::Finalize()
{
    // GPU の処理がすべて終わるまで待つ
    WaitForGpu();

#ifdef _DEBUG
    if (infoQueue_ && diagnosticsCallbackCookie_ != 0) {
        infoQueue_->UnregisterMessageCallback(diagnosticsCallbackCookie_);
        diagnosticsCallbackCookie_ = 0;
    }
#endif

    // スワップチェーンのフルスクリーン解除（解放前に必須）
    BOOL fullscreen = FALSE;

    if (swapChain_) {
        swapChain_->GetFullscreenState(&fullscreen, nullptr);

        if (fullscreen) {
            swapChain_->SetFullscreenState(FALSE, nullptr);
        }
    }

    // イベントハンドルを閉じる
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void DirectXCommon::PreDraw()
{
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // このバックバッファ用のアロケータをGPUが使い終えるまで待つ（前回このインデックスを使った時のフェンス値まで）
    // 毎フレーム無条件に全待ちする代わりに、実際に必要な時だけ止めることでCPU/GPUを並行して動かす
    if (frameFenceValues_[backBufferIndex] != 0
        && fence_->GetCompletedValue() < frameFenceValues_[backBufferIndex]) {
        fence_->SetEventOnCompletion(frameFenceValues_[backBufferIndex], fenceEvent_);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    // コマンドアロケータとリストをリセット
    HRESULT hr = commandAllocators_[backBufferIndex]->Reset();
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = commandList_->Reset(commandAllocators_[backBufferIndex].Get(), nullptr);
    ENGINE_ASSERT(SUCCEEDED(hr));

    // リソースバリア
    TransitionBarrier(commandList_.Get(), swapChainResources_[backBufferIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    // 描画先と深度を設定
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], false, &dsvHandle);

    // 画面クリア
    static constexpr float kClearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex], kClearColor, 0, nullptr);

    // 深度クリア
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // ビューポートとシザー矩形の設定
    D3D12_VIEWPORT viewport = { };
    viewport.Width = static_cast<float>(winApp_->kClientWidth);
    viewport.Height = static_cast<float>(winApp_->kClientHeight);
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    commandList_->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = { };
    scissorRect.left = 0;
    scissorRect.right = static_cast<LONG>(winApp_->kClientWidth);
    scissorRect.top = 0;
    scissorRect.bottom = static_cast<LONG>(winApp_->kClientHeight);
    commandList_->RSSetScissorRects(1, &scissorRect);
}

void DirectXCommon::PostDraw()
{
    HRESULT hr;

    // リソースバリア
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    TransitionBarrier(commandList_.Get(), swapChainResources_[backBufferIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    // コマンドリストを閉じる
    hr = commandList_->Close();
    ENGINE_ASSERT(SUCCEEDED(hr));

    // GPUコマンド実行（FPS待機より先に投入してGPUを遊ばせない）
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);

    // このフレームで使ったアロケータの完了目印としてフェンス値を積む
    // （ここでは待たない。実際に待つのは、次にこのバックバッファを使い回す時＝PreDraw側）
    ++fenceValue_;
    hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
    if (FAILED(hr)) {
        const HRESULT reason = device_->GetDeviceRemovedReason();
        Logger::LogError(std::format(
            "GPU command submission failed. context={} signal=0x{:08X} reason=0x{:08X}",
            diagnosticContext_, static_cast<unsigned long>(hr), static_cast<unsigned long>(reason)));
    }
    ENGINE_ASSERT(SUCCEEDED(hr));
    frameFenceValues_[backBufferIndex] = fenceValue_;

    // フリップ (画面更新)
    // VSyncオフ時はSyncInterval=0。ティアリング許可済みならALLOW_TEARINGを付けて真の非同期表示にする
    const UINT syncInterval = vsyncEnabled_ ? 1 : 0;
    const UINT presentFlags = (!vsyncEnabled_ && tearingSupported_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    hr = swapChain_->Present(syncInterval, presentFlags);

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT reason = device_->GetDeviceRemovedReason();
            Logger::LogError(std::format(
                "GPU device removed! Present hr=0x{:08X} reason=0x{:08X} "
                "(ドライバクラッシュ・TDR・GPUの物理的な切断などが考えられます)",
                static_cast<unsigned long>(hr), static_cast<unsigned long>(reason)));
        }
        ENGINE_ASSERT(SUCCEEDED(hr));
    }

    // FPS固定（GPU が動いている間に CPU 側で余った時間を使って待機）
    UpdateFixFPS();
}

// ══════════════════════════════════════════════════════
// シェーダーコンパイル
// ══════════════════════════════════════════════════════

Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(const std::wstring& filePath, const wchar_t* profile)
{
    const std::wstring cacheKey = filePath + L"|" + profile;
    {
        std::scoped_lock lock(shaderCacheMutex_);
        const auto cached = shaderCache_.find(cacheKey);
        if (cached != shaderCache_.end()) {
            return cached->second;
        }
    }

    // hlslファイルを読む
    Logger::Log(StringUtility::ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile)));

    IDxcBlobEncoding* shaderSource = nullptr;
    HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    ENGINE_ASSERT(SUCCEEDED(hr));

    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    // Compileする
    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E",
        L"main",
        L"-T",
        profile,
        L"-Zi",
        L"-Qembed_debug",
        L"-Od",
        L"-Zpr",
    };

    IDxcResult* shaderResult = nullptr;
    hr = dxcCompiler_->Compile(
        &shaderSourceBuffer,
        arguments,
        _countof(arguments),
        includeHandler_.Get(),
        IID_PPV_ARGS(&shaderResult));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // 警告・エラー確認
    IDxcBlobUtf8* shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);

    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Logger::LogError(shaderError->GetStringPointer());
        ENGINE_ASSERT(false);
    }

    // 結果を受け取る
    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    ENGINE_ASSERT(SUCCEEDED(hr));

    Logger::Log(StringUtility::ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile)));

    shaderSource->Release();
    shaderResult->Release();

    {
        std::scoped_lock lock(shaderCacheMutex_);
        shaderCache_[cacheKey] = shaderBlob;
    }

    return shaderBlob;
}

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

// ══════════════════════════════════════════════════════
// リサイズと同期
// ══════════════════════════════════════════════════════

void DirectXCommon::OnResize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || !swapChain_) {
        return;
    } // 最小化中などは無視

    DXGI_SWAP_CHAIN_DESC1 desc = { };
    swapChain_->GetDesc1(&desc);
    if (desc.Width == width && desc.Height == height) {
        return;
    }

    // GPUがバックバッファの参照を終えるまで待ってから解放する
    WaitForGpu();
    swapChainResources_[0].Reset();
    swapChainResources_[1].Reset();

    // フォーマットは DXGI_FORMAT_UNKNOWN を渡して既存のもの（作成時のUNORM）を維持する
    // Flagsは作成時（tearingSupported_に応じたALLOW_TEARING）と同じ値にする必要がある
    const UINT flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = swapChain_->ResizeBuffers(2, width, height, DXGI_FORMAT_UNKNOWN, flags);
    ENGINE_ASSERT(SUCCEEDED(hr));

    CreateRTV();
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

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCurrentBackBufferHandle()
{
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    return rtvHandles_[backBufferIndex];
}

ID3D12Resource* DirectXCommon::GetCurrentBackBufferResource()
{
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    return swapChainResources_[backBufferIndex].Get();
}

// ヘルパー関数（バッファ作成用）
// ══════════════════════════════════════════════════════
// 共通リソース操作
// ══════════════════════════════════════════════════════

Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes)
{
    HRESULT hr;
    D3D12_HEAP_PROPERTIES uploadHeapProperties { };
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc { };
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
    hr = device_->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&resource));
    ENGINE_ASSERT(SUCCEEDED(hr));

    return resource;
}

void DirectXCommon::WaitForFence(
    ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64& fenceValue, HANDLE fenceEvent)
{
    ++fenceValue;
    queue->Signal(fence, fenceValue);

    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void DirectXCommon::WaitForGpu()
{
    WaitForFence(commandQueue_.Get(), fence_.Get(), fenceValue_, fenceEvent_);
}

D3D12_RESOURCE_BARRIER DirectXCommon::MakeTransitionBarrier(
    ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource)
{
    ENGINE_ASSERT(resource);

    // 全サブリソース遷移だけを追跡する
    // 個別Mipの状態は一つの値で表せないため呼び出し側の指定をそのまま使う
    if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
        D3D12_RESOURCE_STATES trackedState;
        if (ReadTrackedState(resource, trackedState) && trackedState != before) {
            Logger::LogWarning(std::format(
                "Resource state mismatch. requested_before=0x{:X} tracked_before=0x{:X} after=0x{:X}",
                static_cast<unsigned>(before), static_cast<unsigned>(trackedState), static_cast<unsigned>(after)));

            // 既に目的状態なら同一状態バリアを避けるため呼び出し側の値を残す
            // それ以外は追跡済みの実状態から遷移する
            if (trackedState != after) {
                before = trackedState;
            }
        }
        WriteTrackedState(resource, after);
    }

    D3D12_RESOURCE_BARRIER barrier = { };
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = subresource;
    return barrier;
}

void DirectXCommon::TransitionBarrier(
    ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource)
{
    D3D12_RESOURCE_BARRIER barrier = MakeTransitionBarrier(resource, before, after, subresource);
    commandList->ResourceBarrier(1, &barrier);
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    desc.Type = type;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    ENGINE_ASSERT(SUCCEEDED(hr));
    return heap;
}
