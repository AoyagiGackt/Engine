#include "MotionBlurEffect.h"
#include "WinApp.h"
#include <cassert>

using namespace Microsoft::WRL;

MotionBlurEffect* MotionBlurEffect::GetInstance()
{
    static MotionBlurEffect inst;
    return &inst;
}

void MotionBlurEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    srvManager_ = srvManager;

    ID3D12Device* device = dxCommon->GetDevice();

    cb_ = dxCommon->CreateBufferResource((sizeof(CBLayout) + 255) & ~255u);
    cb_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    cbData_->strength   = 1.0f;
    cbData_->numSamples = 8;

    // root signature: slot 0 = CBV(b0,PS), slot 1 = SRV table(t0,PS), slot 2 = SRV table(t1,PS)
    {
        D3D12_DESCRIPTOR_RANGE srvRange0 = {};
        srvRange0.RangeType                            = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange0.NumDescriptors                       = 1;
        srvRange0.BaseShaderRegister                   = 0;
        srvRange0.OffsetInDescriptorsFromTableStart    = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE srvRange1 = {};
        srvRange1.RangeType                            = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange1.NumDescriptors                       = 1;
        srvRange1.BaseShaderRegister                   = 1;
        srvRange1.OffsetInDescriptorsFromTableStart    = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType                            = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.pDescriptorRanges        = &srvRange0;
        params[1].DescriptorTable.NumDescriptorRanges      = 1;
        params[1].ShaderVisibility                         = D3D12_SHADER_VISIBILITY_PIXEL;
        params[2].ParameterType                            = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.pDescriptorRanges        = &srvRange1;
        params[2].DescriptorTable.NumDescriptorRanges      = 1;
        params[2].ShaderVisibility                         = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
        sampler.MaxLOD           = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister   = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters     = 3;
        rsDesc.pParameters       = params;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers   = &sampler;
        rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sigBlob, errBlob;
        HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
        assert(SUCCEEDED(hr));
        hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rs_));
        assert(SUCCEEDED(hr));
    }

    IDxcBlob* vsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    IDxcBlob* psBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/MotionBlurPS.hlsl", L"ps_6_0");

    D3D12_BLEND_DESC blend = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rast = {};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = rs_.Get();
    psoDesc.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState            = blend;
    psoDesc.RasterizerState       = rast;
    psoDesc.DepthStencilState     = depth;
    psoDesc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = dxCommon->GetBackBufferFormat();
    psoDesc.SampleDesc.Count      = 1;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
    assert(SUCCEEDED(hr));
}

void MotionBlurEffect::BeginFrame(const Matrix4x4& viewProjection)
{
    if (!cbData_) { return; }
    cbData_->prevViewProj = prevVP_;
    cbData_->invViewProj  = Inverse(viewProjection);
    prevVP_               = viewProjection;
}

void MotionBlurEffect::Apply(DirectXCommon* dxCommon, uint32_t colorSrvIndex, uint32_t depthSrvIndex)
{
    if (!enabled_) { return; }

    auto*  cmd = dxCommon->GetCommandList();
    const UINT W = WinApp::kClientWidth;
    const UINT H = WinApp::kClientHeight;

    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)W, (float)H, 0.f, 1.f };
    D3D12_RECT     sc = { 0, 0, (LONG)W, (LONG)H };

    D3D12_CPU_DESCRIPTOR_HANDLE backBuffer = dxCommon->GetCurrentBackBufferHandle();
    cmd->OMSetRenderTargets(1, &backBuffer, FALSE, nullptr);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    cmd->SetGraphicsRootSignature(rs_.Get());
    cmd->SetPipelineState(pso_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(0, cb_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(colorSrvIndex));
    cmd->SetGraphicsRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(depthSrvIndex));
    cmd->DrawInstanced(3, 1, 0, 0);
}
