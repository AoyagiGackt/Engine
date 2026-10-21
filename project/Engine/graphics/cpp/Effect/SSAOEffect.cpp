/**
 * @file SSAOEffect.cpp
 * @brief 画面空間の環境遮蔽（SSAOEffect）の実装
 */
#include "SSAOEffect.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "GraphicsTuning.h"
#include "EngineAssert.h"
#include <cmath>
#include <random>

namespace engine::graphics {
namespace {
/** @brief 法線の描画先を消去する色（法線なし） */
constexpr float kNormalClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
/** @brief AO とそのぼかしの描画先を消去する色（遮蔽なし＝白） */
constexpr float kOcclusionClearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

// 半球サンプリング
constexpr unsigned int kKernelSeed = 42;
constexpr float kKernelDirectionEpsilon = 1e-5f;
constexpr float kKernelMinRadius = 0.1f;
constexpr float kKernelRadiusRange = 0.9f;

}

using namespace Microsoft::WRL;

// ランダム半球カーネル

/** @brief 法線側の半球に偏った SSAO のサンプル方向を count 個作る */
static void GenerateKernel(Vector4* out, int count)
{
    std::mt19937 rng(kKernelSeed);
    std::uniform_real_distribution<float> distF(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    int n = 0;
    while (n < count)
    {
        // z を[0,1]にすることで法線側（+Z）半球に偏ったサンプルにする（SSAOは表面より上だけを見ればよい）
        float x = distF(rng), y = distF(rng), z = dist01(rng);
        float len = std::sqrt(x * x + y * y + z * z);
        if (len < kKernelDirectionEpsilon)
        {
            continue;
        }
        // 単位ベクトル化して方向だけを残す長さ（強さ）は次のscaleで別途つける
        x /= len;
        y /= len;
        z /= len;
        // サンプルの半径方向の強さを2乗補間原点付近に密集させ、遠いサンプルほどまばらにすることで
        // 近距離のオクルージョンを高解像度に、遠距離は粗く拾う（0.1〜1.0の範囲でスケール）
        float scale = float(n) / float(count);
        scale = kKernelMinRadius + kKernelRadiusRange * scale * scale;
        out[n] = { x * scale, y * scale, z * scale, 0.0f };
        ++n;
    }
}

// ヘルパー

// ══════════════════════════════════════════════════════
// リソース生成補助
// ══════════════════════════════════════════════════════

/** @brief リソースの状態を切り替えるバリアを積む */
void SSAOEffect::Barrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) const
{
    DirectXCommon::TransitionBarrier(cmd, res, before, after);
}

/** @brief SSAOの途中結果を書くレンダーテクスチャを生成する */
void SSAOEffect::CreateRT(ID3D12Device* device, SrvManager* srvManager,
    UINT w, UINT h, DXGI_FORMAT fmt,
    ComPtr<ID3D12Resource>& res,
    ComPtr<ID3D12DescriptorHeap>& rtvHeap,
    D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
    uint32_t& srvIndex,
    const float (&clearColor)[4])
{
    // 生成直後からシェーダで読める状態(PIXEL_SHADER_RESOURCE)にしておく。描画時はここからRTV用に遷移する
    // 消去値は実際に消去で使う色を登録し、ドライバ側のクリア最適化を効かせる
    PipelineHelper::CreateRenderTarget(device, srvManager, w, h, { fmt, fmt, fmt }, clearColor,
        { res, rtvHeap, rtvHandle, srvIndex }, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// ルートシグネチャ生成ヘルパー（static）

// NormalCapture RS: slot0 = CBV (VS, b0) per-object transform / slot1 = CBV (PS, b1) view matrix
static ComPtr<ID3D12RootSignature> CreateNormalCaptureRS(ID3D12Device* device)
{
    D3D12_ROOT_PARAMETER params[2] = { };
    params[0] = PipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX);
    params[1] = PipelineHelper::MakeCbvParam(1, D3D12_SHADER_VISIBILITY_PIXEL);

    auto rs = PipelineHelper::CreateRootSignature(device, params, 2,
        nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rs);
    return rs;
}

// SSAO・ブラー用のルートシグネチャ: slot0 = CBV (全ステージ, b0) / slot1 = SRVのディスクリプタテーブル (PS, t0)
static ComPtr<ID3D12RootSignature> CreateRS_CBV_SRV(ID3D12Device* device)
{
    auto rs = PipelineHelper::CreateConstantTextureSignature(device, D3D12_SHADER_VISIBILITY_PIXEL,
        PipelineHelper::MakeLinearClampSampler(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rs);
    return rs;
}

// 合成用のルートシグネチャ: slot0 = SRVのディスクリプタテーブル (PS, t0)
static ComPtr<ID3D12RootSignature> CreateApplyRS(ID3D12Device* device)
{
    const D3D12_DESCRIPTOR_RANGE srvRange = PipelineHelper::MakeSrvRange(0); // t0

    D3D12_ROOT_PARAMETER param = PipelineHelper::MakeSrvTableParam(&srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = PipelineHelper::MakeLinearClampSampler();

    auto rs = PipelineHelper::CreateRootSignature(device, &param, 1,
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rs);
    return rs;
}

/** @brief 全画面描画用のPSOを生成する */
static ComPtr<ID3D12PipelineState> CreateFullscreenPSO(
    ID3D12Device* device, ID3D12RootSignature* rs,
    IDxcBlob* vs, IDxcBlob* ps, DXGI_FORMAT fmt,
    bool multiplyBlend = false)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = PipelineHelper::MakeFullscreenPsoDesc();
    desc.pRootSignature = rs;
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.RTVFormats[0] = fmt;

    auto& b = desc.BlendState.RenderTarget[0];
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (multiplyBlend)
    {
        b.BlendEnable = TRUE;
        b.SrcBlend = D3D12_BLEND_DEST_COLOR;
        b.DestBlend = D3D12_BLEND_ZERO;
        b.BlendOp = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D12_BLEND_ONE;
        b.DestBlendAlpha = D3D12_BLEND_ZERO;
        b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    ComPtr<ID3D12PipelineState> pso;
    pso = PipelineHelper::CreateGraphicsPipeline(device, desc);
    return pso;
}

// 定数バッファ生成ヘルパー

/** @brief アップロードヒープに定数バッファを作り、書き込み先を mapped へ返す */
template <typename T>
static Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadCB(ID3D12Device* device, T** mapped)
{
    auto res = PipelineHelper::CreateUploadBuffer(device, AlignConstantBufferSize(sizeof(T)));
    res->Map(0, nullptr, reinterpret_cast<void**>(mapped));
    return res;
}

// ══════════════════════════════════════════════════════
// 初期化と終了処理
// ══════════════════════════════════════════════════════

/** @brief レンダーターゲット・PSO・定数バッファを作成して初期化する */
void SSAOEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon->GetDevice();

    UINT width = WinApp::kClientWidth;
    UINT height = WinApp::kClientHeight;

    // レンダーターゲット作成（法線は0で、AO とそのぼかしは遮蔽なし＝白で消去する）

    CreateRT(device, srvManager, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT,
        normalTex_, normalRtvHeap_, normalRtvHandle_, normalSrvIndex_, kNormalClearColor);

    CreateRT(device, srvManager, width, height, DXGI_FORMAT_R8_UNORM,
        ssaoTex_, ssaoRtvHeap_, ssaoRtvHandle_, ssaoSrvIndex_, kOcclusionClearColor);

    CreateRT(device, srvManager, width, height, DXGI_FORMAT_R8_UNORM,
        blurTex_, blurRtvHeap_, blurRtvHandle_, blurSrvIndex_, kOcclusionClearColor);

    // NormalCapture 専用深度バッファ
    {
        depthTex_ = PipelineHelper::CreateDepthTexture(device, width, height, 1, DXGI_FORMAT_D32_FLOAT);

        dsvHeap_ = DirectXCommon::CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
        dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        device->CreateDepthStencilView(depthTex_.Get(), nullptr, dsvHandle_);
    }

    // 定数バッファ
    normalCb_ = CreateUploadCB<NormalCaptureCB>(device, &normalCbData_);

    // 遮蔽を調べる半径・強さ・サンプル方向
    ssaoCb_ = CreateUploadCB<SSAOParams>(device, &ssaoCbData_);
    *ssaoCbData_ = SSAOParams {};
    ssaoCbData_->texW = float(width);
    ssaoCbData_->texH = float(height);
    ssaoCbData_->radius = GraphicsTuning::Positive("ssao.radius", ssaoCbData_->radius);
    ssaoCbData_->strength = GraphicsTuning::Float("ssao.strength", ssaoCbData_->strength);
    ssaoCbData_->bias = GraphicsTuning::Float("ssao.bias", ssaoCbData_->bias);
    ssaoCbData_->numSamples = GraphicsTuning::Int("ssao.numSamples", kKernelSampleCount, 1, kKernelSampleCount);
    GenerateKernel(ssaoCbData_->kernel, kKernelSampleCount);

    // AO のぼかしに使う画面の大きさ
    blurCb_ = CreateUploadCB<BlurParams>(device, &blurCbData_);
    blurCbData_->texW = float(width);
    blurCbData_->texH = float(height);
    blurCbData_->pad[0] = blurCbData_->pad[1] = 0.0f;

    // シェーダーコンパイル
    Microsoft::WRL::ComPtr<IDxcBlob> fsVS = dxCommon->CompileShader(L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> ssaoPS = dxCommon->CompileShader(L"Resources/shaders/postprocess/SSAOPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> blurPS = dxCommon->CompileShader(L"Resources/shaders/postprocess/SSAOBlurPS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> applyPS = dxCommon->CompileShader(L"Resources/shaders/postprocess/SSAOApplyPS.hlsl", L"ps_6_0");

    // NormalCapture PSO: Object3dVS + NormalCapturePS
    {
        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        normalRS_ = CreateNormalCaptureRS(device);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = PipelineHelper::MakeFullscreenPsoDesc();
        psoDesc.pRootSignature = normalRS_.Get();
        psoDesc.InputLayout = { layout, _countof(layout) };
        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        normalPSO_ = PipelineHelper::CreateGraphicsPipeline(dxCommon, psoDesc,
            L"Resources/shaders/object3d/Object3dVS.hlsl", L"Resources/shaders/postprocess/NormalCapturePS.hlsl");
    }

    // SSAO, Blur PSO
    ssaoRS_ = CreateRS_CBV_SRV(device);
    ssaoPSO_ = CreateFullscreenPSO(device, ssaoRS_.Get(), fsVS.Get(), ssaoPS.Get(), DXGI_FORMAT_R8_UNORM);

    blurRS_ = CreateRS_CBV_SRV(device);
    blurPSO_ = CreateFullscreenPSO(device, blurRS_.Get(), fsVS.Get(), blurPS.Get(), DXGI_FORMAT_R8_UNORM);

    // Apply PSO (multiply blend → AO を乗算でシーンに合成)
    applyRS_ = CreateApplyRS(device);
    applyPSO_ = CreateFullscreenPSO(device, applyRS_.Get(), fsVS.Get(), applyPS.Get(),
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, /*multiplyBlend=*/true);
}

/** @brief SSAOの描画資源を解放する */
void SSAOEffect::Finalize()
{
    if (normalCbData_)
    {
        normalCb_->Unmap(0, nullptr);
        normalCbData_ = nullptr;
    }
    if (ssaoCbData_)
    {
        ssaoCb_->Unmap(0, nullptr);
        ssaoCbData_ = nullptr;
    }
    if (blurCbData_)
    {
        blurCb_->Unmap(0, nullptr);
        blurCbData_ = nullptr;
    }
}

// NormalCapture

// ══════════════════════════════════════════════════════
// 法線キャプチャ
// ══════════════════════════════════════════════════════

/** @brief 法線を書き出すパスを開始する（メイン描画の前に呼ぶ） */
void SSAOEffect::BeginNormalCapture(DirectXCommon* dxCommon, Camera* camera)
{
    if (!enabled_)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon->GetCommandList();

    Barrier(cmd, normalTex_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmd->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cmd->ClearRenderTargetView(normalRtvHandle_, kNormalClearColor, 0, nullptr);
    cmd->OMSetRenderTargets(1, &normalRtvHandle_, FALSE, &dsvHandle_);

    PipelineHelper::SetClientViewportAndScissor(cmd);

    PipelineHelper::SetTrianglePipeline(cmd, normalRS_.Get(), normalPSO_.Get());

    // slot1 = NormalCaptureCB (PS, b1) - view matrix
    normalCbData_->view = camera->GetViewMatrix();
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::NormalPrepassRootSlot::kNormals, normalCb_->GetGPUVirtualAddress());

    normalInRTV_ = true;
}

/** @brief ノーマルキャプチャパスを終了する */
void SSAOEffect::EndNormalCapture(DirectXCommon* dxCommon)
{
    if (!enabled_ || !normalInRTV_)
    {
        return;
    }

    Barrier(dxCommon->GetCommandList(), normalTex_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    normalInRTV_ = false;
}

/** @brief ノーマルキャプチャ中に Object3d から呼ぶper-object トランスフォームを slot 0 に設定する */
void SSAOEffect::SetObjectTransform(ID3D12GraphicsCommandList* cmd,
    D3D12_GPU_VIRTUAL_ADDRESS transformAddr) const
{
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::NormalPrepassRootSlot::kTransform, transformAddr);
}

// SSAO Compute

// ══════════════════════════════════════════════════════
// SSAO計算と合成
// ══════════════════════════════════════════════════════

/** @brief 法線と深度から AO の値を計算する */
void SSAOEffect::Compute(DirectXCommon* dxCommon, Camera* camera)
{
    if (!enabled_)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon->GetCommandList();

    Barrier(cmd, ssaoTex_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmd->ClearRenderTargetView(ssaoRtvHandle_, kOcclusionClearColor, 0, nullptr);
    cmd->OMSetRenderTargets(1, &ssaoRtvHandle_, FALSE, nullptr);

    PipelineHelper::SetClientViewportAndScissor(cmd);

    ssaoCbData_->projection = camera->GetProjectionMatrix();
    ssaoCbData_->projectionInverse = Inverse(camera->GetProjectionMatrix());

    PipelineHelper::SetTrianglePipeline(cmd, ssaoRS_.Get(), ssaoPSO_.Get());
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::FullscreenRootSlot::kConstants, ssaoCb_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::FullscreenRootSlot::kTexture, srvManager_->GetGPUDescriptorHandle(normalSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);

    Barrier(cmd, ssaoTex_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/** @brief AO テクスチャにブラーをかける */
void SSAOEffect::Blur(DirectXCommon* dxCommon)
{
    if (!enabled_)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon->GetCommandList();

    Barrier(cmd, blurTex_.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    cmd->ClearRenderTargetView(blurRtvHandle_, kOcclusionClearColor, 0, nullptr);
    cmd->OMSetRenderTargets(1, &blurRtvHandle_, FALSE, nullptr);

    PipelineHelper::SetClientViewportAndScissor(cmd);

    PipelineHelper::SetTrianglePipeline(cmd, blurRS_.Get(), blurPSO_.Get());
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::FullscreenRootSlot::kConstants, blurCb_->GetGPUVirtualAddress()); // BlurParams {texW, texH}
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::FullscreenRootSlot::kTexture, srvManager_->GetGPUDescriptorHandle(ssaoSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);

    Barrier(cmd, blurTex_.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// Apply (乗算ブレンドで AO をシーンに合成)

/** @brief 計算した AO をシーンに乗算して適用する（メイン描画の後に呼ぶ） */
void SSAOEffect::Apply(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    if (!enabled_)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmd = dxCommon->GetCommandList();

    // blurTex_ はすでに PIXEL_SHADER_RESOURCE 状態
    PipelineHelper::SetTrianglePipeline(cmd, applyRS_.Get(), applyPSO_.Get());
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::SingleTextureRootSlot::kTexture, srvManager->GetGPUDescriptorHandle(blurSrvIndex_));
    PipelineHelper::DrawFullscreenTriangle(cmd);
}

} // namespace engine::graphics
