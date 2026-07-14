/**
 * @file DirectXCommon.h
 * @brief DirectX12のデバイス生成、コマンド管理、スワップチェーンなどの基盤機能を管理するファイル
 */
#pragma once
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <thread>
#include <wrl/client.h>
namespace engine {
class WinApp;

/**
 * @brief DirectX12の共通基盤クラス
 * @note アプリケーション全体で共有されるDirectX12の基本オブジェクト（Device, CommandList等）を保持します
 * 描画の開始(PreDraw)と終了(PostDraw)の管理、およびFPSの固定化機能も備えています
 */
class DirectXCommon {
public: // メンバ関数
    /**
     * @brief DirectX12基盤の初期化
     * @param winApp 管理対象となるWinApp（ウィンドウ管理）のポインタ
     */
    void Initialize(WinApp* winApp);

    /**
     * @brief DirectX12基盤の終了処理
     * @note GPU完了を待ち、スワップチェーンのフルスクリーン解除など安全な順序で解放する
     */
    void Finalize();

    /**
     * @brief 描画前処理
     * @note コマンドアロケータとリストのリセット、バックバッファの状態遷移、画面クリアなどを行います
     */
    void PreDraw();

    /**
     * @brief 描画後処理
     * @note バックバッファをPresentation状態に戻し、コマンドの実行、画面の入れ替え（Flip）、FPS固定の待ちを行います
     */
    void PostDraw();

    /**
     * @brief ウィンドウサイズ変更に合わせてスワップチェーンのバックバッファを作り直す
     * @param width  新しいクライアント領域の幅
     * @param height 新しいクライアント領域の高さ
     * @note ゲーム内部の描画解像度（ビューポート・UI座標等）は WinApp::kClientWidth/Height の
     * 固定値のまま変えない。ウィンドウが大きければ余白は黒のまま、小さければ描画がクリップされる。
     * あくまで「リサイズしてもクラッシュ・表示崩壊しない」ことを保証するための対応
     */
    void OnResize(uint32_t width, uint32_t height);

    /** @brief VSyncの有効/無効を設定する */
    void SetVSyncEnabled(bool enabled) { vsyncEnabled_ = enabled; }
    /** @brief VSyncが有効かどうかを返す */
    bool IsVSyncEnabled() const { return vsyncEnabled_; }
    /** @brief VSyncの有効/無効を切り替える */
    void ToggleVSync() { vsyncEnabled_ = !vsyncEnabled_; }

    /**
     * @brief シェーダーファイルをコンパイルする
     * @param filePath シェーダーファイルのパス
     * @param profile コンパイルプロファイル（例: L"vs_6_0", L"ps_6_0"）
     * @return コンパイルされたシェーダーデータ
     */
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

    /** @brief デバイスの取得 */
    ID3D12Device* GetDevice() { return device_.Get(); }

    /** @brief グラフィックスコマンドリストの取得 */
    ID3D12GraphicsCommandList* GetCommandList() { return commandList_.Get(); }

    /** @brief コマンドキューの取得 */
    ID3D12CommandQueue* GetCommandQueue() { return commandQueue_.Get(); }

    /** @brief スワップチェーンの取得 */
    IDXGISwapChain4* GetSwapChain() { return swapChain_.Get(); }

    /** @brief 現在のバックバッファ用コマンドアロケータの取得 */
    ID3D12CommandAllocator* GetCommandAllocator() { return commandAllocators_[swapChain_->GetCurrentBackBufferIndex()].Get(); }

    /** @brief SRV用デスクリプタヒープの取得 */
    ID3D12DescriptorHeap* GetSrvDescriptorHeap() { return srvDescriptorHeap_.Get(); }

    /**
     * @brief バッファリソース（定数バッファ等）を生成する
     * @param sizeInBytes 生成するバッファのサイズ
     * @return Microsoft::WRL::ComPtr<ID3D12Resource> 生成されたリソース
     */
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

    /**
     * @brief 単機能クラス専用のデスクリプタヒープ（RTV/DSV等）を生成する
     * @param device         生成に使用するデバイス
     * @param type           ヒープの種類（D3D12_DESCRIPTOR_HEAP_TYPE_RTV等）
     * @param numDescriptors 確保するディスクリプタ数
     * @param shaderVisible  シェーダーから参照する必要があるか（RTV/DSVは通常false）
     * @return Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> 生成されたヒープ
     * @note staticなので、DirectXCommonのインスタンスを持たずID3D12Device*しか無い箇所からも呼べる
     */
    static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
        ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible = false);

    /**
     * @brief コマンドキューにフェンス値を積み、GPUがそこまでの処理を完了するまでCPU側で待機する
     * @note このDirectXCommonが持つグラフィックスキュー・フェンスに対して待機する
     */
    void WaitForGpu();

    /**
     * @brief 任意のコマンドキュー・フェンスに対してフェンス値を積み、GPU完了まで待機する
     * @param queue      待機対象のコマンドキュー
     * @param fence      待機対象のフェンス
     * @param fenceValue 直近で使ったフェンス値（呼び出しごとにインクリメントされる）
     * @param fenceEvent 完了通知に使うイベントハンドル
     * @note グラフィックスキュー以外（TextureManagerのコピーキュー等）を待つ場合に使う
     */
    static void WaitForFence(
        ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64& fenceValue, HANDLE fenceEvent);

    /**
     * @brief リソースの状態遷移バリア（TYPE_TRANSITION）を構築するだけで発行はしない
     * @note 複数バリアをまとめて1回のResourceBarrier()で発行したい場合はこちらを使う
     */
    static D3D12_RESOURCE_BARRIER MakeTransitionBarrier(
        ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    /**
     * @brief リソースの状態遷移バリアを構築し、その場で1つだけ発行する
     * @param commandList 発行先のコマンドリスト
     * @param resource    遷移させるリソース
     * @param before      遷移前の状態
     * @param after       遷移後の状態
     * @param subresource 対象サブリソース（省略時は全サブリソース）
     */
    static void TransitionBarrier(
        ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    // --- RTV関連 ---

    /** @brief 現在のバックバッファのRTVハンドルを取得 */
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferHandle();

    /** @brief 現在のバックバッファリソースを取得 */
    ID3D12Resource* GetCurrentBackBufferResource();

    /** @brief バックバッファのフォーマットを取得（デフォルトはSRGB） */
    DXGI_FORMAT GetBackBufferFormat() const { return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }

    /** @brief スワップチェーンのバッファ数を取得（ダブルバッファリングなら2） */
    UINT GetBufferCount() const { return kFrameCount; }

    /** @brief DSV（深度バッファ）のハンドルを取得 */
    D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() { return dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart(); }

    /** @brief 深度ステンシルリソースを取得（SRV生成に使用） */
    ID3D12Resource* GetDepthStencilResource() const { return depthStencilResource_.Get(); }

    // --- フェンス関連 ---

    /** @brief フェンスの取得 */
    ID3D12Fence* GetFence() { return fence_.Get(); }

    /** @brief 現在のフェンス値を取得 */
    uint64_t GetFenceValue() const { return fenceValue_; }

    /** @brief フェンスイベントのハンドルを取得 */
    HANDLE GetFenceEvent() { return fenceEvent_; }

    /** @brief フェンス値をインクリメントする */
    void IncrementFenceValue() { fenceValue_++; }

private:
    // 内部初期化関数群

    // デバイスとファクトリの生成
    void InitializeDevice();

    // コマンドキュー・アロケータ・リストの生成
    void CreateCommand();

    // スワップチェーンの生成
    void CreateSwapChain();

    // 各種デスクリプタヒープの生成
    void CreateDescriptorHeaps();

    // レンダーターゲットビューの生成
    void CreateRTV();

    // 深度バッファリソースとDSVの生成
    void CreateDepthBuffer();

    // GPU同期用フェンスの生成
    void CreateFence();

    // DXCシェーダーコンパイラの初期化
    void InitializeDXC();

    // FPS固定関連
    void InitializeFixFPS(); ///< FPS固定用の参照時間初期化
    void UpdateFixFPS(); ///< 1/60秒に満たない場合に待機する

private:
    /** @brief フレームインフライト数（スワップチェーンのバッファ数と一致させる） */
    static constexpr UINT kFrameCount = 2;

    // DirectX12基盤オブジェクト
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    /** @brief バックバッファごとに1つずつ持つコマンドアロケータ
     * @note 1つを使い回すと毎フレームGPU完了を待つ必要が生じるため、
     * バッファ数ぶん用意してCPUとGPUを並行して動かせるようにしている */
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocators_[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

    // デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

    // リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

    // DXC (Shader Compiler)
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;

    // 同期・管理用メンバ
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[2];
    uint64_t fenceValue_;
    HANDLE fenceEvent_;
    /** @brief バックバッファごとに「そのアロケータをGPUが使い終わったフェンス値」を記録する
     * @note PreDraw() で該当インデックスのアロケータを使い回す直前にだけこの値を待つことで、
     * 毎フレーム無条件にWaitForGpu()するのを避けている */
    uint64_t frameFenceValues_[kFrameCount] = {};

    // FPS固定用
    std::chrono::steady_clock::time_point reference_;

    WinApp* winApp_ = nullptr;

    // VSync（ティアリング許可）
    bool vsyncEnabled_    = true;
    bool tearingSupported_ = false;
};

} // namespace engine
