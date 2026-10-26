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
#include <algorithm>
#include <cmath>

namespace engine::graphics {
namespace {

// 演出時間の下限
constexpr float kMinDuration = 0.01f;
// 斬線で割る時の、分離が終わる時刻と破片ごとの落下の遅れの上限（どちらも演出全体に対する割合）
constexpr float kMaxPhaseEnd = 0.99f;
constexpr float kMaxFallDelay = 0.95f;

}

using namespace Microsoft::WRL;

/** @brief PSO・定数バッファ等を初期化する */
void GlassShatterEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon_->GetDevice();

    // 定数バッファ
    cbResource_ = dxCommon_->CreateBufferResource(AlignConstantBufferSize(sizeof(ShatterParams)));
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = ShatterParams { };
    cbData_->crackWidth = GraphicsTuning::Float("glassShatter.crackWidth", cbData_->crackWidth);
    cbData_->impactU = GraphicsTuning::Float("glassShatter.impactU", cbData_->impactU, 0.0f, 1.0f);
    cbData_->impactV = GraphicsTuning::Float("glassShatter.impactV", cbData_->impactV, 0.0f, 1.0f);
    cbData_->shardSpeed = GraphicsTuning::Float("glassShatter.shardSpeed", cbData_->shardSpeed);
    duration_ = GraphicsTuning::Float("glassShatter.duration", kDefaultDuration, kMinDuration);

    // 斬線で割る時の分離と落下の見た目を読み、初期化前に設定された斬線も定数バッファへ送る
    cbData_->cutSeparateEnd = GraphicsTuning::Float("glassShatter.cutSeparateEnd", cbData_->cutSeparateEnd, kMinDuration, kMaxPhaseEnd);
    cbData_->cutSlide = GraphicsTuning::Float("glassShatter.cutSlide", cbData_->cutSlide);
    cbData_->cutGlowWidth = GraphicsTuning::Positive("glassShatter.cutGlowWidth", cbData_->cutGlowWidth);
    cbData_->cutGlowStrength = GraphicsTuning::Float("glassShatter.cutGlowStrength", cbData_->cutGlowStrength);
    cbData_->cutFallDelay = GraphicsTuning::Float("glassShatter.cutFallDelay", cbData_->cutFallDelay, 0.0f, kMaxFallDelay);
    cbData_->cutSpread = GraphicsTuning::Float("glassShatter.cutSpread", cbData_->cutSpread);
    SetCutLines(cutLines_);

    // フリーズテクスチャ（バックバッファと同フォーマット）
    freezeTexture_ = pipelineHelper::CreateBackBufferCopyTexture(dxCommon_, srvManager_, freezeSrvIndex_);

    rootSignature_ = pipelineHelper::CreateCapturedFullscreenSignature(device);
    ENGINE_ASSERT(rootSignature_);
    pipelineState_ = pipelineHelper::CreateFullscreenPipeline(dxCommon_, rootSignature_.Get(),
        L"Resources/shaders/postprocess/GlassShatterPS.hlsl", BlendMode::kAlpha);
}

/** @brief GPUリソースを解放する */
void GlassShatterEffect::Finalize() {
    pipelineHelper::ReleaseEffectResources(cbResource_, cbData_, pipelineState_, rootSignature_);
    freezeTexture_.Reset();
}

/** @brief 内部タイマーを進める（毎フレーム呼ぶ） */
void GlassShatterEffect::Update(float dt) {
    if (!active_ || finished_) {
        return;
    }

    timer_ += dt;
    float t = timer_ / duration_;

    if (t >= 1.0f) {
        t = 1.0f;
        finished_ = true;
        active_ = false;
    }

    if (cbData_) {
        cbData_->time = t;
    }
}

/** @brief ゲーム画面をキャプチャする（全シーン描画直後、Apply() の前に一度だけ呼ぶ） */
void GlassShatterEffect::CaptureFrame() {
    if (!freezeTexture_ || !captureNeeded_) {
        return;
    }

    auto* cmd = dxCommon_->GetCommandList();
    auto* backBuf = dxCommon_->GetCurrentBackBufferResource();

    // バックバッファ → COPY_SOURCE、フリーズテクスチャ → COPY_DEST
    pipelineHelper::CaptureRenderTarget(cmd, backBuf, freezeTexture_.Get());

    captureNeeded_ = false;
}

/** @brief バックバッファ上にエフェクトを重ねて描画する（シーン描画後に呼ぶ） */
void GlassShatterEffect::Apply() {
    if (!active_ || captureNeeded_) {
        return;
    }

    auto* cmd = dxCommon_->GetCommandList();

    // 割れた破片はバックバッファへ深度なしで描く
    pipelineHelper::BindBackBufferTarget(dxCommon_, cmd);

    // SrvManager のヒープをバインド（他の描画が上書きしている可能性があるため再セット）
    if (srvManager_) {
        ID3D12DescriptorHeap* heaps[] = { srvManager_->GetSrvDescriptorHeap() };
        cmd->SetDescriptorHeaps(1, heaps);
    }

    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(pipelineHelper::fullscreenRootSlot::kConstants, cbResource_->GetGPUVirtualAddress());

    if (srvManager_ && freezeSrvIndex_ != UINT32_MAX) {
        cmd->SetGraphicsRootDescriptorTable(
            pipelineHelper::fullscreenRootSlot::kTexture, srvManager_->GetGPUDescriptorHandle(freezeSrvIndex_));
    }

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pipelineHelper::DrawFullscreenTriangle(cmd);
}

/** @brief エフェクトを最初から再生する */
void GlassShatterEffect::Start() {
    timer_ = 0.0f;
    active_ = true;
    finished_ = false;
    captureNeeded_ = true;
    if (cbData_) {
        cbData_->time = 0.0f;
    }
}

/** @brief タイマーをリセットして非アクティブ状態に戻す */
void GlassShatterEffect::Reset() {
    timer_ = 0.0f;
    active_ = false;
    finished_ = false;
    captureNeeded_ = false;
    if (cbData_) {
        cbData_->time = 0.0f;
    }
}

/** @brief ガラスが割れる中心の位置（UV）を設定する */
void GlassShatterEffect::SetImpactUV(float u, float v) {
    if (cbData_) {
        cbData_->impactU = u;
        cbData_->impactV = v;
    }
}

/** @brief ひび割れ線の太さを設定する */
void GlassShatterEffect::SetCrackWidth(float w) {
    if (cbData_) {
        cbData_->crackWidth = w;
    }
}

/** @brief 破片が飛び散る速さを設定する */
void GlassShatterEffect::SetShardSpeed(float s) {
    if (cbData_) {
        cbData_->shardSpeed = s;
    }
}

/** @brief 斬線の通過点と法線を、定数バッファへ転送する */
void GlassShatterEffect::SetCutLines(const std::vector<CutLine>& lines) {
    // 線は初期化前でも保持する（Initialize が保持している配列自身を渡して送り直す場合は、写さずにそのまま使う）
    if (&lines != &cutLines_) {
        const size_t count = (std::min)(lines.size(), kMaxCutLines);
        cutLines_.assign(lines.begin(), lines.begin() + count);
    }
    if (!cbData_) {
        return;
    }

    // シェーダーは画面の高さを1とした座標で左右を判定するので、法線は傾きからそのまま作る（数値でない値は既定値にする）
    cbData_->cutLineCount = static_cast<uint32_t>(cutLines_.size());
    cbData_->cutLines = {};
    for (size_t index = 0; index < cutLines_.size(); ++index) {
        const auto& line = cutLines_[index];
        const float angle = std::isfinite(line.angleRadians) ? line.angleRadians : 0.0f;
        const float u = std::isfinite(line.u) ? line.u : 0.5f;
        const float v = std::isfinite(line.v) ? line.v : 0.5f;
        cbData_->cutLines[index] = { u, v, -std::sin(angle), std::cos(angle) };
    }
}

/** @brief 割れ始める点を、定数バッファへ転送する */
void GlassShatterEffect::SetImpactPoints(const std::vector<ImpactPoint>& points) {
    if (!cbData_) {
        return;
    }
    const size_t count = (std::min)(points.size(), kMaxImpacts);
    cbData_->impactCount = static_cast<uint32_t>(count);
    cbData_->impacts = {};
    for (size_t index = 0; index < count; ++index) {
        cbData_->impacts[index] = { points[index].u, points[index].v, 0.0f, 0.0f };
    }
}

/** @brief 割れた後に集まり直すかを設定する */
void GlassShatterEffect::SetReassemble(bool reassemble) {
    if (cbData_) {
        cbData_->reassemble = reassemble ? 1u : 0u;
    }
}

/** @brief 演出の再生時間を設定する */
void GlassShatterEffect::SetDuration(float seconds) {
    duration_ = seconds > kMinDuration ? seconds : kMinDuration;
}

} // namespace engine::graphics
