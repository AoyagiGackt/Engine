/**
 * @file BloomEffect.cpp
 * @brief 明るい部分をにじませるブルーム（BloomEffect）の実装
 */
#include "BloomEffect.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "GraphicsTuning.h"
#include "EngineAssert.h"
#include "Vector2.h"
#include <iterator>

namespace engine::graphics {

using namespace Microsoft::WRL;

/** @brief リソースの状態を切り替えるバリアを積む */
void BloomEffect::Barrier(ID3D12GraphicsCommandList* cmd,
    ID3D12Resource* res,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) const
{
    DirectXCommon::TransitionBarrier(cmd, res, before, after);
}

/** @brief ブルームの途中結果を書くレンダーテクスチャを生成する */
void BloomEffect::CreateRenderTexture(ID3D12Device* device,
    SrvManager* srvManager,
    UINT width, UINT height,
    ComPtr<ID3D12Resource>& outRes,
    ComPtr<ID3D12DescriptorHeap>& outRtvHeap,
    D3D12_CPU_DESCRIPTOR_HANDLE& outRtvHandle,
    uint32_t& outSrvIndex,
    DXGI_FORMAT rtvFmt,
    DXGI_FORMAT srvFmt)
{
    // 途中結果は透明な黒で消去する。本体・RTV は同じ形式で作る
    constexpr float kTransparentBlack[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    PipelineHelper::CreateRenderTarget(device, srvManager, width, height, { rtvFmt, rtvFmt, srvFmt }, kTransparentBlack,
        { outRes, outRtvHeap, outRtvHandle, outSrvIndex });
}

/** @brief 定数バッファ1つとSRV1つを使うルートシグネチャを生成する */
ComPtr<ID3D12RootSignature> BloomEffect::CreateRS_CB_SRV(ID3D12Device* device) const
{
    auto rs = PipelineHelper::CreateConstantTextureSignature(device, D3D12_SHADER_VISIBILITY_PIXEL,
        PipelineHelper::MakeLinearClampSampler(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(rs);
    return rs;
}

/** @brief SRVを2つ使うルートシグネチャを生成する */
ComPtr<ID3D12RootSignature> BloomEffect::CreateRS_SRV2(ID3D12Device* device) const
{
    // t0 と t1 を別々のルートパラメータとして定義
    const D3D12_DESCRIPTOR_RANGE srvRange0 = PipelineHelper::MakeSrvRange(0); // t0

    const D3D12_DESCRIPTOR_RANGE srvRange1 = PipelineHelper::MakeSrvRange(1); // t1

    D3D12_ROOT_PARAMETER params[2] = { };
    params[0] = PipelineHelper::MakeSrvTableParam(&srvRange0, D3D12_SHADER_VISIBILITY_PIXEL);

    params[1] = PipelineHelper::MakeSrvTableParam(&srvRange1, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = PipelineHelper::MakeLinearClampSampler();

    auto rs = PipelineHelper::CreateRootSignature(device, params, 2,
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(rs);
    return rs;
}

/** @brief 指定したピクセルシェーダーで全画面描画用のPSOを生成する */
ComPtr<ID3D12PipelineState> BloomEffect::CreatePSO(ID3D12Device* device,
    ID3D12RootSignature* rootSig,
    IDxcBlob* vsBlob,
    IDxcBlob* psBlob,
    DXGI_FORMAT rtvFmt) const
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = PipelineHelper::MakeFullscreenPsoDesc();
    psoDesc.pRootSignature = rootSig;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RTVFormats[0] = rtvFmt;

    ComPtr<ID3D12PipelineState> pso;
    pso = PipelineHelper::CreateGraphicsPipeline(device, psoDesc);
    return pso;
}

/** @brief シーンキャプチャ用RT・ブルームピンポンRT・各パスのPSO/定数バッファを生成する */
void BloomEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    // 描画先はすべて画面と同じ大きさで作る
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();

    const UINT width = WinApp::kClientWidth;
    const UINT height = WinApp::kClientHeight;

    // シーンキャプチャ RT（GrayscaleEffect と同じ TYPELESS → RTV=SRGB, SRV=SRGB）
    constexpr PipelineHelper::RenderTargetFormats kSceneFormats = {
        DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    };
    PipelineHelper::CreateRenderTarget(device, srvManager, width, height, kSceneFormats, PipelineHelper::kOffscreenClearColor,
        { sceneTexture_, sceneRtvHeap_, sceneRtvHandle_, sceneSrvIndex_ });

    // Bloom ピンポン RT
    constexpr DXGI_FORMAT kBloomFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    for (size_t i = 0; i < std::size(bloomTex_); ++i)
    {
        CreateRenderTexture(device, srvManager,
            width, height,
            bloomTex_[i],
            bloomRtvHeap_[i],
            bloomRtvHandle_[i],
            bloomSrvIndex_[i],
            kBloomFmt, kBloomFmt);
        bloomInSrv_[i] = false; // 初期状態は RTV
    }

    // 全画面三角形の頂点シェーダーを、明部抽出・ぼかし・合成の3つのピクセルシェーダーで共有する
    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> brightPS = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/BloomBrightPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> blurPS = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/BloomBlurPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> combPS = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/BloomCombinePS.hlsl", L"ps_6_0");

    // 明部抽出とぼかしは定数1つ＋テクスチャ1枚、合成は元画像とぼかし結果の2枚を読む
    brightRS_ = CreateRS_CB_SRV(device);
    blurRS_ = CreateRS_CB_SRV(device);
    combineRS_ = CreateRS_SRV2(device);

    brightPSO_ = CreatePSO(device, brightRS_.Get(), vsBlob.Get(), brightPS.Get(), kBloomFmt);
    blurPSO_ = CreatePSO(device, blurRS_.Get(), vsBlob.Get(), blurPS.Get(), kBloomFmt);
    // Combine は SRGB バックバッファへ書き込む
    combinePSO_ = CreatePSO(device, combineRS_.Get(), vsBlob.Get(), combPS.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    // 明るさのしきい値と強さは graphics_tuning.json から読む
    brightCb_ = dxCommon->CreateBufferResource(kConstantBufferAlignment);
    brightCb_->Map(0, nullptr, reinterpret_cast<void**>(&brightCbData_));
    brightCbData_->threshold = GraphicsTuning::Float("bloom.threshold", kDefaultThreshold);
    brightCbData_->intensity = GraphicsTuning::Float("bloom.intensity", 1.0f);

    // ぼかしは1回目を横、2回目を縦にかける（テクセルの大きさは両方同じ）
    constexpr Vector2 kBlurDirections[] = { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    static_assert(std::size(kBlurDirections) == std::size(blurCb_));
    for (size_t i = 0; i < std::size(blurCb_); ++i)
    {
        blurCb_[i] = dxCommon->CreateBufferResource(kConstantBufferAlignment);
        blurCb_[i]->Map(0, nullptr, reinterpret_cast<void**>(&blurCbData_[i]));
        blurCbData_[i]->dirX = kBlurDirections[i].x;
        blurCbData_[i]->dirY = kBlurDirections[i].y;
        blurCbData_[i]->texW = 1.0f / static_cast<float>(width);
        blurCbData_[i]->texH = 1.0f / static_cast<float>(height);
    }
}

/** @brief 生成した全RT・PSO・定数バッファを解放する */
void BloomEffect::Finalize()
{
    for (size_t i = 0; i < std::size(bloomTex_); ++i)
    {
        if (blurCbData_[i])
        {
            blurCb_[i]->Unmap(0, nullptr);
            blurCbData_[i] = nullptr;
        }
        blurCb_[i].Reset();
        bloomTex_[i].Reset();
        bloomRtvHeap_[i].Reset();
    }
    if (brightCbData_)
    {
        brightCb_->Unmap(0, nullptr);
        brightCbData_ = nullptr;
    }
    brightCb_.Reset();
    combinePSO_.Reset();
    combineRS_.Reset();
    blurPSO_.Reset();
    blurRS_.Reset();
    brightPSO_.Reset();
    brightRS_.Reset();
    sceneTexture_.Reset();
    sceneRtvHeap_.Reset();
}

/** @brief シーンキャプチャ用RTをレンダーターゲットに設定する */
void BloomEffect::BeginScene()
{
    auto* cmd = dxCommon_->GetCommandList();

    if (!sceneFirstFrame_)
    {
        Barrier(cmd, sceneTexture_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    sceneFirstFrame_ = false;

    PipelineHelper::BindSceneTarget(dxCommon_, sceneRtvHandle_);
}

/** @brief シーンキャプチャ用テクスチャをSRV状態へ遷移させる */
void BloomEffect::EndScene()
{
    auto* cmd = dxCommon_->GetCommandList();
    Barrier(cmd, sceneTexture_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/** @brief ブルームの途中結果 bloomTex_[index] へ全画面パスを1回描き、描き終えたらシェーダーから参照できる状態にする */
void BloomEffect::DrawToBloomTexture(ID3D12GraphicsCommandList* cmd, SrvManager* srvManager, int index,
    ID3D12RootSignature* rootSignature, ID3D12PipelineState* pipelineState,
    D3D12_GPU_VIRTUAL_ADDRESS constants, uint32_t sourceSrvIndex)
{
    const UINT width = WinApp::kClientWidth;
    const UINT height = WinApp::kClientHeight;
    const D3D12_VIEWPORT vp = { 0.f, 0.f, (float)width, (float)height, 0.f, 1.f };
    const D3D12_RECT sc = { 0, 0, (LONG)width, (LONG)height };
    constexpr float kBlack[4] = { 0.f, 0.f, 0.f, 0.f };

    // 参照用の状態なら描画先の状態へ戻す
    if (bloomInSrv_[index])
    {
        Barrier(cmd, bloomTex_[index].Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        bloomInSrv_[index] = false;
    }

    // 黒で消してから全画面三角形を描く
    cmd->OMSetRenderTargets(1, &bloomRtvHandle_[index], FALSE, nullptr);
    cmd->ClearRenderTargetView(bloomRtvHandle_[index], kBlack, 0, nullptr);
    PipelineHelper::SetViewportAndScissor(cmd, vp, sc);
    PipelineHelper::SetTrianglePipeline(cmd, rootSignature, pipelineState);
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::FullscreenRootSlot::kConstants, constants);
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::FullscreenRootSlot::kTexture, srvManager->GetGPUDescriptorHandle(sourceSrvIndex));
    PipelineHelper::DrawFullscreenTriangle(cmd);

    // 次のパスで読めるよう参照用の状態にする
    Barrier(cmd, bloomTex_[index].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    bloomInSrv_[index] = true;
}

/** @brief 輝度抽出→水平ブラー→垂直ブラー→加算合成の4パスを実行しバックバッファへ合成する */
void BloomEffect::Apply(SrvManager* srvManager)
{
    auto* cmd = dxCommon_->GetCommandList();

    // Pass 1: 輝度抽出（シーン → bloomTex_[0]）
    DrawToBloomTexture(cmd, srvManager, 0, brightRS_.Get(), brightPSO_.Get(),
        brightCb_->GetGPUVirtualAddress(), sceneSrvIndex_);

    // Pass 2: 横ブラー（bloomTex_[0] → bloomTex_[1]）
    DrawToBloomTexture(cmd, srvManager, 1, blurRS_.Get(), blurPSO_.Get(),
        blurCb_[0]->GetGPUVirtualAddress(), bloomSrvIndex_[0]);

    // Pass 3: 縦ブラー（bloomTex_[1] → bloomTex_[0]）
    DrawToBloomTexture(cmd, srvManager, 0, blurRS_.Get(), blurPSO_.Get(),
        blurCb_[1]->GetGPUVirtualAddress(), bloomSrvIndex_[1]);

    // Pass 4: 合成（内部解像度のテクスチャを実バックバッファへレターボックス表示）
    {
        PipelineHelper::BindBackBufferTarget(dxCommon_, cmd);

        PipelineHelper::SetTrianglePipeline(cmd, combineRS_.Get(), combinePSO_.Get());
        // t0 = scene, t1 = bloom
        cmd->SetGraphicsRootDescriptorTable(PipelineHelper::BloomCompositeRootSlot::kScene, srvManager->GetGPUDescriptorHandle(sceneSrvIndex_));
        cmd->SetGraphicsRootDescriptorTable(PipelineHelper::BloomCompositeRootSlot::kBloom, srvManager->GetGPUDescriptorHandle(bloomSrvIndex_[0]));
        PipelineHelper::DrawFullscreenTriangle(cmd);
    }
}

} // namespace engine::graphics
