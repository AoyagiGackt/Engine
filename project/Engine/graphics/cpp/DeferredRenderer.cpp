#include "DeferredRenderer.h"
#include <cassert>

using namespace Microsoft::WRL;

// ---- ヘルパー ----

void DeferredRenderer::Barrier(ID3D12Resource* res,
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

void DeferredRenderer::CreateRT(UINT w, UINT h, DXGI_FORMAT fmt,
    ComPtr<ID3D12Resource>& res,
    ComPtr<ID3D12DescriptorHeap>& rtvHeap,
    D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
    uint32_t& srvIndex)
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = w;
    desc.Height           = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = fmt;
    desc.SampleDesc.Count = 1;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE cv = { fmt, {} };

    D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
    device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv,
        IID_PPV_ARGS(&res));

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 };
    device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap));
    rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(res.Get(), nullptr, rtvHandle);

    srvIndex = srvManager_->Allocate();
    srvManager_->CreateSRVforTexture2D(srvIndex, res.Get(), fmt, 1);
}

// ---- Initialize ----

void DeferredRenderer::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_   = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon->GetDevice();

    UINT W = WinApp::kClientWidth;
    UINT H = WinApp::kClientHeight;

    // G-Buffer レンダーターゲット
    CreateRT(W, H, DXGI_FORMAT_R8G8B8A8_UNORM,      albedoTex_,   albedoRtvHeap_,   albedoRtvHandle_,   albedoSrvIndex_);
    CreateRT(W, H, DXGI_FORMAT_R16G16B16A16_FLOAT,   normalTex_,   normalRtvHeap_,   normalRtvHandle_,   normalSrvIndex_);
    CreateRT(W, H, DXGI_FORMAT_R8G8B8A8_UNORM,       materialTex_, materialRtvHeap_, materialRtvHandle_, materialSrvIndex_);

    // 深度バッファの SRV（DirectXCommon の深度バッファを読み取り専用で使用）
    {
        depthSrvIndex_ = srvManager->Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels       = 1;
        device->CreateShaderResourceView(dxCommon->GetDepthStencilResource(), &srvDesc,
            srvManager->GetCPUDescriptorHandle(depthSrvIndex_));
    }

    // G-Buffer 用統合 RTV ヒープ（3 RT 同時クリアに使用）
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3 };
        device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&gbufferRtvHeap_));
        UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        auto base = gbufferRtvHeap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE slots[3] = {
            { base.ptr                   },
            { base.ptr + rtvSize         },
            { base.ptr + rtvSize * 2     },
        };
        device->CreateRenderTargetView(albedoTex_.Get(),   nullptr, slots[0]);
        device->CreateRenderTargetView(normalTex_.Get(),   nullptr, slots[1]);
        device->CreateRenderTargetView(materialTex_.Get(), nullptr, slots[2]);
    }

    // --- G-Buffer Root Signature ---
    // slot 0: PS b0 (Material)
    // slot 1: VS b0 (TransformationMatrix)
    // slot 2: PS t0 (texture)
    // slot 3: PS t1 (normal map)
    {
        D3D12_DESCRIPTOR_RANGE texRange = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
        D3D12_DESCRIPTOR_RANGE normRange= { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };

        D3D12_ROOT_PARAMETER params[4] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; params[0].Descriptor.ShaderRegister = 0;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; params[1].Descriptor.ShaderRegister = 0;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[2].DescriptorTable = { 1, &texRange };
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[3].DescriptorTable = { 1, &normRange };

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.ShaderRegister = 0; sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 4; rsDesc.pParameters = params;
        rsDesc.NumStaticSamplers = 1; rsDesc.pStaticSamplers = &sampler;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> blob, err;
        D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&gbufferRS_));
    }

    // --- G-Buffer PSO ---
    {
        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        IDxcBlob* vsBlob = dxCommon->CompileShader(L"Resources/shaders/object3d/Object3dVS.hlsl", L"vs_6_0");
        IDxcBlob* psBlob = dxCommon->CompileShader(L"Resources/shaders/deferred/GBufferPS.hlsl",  L"ps_6_0");

        DXGI_FORMAT rtvFormats[3] = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R8G8B8A8_UNORM,
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature        = gbufferRS_.Get();
        desc.InputLayout           = { layout, _countof(layout) };
        desc.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        desc.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        desc.RasterizerState.CullMode  = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FillMode  = D3D12_FILL_MODE_SOLID;
        desc.DepthStencilState.DepthEnable    = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.DSVFormat             = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.NumRenderTargets      = 3;
        for (int i = 0; i < 3; ++i) {
            desc.RTVFormats[i] = rtvFormats[i];
            desc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
        desc.SampleDesc.Count      = 1;
        HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&gbufferPSO_));
        assert(SUCCEEDED(hr));
    }

    // --- Lighting Root Signature ---
    // slot 0: PS b0 LightingParams
    // slot 1: PS t0 albedo, slot 2: PS t1 normal, slot 3: PS t2 material, slot 4: PS t3 depth, slot 5: PS t4 shadow
    {
        D3D12_DESCRIPTOR_RANGE ranges[5];
        for (int i = 0; i < 5; ++i) {
            ranges[i] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, (UINT)i, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
        }

        D3D12_ROOT_PARAMETER params[6] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; params[0].Descriptor.ShaderRegister = 0;
        for (int i = 0; i < 5; ++i) {
            params[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            params[i + 1].DescriptorTable = { 1, &ranges[i] };
        }

        D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ShaderRegister = 0; samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[1].ShaderRegister = 1; samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 6; rsDesc.pParameters = params;
        rsDesc.NumStaticSamplers = 2; rsDesc.pStaticSamplers = samplers;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> blob, err;
        D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&lightingRS_));
    }

    // --- Lighting PSO ---
    {
        IDxcBlob* fsVS = dxCommon->CompileShader(L"Resources/shaders/postprocess/FullscreenVS.hlsl",        L"vs_6_0");
        IDxcBlob* psBlob = dxCommon->CompileShader(L"Resources/shaders/deferred/DeferredLightingPS.hlsl",  L"ps_6_0");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = lightingRS_.Get();
        desc.VS = { fsVS->GetBufferPointer(), fsVS->GetBufferSize() };
        desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        desc.RasterizerState.CullMode  = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FillMode  = D3D12_FILL_MODE_SOLID;
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.NumRenderTargets      = 1;
        desc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
        desc.SampleDesc.Count      = 1;
        HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&lightingPSO_));
        assert(SUCCEEDED(hr));
    }

    // --- Lighting 定数バッファ ---
    {
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = (sizeof(LightingParams) + 255) & ~255u;
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&lightingCBRes_));
        lightingCBRes_->Map(0, nullptr, reinterpret_cast<void**>(&lightingCBData_));
    }
}

// ---- BeginGBuffer / EndGBuffer ----

void DeferredRenderer::BeginGBuffer()
{
    if (!enabled_) return;

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    Barrier(albedoTex_.Get(),   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Barrier(normalTex_.Get(),   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Barrier(materialTex_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

    float clearA[4] = {};
    float clearN[4] = { 0.5f, 0.5f, 0.5f, 1.0f }; // エンコード済み法線のデフォルト
    float clearM[4] = {};
    cmd->ClearRenderTargetView(albedoRtvHandle_,   clearA, 0, nullptr);
    cmd->ClearRenderTargetView(normalRtvHandle_,   clearN, 0, nullptr);
    cmd->ClearRenderTargetView(materialRtvHandle_, clearM, 0, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE gbRTVs[3] = { albedoRtvHandle_, normalRtvHandle_, materialRtvHandle_ };
    D3D12_CPU_DESCRIPTOR_HANDLE dsv       = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(3, gbRTVs, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 1 };
    D3D12_RECT sc     = { 0, 0, static_cast<LONG>(WinApp::kClientWidth), static_cast<LONG>(WinApp::kClientHeight) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
}

void DeferredRenderer::EndGBuffer()
{
    if (!enabled_) return;

    Barrier(albedoTex_.Get(),   D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Barrier(normalTex_.Get(),   D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Barrier(materialTex_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// ---- ApplyLighting ----

void DeferredRenderer::ApplyLighting(Camera* camera,
    uint32_t cascadeShadowSrvIndex,
    const Matrix4x4& cascadeVP0, const Matrix4x4& cascadeVP1, const Matrix4x4& cascadeVP2,
    float splitDist0, float splitDist1, float splitDist2, float numCascades)
{
    if (!enabled_) return;

    // 定数バッファ更新
    lightingCBData_->lightColor        = { 1,1,1,1 };
    lightingCBData_->lightDirection    = { 0.5f, -0.8f, 0.3f };
    lightingCBData_->lightIntensity    = 1.0f;
    lightingCBData_->ambientColor      = { 1,1,1 };
    lightingCBData_->ambientIntensity  = 0.3f;
    lightingCBData_->cameraWorldPos    = camera->GetTranslate();
    lightingCBData_->cascadeVP[0]      = cascadeVP0;
    lightingCBData_->cascadeVP[1]      = cascadeVP1;
    lightingCBData_->cascadeVP[2]      = cascadeVP2;
    lightingCBData_->cascadeSplits[0]  = splitDist0;
    lightingCBData_->cascadeSplits[1]  = splitDist1;
    lightingCBData_->cascadeSplits[2]  = splitDist2;
    lightingCBData_->numCascades       = numCascades;
    lightingCBData_->invViewProjection = Inverse(Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix()));
    lightingCBData_->screenW           = float(WinApp::kClientWidth);
    lightingCBData_->screenH           = float(WinApp::kClientHeight);

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    D3D12_CPU_DESCRIPTOR_HANDLE bbRtv = dxCommon_->GetCurrentBackBufferHandle();
    cmd->OMSetRenderTargets(1, &bbRtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 1 };
    D3D12_RECT sc     = { 0, 0, static_cast<LONG>(WinApp::kClientWidth), static_cast<LONG>(WinApp::kClientHeight) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    cmd->SetGraphicsRootSignature(lightingRS_.Get());
    cmd->SetPipelineState(lightingPSO_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmd->SetGraphicsRootConstantBufferView(0, lightingCBRes_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(1, srvManager_->GetGPUDescriptorHandle(albedoSrvIndex_));
    cmd->SetGraphicsRootDescriptorTable(2, srvManager_->GetGPUDescriptorHandle(normalSrvIndex_));
    cmd->SetGraphicsRootDescriptorTable(3, srvManager_->GetGPUDescriptorHandle(materialSrvIndex_));
    cmd->SetGraphicsRootDescriptorTable(4, srvManager_->GetGPUDescriptorHandle(depthSrvIndex_));
    cmd->SetGraphicsRootDescriptorTable(5, srvManager_->GetGPUDescriptorHandle(cascadeShadowSrvIndex));
    cmd->DrawInstanced(3, 1, 0, 0);
}
