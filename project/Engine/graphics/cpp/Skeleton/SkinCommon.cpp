/**
 * @file SkinCommon.cpp
 * @brief スキンメッシュ共通のルートシグネチャ・PSO（SkinCommon）の実装
 */
#include "SkinCommon.h"
#include "PipelineHelper.h"
#include <cassert>

using namespace Microsoft::WRL;

namespace engine::graphics {
namespace ph = engine::graphics::pipelineHelper;

/** @brief ルートシグネチャと SkinnedVS/Object3dPS を使った PSO 一式（各ブレンドモード分）を構築する */
void SkinCommon::Initialize(DirectXCommon* dxCommon) {
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // Root Signature (ModelCommon と同一 + スロット 6, 7, 8 追加)
    // スロット 0 (PS, b0) : マテリアル
    // スロット 1 (VS, b0) : 変換行列
    // スロット 2 (PS, t0) : テクスチャ SRV
    // スロット 3 (PS, b1) : 平行光源
    // スロット 4 (PS, t1) : シャドウマップ SRV
    // スロット 5 (PS, t2) : キューブマップテクスチャ SRV
    // スロット 6 (VS, b1) : スキニングパレット CBV
    // スロット 7 (PS, b2) : ポイントライト配列
    // スロット 8 (PS, t3) : 法線マップ SRV

    // t0: テクスチャ / t1: シャドウマップ / t2: キューブマップ / t3: 法線マップ
    const auto ranges = ph::MakeModelTextureRanges();
    const auto modelParameters = ph::MakeModelRootParameters(ranges);
    // スロット6へスキニングパレットを挿入し、ライトと法線マップを後ろへ送る。
    const std::array<D3D12_ROOT_PARAMETER, ph::kModelRootParameterCount + 1> rootParameters = {
        modelParameters[0], modelParameters[1], modelParameters[2],
        modelParameters[3], modelParameters[4], modelParameters[5],
        ph::MakeCbvParam(1, D3D12_SHADER_VISIBILITY_VERTEX), // 6: スキニングパレット (VS, b1)
        modelParameters[6], modelParameters[7],
    };

    // スキンメッシュも3Dモデルと同じく最高解像度ミップのみ使用する（MaxLOD=0）
    auto staticSamplers = ph::MakeDefaultSamplers(0.0f);

    rootSignature_ = ph::CreateRootSignature(device, rootParameters.data(), static_cast<UINT>(rootParameters.size()),
        staticSamplers.data(), static_cast<UINT>(staticSamplers.size()), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // 入力レイアウト（スキニング用: 法線+ボーン情報追加）
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Resources/shaders/skinned/SkinnedVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dPS.hlsl", L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = ph::MakeDefault3dPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    ph::CreateBlendPipelines(device, psoDesc, L"SkinCommon.Object3dPS.Blend", graphicsPipelineStates_);
}

/** @brief 指定ブレンドモードのルートシグネチャ・PSO・プリミティブトポロジをコマンドリストにセットする */
void SkinCommon::CommonDrawSettings(BlendMode blendMode) {
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    pipelineHelper::SetTrianglePipeline(cmd, rootSignature_.Get(), graphicsPipelineStates_[static_cast<size_t>(blendMode)].Get());
}

} // namespace engine::graphics
