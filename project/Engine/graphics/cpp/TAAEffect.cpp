#include "TAAEffect.h"
#include "WinApp.h"
#include <cassert>

using namespace Microsoft::WRL;

TAAEffect* TAAEffect::GetInstance()
{
    static TAAEffect inst;
    return &inst;
}

void TAAEffect::Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const
{
    D3D12_RESOURCE_BARRIER b    = {};
    b.Type                      = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource      = res;
    b.Transition.StateBefore    = before;
    b.Transition.StateAfter     = after;
    b.Transition.Subresource    = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
}

void TAAEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_   = dxCommon;
    srvManager_ = srvManager;

    ID3D12Device* device = dxCommon->GetDevice();
    const UINT W = WinApp::kClientWidth;
    const UINT H = WinApp::kClientHeight;

    // history RT (R16G16B16A16_FLOAT)
    {
        constexpr DXGI_FORMAT kFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format = kFmt;

        D3D12_RESOURCE_DESC rd  = {};
        rd.Dimension            = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width                = W;
        rd.Height               = H;
        rd.DepthOrArraySize     = 1;
        rd.MipLevels            = 1;
        rd.Format               = kFmt;
        rd.SampleDesc.Count     = 1;
        rd.Flags                = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&historyRT_));
        assert(SUCCEEDED(hr));

        D3D12_DESCRIPTOR_HEAP_DESC rtvHD = {};
        rtvHD.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHD.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&rtvHD, IID_PPV_ARGS(&historyRtvHeap_));
        assert(SUCCEEDED(hr));

        historyRtvHandle_ = historyRtvHeap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_RENDER_TARGET_VIEW_DESC rtvV = {};
        rtvV.Format        = kFmt;
        rtvV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(historyRT_.Get(), &rtvV, historyRtvHandle_);

        historySrvIndex_ = srvManager->Allocate();
        srvManager->CreateSRVforTexture2D(historySrvIndex_, historyRT_.Get(), kFmt, 1);
    }

    // accumulation RT — TAA ブレンド結果を一時保存し、history と backbuffer に配布する
    {
        constexpr DXGI_FORMAT kFmt = DXGI_FORMAT_R16G16B16A16_FLOAT;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format = kFmt;

        D3D12_RESOURCE_DESC rd  = {};
        rd.Dimension            = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width                = W;
        rd.Height               = H;
        rd.DepthOrArraySize     = 1;
        rd.MipLevels            = 1;
        rd.Format               = kFmt;
        rd.SampleDesc.Count     = 1;
        rd.Flags                = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&accumRT_));
        assert(SUCCEEDED(hr));

        D3D12_DESCRIPTOR_HEAP_DESC rtvHD = {};
        rtvHD.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHD.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&rtvHD, IID_PPV_ARGS(&accumRtvHeap_));
        assert(SUCCEEDED(hr));

        accumRtvHandle_ = accumRtvHeap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_RENDER_TARGET_VIEW_DESC rtvV = {};
        rtvV.Format        = kFmt;
        rtvV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(accumRT_.Get(), &rtvV, accumRtvHandle_);

        accumSrvIndex_ = srvManager->Allocate();
        srvManager->CreateSRVforTexture2D(accumSrvIndex_, accumRT_.Get(), kFmt, 1);
    }

    // constant buffer
    cb_ = dxCommon->CreateBufferResource((sizeof(CBLayout) + 255) & ~255u);
    cb_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    cbData_->blendAlpha = 0.1f;

    // root signature: b0(CBV,PS) + t0(SRV table,PS) + t1(SRV table,PS)
    {
        D3D12_DESCRIPTOR_RANGE srvRange0 = {};
        srvRange0.RangeType                                    = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange0.NumDescriptors                               = 1;
        srvRange0.BaseShaderRegister                           = 0;
        srvRange0.OffsetInDescriptorsFromTableStart            = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE srvRange1 = {};
        srvRange1.RangeType                                    = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange1.NumDescriptors                               = 1;
        srvRange1.BaseShaderRegister                           = 1;
        srvRange1.OffsetInDescriptorsFromTableStart            = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[3] = {};
        params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType                                = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.pDescriptorRanges            = &srvRange0;
        params[1].DescriptorTable.NumDescriptorRanges          = 1;
        params[1].ShaderVisibility                             = D3D12_SHADER_VISIBILITY_PIXEL;
        params[2].ParameterType                                = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.pDescriptorRanges            = &srvRange1;
        params[2].DescriptorTable.NumDescriptorRanges          = 1;
        params[2].ShaderVisibility                             = D3D12_SHADER_VISIBILITY_PIXEL;

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
    IDxcBlob* psBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/TAAPS.hlsl",        L"ps_6_0");

    D3D12_BLEND_DESC blend               = {};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rast           = {};
    rast.FillMode = D3D12_FILL_MODE_SOLID;
    rast.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth       = {};
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

    // psoFloat_: accumRT_（R16G16B16A16_FLOAT）への描画用。シェーダーは同じ
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&psoFloat_));
    assert(SUCCEEDED(hr));
}

void TAAEffect::Finalize()
{
    if (cbData_) { cb_->Unmap(0, nullptr); cbData_ = nullptr; }
    cb_.Reset();
    pso_.Reset(); psoFloat_.Reset(); rs_.Reset();
    historyRT_.Reset(); historyRtvHeap_.Reset();
    accumRT_.Reset();   accumRtvHeap_.Reset();
}

static float Halton(int index, int base)
{
    float f = 1.0f, r = 0.0f;
    int i = index;
    while (i > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
    }
    return r;
}

Vector2 TAAEffect::BeginFrame()
{
    frameIdx_++;
    int idx = static_cast<int>(frameIdx_);
    float jx = Halton(idx, 2) - 0.5f;
    float jy = Halton(idx, 3) - 0.5f;
    if (cbData_) {
        cbData_->jitterX = jx / static_cast<float>(WinApp::kClientWidth);
        cbData_->jitterY = jy / static_cast<float>(WinApp::kClientHeight);
    }
    return { jx, jy };
}

void TAAEffect::Apply(DirectXCommon* dxCommon, uint32_t currentSrvIndex)
{
    if (!enabled_) { return; }

    auto* cmd    = dxCommon->GetCommandList();
    const UINT W = WinApp::kClientWidth;
    const UINT H = WinApp::kClientHeight;

    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)W, (float)H, 0.f, 1.f };
    D3D12_RECT     sc = { 0, 0, (LONG)W, (LONG)H };

    auto DrawPass = [&](D3D12_CPU_DESCRIPTOR_HANDLE rtv, uint32_t t0, uint32_t t1,
                        ID3D12PipelineState* pso) {
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->SetGraphicsRootSignature(rs_.Get());
        cmd->SetPipelineState(pso);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootConstantBufferView(0, cb_->GetGPUVirtualAddress());
        cmd->SetGraphicsRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(t0));
        cmd->SetGraphicsRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(t1));
        cmd->DrawInstanced(3, 1, 0, 0);
    };

    D3D12_CPU_DESCRIPTOR_HANDLE backBuffer = dxCommon->GetCurrentBackBufferHandle();

    // Pass 1: blend(current, old_history) → accumRT_ (float16)
    // historyRT_ starts in RENDER_TARGET; transition to SRV to read old history.
    // accumRT_ starts in RENDER_TARGET; draw directly to it.
    Barrier(cmd, historyRT_.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    DrawPass(accumRtvHandle_, currentSrvIndex, historySrvIndex_, psoFloat_.Get());

    // Pass 2: copy accumRT_ → backbuffer (display)
    // Transition accumRT_ to SRV; draw a passthrough quad to the SRGB backbuffer.
    Barrier(cmd, accumRT_.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    float savedAlpha = cbData_->blendAlpha;
    float savedJX    = cbData_->jitterX;
    float savedJY    = cbData_->jitterY;
    cbData_->blendAlpha = 1.0f;  // lerp(accum, accum, 1.0) = accum (passthrough)
    cbData_->jitterX    = 0.0f;
    cbData_->jitterY    = 0.0f;
    DrawPass(backBuffer, accumSrvIndex_, accumSrvIndex_, pso_.Get());
    cbData_->blendAlpha = savedAlpha;
    cbData_->jitterX    = savedJX;
    cbData_->jitterY    = savedJY;

    // Pass 3: CopyResource accumRT_ → historyRT_ (both R16G16B16A16_FLOAT)
    // This stores the blended result as history for the next frame.
    Barrier(cmd, accumRT_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
    Barrier(cmd, historyRT_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_COPY_DEST);
    cmd->CopyResource(historyRT_.Get(), accumRT_.Get());

    // Return both to RENDER_TARGET for the next frame's Apply()
    Barrier(cmd, historyRT_.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    Barrier(cmd, accumRT_.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
}
