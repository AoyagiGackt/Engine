#include "DebugDrawGpu.h"
#include "GameConstants.h"
#include "WinApp.h"
#include "EngineAssert.h"
#include <cmath>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

DebugDrawGpu* DebugDrawGpu::GetInstance()
{
    static DebugDrawGpu inst;
    return &inst;
}

void DebugDrawGpu::Initialize(DirectXCommon* dxCommon)
{
    if (initialized_) { return; }
    dxCommon_    = dxCommon;
    initialized_ = true;

    ID3D12Device* device = dxCommon->GetDevice();

    vertexBuf_ = dxCommon->CreateBufferResource(sizeof(Vertex) * kMaxVerts);
    vertexBuf_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

    vbView_.BufferLocation = vertexBuf_->GetGPUVirtualAddress();
    vbView_.SizeInBytes    = sizeof(Vertex) * kMaxVerts;
    vbView_.StrideInBytes  = sizeof(Vertex);

    vpCBuf_ = dxCommon->CreateBufferResource((sizeof(Matrix4x4) + 255) & ~255u);
    vpCBuf_->Map(0, nullptr, reinterpret_cast<void**>(&vpData_));

    // root signature: slot 0 = CBV(b0, VS)
    {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        param.Descriptor.ShaderRegister = 0;
        param.ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 1;
        rsDesc.pParameters   = &param;
        rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> sigBlob, errBlob;
        HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
        ENGINE_ASSERT(SUCCEEDED(hr));
        hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rs_));
        ENGINE_ASSERT(SUCCEEDED(hr));
    }

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon->CompileShader(L"Resources/shaders/debug/DebugDrawVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon->CompileShader(L"Resources/shaders/debug/DebugDrawPS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(float)*3, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_BLEND_DESC blend = {};
    auto& rt = blend.RenderTarget[0];
    rt.BlendEnable           = TRUE;
    rt.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp               = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha         = D3D12_BLEND_ONE;
    rt.DestBlendAlpha        = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rast = {};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable    = FALSE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = rs_.Get();
    psoDesc.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState            = blend;
    psoDesc.RasterizerState       = rast;
    psoDesc.DepthStencilState     = depth;
    psoDesc.InputLayout           = { inputLayout, _countof(inputLayout) };
    psoDesc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = dxCommon->GetBackBufferFormat();
    psoDesc.SampleDesc.Count      = 1;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    pending_.reserve(4096);
}

void DebugDrawGpu::Line(const Vector3& a, const Vector3& b, const Vector4& c)
{
    if (!enabled_) { return; }
    pending_.push_back({ a.x, a.y, a.z, c.x, c.y, c.z, c.w });
    pending_.push_back({ b.x, b.y, b.z, c.x, c.y, c.z, c.w });
}

void DebugDrawGpu::Box(const Vector3& center, const Vector3& half, const Vector4& color)
{
    if (!enabled_) { return; }
    float x0 = center.x - half.x, x1 = center.x + half.x;
    float y0 = center.y - half.y, y1 = center.y + half.y;
    float z0 = center.z - half.z, z1 = center.z + half.z;

    Vector3 v[8] = {
        {x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
        {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}
    };
    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    for (auto& e : edges) { Line(v[e[0]], v[e[1]], color); }
}

void DebugDrawGpu::Sphere(const Vector3& center, float radius, const Vector4& color, int segments)
{
    if (!enabled_) { return; }
    const float step = GameConstants::kTwoPi / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float a0 = step * i, a1 = step * (i + 1);
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);
        Line({ center.x + radius*c0, center.y + radius*s0, center.z },
             { center.x + radius*c1, center.y + radius*s1, center.z }, color);
        Line({ center.x + radius*c0, center.y,             center.z + radius*s0 },
             { center.x + radius*c1, center.y,             center.z + radius*s1 }, color);
        Line({ center.x,             center.y + radius*c0, center.z + radius*s0 },
             { center.x,             center.y + radius*c1, center.z + radius*s1 }, color);
    }
}

void DebugDrawGpu::Flush(ID3D12GraphicsCommandList* cmd, const Matrix4x4& viewProjection)
{
    if (!enabled_ || pending_.empty() || !initialized_) {
        pending_.clear();
        return;
    }

    int count = static_cast<int>(pending_.size());
    if (count > kMaxVerts) { count = kMaxVerts; }

    std::memcpy(vertexData_, pending_.data(), sizeof(Vertex) * count);
    *vpData_ = viewProjection;

    const UINT W = WinApp::kClientWidth;
    const UINT H = WinApp::kClientHeight;
    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)W, (float)H, 0.f, 1.f };
    D3D12_RECT     sc = { 0, 0, (LONG)W, (LONG)H };

    cmd->SetGraphicsRootSignature(rs_.Get());
    cmd->SetPipelineState(pso_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    cmd->IASetVertexBuffers(0, 1, &vbView_);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
    cmd->SetGraphicsRootConstantBufferView(0, vpCBuf_->GetGPUVirtualAddress());
    cmd->DrawInstanced(count, 1, 0, 0);

    pending_.clear();
}
