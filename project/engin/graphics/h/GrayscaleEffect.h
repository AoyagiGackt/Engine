#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <wrl/client.h>

class GrayscaleEffect {
public:
    static GrayscaleEffect* GetInstance()
    {
        static GrayscaleEffect instance;
        return &instance;
    }

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize();

    // Call before/after scene draw to redirect rendering into the off-screen texture
    void BeginScene();
    void EndScene();

    // Fullscreen grayscale pass onto the backbuffer; call after EndScene()
    void Apply(SrvManager* srvManager);

    void  SetAmount(float amount);
    float GetAmount() const;
    void  SetEnabled(bool enabled) { enabled_ = enabled; }
    bool  IsEnabled() const { return enabled_; }

private:
    GrayscaleEffect() = default;
    ~GrayscaleEffect() = default;
    GrayscaleEffect(const GrayscaleEffect&) = delete;
    GrayscaleEffect& operator=(const GrayscaleEffect&) = delete;

    DirectXCommon* dxCommon_ = nullptr;

    // Off-screen render target (TYPELESS so we can use SRGB for both RTV and SRV)
    Microsoft::WRL::ComPtr<ID3D12Resource>       sceneTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  rtvHandle_ = {};
    uint32_t                                     srvIndex_  = UINT32_MAX;
    bool                                         isFirstFrame_ = true;

    // Fullscreen grayscale PSO
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    // Constant buffer
    struct GrayscaleParams {
        float amount = 0.f;
        float pad[3] = {};
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> cbResource_;
    GrayscaleParams*                       cbData_   = nullptr;

    bool enabled_ = false;
};
