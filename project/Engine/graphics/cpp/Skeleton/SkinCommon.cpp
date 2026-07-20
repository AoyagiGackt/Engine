#include "SkinCommon.h"
#include "PipelineHelper.h"
#include <cassert>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

namespace PH = engine::graphics::PipelineHelper;

void SkinCommon::Initialize(DirectXCommon* dxCommon)
{
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

    D3D12_DESCRIPTOR_RANGE texRange = PH::MakeSrvRange(0); // t0
    D3D12_DESCRIPTOR_RANGE shadowRange = PH::MakeSrvRange(1); // t1
    D3D12_DESCRIPTOR_RANGE cubemapRange = PH::MakeSrvRange(2); // t2
    D3D12_DESCRIPTOR_RANGE normalMapRange = PH::MakeSrvRange(3); // t3

    D3D12_ROOT_PARAMETER rootParameters[9] = {
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL), // 0: マテリアル (PS, b0)
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // 1: 変換行列 (VS, b0)
        PH::MakeSrvTableParam(&texRange, D3D12_SHADER_VISIBILITY_PIXEL), // 2: テクスチャ (PS, t0)
        PH::MakeCbvParam(1, D3D12_SHADER_VISIBILITY_PIXEL), // 3: 平行光源 (PS, b1)
        PH::MakeSrvTableParam(&shadowRange, D3D12_SHADER_VISIBILITY_PIXEL), // 4: シャドウマップ (PS, t1)
        PH::MakeSrvTableParam(&cubemapRange, D3D12_SHADER_VISIBILITY_PIXEL), // 5: キューブマップテクスチャ (PS, t2)
        PH::MakeCbvParam(1, D3D12_SHADER_VISIBILITY_VERTEX), // 6: スキニングパレット (VS, b1)
        PH::MakeCbvParam(2, D3D12_SHADER_VISIBILITY_PIXEL), // 7: ポイントライト配列 (PS, b2)
        PH::MakeSrvTableParam(&normalMapRange, D3D12_SHADER_VISIBILITY_PIXEL), // 8: 法線マップ (PS, t3)
    };

    // スキンメッシュも3Dモデルと同じく最高解像度ミップのみ使用する（MaxLOD=0）
    auto staticSamplers = PH::MakeDefaultSamplers(0.0f);

    D3D12_ROOT_SIGNATURE_DESC rsDesc { };
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rsDesc.pParameters = rootParameters;
    rsDesc.NumParameters = _countof(rootParameters); // スロット 0〜8
    rsDesc.pStaticSamplers = staticSamplers.data();
    rsDesc.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
    rootSignature_ = PH::CreateRootSignature(device, rsDesc);

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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = PH::MakeDefault3dPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    for (int i = 0; i < static_cast<int>(BlendMode::Count); ++i) {
        psoDesc.BlendState.RenderTarget[0] = PH::MakeBlendDesc(static_cast<BlendMode>(i));
        device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&graphicsPipelineStates_[i]));
        const std::wstring name = L"SkinCommon.Object3dPS.Blend" + std::to_wstring(i);
        graphicsPipelineStates_[i]->SetName(name.c_str());
    }
}

void SkinCommon::CommonDrawSettings(BlendMode blendMode)
{
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(graphicsPipelineStates_[static_cast<size_t>(blendMode)].Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
