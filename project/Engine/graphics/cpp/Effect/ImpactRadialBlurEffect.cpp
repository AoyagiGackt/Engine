/**
 * @file ImpactRadialBlurEffect.cpp
 * @brief ImpactRadialBlurEffectのバックバッファのキャプチャと放射状ブラーの描き戻しを実装するファイル
 */
#include "ImpactRadialBlurEffect.h"
#include "PipelineHelper.h"
#include "EngineAssert.h"
#include "WinApp.h"

namespace engine::graphics {
using namespace Microsoft::WRL;

namespace {
constexpr UINT kConstantBufferSize = 256; // 定数バッファは256バイト境界
} // namespace

/** @brief 放射ブラーの描画資源を用意する */
void ImpactRadialBlurEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    ENGINE_ASSERT(dxCommon);
    ENGINE_ASSERT(srvManager);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    cbResource_ = dxCommon_->CreateBufferResource(kConstantBufferSize);
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = RadialBlurParams { };

    // キャプチャテクスチャ（バックバッファと同フォーマット・同サイズ）
    captureTexture_ = PipelineHelper::CreateBackBufferCopyTexture(dxCommon_, srvManager_, captureSrvIndex_);

    CreatePipeline();
}

/** @brief ブラーの中心（画面UV 0〜1）と強さを設定する（強さ0で描画しない） */
void ImpactRadialBlurEffect::SetParams(float centerU, float centerV, float strength, int sampleCount)
{
    strength_ = strength;
    if (cbData_ != nullptr)
    {
        cbData_->centerX = centerU;
        cbData_->centerY = centerV;
        cbData_->strength = strength;
        cbData_->sampleCount = sampleCount;
    }
}

/** @brief バックバッファをキャプチャし、ブラーを掛けて描き戻す（3D描画の後、UIの前に呼ぶ） */
void ImpactRadialBlurEffect::CaptureAndApply()
{
    PipelineHelper::CaptureAndDrawFullscreen(IsActive(), dxCommon_, srvManager_, captureTexture_.Get(),
        rootSignature_.Get(), pipelineState_.Get(), cbResource_.Get(), captureSrvIndex_);
}

/** @brief 放射ブラー用のパイプラインを生成する */
void ImpactRadialBlurEffect::CreatePipeline()
{
    rootSignature_ = PipelineHelper::CreateCapturedFullscreenSignature(dxCommon_->GetDevice());
    ENGINE_ASSERT(rootSignature_);
    pipelineState_ = PipelineHelper::CreateFullscreenPipeline(dxCommon_, rootSignature_.Get(),
        L"Resources/shaders/postprocess/RadialBlurPS.hlsl", BlendMode::None);
}

} // namespace engine::graphics
