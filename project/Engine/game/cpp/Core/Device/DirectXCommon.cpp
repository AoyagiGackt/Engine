/**
 * @file DirectXCommon.cpp
 * @brief DirectXCommonの初期化・終了処理、フレーム制御（PreDraw/PostDraw）、リソースバリア・GPU同期の実装
 * @note デバイス・コマンド・スワップチェーン等の生成処理はDirectXCommonInit.cppに分割されている
 */
#include "DirectXCommon.h"
#include "PipelineHelper.h"
#include "AssetPack.h"
#include "EngineAssert.h"
#include "GameConstants.h"
#include "Input.h"
#include "Logger.h"
#include "StringUtility.h"
#include "WinApp.h"
#include <format>
#include <string>

using Microsoft::WRL::ComPtr;

namespace engine {
namespace {

// 状態をリソース自身へ保存し、解放後に同じアドレスが再利用されても
// 古い追跡情報を引き継がないようにする
constexpr GUID kTrackedResourceStateGuid = {
    0x56f0ea61, 0x3f6b, 0x4f54, { 0xa8, 0x91, 0xc8, 0x15, 0x29, 0x6f, 0x6e, 0x31 }
};

/** @brief リソースに記録しておいた現在のリソース状態を読み出す（記録が無ければ false） */
bool ReadTrackedState(ID3D12Resource* resource, D3D12_RESOURCE_STATES& state)
{
    UINT size = sizeof(state);
    return resource
        && SUCCEEDED(resource->GetPrivateData(kTrackedResourceStateGuid, &size, &state))
        && size == sizeof(state);
}

/** @brief リソースの現在のリソース状態をリソース自身へ記録する */
void WriteTrackedState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
{
    if (resource)
    {
        resource->SetPrivateData(kTrackedResourceStateGuid, sizeof(state), &state);
    }
}

} // namespace

// Initialize関数
// ══════════════════════════════════════════════════════
// 初期化とフレーム制御
// ══════════════════════════════════════════════════════

/** @brief FPS固定・デバイス・コマンド・スワップチェーンなど DirectX12 の基盤を順に初期化する */
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
    CreateBackBufferDepthBuffer();
    // フェンス作成
    CreateFence();
    // DXCコンパイラ初期化
    InitializeDXC();
}

/** @brief DirectX12基盤の終了処理 */
void DirectXCommon::Finalize()
{
    // GPU の処理がすべて終わるまで待つ
    WaitForGpu();

#ifdef _DEBUG
    if (infoQueue_ && diagnosticsCallbackCookie_ != 0)
    {
        infoQueue_->UnregisterMessageCallback(diagnosticsCallbackCookie_);
        diagnosticsCallbackCookie_ = 0;
    }
#endif

    // スワップチェーンのフルスクリーン解除（解放前に必須）
    BOOL fullscreen = FALSE;

    if (swapChain_)
    {
        swapChain_->GetFullscreenState(&fullscreen, nullptr);

        if (fullscreen)
        {
            swapChain_->SetFullscreenState(FALSE, nullptr);
        }
    }

    // イベントハンドルを閉じる
    if (fenceEvent_)
    {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

/** @brief 描画前処理 */
void DirectXCommon::PreDraw()
{
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // このバックバッファ用のアロケータをGPUが使い終えるまで待つ（前回このインデックスを使った時のフェンス値まで）
    // 毎フレーム無条件に全待ちする代わりに、実際に必要な時だけ止めることでCPU/GPUを並行して動かす
    if (frameFenceValues_[backBufferIndex] != 0
        && fence_->GetCompletedValue() < frameFenceValues_[backBufferIndex])
    {
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
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetBackBufferDsvHandle();
    commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], false, &dsvHandle);

    // 縦横比が異なる画面のレターボックス部分は黒でクリアする。
    static constexpr float kClearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex], kClearColor, 0, nullptr);

    // 深度クリア
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList_->ClearDepthStencilView(GetDsvHandle(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // ビューポートとシザー矩形の設定
    // 縦横比を保ち、バックバッファに収まる最大サイズで描画する。
    D3D12_VIEWPORT viewport = GetCenteredClientViewport();
    commandList_->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = GetCenteredClientScissorRect();
    commandList_->RSSetScissorRects(1, &scissorRect);
}

/** @brief 描画後処理 */
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
    if (FAILED(hr))
    {
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

    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
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

/** @brief シェーダーをコンパイルする（同じファイルとプロファイルの結果はキャッシュから返す） */
Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(const std::wstring& filePath, const wchar_t* profile)
{
    // 同じファイル・同じプロファイルを一度コンパイルしていれば、その結果を使い回す
    const std::wstring cacheKey = filePath + L"|" + profile;
    {
        std::scoped_lock lock(shaderCacheMutex_);
        const auto cached = shaderCache_.find(cacheKey);
        if (cached != shaderCache_.end())
        {
            return cached->second;
        }
    }

    // pakにコンパイル済みのバイトコードがあれば、DXCでのコンパイルを省く
    const std::string packKey = "shader:" + StringUtility::ConvertString(cacheKey);
    std::vector<uint8_t> packed;
    if (AssetPack::GetInstance()->Read(packKey, packed))
    {
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> packedBlob;
        if (SUCCEEDED(dxcUtils_->CreateBlob(packed.data(), static_cast<UINT32>(packed.size()), DXC_CP_ACP, &packedBlob)))
        {
            Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
            packedBlob.As(&shaderBlob);
            std::scoped_lock lock(shaderCacheMutex_);
            shaderCache_[cacheKey] = shaderBlob;
            return shaderBlob;
        }
    }

    // hlslファイルを読む
    Logger::Log(StringUtility::ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile)));

    IDxcBlobEncoding* shaderSource = nullptr;
    HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    ENGINE_ASSERT(SUCCEEDED(hr));

    // 読んだソースを UTF-8 としてコンパイラへ渡す
    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    // Compileする
    // 入口は main、デバッグ情報を埋め込み、最適化なし・行優先の行列で出す
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

    if (shaderError != nullptr && shaderError->GetStringLength() != 0)
    {
        Logger::LogError(shaderError->GetStringPointer());
        ENGINE_ASSERT(false);
    }

    // 結果を受け取る
    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    ENGINE_ASSERT(SUCCEEDED(hr));

    Logger::Log(StringUtility::ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile)));

    // コンパイルに使った一時オブジェクトを解放する
    shaderSource->Release();
    shaderResult->Release();

    // pak を作っている最中なら、次回からコンパイルを省けるようバイトコードを記録する
    if (AssetPack::GetInstance()->IsCooking())
    {
        const auto* begin = static_cast<const uint8_t*>(shaderBlob->GetBufferPointer());
        AssetPack::GetInstance()->Record(packKey, std::vector<uint8_t>(begin, begin + shaderBlob->GetBufferSize()));
    }

    // 次に同じシェーダーを頼まれた時のためにキャッシュへ入れる
    {
        std::scoped_lock lock(shaderCacheMutex_);
        shaderCache_[cacheKey] = shaderBlob;
    }

    return shaderBlob;
}


// ══════════════════════════════════════════════════════
// リサイズと同期
// ══════════════════════════════════════════════════════

/** @brief ウィンドウサイズの変更に合わせてスワップチェーンのバックバッファを作り直す（最小化中は何もしない） */
void DirectXCommon::OnResize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || !swapChain_)
    {
        return;
    } // 最小化中などは無視

    DXGI_SWAP_CHAIN_DESC1 desc = { };
    swapChain_->GetDesc1(&desc);
    if (desc.Width == width && desc.Height == height)
    {
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
    CreateBackBufferDepthBuffer();
}

/** @brief 内部解像度(WinApp::kClientWidth/Height)の絵を実際のバックバッファへ */
D3D12_VIEWPORT DirectXCommon::GetBackBufferViewport() const
{
    uint32_t backBufferWidth = winApp_->kClientWidth;
    uint32_t backBufferHeight = winApp_->kClientHeight;
    if (swapChain_)
    {
        DXGI_SWAP_CHAIN_DESC1 desc = { };
        if (SUCCEEDED(swapChain_->GetDesc1(&desc)) && desc.Width > 0 && desc.Height > 0)
        {
            backBufferWidth = desc.Width;
            backBufferHeight = desc.Height;
        }
    }

    // 内部解像度のアスペクト比を保ったまま、バックバッファ内に収まる最大サイズへ拡大する
    const float clientAspect = static_cast<float>(winApp_->kClientWidth) / static_cast<float>(winApp_->kClientHeight);
    const float backAspect = static_cast<float>(backBufferWidth) / static_cast<float>(backBufferHeight);

    float width = static_cast<float>(backBufferWidth);
    float height = static_cast<float>(backBufferHeight);
    if (backAspect > clientAspect)
    {
        // バックバッファの方が横長 → 左右に余白（ピラーボックス）
        width = height * clientAspect;
    }
    else if (backAspect < clientAspect)
    {
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

/** @brief GetBackBufferViewport() に対応するシザー矩形を取得する */
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

/** @brief 縦横比を保って画面内の最大サイズへ拡大し、中央に配置するビューポートを取得する。 */
D3D12_VIEWPORT DirectXCommon::GetCenteredClientViewport() const
{
    return GetBackBufferViewport();
}

/** @brief GetCenteredClientViewport() に対応するシザー矩形を取得する */
D3D12_RECT DirectXCommon::GetCenteredClientScissorRect() const
{
    return GetBackBufferScissorRect();
}

/** @brief バックバッファの RTV と深度を描画先にし、中央寄せのビューポートとシザーを設定する */
void DirectXCommon::SetBackBufferRenderTarget()
{
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetCurrentBackBufferHandle();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetBackBufferDsvHandle();
    commandList_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const D3D12_VIEWPORT viewport = GetCenteredClientViewport();
    const D3D12_RECT scissor = GetCenteredClientScissorRect();
    commandList_->RSSetViewports(1, &viewport);
    commandList_->RSSetScissorRects(1, &scissor);
}


/** @brief 現在のバックバッファのRTVハンドルを取得 */
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCurrentBackBufferHandle()
{
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    return rtvHandles_[backBufferIndex];
}

/** @brief 現在のバックバッファリソースを取得 */
ID3D12Resource* DirectXCommon::GetCurrentBackBufferResource()
{
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    return swapChainResources_[backBufferIndex].Get();
}

// ヘルパー関数（バッファ作成用）
// ══════════════════════════════════════════════════════
// 共通リソース操作
// ══════════════════════════════════════════════════════

/** @brief 指定サイズのアップロード用バッファ（定数バッファ等）を生成する */
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes)
{
    return graphics::PipelineHelper::CreateUploadBuffer(device_.Get(), sizeInBytes);
}

/** @brief 任意のコマンドキュー・フェンスに対してフェンス値を積み、GPU完了まで待機する */
void DirectXCommon::WaitForFence(
    ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64& fenceValue, HANDLE fenceEvent)
{
    ++fenceValue;
    queue->Signal(fence, fenceValue);

    if (fence->GetCompletedValue() < fenceValue)
    {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

/** @brief コマンドキューにフェンス値を積み、GPUがそこまでの処理を完了するまでCPU側で待機する */
void DirectXCommon::WaitForGpu()
{
    WaitForFence(commandQueue_.Get(), fence_.Get(), fenceValue_, fenceEvent_);
}

/** @brief リソースの状態遷移バリア（TYPE_TRANSITION）を構築するだけで発行はしない */
D3D12_RESOURCE_BARRIER DirectXCommon::MakeTransitionBarrier(
    ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource)
{
    ENGINE_ASSERT(resource);

    // 全サブリソース遷移だけを追跡する
    // 個別Mipの状態は一つの値で表せないため呼び出し側の指定をそのまま使う
    if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        D3D12_RESOURCE_STATES trackedState;
        if (ReadTrackedState(resource, trackedState) && trackedState != before)
        {
            Logger::LogWarning(std::format(
                "Resource state mismatch. requested_before=0x{:X} tracked_before=0x{:X} after=0x{:X}",
                static_cast<unsigned>(before), static_cast<unsigned>(trackedState), static_cast<unsigned>(after)));

            // 既に目的状態なら同一状態バリアを避けるため呼び出し側の値を残す
            // それ以外は追跡済みの実状態から遷移する
            if (trackedState != after)
            {
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

/** @brief リソースの状態遷移バリアを構築し、その場で1つだけ発行する */
void DirectXCommon::TransitionBarrier(
    ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource)
{
    D3D12_RESOURCE_BARRIER barrier = MakeTransitionBarrier(resource, before, after, subresource);
    commandList->ResourceBarrier(1, &barrier);
}

/** @brief 単機能クラス専用のデスクリプタヒープ（RTV/DSV等）を生成する */
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

} // namespace engine
