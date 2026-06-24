#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <wrl/client.h>

// HDR レンダリング + ACES トーンマッピング
//
// 使い方（毎フレーム）:
//   auto* hdr = HDREffect::GetInstance();
//   hdr->BeginScene();            // HDR RT に切り替え
//   /* 3D/ポストプロセス描画 */
//   hdr->EndScene();
//   hdr->Apply();                 // トーンマッピング → バックバッファ
//   /* UI (スプライト) 描画 */
class HDREffect {
public:
    static HDREffect* GetInstance() {
        static HDREffect inst;
        return &inst;
    }

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    void BeginScene();  // 描画先を HDR RT に切り替え
    void EndScene();    // HDR RT → SRV へバリア遷移
    void Apply();       // トーンマッピングしてバックバッファへ書き出す

    bool  IsEnabled()          const { return enabled_; }
    void  SetEnabled(bool e)         { enabled_ = e; }
    void  SetExposure(float e)       { if (cbData_) cbData_->exposure = e; }
    void  SetGamma(float g)          { if (cbData_) cbData_->gamma    = g; }

    uint32_t GetHDRSrvIndex() const { return hdrSrvIndex_; }

    D3D12_CPU_DESCRIPTOR_HANDLE GetHDRRtvHandle()  const { return hdrRtvHandle_; }
    ID3D12Resource*             GetHDRResource()   const { return hdrTex_.Get(); }

private:
    HDREffect()  = default;
    ~HDREffect() = default;
    HDREffect(const HDREffect&)            = delete;
    HDREffect& operator=(const HDREffect&) = delete;

    void Barrier(ID3D12Resource* res,
                 D3D12_RESOURCE_STATES before,
                 D3D12_RESOURCE_STATES after);

    struct TonemapParams {
        float exposure = 1.0f;
        float gamma    = 2.2f;
        float pad[2]   = {};
    };

    DirectXCommon* dxCommon_   = nullptr;
    SrvManager*    srvManager_ = nullptr;
    bool           enabled_    = true;
    bool           inHDR_      = false;

    // HDR レンダーターゲット (R16G16B16A16_FLOAT)
    Microsoft::WRL::ComPtr<ID3D12Resource>       hdrTex_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> hdrRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  hdrRtvHandle_ = {};
    uint32_t                                     hdrSrvIndex_  = UINT32_MAX;

    // トーンマップ PSO
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  rs_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  pso_;

    // 定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> cbRes_;
    TonemapParams*                         cbData_ = nullptr;
};
