/**
 * @file DirectXCommon.cpp
 * @brief DirectXCommonの初期化・終了処理、フレーム制御（PreDraw/PostDraw）、リソースバリア・GPU同期の実装
 * @note デバイス・コマンド・スワップチェーン等の生成処理はDirectXCommonInit.cppに分割されている
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
    // 深度バッファ（固定解像度）を併用するため拡大はせず、バックバッファ中央に配置する
    D3D12_VIEWPORT viewport = GetCenteredClientViewport();
    commandList_->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = GetCenteredClientScissorRect();
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

D3D12_VIEWPORT DirectXCommon::GetBackBufferViewport() const
{
    uint32_t backBufferWidth = winApp_->kClientWidth;
    uint32_t backBufferHeight = winApp_->kClientHeight;
    if (swapChain_) {
        DXGI_SWAP_CHAIN_DESC1 desc = { };
        if (SUCCEEDED(swapChain_->GetDesc1(&desc)) && desc.Width > 0 && desc.Height > 0) {
            backBufferWidth = desc.Width;
            backBufferHeight = desc.Height;
        }
    }

    // 内部解像度のアスペクト比を保ったまま、バックバッファ内に収まる最大サイズへ拡大する
    const float clientAspect = static_cast<float>(winApp_->kClientWidth) / static_cast<float>(winApp_->kClientHeight);
    const float backAspect = static_cast<float>(backBufferWidth) / static_cast<float>(backBufferHeight);

    float width = static_cast<float>(backBufferWidth);
    float height = static_cast<float>(backBufferHeight);
    if (backAspect > clientAspect) {
        // バックバッファの方が横長 → 左右に余白（ピラーボックス）
        width = height * clientAspect;
    } else if (backAspect < clientAspect) {
        // バックバッファの方が縦長 → 上下に余白（レターボックス）
        height = width / clientAspect;
    }

    D3D12_VIEWPORT viewport = { };
    viewport.TopLeftX = (static_cast<float>(backBufferWidth) - width) * 0.5f;
    viewport.TopLeftY = (static_cast<float>(backBufferHeight) - height) * 0.5f;
    viewport.Width = width;
    viewport.Height = height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    return viewport;
}

D3D12_RECT DirectXCommon::GetBackBufferScissorRect() const
{
    const D3D12_VIEWPORT viewport = GetBackBufferViewport();
    D3D12_RECT rect = { };
    rect.left = static_cast<LONG>(viewport.TopLeftX);
    rect.top = static_cast<LONG>(viewport.TopLeftY);
    rect.right = static_cast<LONG>(viewport.TopLeftX + viewport.Width);
    rect.bottom = static_cast<LONG>(viewport.TopLeftY + viewport.Height);
    return rect;
}

D3D12_VIEWPORT DirectXCommon::GetCenteredClientViewport() const
{
    uint32_t backBufferWidth = winApp_->kClientWidth;
    uint32_t backBufferHeight = winApp_->kClientHeight;
    if (swapChain_) {
        DXGI_SWAP_CHAIN_DESC1 desc = { };
        if (SUCCEEDED(swapChain_->GetDesc1(&desc)) && desc.Width > 0 && desc.Height > 0) {
            backBufferWidth = desc.Width;
            backBufferHeight = desc.Height;
        }
    }

    D3D12_VIEWPORT viewport = { };
    viewport.TopLeftX = (static_cast<float>(backBufferWidth) - static_cast<float>(winApp_->kClientWidth)) * 0.5f;
    viewport.TopLeftY = (static_cast<float>(backBufferHeight) - static_cast<float>(winApp_->kClientHeight)) * 0.5f;
    viewport.Width = static_cast<float>(winApp_->kClientWidth);
    viewport.Height = static_cast<float>(winApp_->kClientHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    return viewport;
}

D3D12_RECT DirectXCommon::GetCenteredClientScissorRect() const
{
    const D3D12_VIEWPORT viewport = GetCenteredClientViewport();
    D3D12_RECT rect = { };
    rect.left = static_cast<LONG>(viewport.TopLeftX);
    rect.top = static_cast<LONG>(viewport.TopLeftY);
    rect.right = static_cast<LONG>(viewport.TopLeftX + viewport.Width);
    rect.bottom = static_cast<LONG>(viewport.TopLeftY + viewport.Height);
    return rect;
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
