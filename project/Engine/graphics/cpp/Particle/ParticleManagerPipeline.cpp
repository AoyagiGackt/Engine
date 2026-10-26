/**
 * @file ParticleManagerPipeline.cpp
 * @brief ParticleManagerのルートシグネチャ・パイプラインステート・ジオメトリ生成を実装するファイル
 * @note ParticleManager のパイプラインの作成だけを ParticleManager.cpp から分けて置いている（クラスは ParticleManager のまま）
 */
#include "ParticleManager.h"
#include "PipelineHelper.h"
#include "EngineAssert.h"
#include "EngineConstants.h"
#include "TextureManager.h"
#include <cmath>
#include <d3dx12.h>
#include <numbers>
#include <random>

using namespace Microsoft::WRL;

// ══════════════════════════════════════════════════════
// パイプライン生成
// ══════════════════════════════════════════════════════

namespace engine::graphics {
/** @brief パーティクル用のルートシグネチャを生成する */
void ParticleManager::CreateRootSignature() {
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
    rootParameters[0] = pipelineHelper::MakeSrvTableParam(rangeT0, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[1] = pipelineHelper::MakeSrvTableParam(rangeT1, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = pipelineHelper::MakePixelSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    rootSignature_ = pipelineHelper::CreateRootSignature(device, rootParameters, _countof(rootParameters),
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rootSignature_);
}

//  CS ルートシグネチャ

/** @brief 粒子更新用コンピュートシェーダーのルートシグネチャ（CBV と UAV 2つ）を作る */
void ParticleManager::CreateCSRootSignature() {
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_ROOT_PARAMETER params[3] { };
    params[0] = pipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_ALL);
    params[1] = pipelineHelper::MakeUavParam(0);
    params[2] = pipelineHelper::MakeUavParam(1);

    csRootSignature_ = pipelineHelper::CreateRootSignature(device, params, _countof(params),
        nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(csRootSignature_);
}

//  CS パイプラインステート

/** @brief 粒子1枚分の板ポリゴンの頂点・インデックスバッファを作る */
void ParticleManager::CreateQuadGeometry() {
    /** @brief パーティクル描画の頂点位置とテクスチャ座標 */
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

/** @brief パーティクルの更新用のコンピュートPSOを生成する */
void ParticleManager::CreateCSPipelineState() {
    csPipelineState_ = pipelineHelper::CreateComputePipeline(dxCommon_, csRootSignature_.Get(),
        L"Resources/shaders/Particle/ParticleUpdate.CS.hlsl");
}

//  EmitParticle CS ルートシグネチャ / パイプライン

/** @brief 粒子発生用コンピュートシェーダーのルートシグネチャ（CBV と UAV）を作る */
void ParticleManager::CreateCSEmitRootSignature() {
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_ROOT_PARAMETER params[2] { };
    params[0] = pipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_ALL);
    params[1] = pipelineHelper::MakeUavParam(0);

    csEmitRootSignature_ = pipelineHelper::CreateRootSignature(device, params, _countof(params),
        nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(csEmitRootSignature_);
}

/** @brief パーティクルの発生用のコンピュートPSOを生成する */
void ParticleManager::CreateCSEmitPipelineState() {
    csEmitPipelineState_ = pipelineHelper::CreateComputePipeline(dxCommon_, csEmitRootSignature_.Get(),
        L"Resources/shaders/Particle/EmitParticle.CS.hlsl");
}

/** @brief 指定グループのエミッターデータへのポインタを返す */
Emitter* ParticleManager::GetEmitter(const std::string& name) {
    ENGINE_ASSERT(particleGroups_.contains(name));
    return particleGroups_[name].emitterData;
}

//  グラフィックスパイプラインステート

/** @brief 粒子描画用のグラフィックスパイプラインステートを作る */
void ParticleManager::CreatePipelineState() {
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineHelper::MakeFullscreenPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { pipelineHelper::kEffectMeshInputLayout, _countof(pipelineHelper::kEffectMeshInputLayout) };

    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    // RT のアルファをパーティクルのフェードアウトで上書きしない（黒くなる原因の修正）
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // 同じシェーダーを2種類のブレンドPSOの生成が済むまで保持する。
    const auto vs = dxCommon_->CompileShader(L"Resources/shaders/Particle/Particle.VS.hlsl", L"vs_6_0");
    const auto ps = dxCommon_->CompileShader(L"Resources/shaders/Particle/Particle.PS.hlsl", L"ps_6_0");
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    graphicsPipelineState_ = pipelineHelper::CreateGraphicsPipeline(device, psoDesc);

    // Alpha blend variant: DestBlend を INV_SRC_ALPHA に変えるだけ
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    graphicsPipelineStateAlpha_ = pipelineHelper::CreateGraphicsPipeline(device, psoDesc);
}

} // namespace engine::graphics
