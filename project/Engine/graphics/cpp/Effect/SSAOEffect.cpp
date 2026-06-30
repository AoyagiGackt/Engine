#include "SSAOEffect.h"
#include <cassert>
#include <cmath>
#include <random>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

// ---- ランダム半球カーネル ----

static void GenerateKernel(float* out, int count)
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> distF(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    int n = 0;
    while (n < count) {
        float x = distF(rng), y = distF(rng), z = dist01(rng);
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-5f) { continue; }
        x /= len; y /= len; z /= len;
        float scale = float(n) / float(count);
        scale = 0.1f + 0.9f * scale * scale;
        out[n * 4 + 0] = x * scale;
        out[n * 4 + 1] = y * scale;
        out[n * 4 + 2] = z * scale;
        out[n * 4 + 3] = 0.0f;
        ++n;
    }
}

// ---- ヘルパー ----

void SSAOEffect::Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
}

void SSAOEffect::CreateRT(ID3D12Device* device, SrvManager* srvManager,
    UINT w, UINT h, DXGI_FORMAT fmt,
    ComPtr<ID3D12Resource>& res,
    ComPtr<ID3D12DescriptorHeap>& rtvHeap,
    D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
    uint32_t& srvIndex,
    const float clearColor[4])
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = w;
    desc.Height           = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = fmt;
    desc.SampleDesc.Count = 1;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE cv = {};
    cv.Format = fmt;
    cv.Color[0] = clearColor[0]; cv.Color[1] = clearColor[1];
    cv.Color[2] = clearColor[2]; cv.Color[3] = clearColor[3];

    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv,
        IID_PPV_ARGS(&res));
    assert(SUCCEEDED(hr));

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap));
    rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(res.Get(), nullptr, rtvHandle);

    srvIndex = srvManager->Allocate();
    srvManager->CreateSRVforTexture2D(srvIndex, res.Get(), fmt, 1);
}

// ---- ルートシグネチャ生成ヘルパー（static）----

// NormalCapture RS: slot0 = CBV (VS, b0) per-object transform / slot1 = CBV (PS, b1) view matrix
static ComPtr<ID3D12RootSignature> CreateNormalCaptureRS(ID3D12Device* device)
{
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
    params[0].Descriptor.ShaderRegister = 0; // b0 - TransformationMatrix
    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].Descriptor.ShaderRegister = 1; // b1 - NormalCaptureCB

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters   = params;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ComPtr<ID3D12RootSignature> rs;
    device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&rs));
    return rs;
}

// SSAO / Blur RS: slot0 = CBV (ALL, b0) / slot1 = SRV descriptor table (PS, t0)
static ComPtr<ID3D12RootSignature> CreateRS_CBV_SRV(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0; // t0
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0; // b0
    params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.pDescriptorRanges   = &srvRange;
    params[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister   = 0; // s0
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 2;
    rsDesc.pParameters       = params;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ComPtr<ID3D12RootSignature> rs;
    device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&rs));
    return rs;
}

// Apply RS: slot0 = SRV descriptor table (PS, t0)
static ComPtr<ID3D12RootSignature> CreateApplyRS(ID3D12Device* device)
{
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                    = 1;
    srvRange.BaseShaderRegister                = 0; // t0
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
    param.DescriptorTable.pDescriptorRanges   = &srvRange;
    param.DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister   = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters     = 1;
    rsDesc.pParameters       = &param;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers   = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ComPtr<ID3D12RootSignature> rs;
    device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&rs));
    return rs;
}

static ComPtr<ID3D12PipelineState> CreateFullscreenPSO(
    ID3D12Device* device, ID3D12RootSignature* rs,
    IDxcBlob* vs, IDxcBlob* ps, DXGI_FORMAT fmt,
    bool multiplyBlend = false)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = rs;
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.RasterizerState.CullMode  = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FillMode  = D3D12_FILL_MODE_SOLID;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.NumRenderTargets      = 1;
    desc.RTVFormats[0]         = fmt;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
    desc.SampleDesc.Count      = 1;

    auto& b = desc.BlendState.RenderTarget[0];
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (multiplyBlend) {
        b.BlendEnable    = TRUE;
        b.SrcBlend       = D3D12_BLEND_DEST_COLOR;
        b.DestBlend      = D3D12_BLEND_ZERO;
        b.BlendOp        = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha  = D3D12_BLEND_ONE;
        b.DestBlendAlpha = D3D12_BLEND_ZERO;
        b.BlendOpAlpha   = D3D12_BLEND_OP_ADD;
    }

    ComPtr<ID3D12PipelineState> pso;
    device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
    return pso;
}

// ---- 定数バッファ生成ヘルパー ----

template<typename T>
static Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadCB(ID3D12Device* device, T** mapped)
{
    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width            = (sizeof(T) + 255) & ~255u;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> res;
    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
    res->Map(0, nullptr, reinterpret_cast<void**>(mapped));
    return res;
}

void SSAOEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_   = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon->GetDevice();

    UINT W = WinApp::kClientWidth;
    UINT H = WinApp::kClientHeight;

    // レンダーターゲット作成
    float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float zero[4]  = { 0.0f, 0.0f, 0.0f, 0.0f };

    CreateRT(device, srvManager, W, H, DXGI_FORMAT_R16G16B16A16_FLOAT,
        normalTex_, normalRtvHeap_, normalRtvHandle_, normalSrvIndex_, zero);

    CreateRT(device, srvManager, W, H, DXGI_FORMAT_R8_UNORM,
        ssaoTex_, ssaoRtvHeap_, ssaoRtvHandle_, ssaoSrvIndex_, white);

    CreateRT(device, srvManager, W, H, DXGI_FORMAT_R8_UNORM,
        blurTex_, blurRtvHeap_, blurRtvHandle_, blurSrvIndex_, white);

    // NormalCapture 専用深度バッファ
    {
        D3D12_RESOURCE_DESC depthDesc = {};
        depthDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width            = W;
        depthDesc.Height           = H;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels        = 1;
        depthDesc.Format           = DXGI_FORMAT_D32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE cv = {};
        cv.Format             = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 1.0f;
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
            IID_PPV_ARGS(&depthTex_));

        D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
        dsvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvDesc.NumDescriptors = 1;
        device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_));
        dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        device->CreateDepthStencilView(depthTex_.Get(), nullptr, dsvHandle_);
    }

    // 定数バッファ
    normalCb_ = CreateUploadCB<NormalCaptureCB>(device, &normalCbData_);

    ssaoCb_   = CreateUploadCB<SSAOParams>(device, &ssaoCbData_);
    ssaoCbData_->texW       = float(W);
    ssaoCbData_->texH       = float(H);
    ssaoCbData_->numSamples = 16;
    {
        float kernel[16 * 4] = {};
        GenerateKernel(kernel, 16);
        for (int i = 0; i < 16; ++i) {
            ssaoCbData_->kernel[i] = { kernel[i*4], kernel[i*4+1], kernel[i*4+2], kernel[i*4+3] };
        }
    }

    blurCb_  = CreateUploadCB<BlurParams>(device, &blurCbData_);
    blurCbData_->texW   = float(W);
    blurCbData_->texH   = float(H);
    blurCbData_->pad[0] = blurCbData_->pad[1] = 0.0f;

    // シェーダーコンパイル
    IDxcBlob* obj3dVS  = dxCommon->CompileShader(L"Resources/shaders/object3d/Object3dVS.hlsl",         L"vs_6_0");
    IDxcBlob* normalPS = dxCommon->CompileShader(L"Resources/shaders/postprocess/NormalCapturePS.hlsl", L"ps_6_0");
    IDxcBlob* fsVS     = dxCommon->CompileShader(L"Resources/shaders/postprocess/FullscreenVS.hlsl",    L"vs_6_0");
    IDxcBlob* ssaoPS   = dxCommon->CompileShader(L"Resources/shaders/postprocess/SSAOPS.hlsl",          L"ps_6_0");
    IDxcBlob* blurPS   = dxCommon->CompileShader(L"Resources/shaders/postprocess/SSAOBlurPS.hlsl",      L"ps_6_0");
    IDxcBlob* applyPS  = dxCommon->CompileShader(L"Resources/shaders/postprocess/SSAOApplyPS.hlsl",     L"ps_6_0");

    // NormalCapture PSO: Object3dVS + NormalCapturePS
    {
        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        normalRS_ = CreateNormalCaptureRS(device);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature        = normalRS_.Get();
        psoDesc.InputLayout           = { layout, _countof(layout) };
        psoDesc.VS                    = { obj3dVS->GetBufferPointer(),  obj3dVS->GetBufferSize() };
        psoDesc.PS                    = { normalPS->GetBufferPointer(), normalPS->GetBufferSize() };
        psoDesc.RasterizerState.CullMode  = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.FillMode  = D3D12_FILL_MODE_SOLID;
        psoDesc.DepthStencilState.DepthEnable    = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
        psoDesc.NumRenderTargets      = 1;
        psoDesc.RTVFormats[0]         = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
        psoDesc.SampleDesc.Count      = 1;
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&normalPSO_));
    }

    // SSAO, Blur PSO
    ssaoRS_  = CreateRS_CBV_SRV(device);
    ssaoPSO_ = CreateFullscreenPSO(device, ssaoRS_.Get(), fsVS, ssaoPS, DXGI_FORMAT_R8_UNORM);

    blurRS_  = CreateRS_CBV_SRV(device);
    blurPSO_ = CreateFullscreenPSO(device, blurRS_.Get(), fsVS, blurPS, DXGI_FORMAT_R8_UNORM);

    // Apply PSO (multiply blend → AO を乗算でシーンに合成)
    applyRS_  = CreateApplyRS(device);
    applyPSO_ = CreateFullscreenPSO(device, applyRS_.Get(), fsVS, applyPS,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, /*multiplyBlend=*/true);
}

void SSAOEffect::Finalize()
{
    if (normalCbData_) { normalCb_->Unmap(0, nullptr); normalCbData_ = nullptr; }
    if (ssaoCbData_)   { ssaoCb_->Unmap(0, nullptr);   ssaoCbData_  = nullptr; }
    if (blurCbData_)   { blurCb_->Unmap(0, nullptr);   blurCbData_  = nullptr; }
}

// ---- NormalCapture ----

void SSAOEffect::BeginNormalCapture(DirectXCommon* dxCommon, Camera* camera)
{
    if (!enabled_) { return; }

    ID3D12GraphicsCommandList* cmd = dxCommon->GetCommandList();

    Barrier(cmd, normalTex_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmd->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    cmd->ClearRenderTargetView(normalRtvHandle_, clearColor, 0, nullptr);
    cmd->OMSetRenderTargets(1, &normalRtvHandle_, FALSE, &dsvHandle_);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f,
        float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    cmd->SetGraphicsRootSignature(normalRS_.Get());
    cmd->SetPipelineState(normalPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // slot1 = NormalCaptureCB (PS, b1) - view matrix
    normalCbData_->view = camera->GetViewMatrix();
    cmd->SetGraphicsRootConstantBufferView(1, normalCb_->GetGPUVirtualAddress());

    normalInRTV_ = true;
}

void SSAOEffect::EndNormalCapture(DirectXCommon* dxCommon)
{
    if (!enabled_ || !normalInRTV_) { return; }

    Barrier(dxCommon->GetCommandList(), normalTex_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    normalInRTV_ = false;
}

void SSAOEffect::SetObjectTransform(ID3D12GraphicsCommandList* cmd,
    D3D12_GPU_VIRTUAL_ADDRESS transformAddr) const
{
    cmd->SetGraphicsRootConstantBufferView(0, transformAddr);
}

// ---- SSAO Compute ----

void SSAOEffect::Compute(DirectXCommon* dxCommon, Camera* camera)
{
    if (!enabled_) { return; }

    ID3D12GraphicsCommandList* cmd = dxCommon->GetCommandList();

    Barrier(cmd, ssaoTex_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    cmd->ClearRenderTargetView(ssaoRtvHandle_, white, 0, nullptr);
    cmd->OMSetRenderTargets(1, &ssaoRtvHandle_, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f,
        float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    ssaoCbData_->projection        = camera->GetProjectionMatrix();
    ssaoCbData_->projectionInverse = Inverse(camera->GetProjectionMatrix());

    cmd->SetGraphicsRootSignature(ssaoRS_.Get());
    cmd->SetPipelineState(ssaoPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(0, ssaoCb_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(normalSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);

    Barrier(cmd, ssaoTex_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void SSAOEffect::Blur(DirectXCommon* dxCommon)
{
    if (!enabled_) { return; }

    ID3D12GraphicsCommandList* cmd = dxCommon->GetCommandList();

    Barrier(cmd, blurTex_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    cmd->ClearRenderTargetView(blurRtvHandle_, white, 0, nullptr);
    cmd->OMSetRenderTargets(1, &blurRtvHandle_, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f,
        float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    cmd->SetGraphicsRootSignature(blurRS_.Get());
    cmd->SetPipelineState(blurPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(0, blurCb_->GetGPUVirtualAddress()); // BlurParams {texW, texH}
    cmd->SetGraphicsRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(ssaoSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);

    Barrier(cmd, blurTex_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// ---- Apply (乗算ブレンドで AO をシーンに合成) ----

void SSAOEffect::Apply(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    if (!enabled_) { return; }

    ID3D12GraphicsCommandList* cmd = dxCommon->GetCommandList();

    // blurTex_ はすでに PIXEL_SHADER_RESOURCE 状態
    cmd->SetGraphicsRootSignature(applyRS_.Get());
    cmd->SetPipelineState(applyPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootDescriptorTable(0, srvManager->GetGPUDescriptorHandle(blurSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);
}
