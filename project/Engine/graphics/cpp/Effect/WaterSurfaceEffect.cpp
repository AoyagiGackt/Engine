/** @file WaterSurfaceEffect.cpp
 * @brief 水の背景キャプチャと全画面合成を実装する */
#include "WaterSurfaceEffect.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <algorithm>

namespace engine::graphics {
namespace {
// コピー画像は初期化時に確保し、一般的な最大ウィンドウ寸法まで再利用する。
constexpr UINT kCaptureWidth = 3840;
constexpr UINT kCaptureHeight = 2160;
constexpr UINT kFirst = 0;
constexpr UINT kOne = 1;
constexpr float kZero = 0.0f;
constexpr float kUnit = 1.0f;
}

/** @brief シーンの終了時に定数バッファの永続マップを閉じる */
WaterSurfaceEffect::~WaterSurfaceEffect() {
    if (mapped_) {
        constants_->Unmap(kFirst, nullptr);
    }
}

/** @brief PSO、背景画像、永続マップする定数を初期化する */
void WaterSurfaceEffect::Initialize(engine::DirectXCommon* dxCommon, SrvManager* srvManager) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    auto* device = dxCommon_->GetDevice();
    const auto sourceDesc = dxCommon_->GetCurrentBackBufferResource()->GetDesc();
    auto captureDesc = pipelineHelper::MakeRenderTextureDesc(
        (std::max)(kCaptureWidth, static_cast<UINT>(sourceDesc.Width)),
        (std::max)(kCaptureHeight, sourceDesc.Height), sourceDesc.Format);
    captureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    captureTexture_ = pipelineHelper::CreateDefaultResource(device, captureDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    captureSrv_ = srvManager_->Allocate();
    // 描画先と同じ色空間で背景を読む。
    srvManager_->CreateSrvForTexture2D(captureSrv_, captureTexture_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kOne);
    const pipelineHelper::RenderTargetFormats kFormats = {
        DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    };
    pipelineHelper::CreateRenderTarget(device, srvManager_, engine::WinApp::kClientWidth, engine::WinApp::kClientHeight,
        kFormats, pipelineHelper::kOffscreenClearColor,
        { backgroundTexture_, backgroundRtvHeap_, backgroundRtv_, backgroundSrv_ }, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // GPUが参照中のフレームへCPUが上書きしないよう画像番号ごとに領域を持つ。
    constantStride_ = AlignConstantBufferSize(sizeof(Parameters));
    constants_ = dxCommon_->CreateBufferResource(constantStride_ * dxCommon_->GetBufferCount());
    constants_->Map(kFirst, nullptr, reinterpret_cast<void**>(&mapped_));
    rootSignature_ = pipelineHelper::CreateCapturedFullscreenSignature(device);
    pipelineState_ = pipelineHelper::CreateFullscreenPipeline(dxCommon_, rootSignature_.Get(),
        L"Resources/shaders/postprocess/WaterSurfacePS.hlsl", BlendMode::kAlpha);
}

/** @brief 有効なポストエフェクトの内部画像に代わる背景を準備する */
void WaterSurfaceEffect::BeginBackground(bool targetsBackBuffer) {
    usesBackground_ = !targetsBackBuffer;
    if (!usesBackground_) {
        return;
    }
    auto* cmd = dxCommon_->GetCommandList();
    const auto barrier = engine::DirectXCommon::MakeTransitionBarrier(backgroundTexture_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(kOne, &barrier);
    // 背景と後続の役者で同じ深度バッファを共有する。
    const auto dsv = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(kOne, &backgroundRtv_, FALSE, &dsv);
    cmd->ClearRenderTargetView(backgroundRtv_, pipelineHelper::kOffscreenClearColor, kFirst, nullptr);
}

/** @brief シミュレーションから届いた定数だけを書き換える */
void WaterSurfaceEffect::Update(const Parameters& parameters) {
    const size_t frameIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
    auto* frameParameters = reinterpret_cast<Parameters*>(mapped_ + frameIndex * constantStride_);
    *frameParameters = parameters;
}

/** @brief 完成した背景を参照して水のある画素へ屈折を適用する */
void WaterSurfaceEffect::Draw() {
    auto* cmd = dxCommon_->GetCommandList();
    const size_t frameIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
    auto* frameParameters = reinterpret_cast<Parameters*>(mapped_ + frameIndex * constantStride_);
    uint32_t imageSrv = backgroundSrv_;
    if (usesBackground_) {
        const auto barrier = engine::DirectXCommon::MakeTransitionBarrier(backgroundTexture_.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(kOne, &barrier);
        frameParameters->captureUv = { kZero, kZero, kUnit, kUnit };
    } else {
        // 背景を描き終えたバックバッファだけを写し、余分な領域はサンプルしない。
        auto* source = dxCommon_->GetCurrentBackBufferResource();
        const auto sourceDesc = source->GetDesc();
        const auto copyDesc = captureTexture_->GetDesc();
        if (sourceDesc.Width > copyDesc.Width || sourceDesc.Height > copyDesc.Height) {
            return;
        }
        constexpr UINT kBarrierCount = 2;
        const D3D12_RESOURCE_BARRIER barriers[kBarrierCount] = {
            engine::DirectXCommon::MakeTransitionBarrier(source, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
            engine::DirectXCommon::MakeTransitionBarrier(captureTexture_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
        };
        cmd->ResourceBarrier(kBarrierCount, barriers);
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = captureTexture_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION origin{};
        origin.pResource = source;
        origin.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cmd->CopyTextureRegion(&destination, kFirst, kFirst, kFirst, &origin, nullptr);
        const D3D12_RESOURCE_BARRIER restore[kBarrierCount] = {
            engine::DirectXCommon::MakeTransitionBarrier(source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
            engine::DirectXCommon::MakeTransitionBarrier(captureTexture_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
        };
        cmd->ResourceBarrier(kBarrierCount, restore);
        const auto viewport = dxCommon_->GetCenteredClientViewport();
        frameParameters->captureUv = { viewport.TopLeftX / static_cast<float>(copyDesc.Width), viewport.TopLeftY / static_cast<float>(copyDesc.Height),
            viewport.Width / static_cast<float>(copyDesc.Width), viewport.Height / static_cast<float>(copyDesc.Height) };
        imageSrv = captureSrv_;
    }
    // 呼び出し元が復帰した描画先とビューポートを維持して描く。
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetSrvDescriptorHeap() };
    cmd->SetDescriptorHeaps(kOne, heaps);
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->SetPipelineState(pipelineState_.Get());
    cmd->SetGraphicsRootConstantBufferView(pipelineHelper::fullscreenRootSlot::kConstants,
        constants_->GetGPUVirtualAddress() + frameIndex * constantStride_);
    cmd->SetGraphicsRootDescriptorTable(pipelineHelper::fullscreenRootSlot::kTexture, srvManager_->GetGPUDescriptorHandle(imageSrv));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pipelineHelper::DrawFullscreenTriangle(cmd);
}
} // namespace engine::graphics
