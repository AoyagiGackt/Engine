/**
 * @file SSAOEffect.h
 * @brief Screen Space Ambient Occlusion（スクリーン空間アンビエントオクルージョン）エフェクトを管理するクラス
 */
#pragma once
#include "Camera.h"
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief SSAO（スクリーン空間 AO）を実装するシングルトンクラス
 * @note ノーマルキャプチャ → AO計算 → ブラー → 適用 の4ステップで構成される
 *       使い方（毎フレーム）:
 *       1. BeginNormalCapture() / DrawForNormalCapture() / EndNormalCapture()
 *       2. Compute() → Blur()
 *       3. 通常の Draw()
 *       4. Apply()
 */
class SSAOEffect {
public:
    static SSAOEffect* GetInstance() {
        static SSAOEffect inst;
        return &inst;
    }

    /**
     * @brief 初期化レンダーターゲット・PSO・定数バッファを作成する
     * @param dxCommon   DirectX共通基盤
     * @param srvManager SRVディスクリプタヒープ管理
     */
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize();

    /** @brief ノーマルキャプチャパスを開始するメイン描画の前に呼ぶ */
    void BeginNormalCapture(engine::DirectXCommon* dxCommon, Camera* camera);
    /** @brief ノーマルキャプチャパスを終了する */
    void EndNormalCapture(engine::DirectXCommon* dxCommon);
    /** @brief AO値を計算するコンピュートパスを実行する */
    void Compute(engine::DirectXCommon* dxCommon, Camera* camera);
    /** @brief AO テクスチャにブラーをかける */
    void Blur(engine::DirectXCommon* dxCommon);
    /** @brief AO を乗算してシーンに適用するメイン描画の後に呼ぶ */
    void Apply(engine::DirectXCommon* dxCommon, SrvManager* srvManager);

    bool  IsEnabled()     const { return enabled_; }
    void  SetEnabled(bool e)    { enabled_ = e; }
    void  SetRadius(float r)    { if (ssaoCbData_) { ssaoCbData_->radius   = r; } }
    void  SetStrength(float s)  { if (ssaoCbData_) { ssaoCbData_->strength = s; } }

    /** @brief ノーマルキャプチャ中に Object3d から呼ぶper-object トランスフォームを slot 0 に設定する */
    void SetObjectTransform(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS transformAddr) const;

private:
    SSAOEffect()  = default;
    ~SSAOEffect() = default;
    SSAOEffect(const SSAOEffect&) = delete;
    SSAOEffect& operator=(const SSAOEffect&) = delete;

    void CreateRT(ID3D12Device* device, SrvManager* srvManager,
        UINT w, UINT h, DXGI_FORMAT fmt,
        Microsoft::WRL::ComPtr<ID3D12Resource>& res,
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& rtvHeap,
        D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
        uint32_t& srvIndex,
        const float clearColor[4]);

    void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const;

    // --- Normal RT (RGBA16F) ---
    Microsoft::WRL::ComPtr<ID3D12Resource>       normalTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> normalRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  normalRtvHandle_ = {};
    uint32_t                                     normalSrvIndex_  = UINT32_MAX;

    // --- SSAO RT (R8_UNORM) ---
    Microsoft::WRL::ComPtr<ID3D12Resource>       ssaoTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ssaoRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  ssaoRtvHandle_ = {};
    uint32_t                                     ssaoSrvIndex_  = UINT32_MAX;

    // --- Blur RT (R8_UNORM) ---
    Microsoft::WRL::ComPtr<ID3D12Resource>       blurTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> blurRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  blurRtvHandle_ = {};
    uint32_t                                     blurSrvIndex_  = UINT32_MAX;

    // --- 専用 DSV ---
    Microsoft::WRL::ComPtr<ID3D12Resource>       depthTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  dsvHandle_ = {};

    // --- PSO / RS ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  normalRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  normalPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  ssaoRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  ssaoPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  blurRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  blurPSO_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  applyRS_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  applyPSO_;

    // --- 定数バッファ ---
    struct NormalCaptureCB { Matrix4x4 view; };
    Microsoft::WRL::ComPtr<ID3D12Resource> normalCb_;
    NormalCaptureCB* normalCbData_ = nullptr;

    struct SSAOParams {
        Matrix4x4 projection;
        Matrix4x4 projectionInverse;
        float radius    = 0.5f;
        float strength  = 1.5f;
        float bias      = 0.025f;
        float texW      = 0.0f;
        float texH      = 0.0f;
        int   numSamples = 16;
        float pad[2];
        Vector4 kernel[16];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> ssaoCb_;
    SSAOParams* ssaoCbData_ = nullptr;

    struct BlurParams { float texW; float texH; float pad[2]; };
    Microsoft::WRL::ComPtr<ID3D12Resource> blurCb_;
    BlurParams* blurCbData_ = nullptr;

    engine::DirectXCommon* dxCommon_    = nullptr;
    SrvManager*    srvManager_  = nullptr;
    bool           enabled_     = true;
    bool           normalInRTV_ = false;
};

} // namespace engine::graphics
