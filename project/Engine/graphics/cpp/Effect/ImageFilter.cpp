/**
 * @file ImageFilter.cpp
 * @brief ImageFilterが担当する処理を実装するファイル
 */
#include "ImageFilter.h"
#include "EngineAssert.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

// ヘルパー  カラーテクスチャ + RTV + SRV を作成

static void CreateOffscreenTexture(
    ID3D12Device* device,
    SrvManager* srvManager,
    uint32_t width, uint32_t height,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outTexture,
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& outRtvHeap,
    D3D12_CPU_DESCRIPTOR_HANDLE& outRtvHandle,
    uint32_t& outSrvIndex)
{
    constexpr DXGI_FORMAT kResourceFmt = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    constexpr DXGI_FORMAT kRtvFmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    constexpr DXGI_FORMAT kSrvFmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    constexpr float kClear[4] = { 0.1f, 0.25f, 0.5f, 1.0f };

    D3D12_HEAP_PROPERTIES heapProps = { };
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = { };
    clearValue.Format = kRtvFmt;
    clearValue.Color[0] = kClear[0];
    clearValue.Color[1] = kClear[1];
    clearValue.Color[2] = kClear[2];
    clearValue.Color[3] = kClear[3];

    D3D12_RESOURCE_DESC desc = { };
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kResourceFmt;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue, IID_PPV_ARGS(&outTexture));
    ENGINE_ASSERT(SUCCEEDED(hr));

    outRtvHeap = DirectXCommon::CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);

    outRtvHandle = outRtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = { };
    rtvDesc.Format = kRtvFmt;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(outTexture.Get(), &rtvDesc, outRtvHandle);

    outSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVforTexture2D(outSrvIndex, outTexture.Get(), kSrvFmt, 1);
}

// Initialize（分割ヘルパーを呼び出すだけ）

// ══════════════════════════════════════════════════════
// 初期化とリソース生成
// ══════════════════════════════════════════════════════

void ImageFilter::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();
    const uint32_t width = WinApp::kClientWidth;
    const uint32_t height = WinApp::kClientHeight;

    CreateOffscreenTexture(device, srvManager, width, height,
        sceneTexture_, sceneRtvHeap_, sceneRtvHandle_, sceneSrvIndex_);
    CreateOffscreenTexture(device, srvManager, width, height,
        intermediateTexture_, intermediateRtvHeap_, intermediateRtvHandle_, intermediateSrvIndex_);

    depthSrvIndex_ = srvManager->Allocate();
    srvManager->CreateSRVforDepthTexture(depthSrvIndex_, dxCommon->GetDepthStencilResource());

    InitConstantBuffers(dxCommon, width, height);
    InitRootSignatures(dxCommon);
    InitPipelineStates(dxCommon);

    RebuildKernel();
}

// InitConstantBuffers  各エフェクト用 CB の作成とマップ

void ImageFilter::InitConstantBuffers(DirectXCommon* dxCommon, uint32_t width, uint32_t height)
{
    const float rw = 1.0f / float(width);
    const float rh = 1.0f / float(height);

    // ブラー用 CB（H と V の 2 スロット × 256 バイト = 512 バイト）
    cbResource_ = dxCommon->CreateBufferResource(512);
    void* mapped = nullptr;
    cbResource_->Map(0, nullptr, &mapped);
    cbH_ = reinterpret_cast<FilterParams*>(mapped);
    cbV_ = reinterpret_cast<FilterParams*>(reinterpret_cast<uint8_t*>(mapped) + 256);
    cbH_->texelSizeX = rw;
    cbH_->texelSizeY = rh;
    cbH_->dirX = 1.0f;
    cbH_->dirY = 0.0f;
    cbV_->texelSizeX = rw;
    cbV_->texelSizeY = rh;
    cbV_->dirX = 0.0f;
    cbV_->dirY = 1.0f;

    // アウトライン用 CB（1 スロット × 256 バイト）
    outlineCbResource_ = dxCommon->CreateBufferResource(256);
    void* outlineMapped = nullptr;
    outlineCbResource_->Map(0, nullptr, &outlineMapped);
    outlineCb_ = reinterpret_cast<OutlineParams*>(outlineMapped);
    outlineCb_->texelSizeX = rw;
    outlineCb_->texelSizeY = rh;
    outlineCb_->threshold = 0.05f;
    outlineCb_->edgeStrength = 5.0f;
    outlineCb_->outlineR = 0.0f;
    outlineCb_->outlineG = 0.0f;
    outlineCb_->outlineB = 0.0f;
    outlineCb_->outlineA = 1.0f;
    outlineCb_->depthScale = 100.0f;

    // ラジアルブラー用 CB（1 スロット × 256 バイト）
    radialBlurCbResource_ = dxCommon->CreateBufferResource(256);
    void* radialMapped = nullptr;
    radialBlurCbResource_->Map(0, nullptr, &radialMapped);
    radialBlurCb_ = reinterpret_cast<RadialBlurParams*>(radialMapped);
    radialBlurCb_->centerX = 0.5f;
    radialBlurCb_->centerY = 0.5f;
    radialBlurCb_->strength = 0.1f;
    radialBlurCb_->sampleCount = 16;

    // ディゾルブ用 CB（1 スロット × 256 バイト）
    // ノイズマスクは Apply() の初回 Dissolve 呼び出し時に遅延ロードする
    dissolveCbResource_ = dxCommon->CreateBufferResource(256);
    void* dissolveMapped = nullptr;
    dissolveCbResource_->Map(0, nullptr, &dissolveMapped);
    dissolveCb_ = reinterpret_cast<DissolveParams*>(dissolveMapped);
    dissolveCb_->threshold = 0.0f;
    dissolveCb_->edgeWidth = 0.05f;
    dissolveCb_->edgeR = 1.0f;
    dissolveCb_->edgeG = 0.5f;
    dissolveCb_->edgeB = 0.0f;
    dissolveCb_->edgeA = 1.0f;

    // プロシージャルノイズ用 CB（1 スロット × 256 バイト）
    noiseGenCbResource_ = dxCommon->CreateBufferResource(256);
    void* noiseGenMapped = nullptr;
    noiseGenCbResource_->Map(0, nullptr, &noiseGenMapped);
    noiseGenCb_ = reinterpret_cast<NoiseGenParams*>(noiseGenMapped);
    noiseGenCb_->scaleX = 4.0f;
    noiseGenCb_->scaleY = 4.0f;
    noiseGenCb_->seed = 0.0f;
    noiseGenCb_->octaves = 4;
    noiseGenCb_->persistence = 0.5f;
    noiseGenCb_->lacunarity = 2.0f;
    noiseGenCb_->colorMode = 0;
    noiseGenCb_->opacity = 1.0f;
}

// InitRootSignatures  ブラー用・アウトライン用 Root Signature 作成

void ImageFilter::InitRootSignatures(DirectXCommon* dxCommon)
{
    ID3D12Device* device = dxCommon->GetDevice();

    D3D12_STATIC_SAMPLER_DESC sampler = { };
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // ブラー用 Root Signature（b0 + t0）
    D3D12_DESCRIPTOR_RANGE srvRange0 = { };
    srvRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange0.NumDescriptors = 1;
    srvRange0.BaseShaderRegister = 0;
    srvRange0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER blurParams[2] = { };
    blurParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    blurParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    blurParams[0].Descriptor.ShaderRegister = 0;
    blurParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    blurParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    blurParams[1].DescriptorTable.pDescriptorRanges = &srvRange0;
    blurParams[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_ROOT_SIGNATURE_DESC blurSigDesc = { };
    blurSigDesc.NumParameters = 2;
    blurSigDesc.pParameters = blurParams;
    blurSigDesc.NumStaticSamplers = 1;
    blurSigDesc.pStaticSamplers = &sampler;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&blurSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // アウトライン / ディゾルブ用 Root Signature（b0 + t0 + t1）
    D3D12_DESCRIPTOR_RANGE srvRange1 = { };
    srvRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange1.NumDescriptors = 1;
    srvRange1.BaseShaderRegister = 1; // t1
    srvRange1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER outlineParams[3] = { };
    outlineParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    outlineParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    outlineParams[0].Descriptor.ShaderRegister = 0;
    outlineParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    outlineParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    outlineParams[1].DescriptorTable.pDescriptorRanges = &srvRange0;
    outlineParams[1].DescriptorTable.NumDescriptorRanges = 1;
    outlineParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    outlineParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    outlineParams[2].DescriptorTable.pDescriptorRanges = &srvRange1;
    outlineParams[2].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_ROOT_SIGNATURE_DESC outlineSigDesc = { };
    outlineSigDesc.NumParameters = 3;
    outlineSigDesc.pParameters = outlineParams;
    outlineSigDesc.NumStaticSamplers = 1;
    outlineSigDesc.pStaticSamplers = &sampler;

    ComPtr<ID3DBlob> outlineSigBlob, outlineErrBlob;
    hr = D3D12SerializeRootSignature(&outlineSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &outlineSigBlob, &outlineErrBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, outlineSigBlob->GetBufferPointer(), outlineSigBlob->GetBufferSize(),
        IID_PPV_ARGS(&outlineRootSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

// InitPipelineStates  シェーダーコンパイル + 全 PSO 作成

void ImageFilter::InitPipelineStates(DirectXCommon* dxCommon)
{
    ID3D12Device* device = dxCommon->GetDevice();

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> boxPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/KernelFilterPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> gaussianPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/GaussianFilterPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> prewittPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/PrewittEdgePS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> depthOlPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/DepthOutlinePS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> radialBlurPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/RadialBlurPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> dissolvePsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/DissolvePS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> noiseGenPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/NoisePS.hlsl", L"ps_6_0");

    // 共通 PSO ベース（深度なし、カリングなし、フルスクリーントライアングル）
    D3D12_BLEND_DESC blendDesc = { };
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    D3D12_RASTERIZER_DESC rastDesc = { };
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;
    D3D12_DEPTH_STENCIL_DESC depthDesc = { };
    depthDesc.DepthEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = { };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.BlendState = blendDesc;
    psoDesc.RasterizerState = rastDesc;
    psoDesc.DepthStencilState = depthDesc;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleDesc.Count = 1;

    auto createPSO = [&](ID3D12RootSignature* sig, IDxcBlob* ps, ComPtr<ID3D12PipelineState>& out) {
        psoDesc.pRootSignature = sig;
        psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&out));
        ENGINE_ASSERT(SUCCEEDED(hr));
    };

    // rootSignature_（b0 + t0）を使う PSO
    createPSO(rootSignature_.Get(), boxPsBlob.Get(), boxPso_);
    createPSO(rootSignature_.Get(), gaussianPsBlob.Get(), gaussianPso_);
    createPSO(rootSignature_.Get(), prewittPsBlob.Get(), prewittPso_);
    createPSO(rootSignature_.Get(), radialBlurPsBlob.Get(), radialBlurPso_);
    createPSO(rootSignature_.Get(), noiseGenPsBlob.Get(), noiseGenPso_);

    // outlineRootSignature_（b0 + t0 + t1）を使う PSO
    createPSO(outlineRootSignature_.Get(), depthOlPsBlob.Get(), depthOutlinePso_);
    createPSO(outlineRootSignature_.Get(), dissolvePsBlob.Get(), dissolvePso_);
}

// ══════════════════════════════════════════════════════
// 終了と共通描画
// ══════════════════════════════════════════════════════

void ImageFilter::Finalize()
{
    if (noiseGenCb_) {
        noiseGenCbResource_->Unmap(0, nullptr);
        noiseGenCb_ = nullptr;
    }
    noiseGenCbResource_.Reset();
    noiseGenPso_.Reset();

    if (dissolveCb_) {
        dissolveCbResource_->Unmap(0, nullptr);
        dissolveCb_ = nullptr;
    }
    dissolveCbResource_.Reset();
    dissolvePso_.Reset();

    if (radialBlurCb_) {
        radialBlurCbResource_->Unmap(0, nullptr);
        radialBlurCb_ = nullptr;
    }
    radialBlurCbResource_.Reset();

    if (outlineCb_) {
        outlineCbResource_->Unmap(0, nullptr);
        outlineCb_ = nullptr;
    }
    outlineCbResource_.Reset();

    if (cbH_) {
        cbResource_->Unmap(0, nullptr);
        cbH_ = cbV_ = nullptr;
    }
    cbResource_.Reset();

    radialBlurPso_.Reset();
    depthOutlinePso_.Reset();
    outlineRootSignature_.Reset();
    prewittPso_.Reset();
    gaussianPso_.Reset();
    boxPso_.Reset();
    rootSignature_.Reset();
    intermediateRtvHeap_.Reset();
    intermediateTexture_.Reset();
    sceneRtvHeap_.Reset();
    sceneTexture_.Reset();
}

void ImageFilter::BeginScene()
{
    auto* cmd = dxCommon_->GetCommandList();
    if (!isSceneFirstFrame_) {
        DirectXCommon::TransitionBarrier(cmd, sceneTexture_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    isSceneFirstFrame_ = false;

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(1, &sceneRtvHandle_, FALSE, &dsv);
    constexpr float kClear[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
    cmd->ClearRenderTargetView(sceneRtvHandle_, kClear, 0, nullptr);

    D3D12_VIEWPORT vp = { 0.f, 0.f, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.f, 1.f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
}

void ImageFilter::EndScene()
{
    DirectXCommon::TransitionBarrier(dxCommon_->GetCommandList(), sceneTexture_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void ImageFilter::Apply(SrvManager* srvManager)
{
    auto* cmd = dxCommon_->GetCommandList();

    D3D12_VIEWPORT vp = { 0.f, 0.f, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.f, 1.f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    D3D12_CPU_DESCRIPTOR_HANDLE backRtv = dxCommon_->GetCurrentBackBufferHandle();

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    GetFilterMode(mode_).Apply(*this, cmd, srvManager, vp, scissor, backRtv);
}

// Filter Mode Strategy  Mode ごとの Apply 実装

// ══════════════════════════════════════════════════════
// フィルタ方式別の描画
// ══════════════════════════════════════════════════════

void ImageFilter::PrewittEdgeFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // Prewitt エッジ検出（輝度ベース）  シングルパス
    cmd->SetGraphicsRootSignature(filter.rootSignature_.Get());
    cmd->SetPipelineState(filter.prewittPso_.Get());
    cmd->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootConstantBufferView(0, filter.outlineCbResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);
}

void ImageFilter::DepthOutlineFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // 深度ベースアウトライン  シングルパス、深度バリア付き
    auto* depthRes = filter.dxCommon_->GetDepthStencilResource();
    DirectXCommon::TransitionBarrier(cmd, depthRes,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmd->SetGraphicsRootSignature(filter.outlineRootSignature_.Get());
    cmd->SetPipelineState(filter.depthOutlinePso_.Get());
    cmd->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootConstantBufferView(0, filter.outlineCbResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    cmd->SetGraphicsRootDescriptorTable(2, srvManager->GetGPUDescriptorHandle(filter.depthSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);

    DirectXCommon::TransitionBarrier(cmd, depthRes,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

void ImageFilter::RadialBlurFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // ラジアルブラー  シングルパス
    cmd->SetGraphicsRootSignature(filter.rootSignature_.Get());
    cmd->SetPipelineState(filter.radialBlurPso_.Get());
    cmd->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootConstantBufferView(0, filter.radialBlurCbResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);
}

void ImageFilter::DissolveFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // ディゾルブ  シングルパス、ノイズマスクを t1 にバインド
    // TextureManager が初期化済みになってから初回ロード
    if (filter.noiseSrvIndex_[0] == UINT32_MAX) {
        auto* texMgr = TextureManager::GetInstance();
        texMgr->LoadTexture("Resources/noise0.png");
        texMgr->LoadTexture("Resources/noise1.png");
        filter.noiseSrvIndex_[0] = texMgr->GetTextureIndexByFilePath("Resources/noise0.png");
        filter.noiseSrvIndex_[1] = texMgr->GetTextureIndexByFilePath("Resources/noise1.png");
    }
    uint32_t maskSrv = filter.noiseSrvIndex_[filter.dissolveMaskIndex_];
    cmd->SetGraphicsRootSignature(filter.outlineRootSignature_.Get());
    cmd->SetPipelineState(filter.dissolvePso_.Get());
    cmd->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootConstantBufferView(0, filter.dissolveCbResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    cmd->SetGraphicsRootDescriptorTable(2, srvManager->GetGPUDescriptorHandle(maskSrv));
    cmd->DrawInstanced(3, 1, 0, 0);
}

void ImageFilter::NoiseGenFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // プロシージャルノイズ  シングルパス（シーンに重ね合わせ）
    // アニメーション  毎フレーム seed を加算して乱数パターンを変化させる
    if (filter.animateNoise_) {
        filter.noiseTime_ += filter.noiseSpeed_;
        filter.noiseGenCb_->seed = filter.noiseManualSeed_ + filter.noiseTime_;
    } else {
        filter.noiseGenCb_->seed = filter.noiseManualSeed_;
    }
    cmd->SetGraphicsRootSignature(filter.rootSignature_.Get());
    cmd->SetPipelineState(filter.noiseGenPso_.Get());
    cmd->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootConstantBufferView(0, filter.noiseGenCbResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);
}

void ImageFilter::BoxGaussianFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // Box / Gaussian  水平→垂直の 2 パス
    cmd->SetGraphicsRootSignature(filter.rootSignature_.Get());
    cmd->SetPipelineState(filter.mode_ == Mode::Box ? filter.boxPso_.Get() : filter.gaussianPso_.Get());

    // Pass 1: 水平（sceneTexture → intermediateTexture）
    if (!filter.isIntermediateFirstFrame_) {
        DirectXCommon::TransitionBarrier(cmd, filter.intermediateTexture_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    filter.isIntermediateFirstFrame_ = false;

    cmd->OMSetRenderTargets(1, &filter.intermediateRtvHandle_, FALSE, nullptr);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootConstantBufferView(0, filter.cbResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);

    DirectXCommon::TransitionBarrier(cmd, filter.intermediateTexture_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Pass 2: 垂直（intermediateTexture → backbuffer）
    cmd->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);
    cmd->SetGraphicsRootConstantBufferView(0, filter.cbResource_->GetGPUVirtualAddress() + 256);
    cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(filter.intermediateSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);
}

// ══════════════════════════════════════════════════════
// フィルタ選択とカーネル更新
// ══════════════════════════════════════════════════════

const ImageFilter::IFilterMode& ImageFilter::GetFilterMode(Mode mode)
{
    static BoxGaussianFilterMode boxGaussian;
    static PrewittEdgeFilterMode prewittEdge;
    static DepthOutlineFilterMode depthOutline;
    static RadialBlurFilterMode radialBlur;
    static DissolveFilterMode dissolve;
    static NoiseGenFilterMode noiseGen;
    switch (mode) {
    case Mode::PrewittEdge:
        return prewittEdge;
    case Mode::DepthOutline:
        return depthOutline;
    case Mode::RadialBlur:
        return radialBlur;
    case Mode::Dissolve:
        return dissolve;
    case Mode::NoiseGen:
        return noiseGen;
    default:
        return boxGaussian; // Box / Gaussian
    }
}

// カーネル重みの計算（Box / Gaussian のみ）

void ImageFilter::RebuildKernel()
{
    if (!cbH_) {
        return;
    }
    if (mode_ == Mode::PrewittEdge || mode_ == Mode::DepthOutline || mode_ == Mode::RadialBlur || mode_ == Mode::Dissolve || mode_ == Mode::NoiseGen) {
        return;
    }

    int r = 1;
    float weights[17] = { };

    if (mode_ == Mode::Box) {
        r = std::clamp(boxRadius_, 0, 8);
        float w = (r == 0) ? 1.0f : 1.0f / float(2 * r + 1);
        for (int i = 0; i <= 2 * r; ++i) {
            weights[i] = w;
        }

    } else { // Gaussian
        float sigma = (std::max)(gaussianSigma_, 0.01f);
        r = std::clamp(static_cast<int>(sigma * 3.0f), 1, 8);
        float s2 = 2.0f * sigma * sigma;
        float total = 0.0f;
        for (int i = 0; i <= 2 * r; ++i) {
            float k = float(i - r);
            weights[i] = std::exp(-(k * k) / s2);
            total += weights[i];
        }
        for (int i = 0; i <= 2 * r; ++i) {
            weights[i] /= total;
        }
    }

    cbH_->radius = r;
    cbV_->radius = r;
    for (int i = 0; i <= 2 * r; ++i) {
        cbH_->kernel[i] = weights[i];
        cbV_->kernel[i] = weights[i];
    }
}

void ImageFilter::SetRadius(int r)
{
    boxRadius_ = r;
    if (mode_ == Mode::Box) {
        RebuildKernel();
    }
}

void ImageFilter::SetSigma(float s)
{
    gaussianSigma_ = s;
    if (mode_ == Mode::Gaussian) {
        RebuildKernel();
    }
}
