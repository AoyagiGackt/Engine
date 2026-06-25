#include "ChromaticAberrationEffect.h"
#include "WinApp.h"
#include <cassert>

using namespace Microsoft::WRL;

void ChromaticAberrationEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();

    // =====================================================
    // オフスクリーンテクスチャの作成
    // =====================================================
    // TYPELESS にすることで同一バッファを
    //   RTV（UNORM_SRGB）として書き込み → SRV（UNORM_SRGB）として読み込み
    // の両方に使える（GrayscaleEffect と同じパターン）
    constexpr DXGI_FORMAT kResourceFmt = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    constexpr DXGI_FORMAT kRtvFmt      = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    constexpr DXGI_FORMAT kSrvFmt      = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    constexpr float kClearColor[4]     = { 0.1f, 0.25f, 0.5f, 1.0f };

    const uint32_t width  = WinApp::kClientWidth;
    const uint32_t height = WinApp::kClientHeight;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT; // GPU 専用メモリ

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format   = kRtvFmt;
    clearValue.Color[0] = kClearColor[0];
    clearValue.Color[1] = kClearColor[1];
    clearValue.Color[2] = kClearColor[2];
    clearValue.Color[3] = kClearColor[3];

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width              = width;
    texDesc.Height             = height;
    texDesc.DepthOrArraySize   = 1;
    texDesc.MipLevels          = 1;
    texDesc.Format             = kResourceFmt;
    texDesc.SampleDesc.Count   = 1;
    texDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET; // RTV として使用できるようにする

    HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &texDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, // 初期状態は RTV
        &clearValue, IID_PPV_ARGS(&sceneTexture_));
    assert(SUCCEEDED(hr));

    // =====================================================
    // RTV（レンダーターゲットビュー）の作成
    // =====================================================
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1; // このエフェクト専用に 1 スロット確保
    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_));
    assert(SUCCEEDED(hr));

    rtvHandle_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format        = kRtvFmt;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(sceneTexture_.Get(), &rtvDesc, rtvHandle_);

    // =====================================================
    // SRV（シェーダーリソースビュー）の作成
    // =====================================================
    // SrvManager にインデックスを確保してもらい、PS でサンプリングできるようにする
    srvIndex_ = srvManager->Allocate();
    srvManager->CreateSRVforTexture2D(srvIndex_, sceneTexture_.Get(), kSrvFmt, 1);

    // =====================================================
    // 定数バッファ（色収差の強さ）の作成
    // =====================================================
    cbResource_ = dxCommon->CreateBufferResource(256); // 256 バイトアライメント必須
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    cbData_->strength = 0.0f; // デフォルトは効果なし

    // =====================================================
    // ルートシグネチャの設定
    // =====================================================
    // slot 0 (PS, b0): ChromaticParams（強度）
    // slot 1 (PS, t0): オフスクリーンテクスチャ（SRV テーブル）
    D3D12_DESCRIPTOR_RANGE srvRange                          = {};
    srvRange.RangeType                                       = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                                  = 1;
    srvRange.BaseShaderRegister                              = 0; // t0
    srvRange.OffsetInDescriptorsFromTableStart               = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2]                       = {};
    rootParams[0].ParameterType                              = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility                           = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[0].Descriptor.ShaderRegister                  = 0; // b0
    rootParams[1].ParameterType                              = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].ShaderVisibility                           = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].DescriptorTable.pDescriptorRanges          = &srvRange;
    rootParams[1].DescriptorTable.NumDescriptorRanges        = 1;

    // クランプサンプラー（画面端でテクスチャが繰り返さないようにクランプ）
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0; // s0
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters    = 2;
    rootSigDesc.pParameters      = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers  = &sampler;
    rootSigDesc.Flags            = D3D12_ROOT_SIGNATURE_FLAG_NONE; // InputLayout なし（フルスクリーン三角形）

    ComPtr<ID3DBlob> sigBlob, errBlob;
    hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));

    // =====================================================
    // シェーダーのコンパイル
    // =====================================================
    // VS: 頂点バッファなし。SV_VertexID から フルスクリーン三角形の頂点を生成する
    // PS: ChromaticAberrationPS.hlsl で R/G/B を別々にサンプリングして色ズレを表現
    IDxcBlob* vsBlob = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    IDxcBlob* psBlob = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/ChromaticAberrationPS.hlsl", L"ps_6_0");

    // =====================================================
    // PSO（パイプラインステートオブジェクト）の設定
    // =====================================================
    D3D12_BLEND_DESC blendDesc                            = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask       = D3D12_COLOR_WRITE_ENABLE_ALL;
    // ブレンドは無効（ポストエフェクトは上書き）

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode              = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode              = D3D12_CULL_MODE_NONE; // フルスクリーン三角形なのでカリングなし

    D3D12_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable              = FALSE; // 深度テスト不要（2D ポストエフェクト）

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc        = {};
    psoDesc.pRootSignature                            = rootSignature_.Get();
    psoDesc.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState            = blendDesc;
    psoDesc.RasterizerState       = rastDesc;
    psoDesc.DepthStencilState     = depthDesc;
    psoDesc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.SampleDesc.Count      = 1;
    // InputLayout は設定しない（VS が SV_VertexID で頂点を生成するため）

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));
}

void ChromaticAberrationEffect::Finalize()
{
    if (cbData_) {
        cbResource_->Unmap(0, nullptr);
        cbData_ = nullptr;
    }
    cbResource_.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();
    rtvHeap_.Reset();
    sceneTexture_.Reset();
}

void ChromaticAberrationEffect::BeginScene()
{
    auto* cmdList = dxCommon_->GetCommandList();

    // 初回以外: SRV → RTV へのリソースバリアを挿入
    // （前フレームの Apply() で SRV 状態になっているため）
    if (!isFirstFrame_) {
        D3D12_RESOURCE_BARRIER barrier                  = {};
        barrier.Type                                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource                    = sceneTexture_.Get();
        barrier.Transition.StateBefore                  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter                   = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cmdList->ResourceBarrier(1, &barrier);
    }
    isFirstFrame_ = false;

    // 描画先をオフスクリーンテクスチャへ切り替える
    // 深度バッファは共通のものを再利用（3D 深度テストが機能する）
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmdList->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsv);

    constexpr float kClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
    cmdList->ClearRenderTargetView(rtvHandle_, kClearColor, 0, nullptr);

    // ビューポートとシザーを画面全体に設定
    D3D12_VIEWPORT vp      = { 0.f, 0.f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.f, 1.f };
    D3D12_RECT     scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &scissor);
}

void ChromaticAberrationEffect::EndScene()
{
    auto* cmdList = dxCommon_->GetCommandList();

    // RTV → SRV へのリソースバリア（Apply() でシェーダーから読み込めるようにする）
    D3D12_RESOURCE_BARRIER barrier                  = {};
    barrier.Type                                    = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource                    = sceneTexture_.Get();
    barrier.Transition.StateBefore                  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter                   = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &barrier);
}

void ChromaticAberrationEffect::Apply(SrvManager* srvManager)
{
    auto* cmdList = dxCommon_->GetCommandList();

    // 描画先をバックバッファに戻す（深度バッファはポストエフェクトには不要なので nullptr）
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetCurrentBackBufferHandle();
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // ビューポートとシザーを画面全体に設定
    D3D12_VIEWPORT vp      = { 0.f, 0.f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.f, 1.f };
    D3D12_RECT     scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmdList->RSSetViewports(1, &vp);
    cmdList->RSSetScissorRects(1, &scissor);

    // 色収差用の PSO と root sig をセット
    cmdList->SetGraphicsRootSignature(rootSignature_.Get());
    cmdList->SetPipelineState(pipelineState_.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // slot 0: 定数バッファ（strength）
    cmdList->SetGraphicsRootConstantBufferView(0, cbResource_->GetGPUVirtualAddress());
    // slot 1: オフスクリーンテクスチャ（SRV テーブル）
    cmdList->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(srvIndex_));

    // 頂点バッファなしでフルスクリーン三角形を描画する
    // FullscreenVS が SV_VertexID (0,1,2) から座標を計算する
    cmdList->DrawInstanced(3, 1, 0, 0);
}
