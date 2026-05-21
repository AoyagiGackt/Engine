#include "RenderTexture.h"
#include <cassert>

void RenderTexture::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height)
{
    dxCommon_ = dxCommon;
    width_    = width;
    height_   = height;

    constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format   = kFormat;
    clearValue.Color[0] = 1.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width            = width;
    desc.Height           = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.Format           = kFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue, IID_PPV_ARGS(&resource_));
    assert(SUCCEEDED(hr));

    // RTV ヒープ（1スロット）
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    hr = dxCommon_->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_));
    assert(SUCCEEDED(hr));

    // RTV 作成
    rtvHandle_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format        = kFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    dxCommon_->GetDevice()->CreateRenderTargetView(resource_.Get(), &rtvDesc, rtvHandle_);

    // SRV 作成（SrvManager 経由）
    srvIndex_ = srvManager->Allocate();
    srvManager->CreateSRVforTexture2D(srvIndex_, resource_.Get(), kFormat, 1);
}

void RenderTexture::BeginRendering()
{
    auto* cmdList = dxCommon_->GetCommandList();

    // 初回は RENDER_TARGET が初期状態なのでバリア不要
    if (!isFirstFrame_) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = resource_.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmdList->ResourceBarrier(1, &barrier);
    }
    isFirstFrame_ = false;

    // レンダーターゲットを設定して赤でクリア
    cmdList->OMSetRenderTargets(1, &rtvHandle_, FALSE, nullptr);

    D3D12_VIEWPORT vp     = { 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
    D3D12_RECT     scissor = { 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &scissor);

    constexpr float kRed[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    cmdList->ClearRenderTargetView(rtvHandle_, kRed, 0, nullptr);
}

void RenderTexture::EndRendering()
{
    auto* cmdList = dxCommon_->GetCommandList();

    // RENDER_TARGET → PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = resource_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);
}
