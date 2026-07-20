#include "SpriteCommon.h"
#include "EngineAssert.h"
#include "PipelineHelper.h"
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

namespace PH = engine::graphics::PipelineHelper;

void SpriteCommon::Initialize(DirectXCommon* dxCommon)
{
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // スプライト専用シェーダーが使用するマテリアル・変換行列・テクスチャだけを宣言する
    D3D12_DESCRIPTOR_RANGE texRange = PH::MakeSrvRange(0); // t0

    D3D12_ROOT_PARAMETER rootParameters[3] = {
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL), // 0: マテリアル (PS, b0)
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // 1: 変換行列 (VS, b0)
        PH::MakeSrvTableParam(&texRange, D3D12_SHADER_VISIBILITY_PIXEL), // 2: テクスチャ (PS, t0)
    };

    // スプライトは全ミップを使用する
    auto staticSamplers = PH::MakeDefaultSamplers(D3D12_FLOAT32_MAX);

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature { };
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pStaticSamplers = staticSamplers.data();
    descriptionRootSignature.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
    rootSignature_ = PH::CreateRootSignature(device, descriptionRootSignature);
    ENGINE_ASSERT(rootSignature_);

    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = dxCommon_->CompileShader(L"Resources/shaders/sprite/SpritePS.hlsl", L"ps_6_0");

    // PSO の作成（アルファブレンド固定・深度テストなし）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc = PH::MakeDefault3dPsoDesc();
    graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
    graphicsPipelineStateDesc.InputLayout = { PH::kStandardInputLayout, _countof(PH::kStandardInputLayout) };
    graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.BlendState.RenderTarget[0] = PH::MakeBlendDesc(BlendMode::Alpha);
    graphicsPipelineStateDesc.DepthStencilState.DepthEnable = FALSE; // 2Dは常に最前面に描く

    HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
        IID_PPV_ARGS(&graphicsPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));
    graphicsPipelineState_->SetName(L"SpriteCommon.SpritePS");

}

void SpriteCommon::CommonDrawSettings()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(graphicsPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

}
