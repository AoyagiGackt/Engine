#include "GlassShatterEffect.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <cassert>

using namespace Microsoft::WRL;

void GlassShatterEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_   = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon_->GetDevice();

    // ---- 定数バッファ ----
    cbResource_ = dxCommon_->CreateBufferResource(256);
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = ShatterParams{};

    // ---- フリーズテクスチャ（バックバッファと同フォーマット）----
    DXGI_FORMAT bbFormat = dxCommon_->GetCurrentBackBufferResource()->GetDesc().Format;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = WinApp::kClientWidth;
    texDesc.Height           = WinApp::kClientHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = 1;
    texDesc.Format           = bbFormat;
    texDesc.SampleDesc       = { 1, 0 };
    texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(&freezeTexture_));
    assert(SUCCEEDED(hr));

    freezeSrvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVforTexture2D(freezeSrvIndex_, freezeTexture_.Get(), bbFormat, 1);

    // ---- ルートシグネチャ ----
    // スロット 0: CBV b0（シャターパラメータ）
    // スロット 1: Descriptor table SRV t0（フリーズテクスチャ）
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0; // t0
    srvRange.RegisterSpace                     = 0;
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].Descriptor.ShaderRegister = 0; // b0

    rootParams[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges   = &srvRange;

    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    staticSampler.MaxLOD           = D3D12_FLOAT32_MAX;
    staticSampler.ShaderRegister   = 0; // s0
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters     = 2;
    rootSigDesc.pParameters       = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers   = &staticSampler;
    rootSigDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    hr = D3D12SerializeRootSignature(
        &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));

    // ---- シェーダーコンパイル ----
    IDxcBlob* vsBlob = dxCommon_->CompileShader(
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    IDxcBlob* psBlob = dxCommon_->CompileShader(
        L"Resources/shaders/postprocess/GlassShatterPS.hlsl", L"ps_6_0");

    // ---- ブレンドステート: 通常アルファブレンド ----
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend = {};
    rtBlend.BlendEnable           = TRUE;
    rtBlend.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp               = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha         = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha        = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0]  = rtBlend;

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature        = rootSignature_.Get();
    psoDesc.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState            = blendDesc;
    psoDesc.RasterizerState       = rastDesc;
    psoDesc.DepthStencilState     = depthDesc;
    psoDesc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleDesc.Count      = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));
}

void GlassShatterEffect::Finalize()
{
    if (cbData_) {
        cbResource_->Unmap(0, nullptr);
        cbData_ = nullptr;
    }
    cbResource_.Reset();
    freezeTexture_.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();
}

void GlassShatterEffect::Update(float dt)
{
    if (!active_ || finished_)
        return;

    timer_ += dt;
    float t = timer_ / duration_;

    if (t >= 1.0f) {
        t         = 1.0f;
        finished_ = true;
        active_   = false;
    }

    if (cbData_)
        cbData_->time = t;
}

void GlassShatterEffect::CaptureFrame()
{
    if (!freezeTexture_ || !captureNeeded_) return;

    auto* cmd      = dxCommon_->GetCommandList();
    auto* backBuf  = dxCommon_->GetCurrentBackBufferResource();

    // バックバッファ → COPY_SOURCE、フリーズテクスチャ → COPY_DEST
    D3D12_RESOURCE_BARRIER bars[2] = {};
    bars[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bars[0].Transition.pResource   = backBuf;
    bars[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bars[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bars[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    bars[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bars[1].Transition.pResource   = freezeTexture_.Get();
    bars[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    bars[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
    bars[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(2, bars);

    cmd->CopyResource(freezeTexture_.Get(), backBuf);

    // 元の状態に戻す
    bars[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bars[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bars[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    bars[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(2, bars);

    captureNeeded_ = false;
}

void GlassShatterEffect::Apply()
{
    if (!active_ || captureNeeded_) { return; }

    auto* cmd = dxCommon_->GetCommandList();

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetCurrentBackBufferHandle();
    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    D3D12_VIEWPORT vp     = { 0.f, 0.f,
        (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.f, 1.f };
    D3D12_RECT scissor    = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    // SrvManager のヒープをバインド（他の描画が上書きしている可能性があるため再セット）
    if (srvManager_) {
        ID3D12DescriptorHeap* heaps[] = { srvManager_->GetSrvDescriptorHeap() };
        cmd->SetDescriptorHeaps(1, heaps);
    }

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, cbResource_->GetGPUVirtualAddress());

    if (srvManager_ && freezeSrvIndex_ != UINT32_MAX) {
        cmd->SetGraphicsRootDescriptorTable(
            1, srvManager_->GetGPUDescriptorHandle(freezeSrvIndex_));
    }

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}

void GlassShatterEffect::Start()
{
    timer_         = 0.0f;
    active_        = true;
    finished_      = false;
    captureNeeded_ = true;
    if (cbData_)
        cbData_->time = 0.0f;
}

void GlassShatterEffect::Reset()
{
    timer_         = 0.0f;
    active_        = false;
    finished_      = false;
    captureNeeded_ = false;
    if (cbData_)
        cbData_->time = 0.0f;
}

void GlassShatterEffect::SetImpactUV(float u, float v)
{
    if (cbData_) { cbData_->impactU = u; cbData_->impactV = v; }
}

void GlassShatterEffect::SetCrackWidth(float w)
{
    if (cbData_) cbData_->crackWidth = w;
}

void GlassShatterEffect::SetShardSpeed(float s)
{
    if (cbData_) cbData_->shardSpeed = s;
}

void GlassShatterEffect::SetDuration(float seconds)
{
    duration_ = seconds > 0.01f ? seconds : 0.01f;
}
