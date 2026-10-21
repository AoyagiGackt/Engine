/**
 * @file RenderTexture.cpp
 * @brief オフスクリーン描画用のレンダーテクスチャ（RenderTexture）の実装
 */
#include "RenderTexture.h"
#include "PipelineHelper.h"
#include "EngineAssert.h"

namespace engine::graphics {
namespace {
/** @brief このテクスチャを消去する色（描いていない所が分かるよう赤にする。作成時の最適化用の消去値と共通） */
constexpr float kClearColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
} // namespace

/** @brief 指定サイズの RTV/SRV 兼用テクスチャ（RGBA8_UNORM）を確保する */
void RenderTexture::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height)
{
    dxCommon_ = dxCommon;
    width_ = width;
    height_ = height;

    // 本体・RTV・SRV とも RGBA8_UNORM で作る
    constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    PipelineHelper::CreateRenderTarget(dxCommon_->GetDevice(), srvManager, width, height, { kFormat, kFormat, kFormat },
        kClearColor, { resource_, rtvHeap_, rtvHandle_, srvIndex_ });
}

/** @brief 確保したSRVインデックスをSrvManagerへ返却する（再生成前・破棄前に呼ぶこと） */
void RenderTexture::Finalize(SrvManager* srvManager)
{
    srvManager->Free(srvIndex_);
}

/** @brief レンダーターゲットとして設定し、ビューポート/シザーを適用して赤でクリアする */
void RenderTexture::BeginRendering()
{
    auto* cmdList = dxCommon_->GetCommandList();

    // 初回は RENDER_TARGET が初期状態なのでバリア不要
    if (!isFirstFrame_)
    {
        DirectXCommon::TransitionBarrier(cmdList, resource_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    isFirstFrame_ = false;

    // レンダーターゲットを設定して赤でクリア
    cmdList->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
    PipelineHelper::SetViewportAndScissor(cmdList, vp, scissor);

    cmdList->ClearRenderTargetView(rtvHandle_, kClearColor, 0, nullptr);
}

/** @brief 描画を終え、リソース状態を RENDER_TARGET から PIXEL_SHADER_RESOURCE へ遷移する */
void RenderTexture::EndRendering()
{
    auto* cmdList = dxCommon_->GetCommandList();

    // RENDER_TARGET → PIXEL_SHADER_RESOURCE
    DirectXCommon::TransitionBarrier(cmdList, resource_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

} // namespace engine::graphics
