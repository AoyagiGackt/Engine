/**
 * @file ImageFilter.cpp
 * @brief ぼかし・輪郭・ディゾルブ・ノイズなどの画像フィルター（ImageFilter）の実装
 */
#include "ImageFilter.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "GraphicsTuning.h"
#include "EngineAssert.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace engine::graphics {
namespace {

// ブラーのカーネル範囲
constexpr int kMaxBlurRadius = 8;
constexpr int kBlurKernelSize = 17;
constexpr float kMinGaussianSigma = 0.01f;
constexpr float kGaussianRadiusScale = 3.0f;
constexpr int kMaxRadialSamples = 128;
constexpr int kMaxNoiseOctaves = 8;
constexpr float kDefaultNoiseLacunarity = 2.0f;
constexpr engine::Vector4 kDefaultOutlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
constexpr engine::Vector4 kDefaultDissolveColor = { 1.0f, 0.5f, 0.0f, 1.0f };

}

using namespace Microsoft::WRL;

/** @brief SRGB で描いて読むオフスクリーンの描画先テクスチャを作る（RTV と SRV を1つずつ） */
static void CreateOffscreenTexture(
    ID3D12Device* device,
    SrvManager* srvManager,
    uint32_t width, uint32_t height,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outTexture,
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& outRtvHeap,
    D3D12_CPU_DESCRIPTOR_HANDLE& outRtvHandle,
    uint32_t& outSrvIndex)
{
    // 本体は TYPELESS にして、RTV と SRV を同じ SRGB 形式で共有する
    constexpr PipelineHelper::RenderTargetFormats kFormats = {
        DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    };
    PipelineHelper::CreateRenderTarget(device, srvManager, width, height, kFormats, PipelineHelper::kOffscreenClearColor,
        { outTexture, outRtvHeap, outRtvHandle, outSrvIndex });
}

// Initialize（分割ヘルパーを呼び出すだけ）

// ══════════════════════════════════════════════════════
// 初期化とリソース生成
// ══════════════════════════════════════════════════════

/** @brief オフスクリーンテクスチャ・定数バッファ・PSO を作成して初期化する */
void ImageFilter::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();
    const uint32_t width = WinApp::kClientWidth;
    const uint32_t height = WinApp::kClientHeight;

    CreateOffscreenTexture(device, srvManager, width, height,
        sceneTexture_, sceneRtvHeap_, sceneRtvHandle_, sceneSrvIndex_);
    CreateOffscreenTexture(device, srvManager, width, height,
        intermediateTexture_, intermediateRtvHeap_, intermediateRtvHandle_, intermediateSrvIndex_);

    depthSrvIndex_ = srvManager->Allocate();
    srvManager->CreateSrvForDepthTexture(depthSrvIndex_, dxCommon->GetDepthStencilResource());

    InitConstantBuffers(dxCommon, width, height);
    InitRootSignatures(dxCommon);
    InitPipelineStates(dxCommon);

    RebuildKernel();
}

// InitConstantBuffers  各エフェクト用 CB の作成とマップ

/** @brief 調整値を読み込み、ブラーやノイズなど各フィルタの定数バッファを作る */
void ImageFilter::InitConstantBuffers(DirectXCommon* dxCommon, uint32_t width, uint32_t height)
{
    // ぼかし半径・ノイズの速さを調整値から読み、1テクセルの大きさを求める
    boxRadius_ = GraphicsTuning::Int("imageFilter.boxRadius", boxRadius_, 0, kMaxBlurRadius);
    gaussianSigma_ = GraphicsTuning::Float("imageFilter.gaussianSigma", gaussianSigma_, kMinGaussianSigma);
    noiseSpeed_ = GraphicsTuning::Float("noise.speed", noiseSpeed_);
    const float rw = 1.0f / float(width);
    const float rh = 1.0f / float(height);

    // ブラー用 CB（H と V の 2 スロット × 256 バイト = 512 バイト）
    cbResource_ = dxCommon->CreateBufferResource(2 * kConstantBufferAlignment);
    void* mapped = nullptr;
    cbResource_->Map(0, nullptr, &mapped);
    cbH_ = reinterpret_cast<FilterParams*>(mapped);
    cbV_ = reinterpret_cast<FilterParams*>(reinterpret_cast<uint8_t*>(mapped) + kConstantBufferAlignment);
    cbH_->texelSizeX = rw;
    cbH_->texelSizeY = rh;
    cbH_->dirX = 1.0f;
    cbH_->dirY = 0.0f;
    cbV_->texelSizeX = rw;
    cbV_->texelSizeY = rh;
    cbV_->dirX = 0.0f;
    cbV_->dirY = 1.0f;

    // アウトライン用 CB（1 スロット × 256 バイト）
    outlineCbResource_ = dxCommon->CreateBufferResource(kConstantBufferAlignment);
    void* outlineMapped = nullptr;
    outlineCbResource_->Map(0, nullptr, &outlineMapped);
    outlineCb_ = reinterpret_cast<OutlineParams*>(outlineMapped);
    outlineCb_->texelSizeX = rw;
    outlineCb_->texelSizeY = rh;
    outlineCb_->threshold = GraphicsTuning::Float("outline.threshold", kDefaultOutlineThreshold);
    outlineCb_->edgeStrength = GraphicsTuning::Float("outline.strength", kDefaultOutlineStrength);
    const Vector4 outlineColor = GraphicsTuning::Color("outline.color", kDefaultOutlineColor);
    outlineCb_->outlineR = outlineColor.x;
    outlineCb_->outlineG = outlineColor.y;
    outlineCb_->outlineB = outlineColor.z;
    outlineCb_->outlineA = outlineColor.w;
    outlineCb_->depthScale = GraphicsTuning::Float("outline.depthScale", kDefaultDepthScale);

    // ラジアルブラー用 CB（1 スロット × 256 バイト）
    radialBlurCbResource_ = dxCommon->CreateBufferResource(kConstantBufferAlignment);
    void* radialMapped = nullptr;
    radialBlurCbResource_->Map(0, nullptr, &radialMapped);
    radialBlurCb_ = reinterpret_cast<RadialBlurParams*>(radialMapped);
    radialBlurCb_->centerX = GraphicsTuning::Float("radialBlur.centerX", 0.5f, 0.0f, 1.0f);
    radialBlurCb_->centerY = GraphicsTuning::Float("radialBlur.centerY", 0.5f, 0.0f, 1.0f);
    radialBlurCb_->strength = GraphicsTuning::Float("radialBlur.strength", kDefaultRadialStrength);
    radialBlurCb_->sampleCount = GraphicsTuning::Int("radialBlur.sampleCount", kDefaultRadialSampleCount, 1, kMaxRadialSamples);

    // ディゾルブ用 CB（1 スロット × 256 バイト）
    // ノイズマスクは Apply() の初回 Dissolve 呼び出し時に遅延ロードする
    dissolveCbResource_ = dxCommon->CreateBufferResource(kConstantBufferAlignment);
    void* dissolveMapped = nullptr;
    dissolveCbResource_->Map(0, nullptr, &dissolveMapped);
    dissolveCb_ = reinterpret_cast<DissolveParams*>(dissolveMapped);
    dissolveCb_->threshold = GraphicsTuning::Float("dissolve.threshold", 0.0f, 0.0f, 1.0f);
    dissolveCb_->edgeWidth = GraphicsTuning::Positive("dissolve.edgeWidth", kDefaultDissolveEdgeWidth);
    const Vector4 dissolveColor = GraphicsTuning::Color("dissolve.edgeColor", kDefaultDissolveColor);
    dissolveCb_->edgeR = dissolveColor.x;
    dissolveCb_->edgeG = dissolveColor.y;
    dissolveCb_->edgeB = dissolveColor.z;
    dissolveCb_->edgeA = dissolveColor.w;

    // プロシージャルノイズ用 CB（1 スロット × 256 バイト）
    noiseGenCbResource_ = dxCommon->CreateBufferResource(kConstantBufferAlignment);
    void* noiseGenMapped = nullptr;
    noiseGenCbResource_->Map(0, nullptr, &noiseGenMapped);
    noiseGenCb_ = reinterpret_cast<NoiseGenParams*>(noiseGenMapped);
    noiseGenCb_->scaleX = GraphicsTuning::Positive("noise.scaleX", kDefaultNoiseScale);
    noiseGenCb_->scaleY = GraphicsTuning::Positive("noise.scaleY", kDefaultNoiseScale);
    noiseGenCb_->seed = 0.0f;
    noiseGenCb_->octaves = GraphicsTuning::Int("noise.octaves", kDefaultNoiseOctaves, 1, kMaxNoiseOctaves);
    noiseGenCb_->persistence = GraphicsTuning::Float("noise.persistence", 0.5f, 0.0f, 1.0f);
    noiseGenCb_->lacunarity = GraphicsTuning::Positive("noise.lacunarity", kDefaultNoiseLacunarity);
    noiseGenCb_->colorMode = 0;
    noiseGenCb_->opacity = GraphicsTuning::Float("noise.opacity", 1.0f, 0.0f, 1.0f);
}

// InitRootSignatures  ブラー用・アウトライン用 Root Signature 作成

/** @brief 各フィルタで使うルートシグネチャを作る */
void ImageFilter::InitRootSignatures(DirectXCommon* dxCommon)
{
    ID3D12Device* device = dxCommon->GetDevice();

    D3D12_STATIC_SAMPLER_DESC sampler = PipelineHelper::MakePixelSampler(D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    // ブラー用 Root Signature（b0 + t0）
    const D3D12_DESCRIPTOR_RANGE srvRange0 = PipelineHelper::MakeSrvRange(0); // t0

    D3D12_ROOT_PARAMETER blurParams[2] = { };
    blurParams[0] = PipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL);
    blurParams[1] = PipelineHelper::MakeSrvTableParam(&srvRange0, D3D12_SHADER_VISIBILITY_PIXEL);

    rootSignature_ = PipelineHelper::CreateRootSignature(device, blurParams, 2,
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(rootSignature_);

    // アウトライン / ディゾルブ用 Root Signature（b0 + t0 + t1）
    const D3D12_DESCRIPTOR_RANGE srvRange1 = PipelineHelper::MakeSrvRange(1); // t1

    D3D12_ROOT_PARAMETER outlineParams[3] = { };
    outlineParams[0] = PipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL);
    outlineParams[1] = PipelineHelper::MakeSrvTableParam(&srvRange0, D3D12_SHADER_VISIBILITY_PIXEL);
    outlineParams[2] = PipelineHelper::MakeSrvTableParam(&srvRange1, D3D12_SHADER_VISIBILITY_PIXEL);

    outlineRootSignature_ = PipelineHelper::CreateRootSignature(device, outlineParams, _countof(outlineParams),
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(outlineRootSignature_);
}

// InitPipelineStates  シェーダーコンパイル + 全 PSO 作成

/** @brief 各フィルタのシェーダーをコンパイルし、パイプラインステートを作る */
void ImageFilter::InitPipelineStates(DirectXCommon* dxCommon)
{
    ID3D12Device* device = dxCommon->GetDevice();

    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> boxPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/KernelFilterPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> gaussianPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/GaussianFilterPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> prewittPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/PrewittEdgePS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> depthOlPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/DepthOutlinePS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> radialBlurPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/RadialBlurPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> dissolvePsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/DissolvePS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> noiseGenPsBlob = dxCommon->CompileShader(L"Resources/shaders/postprocess/NoisePS.hlsl", L"ps_6_0");

    // 共通 PSO ベース（深度なし、カリングなし、フルスクリーントライアングル）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = PipelineHelper::MakeFullscreenPsoDesc();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };

    auto createPSO = [&](ID3D12RootSignature* sig, IDxcBlob* ps, ComPtr<ID3D12PipelineState>& out) {
        psoDesc.pRootSignature = sig;
        psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        out = PipelineHelper::CreateGraphicsPipeline(device, psoDesc);
    };

    // rootSignature_（b0 + t0）を使う PSO
    createPSO(rootSignature_.Get(), boxPsBlob.Get(), boxPso_);
    createPSO(rootSignature_.Get(), gaussianPsBlob.Get(), gaussianPso_);
    createPSO(rootSignature_.Get(), prewittPsBlob.Get(), prewittPso_);
    createPSO(rootSignature_.Get(), radialBlurPsBlob.Get(), radialBlurPso_);
    createPSO(rootSignature_.Get(), noiseGenPsBlob.Get(), noiseGenPso_);

    // outlineRootSignature_（b0 + t0 + t1）を使う PSO
    createPSO(outlineRootSignature_.Get(), depthOlPsBlob.Get(), depthOutlinePso_);
    createPSO(outlineRootSignature_.Get(), dissolvePsBlob.Get(), dissolvePso_);
}

// ══════════════════════════════════════════════════════
// 終了と共通描画
// ══════════════════════════════════════════════════════

/** @brief 定数バッファのマップを解除し、GPU リソースをすべて解放する */
void ImageFilter::Finalize()
{
    // 書き込み先を開いたままの定数バッファは閉じてから解放する
    auto releaseMapped = [](Microsoft::WRL::ComPtr<ID3D12Resource>& resource, auto*& mapped) {
        if (mapped)
        {
            resource->Unmap(0, nullptr);
            mapped = nullptr;
        }
        resource.Reset();
    };
    releaseMapped(noiseGenCbResource_, noiseGenCb_);
    releaseMapped(dissolveCbResource_, dissolveCb_);
    releaseMapped(radialBlurCbResource_, radialBlurCb_);
    releaseMapped(outlineCbResource_, outlineCb_);
    // 横と縦のぼかしは1つのバッファを2つに区切って使っている
    releaseMapped(cbResource_, cbH_);
    cbV_ = nullptr;

    // パイプライン・ルートシグネチャ・描画先を解放する
    noiseGenPso_.Reset();
    dissolvePso_.Reset();
    radialBlurPso_.Reset();
    depthOutlinePso_.Reset();
    outlineRootSignature_.Reset();
    prewittPso_.Reset();
    gaussianPso_.Reset();
    boxPso_.Reset();
    rootSignature_.Reset();
    intermediateRtvHeap_.Reset();
    intermediateTexture_.Reset();
    sceneRtvHeap_.Reset();
    sceneTexture_.Reset();
}

/** @brief オフスクリーンテクスチャをレンダーターゲットとしてバインドし、シーン描画を開始する */
void ImageFilter::BeginScene()
{
    auto* cmd = dxCommon_->GetCommandList();
    if (!isSceneFirstFrame_)
    {
        DirectXCommon::TransitionBarrier(cmd, sceneTexture_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    isSceneFirstFrame_ = false;

    PipelineHelper::BindSceneTarget(dxCommon_, sceneRtvHandle_);
}

/** @brief シーン描画を終了し、オフスクリーンテクスチャを SRV 状態に遷移する */
void ImageFilter::EndScene()
{
    DirectXCommon::TransitionBarrier(dxCommon_->GetCommandList(), sceneTexture_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/** @brief 現在のモードに応じたフィルターをバックバッファに適用する */
void ImageFilter::Apply(SrvManager* srvManager)
{
    auto* cmd = dxCommon_->GetCommandList();

    D3D12_VIEWPORT vp = dxCommon_->GetBackBufferViewport();
    D3D12_RECT scissor = dxCommon_->GetBackBufferScissorRect();
    D3D12_CPU_DESCRIPTOR_HANDLE backRtv = dxCommon_->GetCurrentBackBufferHandle();

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    GetFilterMode(mode_).Apply(*this, cmd, srvManager, vp, scissor, backRtv);
}

// Filter Mode Strategy  Mode ごとの Apply 実装

// ══════════════════════════════════════════════════════
// フィルタ方式別の描画
// ══════════════════════════════════════════════════════

/** @brief Prewitt のエッジ検出（輝度ベース）を1パスでバックバッファへ描く */
void ImageFilter::PrewittEdgeFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // Prewitt エッジ検出（輝度ベース）  シングルパス
    PipelineHelper::BindFullscreenPass(cmd, filter.rootSignature_.Get(), filter.prewittPso_.Get(),
        backRtv, vp, scissor, filter.outlineCbResource_->GetGPUVirtualAddress(),
        srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);
}

/** @brief 深度バッファをSRV状態に一時遷移させ、シーンと深度の2テクスチャからアウトラインを描画する */
void ImageFilter::DepthOutlineFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // 深度ベースアウトライン  シングルパス、深度バリア付き
    auto* depthRes = filter.dxCommon_->GetDepthStencilResource();
    DirectXCommon::TransitionBarrier(cmd, depthRes,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    PipelineHelper::BindFullscreenPass(cmd, filter.outlineRootSignature_.Get(), filter.depthOutlinePso_.Get(),
        backRtv, vp, scissor, filter.outlineCbResource_->GetGPUVirtualAddress(),
        srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::FullscreenRootSlot::kAuxiliaryTexture, srvManager->GetGPUDescriptorHandle(filter.depthSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);

    DirectXCommon::TransitionBarrier(cmd, depthRes,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
}

/** @brief シーンテクスチャに対して放射状サンプリングでブラーをかけて描画する */
void ImageFilter::RadialBlurFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // ラジアルブラー  シングルパス
    PipelineHelper::BindFullscreenPass(cmd, filter.rootSignature_.Get(), filter.radialBlurPso_.Get(),
        backRtv, vp, scissor, filter.radialBlurCbResource_->GetGPUVirtualAddress(),
        srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);
}

/** @brief ノイズマスクテクスチャ（初回使用時に遅延ロード）をt1にバインドして溶解境界を描画する */
void ImageFilter::DissolveFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // ディゾルブ  シングルパス、ノイズマスクを t1 にバインド
    // TextureManager が初期化済みになってから初回ロード
    if (filter.noiseSrvIndex_[0] == UINT32_MAX)
    {
        auto* texMgr = TextureManager::GetInstance();
        texMgr->LoadTexture("Resources/Effects/noise0.png");
        texMgr->LoadTexture("Resources/Effects/noise1.png");
        filter.noiseSrvIndex_[0] = texMgr->GetTextureIndexByFilePath("Resources/Effects/noise0.png");
        filter.noiseSrvIndex_[1] = texMgr->GetTextureIndexByFilePath("Resources/Effects/noise1.png");
    }
    uint32_t maskSrv = filter.noiseSrvIndex_[filter.dissolveMaskIndex_];
    PipelineHelper::BindFullscreenPass(cmd, filter.outlineRootSignature_.Get(), filter.dissolvePso_.Get(),
        backRtv, vp, scissor, filter.dissolveCbResource_->GetGPUVirtualAddress(),
        srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::FullscreenRootSlot::kAuxiliaryTexture, srvManager->GetGPUDescriptorHandle(maskSrv));
    PipelineHelper::DrawFullscreenTriangle(cmd);
}

/** @brief アニメーション有効時はseedを毎フレーム加算してからノイズを描画する */
void ImageFilter::NoiseGenFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // プロシージャルノイズ  シングルパス（シーンに重ね合わせ）
    // アニメーション  毎フレーム seed を加算して乱数パターンを変化させる
    if (filter.animateNoise_)
    {
        filter.noiseTime_ += filter.noiseSpeed_;
        filter.noiseGenCb_->seed = filter.noiseManualSeed_ + filter.noiseTime_;
    }
    else
    {
        filter.noiseGenCb_->seed = filter.noiseManualSeed_;
    }
    PipelineHelper::BindFullscreenPass(cmd, filter.rootSignature_.Get(), filter.noiseGenPso_.Get(),
        backRtv, vp, scissor, filter.noiseGenCbResource_->GetGPUVirtualAddress(),
        srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);
}

/** @brief 水平パス（sceneTexture→intermediateTexture）→垂直パス（intermediateTexture→backbuffer）の順に描画する */
void ImageFilter::BoxGaussianFilterMode::Apply(ImageFilter& filter, ID3D12GraphicsCommandList* cmd, SrvManager* srvManager,
    const D3D12_VIEWPORT& vp, const D3D12_RECT& scissor, D3D12_CPU_DESCRIPTOR_HANDLE backRtv) const
{
    // Box / Gaussian  水平→垂直の 2 パス
    cmd->SetGraphicsRootSignature(filter.rootSignature_.Get());
    cmd->SetPipelineState(GetPipeline(filter));

    // Pass 1: 水平（sceneTexture → intermediateTexture、内部解像度は固定のまま）
    if (!filter.isIntermediateFirstFrame_)
    {
        DirectXCommon::TransitionBarrier(cmd, filter.intermediateTexture_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    filter.isIntermediateFirstFrame_ = false;

    cmd->OMSetRenderTargets(1, &filter.intermediateRtvHandle_, FALSE, nullptr);
    PipelineHelper::SetClientViewportAndScissor(cmd);
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::FullscreenRootSlot::kConstants, filter.cbResource_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::FullscreenRootSlot::kTexture, srvManager->GetGPUDescriptorHandle(filter.sceneSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);

    DirectXCommon::TransitionBarrier(cmd, filter.intermediateTexture_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Pass 2: 垂直（intermediateTexture → backbuffer）
    cmd->OMSetRenderTargets(1, &backRtv, FALSE, nullptr);
    PipelineHelper::SetViewportAndScissor(cmd, vp, scissor);
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::FullscreenRootSlot::kConstants, filter.cbResource_->GetGPUVirtualAddress() + kConstantBufferAlignment);
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::FullscreenRootSlot::kTexture, srvManager->GetGPUDescriptorHandle(filter.intermediateSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);
}

// ══════════════════════════════════════════════════════
// フィルタ選択とカーネル更新
// ══════════════════════════════════════════════════════

/** @brief 指定モードに対応するフィルタ処理（IFilterMode の実装）を返す */
const ImageFilter::IFilterMode& ImageFilter::GetFilterMode(Mode mode)
{
    static BoxFilterMode box;
    static GaussianFilterMode gaussian;
    static PrewittEdgeFilterMode prewittEdge;
    static DepthOutlineFilterMode depthOutline;
    static RadialBlurFilterMode radialBlur;
    static DissolveFilterMode dissolve;
    static NoiseGenFilterMode noiseGen;
    // Mode の並び順（Box と Gaussian はカーネルの重みが違うだけで同じ2パス処理）
    static const IFilterMode* const kModes[] = {
        &box, // Box
        &gaussian, // Gaussian
        &prewittEdge, // PrewittEdge
        &depthOutline, // DepthOutline
        &radialBlur, // RadialBlur
        &dissolve, // Dissolve
        &noiseGen, // NoiseGen
    };
    const size_t index = static_cast<size_t>(mode);
    return index < std::size(kModes) ? *kModes[index] : box;
}

// カーネル重みの計算（Box / Gaussian のみ）

/** @brief ブラーを使うモードのとき、半径とシグマからカーネルの重みを作り直す */
void ImageFilter::RebuildKernel()
{
    if (!cbH_)
    {
        return;
    }
    if (!GetFilterMode(mode_).UsesBlurKernel())
    {
        return;
    }

    // 選択した方式が半径と重みの生成を担当する
    float weights[kBlurKernelSize] = {};
    const int r = GetFilterMode(mode_).BuildKernel(*this, weights);

    // 横と縦の両方のぼかし用定数バッファへ同じ重みを書く
    cbH_->radius = r;
    cbV_->radius = r;
    for (int i = 0; i <= 2 * r; ++i)
    {
        cbH_->kernel[i] = weights[i];
        cbV_->kernel[i] = weights[i];
    }
}

/** @brief Box フィルターのカーネル半径を設定する */
void ImageFilter::SetRadius(int r)
{
    boxRadius_ = r;
    RebuildKernel();
}

/** @brief Gaussian フィルターの標準偏差σを設定する */
void ImageFilter::SetSigma(float s)
{
    gaussianSigma_ = s;
    RebuildKernel();
}

/** @brief 均等な重みを書き込み、半径を返す */
int ImageFilter::BoxFilterMode::BuildKernel(const ImageFilter& filter, float* weights) const
{
    // 中心と左右のサンプルへ均等に重みを配る
    const int radius = std::clamp(filter.boxRadius_, 0, kMaxBlurRadius);
    const float weight = 1.0f / float(2 * radius + 1);
    for (int index = 0; index <= 2 * radius; ++index)
    {
        weights[index] = weight;
    }
    return radius;
}

/** @brief 合計1の正規分布の重みを書き込み、半径を返す */
int ImageFilter::GaussianFilterMode::BuildKernel(const ImageFilter& filter, float* weights) const
{
    // 正規分布の重みを求め、合計が1になるよう割る
    const float sigma = (std::max)(filter.gaussianSigma_, kMinGaussianSigma);
    const int radius = std::clamp(static_cast<int>(sigma * kGaussianRadiusScale), 1, kMaxBlurRadius);
    const float variance = 2.0f * sigma * sigma;
    float total = 0.0f;
    for (int index = 0; index <= 2 * radius; ++index)
    {
        const float distance = float(index - radius);
        weights[index] = std::exp(-(distance * distance) / variance);
        total += weights[index];
    }
    for (int index = 0; index <= 2 * radius; ++index)
    {
        weights[index] /= total;
    }
    return radius;
}

} // namespace engine::graphics
