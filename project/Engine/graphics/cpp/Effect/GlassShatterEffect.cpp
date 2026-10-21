/**
 * @file GlassShatterEffect.cpp
 * @brief 画面がガラスのように割れる演出（GlassShatterEffect）の実装
 */
#include "GlassShatterEffect.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "GraphicsTuning.h"
#include "EngineAssert.h"
#include "SrvManager.h"
#include "WinApp.h"

namespace engine::graphics {
namespace {

// 演出時間の下限
constexpr float kMinDuration = 0.01f;

}

using namespace Microsoft::WRL;

/** @brief PSO・定数バッファ等を初期化する */
void GlassShatterEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon_->GetDevice();

    // 定数バッファ
    cbResource_ = dxCommon_->CreateBufferResource(kConstantBufferAlignment);
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = ShatterParams { };
    cbData_->crackWidth = GraphicsTuning::Float("glassShatter.crackWidth", cbData_->crackWidth);
    cbData_->impactU = GraphicsTuning::Float("glassShatter.impactU", cbData_->impactU, 0.0f, 1.0f);
    cbData_->impactV = GraphicsTuning::Float("glassShatter.impactV", cbData_->impactV, 0.0f, 1.0f);
    cbData_->shardSpeed = GraphicsTuning::Float("glassShatter.shardSpeed", cbData_->shardSpeed);
    duration_ = GraphicsTuning::Float("glassShatter.duration", kDefaultDuration, kMinDuration);

    // フリーズテクスチャ（バックバッファと同フォーマット）
    freezeTexture_ = PipelineHelper::CreateBackBufferCopyTexture(dxCommon_, srvManager_, freezeSrvIndex_);

    rootSignature_ = PipelineHelper::CreateCapturedFullscreenSignature(device);
    ENGINE_ASSERT(rootSignature_);
    pipelineState_ = PipelineHelper::CreateFullscreenPipeline(dxCommon_, rootSignature_.Get(),
        L"Resources/shaders/postprocess/GlassShatterPS.hlsl", BlendMode::Alpha);
}

/** @brief GPUリソースを解放する */
void GlassShatterEffect::Finalize()
{
    PipelineHelper::ReleaseEffectResources(cbResource_, cbData_, pipelineState_, rootSignature_);
    freezeTexture_.Reset();
}

/** @brief 内部タイマーを進める（毎フレーム呼ぶ） */
void GlassShatterEffect::Update(float dt)
{
    if (!active_ || finished_)
    {
        return;
    }

    timer_ += dt;
    float t = timer_ / duration_;

    if (t >= 1.0f)
    {
        t = 1.0f;
        finished_ = true;
        active_ = false;
    }

    if (cbData_)
    {
        cbData_->time = t;
    }
}

/** @brief ゲーム画面をキャプチャする（全シーン描画直後、Apply() の前に一度だけ呼ぶ） */
void GlassShatterEffect::CaptureFrame()
{
    if (!freezeTexture_ || !captureNeeded_)
    {
        return;
    }

    auto* cmd = dxCommon_->GetCommandList();
    auto* backBuf = dxCommon_->GetCurrentBackBufferResource();

    // バックバッファ → COPY_SOURCE、フリーズテクスチャ → COPY_DEST
    PipelineHelper::CaptureRenderTarget(cmd, backBuf, freezeTexture_.Get());

    captureNeeded_ = false;
}

/** @brief バックバッファ上にエフェクトを重ねて描画する（シーン描画後に呼ぶ） */
void GlassShatterEffect::Apply()
{
    if (!active_ || captureNeeded_)
    {
        return;
    }

    auto* cmd = dxCommon_->GetCommandList();

    // 割れた破片はバックバッファへ深度なしで描く
    PipelineHelper::BindBackBufferTarget(dxCommon_, cmd);

    // SrvManager のヒープをバインド（他の描画が上書きしている可能性があるため再セット）
    if (srvManager_)
    {
        ID3D12DescriptorHeap* heaps[] = { srvManager_->GetSrvDescriptorHeap() };
        cmd->SetDescriptorHeaps(1, heaps);
    }

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::FullscreenRootSlot::kConstants, cbResource_->GetGPUVirtualAddress());

    if (srvManager_ && freezeSrvIndex_ != UINT32_MAX)
    {
        cmd->SetGraphicsRootDescriptorTable(
            PipelineHelper::FullscreenRootSlot::kTexture, srvManager_->GetGPUDescriptorHandle(freezeSrvIndex_));
    }

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    PipelineHelper::DrawFullscreenTriangle(cmd);
}

/** @brief エフェクトを最初から再生する */
void GlassShatterEffect::Start()
{
    timer_ = 0.0f;
    active_ = true;
    finished_ = false;
    captureNeeded_ = true;
    if (cbData_)
    {
        cbData_->time = 0.0f;
    }
}

/** @brief タイマーをリセットして非アクティブ状態に戻す */
void GlassShatterEffect::Reset()
{
    timer_ = 0.0f;
    active_ = false;
    finished_ = false;
    captureNeeded_ = false;
    if (cbData_)
    {
        cbData_->time = 0.0f;
    }
}

/** @brief ガラスが割れる中心の位置（UV）を設定する */
void GlassShatterEffect::SetImpactUV(float u, float v)
{
    if (cbData_)
    {
        cbData_->impactU = u;
        cbData_->impactV = v;
    }
}

/** @brief ひび割れ線の太さを設定する */
void GlassShatterEffect::SetCrackWidth(float w)
{
    if (cbData_)
    {
        cbData_->crackWidth = w;
    }
}

/** @brief 破片が飛び散る速さを設定する */
void GlassShatterEffect::SetShardSpeed(float s)
{
    if (cbData_)
    {
        cbData_->shardSpeed = s;
    }
}

/** @brief 演出の再生時間を設定する */
void GlassShatterEffect::SetDuration(float seconds)
{
    duration_ = seconds > kMinDuration ? seconds : kMinDuration;
}

} // namespace engine::graphics
