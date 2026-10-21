/**
 * @file VignetteEffect.cpp
 * @brief 画面の四隅を暗くするビネット（VignetteEffect）の実装
 */
#include "VignetteEffect.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "GraphicsTuning.h"
#include "EngineAssert.h"
#include "WinApp.h"

namespace engine::graphics {
using namespace Microsoft::WRL;

/** @brief 定数バッファ・ルートシグネチャ・PSOを生成する */
void VignetteEffect::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // 定数バッファ
    cbResource_ = dxCommon_->CreateBufferResource(kConstantBufferAlignment);
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = VignetteParams { };
    cbData_->intensity = GraphicsTuning::Float("vignette.intensity", cbData_->intensity);
    cbData_->radius = GraphicsTuning::Float("vignette.radius", cbData_->radius, 0.0f, 1.0f);
    cbData_->softness = GraphicsTuning::Positive("vignette.softness", cbData_->softness);

    // ルートシグネチャ  b0 CBV のみ（SRV 不要、数式で暗転するだけ）
    D3D12_ROOT_PARAMETER rootParam = PipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL);

    rootSignature_ = PipelineHelper::CreateRootSignature(device, &rootParam, 1,
        nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(rootSignature_);

    pipelineState_ = PipelineHelper::CreateFullscreenPipeline(dxCommon_, rootSignature_.Get(),
        L"Resources/shaders/postprocess/VignettePS.hlsl", BlendMode::Alpha);
}

/** @brief 生成したリソース（定数バッファ・PSO・ルートシグネチャ）を解放する */
void VignetteEffect::Finalize()
{
    PipelineHelper::ReleaseEffectResources(cbResource_, cbData_, pipelineState_, rootSignature_);
}

/** @brief バックバッファに周辺減光をアルファブレンドで重ね描きする */
void VignetteEffect::Apply()
{
    if (!enabled_)
    {
        return;
    }

    auto* cmdList = dxCommon_->GetCommandList();

    // バックバッファへ深度なしで重ね描きする
    PipelineHelper::BindBackBufferTarget(dxCommon_, cmdList);

    cmdList->SetGraphicsRootSignature(rootSignature_.Get());
    cmdList->SetPipelineState(pipelineState_.Get());
    cmdList->SetGraphicsRootConstantBufferView(PipelineHelper::FullscreenRootSlot::kConstants, cbResource_->GetGPUVirtualAddress());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    PipelineHelper::DrawFullscreenTriangle(cmdList);
}

/** @brief 暗転の強さを設定する */
void VignetteEffect::SetIntensity(float v)
{
    if (cbData_)
    {
        cbData_->intensity = v;
    }
}
/** @brief 暗転の強さを取得する */
float VignetteEffect::GetIntensity() const {
    return cbData_ ? cbData_->intensity : 0.f;
}

/** @brief 減光が始まる画面中心からの半径を設定する */
void VignetteEffect::SetRadius(float v)
{
    if (cbData_)
    {
        cbData_->radius = v;
    }
}
/** @brief 減光が始まる半径を取得する */
float VignetteEffect::GetRadius() const {
    return cbData_ ? cbData_->radius : 0.f;
}

/** @brief 明部から暗部への遷移のぼかし幅を設定する */
void VignetteEffect::SetSoftness(float v)
{
    if (cbData_)
    {
        cbData_->softness = v;
    }
}
/** @brief 遷移のぼかし幅を取得する */
float VignetteEffect::GetSoftness() const {
    return cbData_ ? cbData_->softness : 0.f;
}

} // namespace engine::graphics
