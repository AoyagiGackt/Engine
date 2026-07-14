#include "InstancedObject3d.h"
#include "CascadedShadowMap.h"
#include "EngineAssert.h"
#include "ModelManager.h"
#include "Object3dCommon.h"
#include "ObjectMaterialLayout.h"
#include "TextureManager.h"
#include <cmath>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

void InstancedObject3d::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
    const std::string& modelPath, uint32_t maxInstances)
{
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    maxInstances_ = maxInstances;
    ID3D12Device* device = dxCommon->GetDevice();

    model_ = ModelManager::GetInstance()->FindModel(modelPath);
    ENGINE_ASSERT(model_ && "モデルが事前にロードされていません");

    // --- インスタンスバッファ (StructuredBuffer<float4x4>) ×2 フレーム分 ---
    // CPU が [frameIdx_&1] に書き込み、GPU は同じ index のバッファを Draw で読む
    // Draw 後に frameIdx_ をインクリメントすることで、次フレームの CPU 書き込みが
    // GPU 実行中のバッファと重ならない
    {
        size_t bufSize = sizeof(Matrix4x4) * maxInstances_;

        for (UINT f = 0; f < kFrameLatency; ++f) {
            D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC desc = { };
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = bufSize;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&instanceBuf_[f]));
            instanceBuf_[f]->Map(0, nullptr, reinterpret_cast<void**>(&instanceBufData_[f]));

            // SRV (StructuredBuffer<float4x4>)
            instanceSrvIndex_[f] = srvManager->Allocate();
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = maxInstances_;
            srvDesc.Buffer.StructureByteStride = sizeof(Matrix4x4);
            device->CreateShaderResourceView(instanceBuf_[f].Get(), &srvDesc,
                srvManager->GetCPUDescriptorHandle(instanceSrvIndex_[f]));
        }
    }

    // --- カメラ VP 定数バッファ ---
    {
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC desc = { };
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = (sizeof(CameraVPLayout) + 255) & ~255u;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cameraVPBuf_));
        cameraVPBuf_->Map(0, nullptr, reinterpret_cast<void**>(&cameraVPData_));
    }

    // --- マテリアル定数バッファ ---
    {
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC desc = { };
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = (sizeof(ObjectMaterialLayout) + 255) & ~255u;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&materialBuf_));
        materialBuf_->Map(0, nullptr, &materialBufData_);
        ObjectMaterialLayout defaultMat;
        // uvTransform に単位行列をセット
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                defaultMat.uvTransform.m[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
        memcpy(materialBufData_, &defaultMat, sizeof(ObjectMaterialLayout));
    }

    // --- ダミーバッファ（スロット3, 6 等のライト類） ---
    {
        D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_UPLOAD };
        D3D12_RESOURCE_DESC desc = { };
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 512;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&dummyBuf_));
    }

    // --- フォールバック 1×1×6 TextureCube (slot 5 は TextureCube t2 を宣言; useCubemap=0) ---
    // GBV は useCubemap フラグに関係なく descriptor 型をチェックするため、正しい型を提供する
    {
        D3D12_HEAP_PROPERTIES hp = { };
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = { };
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = 1;
        rd.Height = 1;
        rd.DepthOrArraySize = 6; // 6 faces
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc.Count = 1;
        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&fallbackCubemap_));
        ENGINE_ASSERT(SUCCEEDED(hr));

        fallbackCubemapSrvIdx_ = srvManager->Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = 1;
        device->CreateShaderResourceView(
            fallbackCubemap_.Get(), &srvDesc,
            srvManager->GetCPUDescriptorHandle(fallbackCubemapSrvIdx_));
    }

    CreateRootSignatureAndPSO();
}

void InstancedObject3d::CreateRootSignatureAndPSO()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    // slot 0: PS b0 material
    // slot 1: VS b0 CameraVP
    // slot 2: PS t0 texture
    // slot 3: PS b1 directional light (dummy)
    // slot 4: PS t1 CSM shadow array
    // slot 5: PS t2 cubemap (dummy)
    // slot 6: PS b2 point lights (dummy)
    // slot 7: PS t3 normal map (dummy)
    // slot 8: PS b3 cascade data
    // slot 9: VS t0 instance world matrices StructuredBuffer

    D3D12_DESCRIPTOR_RANGE texRange = { };
    texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange.NumDescriptors = 1;
    texRange.BaseShaderRegister = 0;
    texRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE shadowRange = { };
    shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors = 1;
    shadowRange.BaseShaderRegister = 1;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE cubemapRange = { };
    cubemapRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    cubemapRange.NumDescriptors = 1;
    cubemapRange.BaseShaderRegister = 2;
    cubemapRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE normalRange = { };
    normalRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    normalRange.NumDescriptors = 1;
    normalRange.BaseShaderRegister = 3;
    normalRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE instanceRange = { };
    instanceRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    instanceRange.NumDescriptors = 1;
    instanceRange.BaseShaderRegister = 0; // VS t0
    instanceRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[10] = { };
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[1].Descriptor.ShaderRegister = 0;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].DescriptorTable = { 1, &texRange };
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[3].Descriptor.ShaderRegister = 1;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[4].DescriptorTable = { 1, &shadowRange };
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[5].DescriptorTable = { 1, &cubemapRange };
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[6].Descriptor.ShaderRegister = 2;
    params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[7].DescriptorTable = { 1, &normalRange };
    params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[8].Descriptor.ShaderRegister = 3;
    params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    params[9].DescriptorTable = { 1, &instanceRange };

    D3D12_STATIC_SAMPLER_DESC samplers[2] = { };
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = samplers[0].AddressV = samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ShaderRegister = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = samplers[1].AddressV = samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = { };
    rsDesc.NumParameters = 10;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 2;
    rsDesc.pStaticSamplers = samplers;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    HRESULT rsHr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    ENGINE_ASSERT(SUCCEEDED(rsHr) && blob);
    device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs_));

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shaders/instance/InstanceVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dPS.hlsl", L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = { };
    psoDesc.pRootSignature = rs_.Get();
    psoDesc.InputLayout = { layout, _countof(layout) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.SampleDesc.Count = 1;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

// ---- SetInstanceTransform / SetInstanceMatrix ----

void InstancedObject3d::SetInstanceTransform(uint32_t i, const Transform& t)
{
    ENGINE_ASSERT(i < maxInstances_);
    instanceBufData_[frameIdx_ & 1][i] = MakeAffineMatrix(t.scale, t.rotate, t.translate);
}

void InstancedObject3d::SetInstanceMatrix(uint32_t i, const Matrix4x4& world)
{
    ENGINE_ASSERT(i < maxInstances_);
    instanceBufData_[frameIdx_ & 1][i] = world;
}

void InstancedObject3d::Update(const Matrix4x4& viewProj, const Matrix4x4& lightVP, const Vector3& cameraPos)
{
    cameraVPData_->viewProjection = viewProj;
    cameraVPData_->lightVP = lightVP;
    cameraVPData_->cameraWorldPos = cameraPos;
}

void InstancedObject3d::Draw()
{
    if (!model_ || instanceCount_ == 0) {
        ++frameIdx_;
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    cmd->SetGraphicsRootSignature(rs_.Get());
    cmd->SetPipelineState(pso_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv = model_->GetVertexBufferView();
    D3D12_INDEX_BUFFER_VIEW ibv = model_->GetIndexBufferView();
    cmd->IASetVertexBuffers(0, 1, &vbv);
    cmd->IASetIndexBuffer(&ibv);

    // slot 0: PS b0 material
    cmd->SetGraphicsRootConstantBufferView(0, materialBuf_->GetGPUVirtualAddress());
    // slot 1: VS b0 Camera VP
    cmd->SetGraphicsRootConstantBufferView(1, cameraVPBuf_->GetGPUVirtualAddress());
    // slot 2: PS t0 テクスチャ
    cmd->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandleGPU(model_->GetTextureFilePath()));
    // slot 3: PS b1 平行光源（ダミー）
    cmd->SetGraphicsRootConstantBufferView(3, dummyBuf_->GetGPUVirtualAddress());
    // slot 4: PS t1 CSM shadow
    CascadedShadowMap::GetInstance()->SetShadowMapSRV(cmd, srvManager_);
    // slot 5: PS t2 cubemap — fallback 1×1×6 TextureCube (useCubemap=0 なのでサンプリングされない)
    cmd->SetGraphicsRootDescriptorTable(5, srvManager_->GetGPUDescriptorHandle(fallbackCubemapSrvIdx_));
    // slot 6: PS b2 ポイントライト（ダミー）
    cmd->SetGraphicsRootConstantBufferView(6, dummyBuf_->GetGPUVirtualAddress());
    // slot 7: PS t3 法線マップ（テクスチャで代替）
    cmd->SetGraphicsRootDescriptorTable(7, TextureManager::GetInstance()->GetSrvHandleGPU(model_->GetTextureFilePath()));
    // slot 8: PS b3 cascade data
    CascadedShadowMap::GetInstance()->SetCascadeDataCBV(cmd, 8);
    // slot 9: VS t0 instance world matrices (今フレームのバッファ)
    cmd->SetGraphicsRootDescriptorTable(9, srvManager_->GetGPUDescriptorHandle(instanceSrvIndex_[frameIdx_ & 1]));

    cmd->DrawIndexedInstanced(
        static_cast<UINT>(model_->GetIndexCount()),
        instanceCount_, 0, 0, 0);

    // GPU がこのフレームのバッファを読んでいる間、次フレームは逆側へ書き込む
    ++frameIdx_;
}
