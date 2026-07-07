#pragma once
#include "DirectXCommon.h"
#include "GrayscaleEffect.h"
#include "ImageFilter.h"
#include "HsvFilter.h"
#include "PostEffectFullscreenPass.h" // IPostEffectSource
#include "WinApp.h"
#include <initializer_list>
namespace engine::graphics {

// 有効なポストエフェクトのオフスクリーンRTVを返す（優先順にチェックし、最初に有効なものを使う）。
// どれも無効ならバックバッファを返す。新しいポストエフェクトは呼び出し側のリストに加えるだけでよく、
// この関数自体を変更する必要はない（IPostEffectSourceによるStrategyパターン）。
inline D3D12_CPU_DESCRIPTOR_HANDLE GetActiveSceneRTVHandle(
    engine::DirectXCommon* dxCommon, std::initializer_list<IPostEffectSource*> effects)
{
    for (IPostEffectSource* effect : effects) {
        if (effect && effect->IsEnabled()) { return effect->GetSceneRTVHandle(); }
    }
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
