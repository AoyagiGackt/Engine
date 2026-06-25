#include "HDREffect.h"
#include "WinApp.h"
#include <cassert>

using namespace Microsoft::WRL;

// ---- ヘルパー ----

void HDREffect::Barrier(ID3D12Resource* res,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter  = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    dxCommon_->GetCommandList()->ResourceBarrier(1, &b);
}

// ---- Initialize ----

void HDREffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_   = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon->GetDevice();

    UINT W = WinApp::kClientWidth;
    UINT H = WinApp::kClientHeight;

    // --- HDR レンダーターゲット (R16G16B16A16_FLOAT) ---
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = W;
        desc.Height           = H;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        D3D12_CLEAR_VALUE cv = {};
        cv.Format   = DXGI_FORMAT_R16G16B16A16_FLOAT;
        cv.Color[0] = clearColor[0]; cv.Color[1] = clearColor[1];
        cv.Color[2] = clearColor[2]; cv.Color[3] = clearColor[3];

        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
        HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv,
            IID_PPV_ARGS(&hdrTex_));
        assert(SUCCEEDED(hr));

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = 1;
        device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&hdrRtvHeap_));
        hdrRtvHandle_ = hdrRtvHeap_->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(hdrTex_.Get(), nullptr, hdrRtvHandle_);

        hdrSrvIndex_ = srvManager->Allocate();
        srvManager->CreateSRVforTexture2D(hdrSrvIndex_, hdrTex_.Get(),
            DXGI_FORMAT_R16G16B16A16_FLOAT, 1);
    }

    // --- 定数バッファ ---
    {
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = (sizeof(TonemapParams) + 255) & ~255u;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cbRes_));
        cbRes_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
        // デフォルト値は TonemapParams メンバ初期値（exposure=1.0, gamma=2.2）のまま
    }

    // --- ルートシグネチャ: slot0 = CBV (PS b0), slot1 = SRV table (PS t0) ---
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
        device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&rs_));
    }

    // --- PSO: FullscreenVS + TonemapPS → バックバッファ (SRGB) ---
    {
        IDxcBlob* fsVS     = dxCommon->CompileShader(L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
        IDxcBlob* tonemapPS = dxCommon->CompileShader(L"Resources/shaders/postprocess/TonemapPS.hlsl",  L"ps_6_0");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rs_.Get();
        desc.VS = { fsVS->GetBufferPointer(),      fsVS->GetBufferSize() };
        desc.PS = { tonemapPS->GetBufferPointer(), tonemapPS->GetBufferSize() };
        desc.RasterizerState.CullMode  = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FillMode  = D3D12_FILL_MODE_SOLID;
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.NumRenderTargets      = 1;
        desc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
        desc.SampleDesc.Count      = 1;
        HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso_));
        assert(SUCCEEDED(hr));
    }
}

// ---- BeginScene ----

void HDREffect::BeginScene()
{
    if (!enabled_ || inHDR_) return;

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    Barrier(hdrTex_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    cmd->ClearRenderTargetView(hdrRtvHandle_, clearColor, 0, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(1, &hdrRtvHandle_, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f,
        float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0,
        static_cast<LONG>(WinApp::kClientWidth),
        static_cast<LONG>(WinApp::kClientHeight) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    inHDR_ = true;
}

// ---- EndScene ----

void HDREffect::EndScene()
{
    if (!enabled_ || !inHDR_) return;

    Barrier(hdrTex_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    inHDR_ = false;
}

// ---- Apply (トーンマッピング → バックバッファ) ----

void HDREffect::Apply()
{
    if (!enabled_) return;

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    // バックバッファを描画先に設定（DirectXCommon::PreDraw が既に遷移済みのはず）
    D3D12_CPU_DESCRIPTOR_HANDLE bbRtv = dxCommon_->GetCurrentBackBufferHandle();
    cmd->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f,
        float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0,
        static_cast<LONG>(WinApp::kClientWidth),
        static_cast<LONG>(WinApp::kClientHeight) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    cmd->SetGraphicsRootSignature(rs_.Get());
    cmd->SetPipelineState(pso_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootConstantBufferView(0, cbRes_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(hdrSrvIndex_));
    cmd->DrawInstanced(3, 1, 0, 0);
}
