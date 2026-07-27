/**
 * @file ParticleManagerPipeline.cpp
 * @brief ParticleManagerのルートシグネチャ・パイプラインステート・ジオメトリ生成を実装するファイル
 * @note ParticleManager.cppからの分割ファイルクラス自体はParticleManagerのまま、定義の置き場所だけを分けている
 */
#include "ParticleManager.h"
#include "EngineAssert.h"
#include "GameConstants.h"
#include "TextureManager.h"
#include <cmath>
#include <d3dx12.h>
#include <numbers>
#include <random>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

// ══════════════════════════════════════════════════════
// パイプライン生成
// ══════════════════════════════════════════════════════

void ParticleManager::CreateRootSignature()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_DESCRIPTOR_RANGE rangeT0[1] { };
    rangeT0[0].BaseShaderRegister = 0;
    rangeT0[0].NumDescriptors = 1;
    rangeT0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeT0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeT1[1] { };
    rangeT1[0].BaseShaderRegister = 1;
    rangeT1[0].NumDescriptors = 1;
    rangeT1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeT1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // slot 0: t0 (VS が読む instancing StructuredBuffer)
    // slot 1: t1 (PS が読む Texture2D)
    // 未使用の CBV スロット (b0/b1) は宣言しない - GBV が null アドレスを検出してクラッシュするため
    D3D12_ROOT_PARAMETER rootParameters[2] { };
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.pDescriptorRanges = rangeT0;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = rangeT1;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler { };
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc { };
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    desc.pParameters = rootParameters;
    desc.NumParameters = _countof(rootParameters);
    desc.pStaticSamplers = &sampler;
    desc.NumStaticSamplers = 1;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

//  CS ルートシグネチャ

void ParticleManager::CreateCSRootSignature()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_ROOT_PARAMETER params[3] { };
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0; // b0
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].Descriptor.ShaderRegister = 0; // u0
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].Descriptor.ShaderRegister = 1; // u1

    D3D12_ROOT_SIGNATURE_DESC desc { };
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    desc.pParameters = params;
    desc.NumParameters = _countof(params);

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_PPV_ARGS(&csRootSignature_));
}

//  CS パイプラインステート

void ParticleManager::CreateQuadGeometry()
{
    struct Vertex {
        float pos[4];
        float uv[2];
        float nrm[3];
    };

    Vertex verts[4] = {
        { { -0.5f, 0.5f, 0.f, 1.f }, { 0.f, 0.f }, { 0.f, 0.f, -1.f } },
        { { 0.5f, 0.5f, 0.f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f, -1.f } },
        { { 0.5f, -0.5f, 0.f, 1.f }, { 1.f, 1.f }, { 0.f, 0.f, -1.f } },
        { { -0.5f, -0.5f, 0.f, 1.f }, { 0.f, 1.f }, { 0.f, 0.f, -1.f } },
    };
    uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

    quadVertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(verts));
    void* mapped = nullptr;
    quadVertexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    quadVertexBuffer_->Unmap(0, nullptr);

    quadVBV_.BufferLocation = quadVertexBuffer_->GetGPUVirtualAddress();
    quadVBV_.SizeInBytes = sizeof(verts);
    quadVBV_.StrideInBytes = sizeof(Vertex);

    quadIndexBuffer_ = dxCommon_->CreateBufferResource(sizeof(indices));
    quadIndexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, indices, sizeof(indices));
    quadIndexBuffer_->Unmap(0, nullptr);

    quadIBV_.BufferLocation = quadIndexBuffer_->GetGPUVirtualAddress();
    quadIBV_.SizeInBytes = sizeof(indices);
    quadIBV_.Format = DXGI_FORMAT_R32_UINT;
}

void ParticleManager::CreateCSPipelineState()
{
    Microsoft::WRL::ComPtr<IDxcBlob> csBlob = dxCommon_->CompileShader(
        L"Resources/shaders/Particle/ParticleUpdate.CS.hlsl", L"cs_6_0");
    ENGINE_ASSERT(csBlob);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.pRootSignature = csRootSignature_.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
    HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&csPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

//  EmitParticle CS ルートシグネチャ / パイプライン

void ParticleManager::CreateCSEmitRootSignature()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_ROOT_PARAMETER params[2] { };
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister = 0; // b0 : EmitConstants
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].Descriptor.ShaderRegister = 0; // u0 : gParticles

    D3D12_ROOT_SIGNATURE_DESC desc { };
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    desc.pParameters = params;
    desc.NumParameters = _countof(params);

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &sigBlob, &errBlob);
    ENGINE_ASSERT(SUCCEEDED(hr));
    device->CreateRootSignature(0, sigBlob->GetBufferPointer(),
        sigBlob->GetBufferSize(), IID_PPV_ARGS(&csEmitRootSignature_));
}

void ParticleManager::CreateCSEmitPipelineState()
{
    Microsoft::WRL::ComPtr<IDxcBlob> csBlob = dxCommon_->CompileShader(
        L"Resources/shaders/Particle/EmitParticle.CS.hlsl", L"cs_6_0");
    ENGINE_ASSERT(csBlob);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.pRootSignature = csEmitRootSignature_.Get();
    psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };
    HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
        &psoDesc, IID_PPV_ARGS(&csEmitPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

Emitter* ParticleManager::GetEmitter(const std::string& name)
{
    ENGINE_ASSERT(particleGroups_.contains(name));
    return particleGroups_[name].emitterData;
}

//  グラフィックスパイプラインステート

void ParticleManager::CreatePipelineState()
{
    ID3D12Device* device = dxCommon_->GetDevice();

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(
        L"Resources/shaders/Particle/Particle.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(
        L"Resources/shaders/Particle/Particle.PS.hlsl", L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
            D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
            D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
            D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    // RT のアルファをパーティクルのフェードアウトで上書きしない（黒くなる原因の修正）
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

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

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // Alpha blend variant: DestBlend を INV_SRC_ALPHA に変えるだけ
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineStateAlpha_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}
