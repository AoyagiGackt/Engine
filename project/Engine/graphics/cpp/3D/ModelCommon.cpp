/**
 * @file ModelCommon.cpp
 * @brief 3Dモデル共通のルートシグネチャ・PSO（ModelCommon）の実装
 */
#include "ModelCommon.h"
#include "PipelineHelper.h"
#include "GraphicsTuning.h"
#include <cassert>

using namespace Microsoft::WRL;

namespace engine::graphics {
namespace PH = engine::graphics::PipelineHelper;

// シャドウパス用ラスタライザバイアス（セルフシャドウ防止）
// kShadowMapSize は ShadowManager::kShadowMapSize と一致させること
static constexpr INT kShadowDepthBias = 10;
static constexpr float kShadowSlopeScaledBias = 1.0f;

/** @brief 共通描画設定の初期化ルートシグネチャとPSOを生成する */
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

    // t0: テクスチャ / t1: シャドウマップ / t2: キューブマップ / t3: 法線マップ
    const auto ranges = PH::MakeModelTextureRanges();
    const auto rootParameters = PH::MakeModelRootParameters(ranges);

    // 静的サンプラー（s0: 通常テクスチャ、s1: シャドウマップ比較用）
    // 3Dモデルは最高解像度ミップのみ使用する（MaxLOD=0）
    auto staticSamplers = PH::MakeDefaultSamplers(0.0f);

    rootSignature_ = PH::CreateRootSignature(device, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
        staticSamplers.data(), static_cast<UINT>(staticSamplers.size()), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dPS.hlsl", L"ps_6_0");

    // 通常描画用 PSO（ブレンドモード別）

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = PH::MakeDefault3dPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { PH::kStandardInputLayout, _countof(PH::kStandardInputLayout) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    PH::CreateBlendPipelines(device, psoDesc, L"ModelCommon.Object3dPS.Blend", graphicsPipelineStates_);

    // シャドウパス用 Root Signature（CBV 1つ  TransformationMatrix）

    D3D12_ROOT_PARAMETER shadowParam[1] = {
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // VS, b0
    };

    shadowRootSignature_ = PH::CreateRootSignature(device, shadowParam, _countof(shadowParam),
        nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // シャドウパス用 PSO（深度のみ書き込み）

    Microsoft::WRL::ComPtr<IDxcBlob> shadowVsBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/ShadowVS.hlsl", L"vs_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = PH::MakeDefault3dPsoDesc();
    shadowPsoDesc.pRootSignature = shadowRootSignature_.Get();
    shadowPsoDesc.InputLayout = { PH::kStandardInputLayout, _countof(PH::kStandardInputLayout) };
    shadowPsoDesc.VS = { shadowVsBlob->GetBufferPointer(), shadowVsBlob->GetBufferSize() };
    shadowPsoDesc.PS = { nullptr, 0 }; // PSなし（深度のみ）
    shadowPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    // 影の自己遮蔽を抑えるバイアスは描画調整値から読む
    shadowPsoDesc.RasterizerState.DepthBias = GraphicsTuning::Int("shadow.depthBias", kShadowDepthBias,
        (std::numeric_limits<INT>::min)(), (std::numeric_limits<INT>::max)());
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = GraphicsTuning::Float("shadow.slopeScaledDepthBias", kShadowSlopeScaledBias);
    shadowPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPsoDesc.NumRenderTargets = 0; // RTVなし
    shadowPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;

    shadowPipelineState_ = PipelineHelper::CreateGraphicsPipeline(device, shadowPsoDesc);
}

/** @brief 描画コマンドにこの共通設定をセットする */
void ModelCommon::CommonDrawSettings(BlendMode blendMode)
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    PipelineHelper::SetTrianglePipeline(commandList, rootSignature_.Get(), graphicsPipelineStates_[static_cast<size_t>(blendMode)].Get());
}

/** @brief シャドウパス開始（シャドウ用 PSO + Root Signature をセット） */
void ModelCommon::BeginShadowPass()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    PipelineHelper::SetTrianglePipeline(commandList, shadowRootSignature_.Get(), shadowPipelineState_.Get());
}

/** @brief シャドウパス終了（通常 PSO への切り替えは次の CommonDrawSettings() で行う） */
void ModelCommon::EndShadowPass()
{
    // 通常描画への切り替えは次の CommonDrawSettings() で行う
}

} // namespace engine::graphics
