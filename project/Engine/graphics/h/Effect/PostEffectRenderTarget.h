#pragma once
#include "DirectXCommon.h"
#include "GrayscaleEffect.h"
#include "ImageFilter.h"
#include "HsvFilter.h"
#include "WinApp.h"
namespace engine::graphics {

// 有効なポストエフェクトのオフスクリーンRTVを返す。どれも無効ならバックバッファを返す
inline D3D12_CPU_DESCRIPTOR_HANDLE GetActiveSceneRTVHandle(
    engine::DirectXCommon* dxCommon, ImageFilter* imageFilter,
    GrayscaleEffect* grayscaleEffect, HsvFilter* hsvFilter)
{
    if (imageFilter->IsEnabled())     { return imageFilter->GetSceneRTVHandle(); }
    if (grayscaleEffect->IsEnabled()) { return grayscaleEffect->GetSceneRTVHandle(); }
    if (hsvFilter->IsEnabled())       { return hsvFilter->GetSceneRTVHandle(); }
    return dxCommon->GetCurrentBackBufferHandle();
}

// RTV/DSVとビューポート・シザーをセットしてメイン描画先を確定する
inline void SetupSceneRenderTarget(engine::DirectXCommon* dxCommon, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0, 0,
        static_cast<float>(engine::WinApp::kClientWidth), static_cast<float>(engine::WinApp::kClientHeight),
        0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, engine::WinApp::kClientWidth, engine::WinApp::kClientHeight };
    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &scissor);
}

} // namespace engine::graphics
