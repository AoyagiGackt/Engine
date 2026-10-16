/**
 * @file ImpactRadialBlurEffect.cpp
 * @brief ImpactRadialBlurEffectのバックバッファのキャプチャと放射状ブラーの描き戻しを実装するファイル
 */
#include "ImpactRadialBlurEffect.h"
#include "EngineAssert.h"
#include "WinApp.h"

namespace {

// 全画面三角形の頂点数
constexpr unsigned int kFullscreenVertexCount = 3;

}
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

namespace {
constexpr UINT kConstantBufferSize = 256; // 定数バッファは256バイト境界
} // namespace

void ImpactRadialBlurEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    ENGINE_ASSERT(dxCommon);
    ENGINE_ASSERT(srvManager);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon_->GetDevice();

    cbResource_ = dxCommon_->CreateBufferResource(kConstantBufferSize);
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = RadialBlurParams { };

    // キャプチャテクスチャ（バックバッファと同フォーマット・同サイズ）
    const DXGI_FORMAT bbFormat = dxCommon_->GetCurrentBackBufferResource()->GetDesc().Format;

    D3D12_RESOURCE_DESC texDesc = { };
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = WinApp::kClientWidth;
    texDesc.Height = WinApp::kClientHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = bbFormat;
    texDesc.SampleDesc = { 1, 0 };
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = { };
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
        IID_PPV_ARGS(&captureTexture_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    captureSrvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVforTexture2D(captureSrvIndex_, captureTexture_.Get(), bbFormat, 1);

    CreatePipeline();
}

void ImpactRadialBlurEffect::SetParams(float centerU, float centerV, float strength, int sampleCount)
{
    strength_ = strength;
    if (cbData_ != nullptr) {
        cbData_->centerX = centerU;
        cbData_->centerY = centerV;
        cbData_->strength = strength;
        cbData_->sampleCount = sampleCount;
    }
}

void ImpactRadialBlurEffect::CaptureAndApply()
{
    if (!IsActive() || !captureTexture_) {
        return;
    }

    auto* cmd = dxCommon_->GetCommandList();
    auto* backBuf = dxCommon_->GetCurrentBackBufferResource();

    // 描き終えたバックバッファを写し取る
    D3D12_RESOURCE_BARRIER toCopy[2] = {
        DirectXCommon::MakeTransitionBarrier(backBuf, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
        DirectXCommon::MakeTransitionBarrier(captureTexture_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
    };
    cmd->ResourceBarrier(2, toCopy);
    cmd->CopyResource(captureTexture_.Get(), backBuf);
    D3D12_RESOURCE_BARRIER toRestore[2] = {
        DirectXCommon::MakeTransitionBarrier(backBuf, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        DirectXCommon::MakeTransitionBarrier(captureTexture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };
    cmd->ResourceBarrier(2, toRestore);

    // ブラーを掛けて全面に描き戻す
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetCurrentBackBufferHandle();
    cmd->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    D3D12_VIEWPORT vp = dxCommon_->GetBackBufferViewport();
    D3D12_RECT scissor = dxCommon_->GetBackBufferScissorRect();
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetSrvDescriptorHeap() };
    cmd->SetDescriptorHeaps(1, heaps);

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(0, cbResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(captureSrvIndex_));

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(kFullscreenVertexCount, 1, 0, 0);
}

void ImpactRadialBlurEffect::CreatePipeline()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_DESCRIPTOR_RANGE srvRange = { };
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0; // t0
    srvRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = { };
    // [0] CBV b0 (ブラーパラメータ)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].Descriptor.ShaderRegister = 0;
    // [1] SRV t0 (キャプチャテクスチャ)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler = { };
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = { };
    rsDesc.NumParameters = _countof(rootParams);
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(
        L"Resources/shaders/postprocess/RadialBlurPS.hlsl", L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = { };
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

    psoDesc.DepthStencilState.DepthEnable = FALSE;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}
