#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <vector>
#include <wrl/client.h>
namespace engine::graphics {

class DebugDrawGpu {
public:
    static DebugDrawGpu* GetInstance();
    void Initialize(engine::DirectXCommon* dxCommon);

    void Line(const Vector3& a, const Vector3& b, const Vector4& color = { 1,1,1,1 });
    void Box(const Vector3& center, const Vector3& halfExtents, const Vector4& color = { 1,1,0,1 });
    void Sphere(const Vector3& center, float radius, const Vector4& color = { 0,1,1,1 }, int segments = 16);

    void Flush(ID3D12GraphicsCommandList* cmd, const Matrix4x4& viewProjection);

    bool IsEnabled() const { return enabled_; }
    void SetEnabled(bool e) { enabled_ = e; }

private:
    DebugDrawGpu() = default;
    ~DebugDrawGpu() = default;
    DebugDrawGpu(const DebugDrawGpu&) = delete;
    DebugDrawGpu& operator=(const DebugDrawGpu&) = delete;

    struct Vertex { float x, y, z; float r, g, b, a; };
    static constexpr int kMaxVerts = 65536;

    Microsoft::WRL::ComPtr<ID3D12Resource>      vertexBuf_;
    Vertex*                                     vertexData_  = nullptr;
    D3D12_VERTEX_BUFFER_VIEW                    vbView_      = {};

    Microsoft::WRL::ComPtr<ID3D12Resource>      vpCBuf_;
    Matrix4x4*                                  vpData_      = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rs_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;

    std::vector<Vertex> pending_;
    bool                enabled_     = true;
    bool                initialized_ = false;

    engine::DirectXCommon* dxCommon_ = nullptr;
};

} // namespace engine::graphics
