/**
 * @file PostEffectFullscreenPass.cpp
 * @brief 全画面ポストエフェクト共通の資源と描画（PostEffectFullscreenPass）の実装
 */
#include "PostEffectFullscreenPass.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "EngineAssert.h"
#include "WinApp.h"

namespace engine::graphics {
using namespace Microsoft::WRL;

/** @brief 全画面ポストエフェクトの共通資源（ルートシグネチャ・PSO・定数バッファ）を作り、定数バッファの書き込み先を返す */
void* PostEffectFullscreenPass::InitializeCommon(DirectXCommon* dxCommon, SrvManager* srvManager, const wchar_t* psShaderPath)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();

    // TYPELESS リソースにすることで、同一バッファを UNORM_SRGB の RTV と SRV として共有できる
    constexpr PipelineHelper::RenderTargetFormats kFormats = {
        DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    };
    PipelineHelper::CreateRenderTarget(device, srvManager, WinApp::kClientWidth, WinApp::kClientHeight,
        kFormats, PipelineHelper::kOffscreenClearColor, { sceneTexture_, rtvHeap_, rtvHandle_, srvIndex_ });

    // 効果ごとの値を書き込む定数バッファ（呼び出し側へ書き込み先を返す）

    cbResource_ = dxCommon->CreateBufferResource(kConstantBufferAlignment);
    cbResource_->Map(0, nullptr, &cbData_);

    const D3D12_DESCRIPTOR_RANGE srvRange = PipelineHelper::MakeSrvRange(0); // t0

    D3D12_ROOT_PARAMETER rootParams[2] = { };
    rootParams[0] = PipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[1] = PipelineHelper::MakeSrvTableParam(&srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = PipelineHelper::MakeLinearClampSampler();

    rootSignature_ = PipelineHelper::CreateRootSignature(device, rootParams, 2,
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(rootSignature_);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = PipelineHelper::MakeFullscreenPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    // InputLayout なし: VS が SV_VertexID から頂点座標を生成する

    pipelineState_ = PipelineHelper::CreateGraphicsPipeline(dxCommon, psoDesc,
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", psShaderPath);

    return cbData_;
}

/** @brief GPUが使用していない共通資源を解放する。呼び出し側で使用完了を保証する */
void PostEffectFullscreenPass::FinalizeCommon()
{
    PipelineHelper::ReleaseEffectResources(cbResource_, cbData_, pipelineState_, rootSignature_);
    rtvHeap_.Reset();
    sceneTexture_.Reset();
}

/** @brief オフスクリーンRTV（＋共有深度バッファ）をレンダーターゲットに設定しクリアする */
void PostEffectFullscreenPass::BeginScene()
{
    auto* cmdList = dxCommon_->GetCommandList();

    if (!isFirstFrame_)
    {
        DirectXCommon::TransitionBarrier(cmdList, sceneTexture_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    isFirstFrame_ = false;

    // 共有の深度バッファを再利用することで、3D 深度テストが正しく機能する
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmdList->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsv);

    constexpr const float (&kClearColor)[4] = PipelineHelper::kOffscreenClearColor;
    cmdList->ClearRenderTargetView(rtvHandle_, kClearColor, 0, nullptr);

    PipelineHelper::SetClientViewportAndScissor(cmdList);
}

/** @brief オフスクリーンテクスチャをピクセルシェーダーリソース状態へ遷移させる */
void PostEffectFullscreenPass::EndScene()
{
    auto* cmdList = dxCommon_->GetCommandList();

    DirectXCommon::TransitionBarrier(cmdList, sceneTexture_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/** @brief バックバッファをレンダーターゲットに戻し、オフスクリーン結果をフルスクリーンPSで合成する */
void PostEffectFullscreenPass::Apply(SrvManager* srvManager)
{
    auto* cmdList = dxCommon_->GetCommandList();

    // バックバッファをアクティブなレンダーターゲットとして復元する（フルスクリーン四角形に深度は不要）
    PipelineHelper::BindBackBufferTarget(dxCommon_, cmdList);

    PipelineHelper::SetTrianglePipeline(cmdList, rootSignature_.Get(), pipelineState_.Get());

    cmdList->SetGraphicsRootConstantBufferView(PipelineHelper::FullscreenRootSlot::kConstants, cbResource_->GetGPUVirtualAddress());
    cmdList->SetGraphicsRootDescriptorTable(PipelineHelper::FullscreenRootSlot::kTexture, srvManager->GetGPUDescriptorHandle(srvIndex_));

    // フルスクリーン三角形を描画する（頂点バッファなし、VS が SV_VertexID を使用）
    PipelineHelper::DrawFullscreenTriangle(cmdList);
}

} // namespace engine::graphics
