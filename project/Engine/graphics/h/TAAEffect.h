#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "SrvManager.h"
#include <wrl/client.h>

class TAAEffect {
public:
    static TAAEffect* GetInstance();
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize();

    Vector2 BeginFrame();
    void Apply(DirectXCommon* dxCommon, uint32_t currentSrvIndex);

    uint32_t GetHistorySrvIndex() const { return historySrvIndex_; }
    bool IsEnabled()  const { return enabled_; }
    void SetEnabled(bool e) { enabled_ = e; }
    void Reset()            { frameIdx_ = 0; }

private:
    TAAEffect() = default;
    ~TAAEffect() = default;
    TAAEffect(const TAAEffect&) = delete;
    TAAEffect& operator=(const TAAEffect&) = delete;

    void Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                 D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const;

    // history render target
    Microsoft::WRL::ComPtr<ID3D12Resource>       historyRT_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> historyRtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  historyRtvHandle_ = {};
    uint32_t                                     historySrvIndex_  = UINT32_MAX;
    bool                                         historyFirstFrame_ = true;

    struct CBLayout {
        float jitterX, jitterY;
        float blendAlpha;
        float _pad;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> cb_;
    CBLayout*                              cbData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rs_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;

    SrvManager*    srvManager_ = nullptr;
    DirectXCommon* dxCommon_   = nullptr;

    uint32_t frameIdx_ = 0;
    bool     enabled_  = true;
};
