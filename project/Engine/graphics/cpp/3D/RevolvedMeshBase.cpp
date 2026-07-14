#include "RevolvedMeshBase.h"
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include "SrvManager.h"
#include "TextureManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace Microsoft::WRL;

void RevolvedMeshBase::SetupCommonBuffers(DirectXCommon* dxCommon, const std::string& textureFilePath,
    int divisions, size_t materialCBSize, void** mappedMaterialOut)
{
    ENGINE_ASSERT(dxCommon);
    ENGINE_ASSERT(divisions >= 3);
    dxCommon_ = dxCommon;
    textureFilePath_ = textureFilePath;
    divisions_ = divisions;
    vertexCount_ = divisions_ * 6;

    TextureManager::GetInstance()->LoadTexture(textureFilePath_);

    materialBuffer_ = dxCommon_->CreateBufferResource(materialCBSize);
    materialBuffer_->Map(0, nullptr, mappedMaterialOut);

    vertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * vertexCount_);
    vertexBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertexCount_);
    vbv_.StrideInBytes = sizeof(VertexData);
}

void RevolvedMeshBase::Update(Camera* camera)
{
    Matrix4x4 world = MakeAffineMatrix({ scale_, scale_, scale_ }, rotation_, position_);
    Matrix4x4 viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    SetWVP(Multiply(world, viewProj));
}

void RevolvedMeshBase::Draw()
{
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv_);

    cmd->SetGraphicsRootConstantBufferView(0, materialBuffer_->GetGPUVirtualAddress());

    SrvManager::GetInstance()->PreDraw();
    D3D12_GPU_DESCRIPTOR_HANDLE texHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);
    cmd->SetGraphicsRootDescriptorTable(1, texHandle);

    cmd->DrawInstanced(vertexCount_, 1, 0, 0);
}

void RevolvedMeshBase::CreatePipelineCommon(const std::wstring& vsPath, const std::wstring& psPath,
    D3D12_TEXTURE_ADDRESS_MODE addressV)
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_DESCRIPTOR_RANGE texRange[1] = { };
    texRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texRange[0].NumDescriptors = 1;
    texRange[0].BaseShaderRegister = 0; // t0
    texRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = { };
    // [0] CBV b0（WVP + カラー、派生クラスによってはアルファ参照値なども含む）
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rootParams[0].Descriptor.ShaderRegister = 0;
    // [1] SRV t0 (テクスチャ)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].DescriptorTable.pDescriptorRanges = texRange;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler = { };
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = addressV;
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
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(vsPath, L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(psPath, L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputElems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = { };
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputElems, _countof(inputElems) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    // 加算ブレンド（発光/エフェクト向け）
    auto& rt = psoDesc.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_ONE;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    // エフェクトはカリングなし・深度書き込みなし
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}
