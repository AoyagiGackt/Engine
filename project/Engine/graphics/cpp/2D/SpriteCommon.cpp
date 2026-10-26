/**
 * @file SpriteCommon.cpp
 * @brief スプライト共通のルートシグネチャ・PSO（SpriteCommon）の実装
 */
#include "SpriteCommon.h"
#include "EngineAssert.h"
#include "PipelineHelper.h"

using namespace Microsoft::WRL;

namespace engine::graphics {
namespace ph = engine::graphics::pipelineHelper;

/** @brief 2D描画共通設定の初期化PSOの生成やルートシグネチャの作成を行う */
void SpriteCommon::Initialize(DirectXCommon* dxCommon) {
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // スプライト専用シェーダーが使用するマテリアル・変換行列・テクスチャだけを宣言する
    D3D12_DESCRIPTOR_RANGE texRange = ph::MakeSrvRange(0); // t0

    D3D12_ROOT_PARAMETER rootParameters[3] = {
        ph::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL), // 0: マテリアル (PS, b0)
        ph::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // 1: 変換行列 (VS, b0)
        ph::MakeSrvTableParam(&texRange, D3D12_SHADER_VISIBILITY_PIXEL), // 2: テクスチャ (PS, t0)
    };

    // スプライトは全ミップを使用する
    auto staticSamplers = ph::MakeDefaultSamplers(D3D12_FLOAT32_MAX);

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature { };
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pStaticSamplers = staticSamplers.data();
    descriptionRootSignature.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
    rootSignature_ = ph::CreateRootSignature(device, descriptionRootSignature);
    ENGINE_ASSERT(rootSignature_);

    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = dxCommon_->CompileShader(L"Resources/shaders/sprite/SpritePS.hlsl", L"ps_6_0");

    // PSO の作成（アルファブレンド固定・深度テストなし）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc = ph::MakeDefault3dPsoDesc();
    graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
    graphicsPipelineStateDesc.InputLayout = { ph::kStandardInputLayout, _countof(ph::kStandardInputLayout) };
    graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.BlendState.RenderTarget[0] = ph::MakeBlendDesc(BlendMode::kAlpha);
    graphicsPipelineStateDesc.DepthStencilState.DepthEnable = FALSE; // 2Dは常に最前面に描く

    HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
        IID_PPV_ARGS(&graphicsPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
    graphicsPipelineState_->SetName(L"SpriteCommon.SpritePS");
}

/** @brief 描画コマンドに2Dスプライト用の共通設定（PSO, ルートシグネチャ, プリミティブトポロジ）をセットする */
void SpriteCommon::CommonDrawSettings() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(graphicsPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

} // namespace engine::graphics
