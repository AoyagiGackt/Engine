#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <wrl/client.h>

class HsvFilter {
public:
    static HsvFilter* GetInstance()
    {
        static HsvFilter instance;
        return &instance;
    }

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize();

    void BeginScene();
    void EndScene();
    void Apply(SrvManager* srvManager);

    void  SetEnabled(bool v) { enabled_ = v; }
    bool  IsEnabled()  const { return enabled_; }

    void  SetHueShift(float degrees);
    float GetHueShift()   const;
    void  SetSaturation(float s);
    float GetSaturation() const;
    void  SetValue(float v);
    float GetValue()      const;

    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRTVHandle() const { return rtvHandle_; }

private:
    HsvFilter() = default;
    ~HsvFilter() = default;
    HsvFilter(const HsvFilter&) = delete;
    HsvFilter& operator=(const HsvFilter&) = delete;

    DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource>       sceneTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  rtvHandle_ = {};
    uint32_t                                     srvIndex_  = UINT32_MAX;
    bool                                         isFirstFrame_ = true;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    struct HsvFilterParams {
        float hueShift   =   0.0f; // -180 〜 +180 度
        float saturation =   1.0f; // 0=グレー, 1=そのまま
        float value      =   1.0f; // 0=黒, 1=そのまま
        float pad        =   0.0f;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> cbResource_;
    HsvFilterParams*                       cbData_ = nullptr;

    bool enabled_ = false;
};
