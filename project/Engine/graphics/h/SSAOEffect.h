#pragma once
#include "Camera.h"
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <wrl/client.h>

// Screen Space Ambient Occlusion
//
// 使い方（毎フレーム）:
//   auto* ssao = SSAOEffect::GetInstance();
//   // 1. ノーマルキャプチャパス（メイン描画の前）
//   ssao->BeginNormalCapture(dxCommon, camera);
//   for (auto& obj : objects) { obj->DrawForNormalCapture(); }
//   ssao->EndNormalCapture(dxCommon);
//   // 2. AO 計算
//   ssao->Compute(dxCommon, camera);
//   ssao->Blur(dxCommon);
//   // 3. メイン描画（通常の Draw()）
//   // 4. AO 乗算適用
//   ssao->Apply(dxCommon, srvManager);
class SSAOEffect {
public:
    static SSAOEffect* GetInstance() {
        static SSAOEffect inst;
        return &inst;
    }

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize();

    void BeginNormalCapture(DirectXCommon* dxCommon, Camera* camera);
    void EndNormalCapture(DirectXCommon* dxCommon);
    void Compute(DirectXCommon* dxCommon, Camera* camera);
    void Blur(DirectXCommon* dxCommon);
    void Apply(DirectXCommon* dxCommon, SrvManager* srvManager);

    bool  IsEnabled()     const { return enabled_; }
    void  SetEnabled(bool e)    { enabled_ = e; }
    void  SetRadius(float r)    { if (ssaoCbData_) ssaoCbData_->radius   = r; }
    void  SetStrength(float s)  { if (ssaoCbData_) ssaoCbData_->strength = s; }

    // NormalCapture 中に Object3d から呼ぶ: per-object transform の slot 0 を設定
    // Object3d::DrawForNormalCapture() の中で使用
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

    DirectXCommon* dxCommon_    = nullptr;
    SrvManager*    srvManager_  = nullptr;
    bool           enabled_     = true;
    bool           normalInRTV_ = false;
};
