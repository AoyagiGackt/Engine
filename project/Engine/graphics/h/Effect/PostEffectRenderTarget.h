/**
 * @file PostEffectRenderTarget.h
 * @brief 有効なポストエフェクトから、シーンを描き込む先を選ぶ関数
 */
#pragma once
#include "DirectXCommon.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImageFilter.h"
#include "PostEffectFullscreenPass.h" // IPostEffectSource
#include "WinApp.h"
#include <initializer_list>
namespace engine::graphics {

/** @brief リストの優先順で最初の有効なポストエフェクトを返す */
inline IPostEffectSource* FindActivePostEffect(const std::initializer_list<IPostEffectSource*>& effects)
{
    for (IPostEffectSource* effect : effects)
    {
        if (effect && effect->IsEnabled())
        {
            return effect;
        }
    }
    return nullptr;
}

/** @brief 有効なエフェクトの描画先を返し、無ければバックバッファを使う */
inline D3D12_CPU_DESCRIPTOR_HANDLE GetActiveSceneRTVHandle(
    engine::DirectXCommon* dxCommon, const std::initializer_list<IPostEffectSource*>& effects)
{
    const IPostEffectSource* effect = FindActivePostEffect(effects);
    return effect ? effect->GetSceneRTVHandle() : dxCommon->GetCurrentBackBufferHandle();
}

// RTV/DSVとビューポート・シザーをセットしてメイン描画先を確定する
// rtv がバックバッファそのものの場合（＝ポストエフェクトが無効で直接描画するケース）は、
// バックバッファ用深度を使い、縦横比を保って画面内の最大サイズへ拡大する
// （オフスクリーンテクスチャへ描く場合は元々サイズが一致しているので内部解像度のまま）
inline void SetupSceneRenderTarget(engine::DirectXCommon* dxCommon, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();
    const bool targetsBackBuffer = (rtv.ptr == dxCommon->GetCurrentBackBufferHandle().ptr);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = targetsBackBuffer
        ? dxCommon->GetBackBufferDsvHandle() : dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    if (targetsBackBuffer)
    {
        D3D12_VIEWPORT vp = dxCommon->GetCenteredClientViewport();
        D3D12_RECT scissor = dxCommon->GetCenteredClientScissorRect();
        commandList->RSSetViewports(1, &vp);
        commandList->RSSetScissorRects(1, &scissor);
    }
    else
    {
        D3D12_VIEWPORT vp = { 0, 0,
            static_cast<float>(engine::WinApp::kClientWidth), static_cast<float>(engine::WinApp::kClientHeight),
            0.0f, 1.0f };
        D3D12_RECT scissor = { 0, 0, engine::WinApp::kClientWidth, engine::WinApp::kClientHeight };
        commandList->RSSetViewports(1, &vp);
        commandList->RSSetScissorRects(1, &scissor);
    }
}

} // namespace engine::graphics
