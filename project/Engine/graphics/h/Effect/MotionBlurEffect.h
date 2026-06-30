#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "SrvManager.h"
#include <wrl/client.h>
namespace engine::graphics {

class MotionBlurEffect {
public:
    static MotionBlurEffect* GetInstance();
    void Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager);

    void BeginFrame(const Matrix4x4& viewProjection);
    void Apply(engine::DirectXCommon* dxCommon, uint32_t colorSrvIndex, uint32_t depthSrvIndex);

    void SetStrength(float s)   { if (cbData_) cbData_->strength = s; }
    void SetNumSamples(int n)   { if (cbData_) cbData_->numSamples = n; }
    bool IsEnabled()  const     { return enabled_; }
    void SetEnabled(bool e)     { enabled_ = e; }

private:
    MotionBlurEffect() = default;
    ~MotionBlurEffect() = default;
    MotionBlurEffect(const MotionBlurEffect&) = delete;
    MotionBlurEffect& operator=(const MotionBlurEffect&) = delete;

    struct CBLayout {
        Matrix4x4 invViewProj;
        Matrix4x4 prevViewProj;
        float     strength   = 1.0f;
        int       numSamples = 8;
        float     _pad[2];
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> cb_;
    CBLayout*                              cbData_  = nullptr;
    Matrix4x4                              prevVP_  = {};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rs_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;

    SrvManager* srvManager_ = nullptr;
    bool        enabled_    = true;
};

} // namespace engine::graphics
