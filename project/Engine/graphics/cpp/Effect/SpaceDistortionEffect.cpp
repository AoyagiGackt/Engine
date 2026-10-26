/**
 * @file SpaceDistortionEffect.cpp
 * @brief 空間のゆがみの演出（SpaceDistortionEffect）の実装
 */
#include "SpaceDistortionEffect.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "GraphicsTuning.h"
#include "EngineAssert.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>

namespace engine::graphics {
using namespace Microsoft::WRL;

/** @brief 空間のゆがみの描画資源を用意する */
void SpaceDistortionEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
    ENGINE_ASSERT(dxCommon);
    ENGINE_ASSERT(srvManager);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    cbResource_ = dxCommon_->CreateBufferResource(kConstantBufferAlignment);
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = WarpParams { };
    cbData_->aspect = static_cast<float>(WinApp::kClientWidth) / static_cast<float>(WinApp::kClientHeight);

    // キャプチャテクスチャ（バックバッファと同フォーマット）
    captureTexture_ = pipelineHelper::CreateBackBufferCopyTexture(dxCommon_, srvManager_, captureSrvIndex_);

    CreatePipeline();
}

/** @brief 空間のゆがみの描画資源を解放する */
void SpaceDistortionEffect::Finalize() {
    if (cbData_) {
        cbResource_->Unmap(0, nullptr);
        cbData_ = nullptr;
    }
    cbResource_.Reset();
    captureTexture_.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();
}

/** @brief 歪みの中心を画面UV（0〜1）で設定する */
void SpaceDistortionEffect::SetCenterUV(float u, float v) {
    if (cbData_) {
        cbData_->centerU = u;
        cbData_->centerV = v;
    }
}

/** @brief 歪みエネルギーを加える（合計は1にクランプ） */
void SpaceDistortionEffect::AddImpulse(float amount) {
    energy_ = std::clamp(energy_ + amount, 0.0f, 1.0f);
}

/** @brief エネルギー減衰と脈動時間を進める毎フレーム呼ぶ */
void SpaceDistortionEffect::Update(float dt) {
    if (energy_ <= kMinEnergy) {
        energy_ = 0.0f;
        return;
    }

    time_ += dt;
    const float decayTau = GraphicsTuning::Positive("spaceDistortion.decayTau", kDecayTau);
    energy_ *= std::exp(-dt / decayTau);

    if (cbData_) {
        cbData_->strength = energy_;
        cbData_->time = time_;
    }
}

/** @brief エネルギーを消して非アクティブに戻す */
void SpaceDistortionEffect::Reset() {
    energy_ = 0.0f;
    time_ = 0.0f;
}

/** @brief バックバッファをキャプチャし、歪みを重ねて描画する3D描画の後に呼ぶ */
void SpaceDistortionEffect::CaptureAndApply() {
    pipelineHelper::CaptureAndDrawFullscreen(IsActive(), dxCommon_, srvManager_, captureTexture_.Get(),
        rootSignature_.Get(), pipelineState_.Get(), cbResource_.Get(), captureSrvIndex_);
}

/** @brief ルートシグネチャ・PSO・定数バッファを生成する */
void SpaceDistortionEffect::CreatePipeline() {
    rootSignature_ = pipelineHelper::CreateCapturedFullscreenSignature(dxCommon_->GetDevice());
    ENGINE_ASSERT(rootSignature_);
    pipelineState_ = pipelineHelper::CreateFullscreenPipeline(dxCommon_, rootSignature_.Get(),
        L"Resources/shaders/postprocess/SpaceDistortionPS.hlsl", BlendMode::kNone);
}

} // namespace engine::graphics
