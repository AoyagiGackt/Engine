/**
 * @file ModelCommon.cpp
 * @brief ModelCommonが担当する処理を実装するファイル
 */
#include "ModelCommon.h"
#include "PipelineHelper.h"
#include <cassert>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

namespace PH = engine::graphics::PipelineHelper;

// シャドウパス用ラスタライザバイアス（セルフシャドウ防止）
// kShadowMapSize は ShadowManager::kShadowMapSize と一致させること
static constexpr INT kShadowDepthBias = 10;
static constexpr float kShadowSlopeScaledBias = 1.0f;

void ModelCommon::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();


    // 通常描画用 Root Signature
    // スロット 0 (PS, b0)   マテリアル
    // スロット 1 (VS, b0)   変換行列
    // スロット 2 (PS, t0)   テクスチャ SRV
    // スロット 3 (PS, b1)   平行光源
    // スロット 4 (PS, t1)   シャドウマップ SRV
    // スロット 5 (PS, t2)   キューブマップ SRV
    // スロット 6 (PS, b2)   ポイントライト配列
    // スロット 7 (PS, t3)   法線マップ SRV

    D3D12_DESCRIPTOR_RANGE texRange = PH::MakeSrvRange(0); // t0
    D3D12_DESCRIPTOR_RANGE shadowRange = PH::MakeSrvRange(1); // t1
    D3D12_DESCRIPTOR_RANGE cubemapRange = PH::MakeSrvRange(2); // t2
    D3D12_DESCRIPTOR_RANGE normalMapRange = PH::MakeSrvRange(3); // t3

    D3D12_ROOT_PARAMETER rootParameters[8] = {
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL), // 0: マテリアル (PS, b0)
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // 1: 変換行列 (VS, b0)
        PH::MakeSrvTableParam(&texRange, D3D12_SHADER_VISIBILITY_PIXEL), // 2: テクスチャ (PS, t0)
        PH::MakeCbvParam(1, D3D12_SHADER_VISIBILITY_PIXEL), // 3: 平行光源 (PS, b1)
        PH::MakeSrvTableParam(&shadowRange, D3D12_SHADER_VISIBILITY_PIXEL), // 4: シャドウマップ (PS, t1)
        PH::MakeSrvTableParam(&cubemapRange, D3D12_SHADER_VISIBILITY_PIXEL), // 5: TextureCube (PS, t2) ― 天球キューブマップ用
        PH::MakeCbvParam(2, D3D12_SHADER_VISIBILITY_PIXEL), // 6: ポイントライト配列 (PS, b2)
        PH::MakeSrvTableParam(&normalMapRange, D3D12_SHADER_VISIBILITY_PIXEL), // 7: 法線マップ (PS, t3)
    };

    // 静的サンプラー（s0: 通常テクスチャ、s1: シャドウマップ比較用）
    // 3Dモデルは最高解像度ミップのみ使用する（MaxLOD=0）
    auto staticSamplers = PH::MakeDefaultSamplers(0.0f);

    D3D12_ROOT_SIGNATURE_DESC rsDesc { };
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rsDesc.pParameters = rootParameters;
    rsDesc.NumParameters = _countof(rootParameters); // スロット 0〜7
    rsDesc.pStaticSamplers = staticSamplers.data();
    rsDesc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
    rootSignature_ = PH::CreateRootSignature(device, rsDesc);

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dPS.hlsl", L"ps_6_0");


    // 通常描画用 PSO（ブレンドモード別）

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = PH::MakeDefault3dPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { PH::kStandardInputLayout, _countof(PH::kStandardInputLayout) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    for (int i = 0; i < static_cast<int>(BlendMode::Count); ++i) {
        psoDesc.BlendState.RenderTarget[0] = PH::MakeBlendDesc(static_cast<BlendMode>(i));
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineStates_[i]));
        const std::wstring name = L"ModelCommon.Object3dPS.Blend" + std::to_wstring(i);
        graphicsPipelineStates_[i]->SetName(name.c_str());
    }


    // シャドウパス用 Root Signature（CBV 1つ  TransformationMatrix）

    D3D12_ROOT_PARAMETER shadowParam[1] = {
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // VS, b0
    };

    D3D12_ROOT_SIGNATURE_DESC shadowRsDesc { };
    shadowRsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    shadowRsDesc.pParameters = shadowParam;
    shadowRsDesc.NumParameters = _countof(shadowParam);
    shadowRootSignature_ = PH::CreateRootSignature(device, shadowRsDesc);


    // シャドウパス用 PSO（深度のみ書き込み）

    Microsoft::WRL::ComPtr<IDxcBlob> shadowVsBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/ShadowVS.hlsl", L"vs_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = PH::MakeDefault3dPsoDesc();
    shadowPsoDesc.pRootSignature = shadowRootSignature_.Get();
    shadowPsoDesc.InputLayout = { PH::kStandardInputLayout, _countof(PH::kStandardInputLayout) };
    shadowPsoDesc.VS = { shadowVsBlob->GetBufferPointer(), shadowVsBlob->GetBufferSize() };
    shadowPsoDesc.PS = { nullptr, 0 }; // PSなし（深度のみ）
    shadowPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    shadowPsoDesc.RasterizerState.DepthBias = kShadowDepthBias;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = kShadowSlopeScaledBias;
    shadowPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPsoDesc.NumRenderTargets = 0; // RTVなし
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;

    device->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&shadowPipelineState_));
}

void ModelCommon::CommonDrawSettings(BlendMode blendMode)
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(graphicsPipelineStates_[static_cast<size_t>(blendMode)].Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ModelCommon::BeginShadowPass()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(shadowRootSignature_.Get());
    commandList->SetPipelineState(shadowPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ModelCommon::EndShadowPass()
{
    // 通常描画への切り替えは次の CommonDrawSettings() で行う
}
