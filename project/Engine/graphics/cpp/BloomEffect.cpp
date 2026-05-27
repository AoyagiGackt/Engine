#include "BloomEffect.h"
#include <cassert>

using namespace Microsoft::WRL;

// ============================================================
// ユーティリティ
// ============================================================

void BloomEffect::Barrier(ID3D12GraphicsCommandList* cmd,
                          ID3D12Resource*            res,
                          D3D12_RESOURCE_STATES      before,
                          D3D12_RESOURCE_STATES      after) const
{
    D3D12_RESOURCE_BARRIER b    = {};
    b.Type                      = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource      = res;
    b.Transition.StateBefore    = before;
    b.Transition.StateAfter     = after;
    b.Transition.Subresource    = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &b);
}

// ---- RT + SRV リソース生成 ----
void BloomEffect::CreateRenderTexture(ID3D12Device*          device,
                                      SrvManager*            srvManager,
                                      UINT width, UINT height,
                                      ComPtr<ID3D12Resource>&       outRes,
                                      ComPtr<ID3D12DescriptorHeap>& outRtvHeap,
                                      D3D12_CPU_DESCRIPTOR_HANDLE&  outRtvHandle,
                                      uint32_t&                     outSrvIndex,
                                      DXGI_FORMAT rtvFmt,
                                      DXGI_FORMAT srvFmt)
{
    // リソース作成
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    constexpr float kClear[4] = { 0.f, 0.f, 0.f, 0.f };
    D3D12_CLEAR_VALUE cv = {};
    cv.Format            = rtvFmt;
    cv.Color[0] = cv.Color[1] = cv.Color[2] = cv.Color[3] = 0.f;

    D3D12_RESOURCE_DESC rd  = {};
    rd.Dimension            = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width                = width;
    rd.Height               = height;
    rd.DepthOrArraySize     = 1;
    rd.MipLevels            = 1;
    rd.Format               = rtvFmt; // UNORM を直接使う（TYPELESS 不要）
    rd.SampleDesc.Count     = 1;
    rd.Flags                = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE,
        &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
        &cv, IID_PPV_ARGS(&outRes));
    assert(SUCCEEDED(hr));

    // RTV ヒープ
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    hr = device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&outRtvHeap));
    assert(SUCCEEDED(hr));

    outRtvHandle = outRtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rtvView = {};
    rtvView.Format        = rtvFmt;
    rtvView.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(outRes.Get(), &rtvView, outRtvHandle);

    // SRV
    outSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVforTexture2D(outSrvIndex, outRes.Get(), srvFmt, 1);
}

// ---- RS: b0(CBV) + t0(SRV) ----
ComPtr<ID3D12RootSignature> BloomEffect::CreateRS_CB_SRV(ID3D12Device* device) const
{
    D3D12_DESCRIPTOR_RANGE srvRange                            = {};
    srvRange.RangeType                                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors                                    = 1;
    srvRange.BaseShaderRegister                                = 0; // t0
    srvRange.OffsetInDescriptorsFromTableStart                 = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]                             = {};
    // b0: 定数バッファ
    params[0].ParameterType                                    = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister                        = 0;
    params[0].ShaderVisibility                                 = D3D12_SHADER_VISIBILITY_PIXEL;
    // t0: SRV テーブル
    params[1].ParameterType                                    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.pDescriptorRanges                = &srvRange;
    params[1].DescriptorTable.NumDescriptorRanges              = 1;
    params[1].ShaderVisibility                                 = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler                          = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters             = 2;
    rsDesc.pParameters               = params;
    rsDesc.NumStaticSamplers         = 1;
    rsDesc.pStaticSamplers           = &sampler;
    rsDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12RootSignature> rs;
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rs));
    assert(SUCCEEDED(hr));
    return rs;
}

// ---- RS: t0 + t1（Combine 用、CBV なし） ----
ComPtr<ID3D12RootSignature> BloomEffect::CreateRS_SRV2(ID3D12Device* device) const
{
    // t0 と t1 を別々のルートパラメータとして定義
    D3D12_DESCRIPTOR_RANGE srvRange0                           = {};
    srvRange0.RangeType                                        = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange0.NumDescriptors                                   = 1;
    srvRange0.BaseShaderRegister                               = 0; // t0
    srvRange0.OffsetInDescriptorsFromTableStart                = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvRange1                           = {};
    srvRange1.RangeType                                        = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange1.NumDescriptors                                   = 1;
    srvRange1.BaseShaderRegister                               = 1; // t1
    srvRange1.OffsetInDescriptorsFromTableStart                = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]                             = {};
    params[0].ParameterType                                    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.pDescriptorRanges                = &srvRange0;
    params[0].DescriptorTable.NumDescriptorRanges              = 1;
    params[0].ShaderVisibility                                 = D3D12_SHADER_VISIBILITY_PIXEL;

    params[1].ParameterType                                    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.pDescriptorRanges                = &srvRange1;
    params[1].DescriptorTable.NumDescriptorRanges              = 1;
    params[1].ShaderVisibility                                 = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler                          = {};
    sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD           = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister   = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters             = 2;
    rsDesc.pParameters               = params;
    rsDesc.NumStaticSamplers         = 1;
    rsDesc.pStaticSamplers           = &sampler;
    rsDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12RootSignature> rs;
    hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rs));
    assert(SUCCEEDED(hr));
    return rs;
}

// ---- PSO 生成 ----
ComPtr<ID3D12PipelineState> BloomEffect::CreatePSO(ID3D12Device*        device,
                                                    ID3D12RootSignature* rootSig,
                                                    IDxcBlob*            vsBlob,
                                                    IDxcBlob*            psBlob,
                                                    DXGI_FORMAT          rtvFmt) const
{
    D3D12_BLEND_DESC blend                                             = {};
    blend.RenderTarget[0].RenderTargetWriteMask                        = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rast                                         = {};
    rast.FillMode                                                      = D3D12_FILL_MODE_SOLID;
    rast.CullMode                                                      = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depth                                     = {};
    depth.DepthEnable                                                  = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc                         = {};
    psoDesc.pRootSignature                                             = rootSig;
    psoDesc.VS                    = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS                    = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState            = blend;
    psoDesc.RasterizerState       = rast;
    psoDesc.DepthStencilState     = depth;
    psoDesc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = rtvFmt;
    psoDesc.SampleDesc.Count      = 1;

    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    assert(SUCCEEDED(hr));
    return pso;
}

// ============================================================
// 初期化
// ============================================================

void BloomEffect::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();

    const UINT W = WinApp::kClientWidth;
    const UINT H = WinApp::kClientHeight;

    // ---- シーンキャプチャ RT（SRGB で既存パイプラインと合わせる） ----
    {
        // GrayscaleEffect と同じ TYPELESS → RTV=SRGB, SRV=SRGB
        constexpr DXGI_FORMAT kResFmt = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        constexpr DXGI_FORMAT kRtvFmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        constexpr DXGI_FORMAT kSrvFmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_CLEAR_VALUE cv     = {};
        cv.Format                = kRtvFmt;
        cv.Color[0]              = 0.1f; cv.Color[1] = 0.25f; cv.Color[2] = 0.5f; cv.Color[3] = 1.f;

        D3D12_RESOURCE_DESC rd   = {};
        rd.Dimension             = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width                 = W; rd.Height = H;
        rd.DepthOrArraySize      = 1; rd.MipLevels = 1;
        rd.Format                = kResFmt;
        rd.SampleDesc.Count      = 1;
        rd.Flags                 = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        HRESULT hr = device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&sceneTexture_));
        assert(SUCCEEDED(hr));

        D3D12_DESCRIPTOR_HEAP_DESC rtvHD = {};
        rtvHD.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHD.NumDescriptors = 1;
        hr = device->CreateDescriptorHeap(&rtvHD, IID_PPV_ARGS(&sceneRtvHeap_));
        assert(SUCCEEDED(hr));

        sceneRtvHandle_ = sceneRtvHeap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_RENDER_TARGET_VIEW_DESC rtvV = {};
        rtvV.Format        = kRtvFmt;
        rtvV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(sceneTexture_.Get(), &rtvV, sceneRtvHandle_);

        sceneSrvIndex_ = srvManager->Allocate();
        srvManager->CreateSRVforTexture2D(sceneSrvIndex_, sceneTexture_.Get(), kSrvFmt, 1);
    }

    // ---- Bloom ピンポン RT（UNORM: 中間バッファなので gamma 補正不要） ----
    constexpr DXGI_FORMAT kBloomFmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    for (int i = 0; i < 2; i++) {
        CreateRenderTexture(device, srvManager,
                            W, H,
                            bloomTex_[i],
                            bloomRtvHeap_[i],
                            bloomRtvHandle_[i],
                            bloomSrvIndex_[i],
                            kBloomFmt, kBloomFmt);
        bloomInSrv_[i] = false; // 初期状態は RTV
    }

    // ---- シェーダーコンパイル ----
    IDxcBlob* vsBlob = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"vs_6_0");
    IDxcBlob* brightPS = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/BloomBrightPS.hlsl",  L"ps_6_0");
    IDxcBlob* blurPS   = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/BloomBlurPS.hlsl",    L"ps_6_0");
    IDxcBlob* combPS   = dxCommon->CompileShader(
        L"Resources/shaders/postprocess/BloomCombinePS.hlsl", L"ps_6_0");

    // ---- ルートシグネチャ & PSO ----
    brightRS_  = CreateRS_CB_SRV(device);
    blurRS_    = CreateRS_CB_SRV(device);
    combineRS_ = CreateRS_SRV2(device);

    brightPSO_  = CreatePSO(device, brightRS_.Get(),  vsBlob, brightPS, kBloomFmt);
    blurPSO_    = CreatePSO(device, blurRS_.Get(),    vsBlob, blurPS,   kBloomFmt);
    // Combine は SRGB バックバッファへ書き込む
    combinePSO_ = CreatePSO(device, combineRS_.Get(), vsBlob, combPS,
                            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    // ---- 定数バッファ: Bright ----
    brightCb_ = dxCommon->CreateBufferResource(256);
    brightCb_->Map(0, nullptr, reinterpret_cast<void**>(&brightCbData_));
    brightCbData_->threshold = 0.7f;
    brightCbData_->intensity = 1.0f;

    // ---- 定数バッファ: Blur H / V ----
    for (int i = 0; i < 2; i++) {
        blurCb_[i] = dxCommon->CreateBufferResource(256);
        blurCb_[i]->Map(0, nullptr, reinterpret_cast<void**>(&blurCbData_[i]));
    }
    // H
    blurCbData_[0]->dirX = 1.f; blurCbData_[0]->dirY = 0.f;
    blurCbData_[0]->texW = 1.f / (float)W; blurCbData_[0]->texH = 1.f / (float)H;
    // V
    blurCbData_[1]->dirX = 0.f; blurCbData_[1]->dirY = 1.f;
    blurCbData_[1]->texW = 1.f / (float)W; blurCbData_[1]->texH = 1.f / (float)H;
}

// ============================================================
// 終了処理
// ============================================================

void BloomEffect::Finalize()
{
    for (int i = 0; i < 2; i++) {
        if (blurCbData_[i]) { blurCb_[i]->Unmap(0, nullptr); blurCbData_[i] = nullptr; }
        blurCb_[i].Reset();
        bloomTex_[i].Reset();
        bloomRtvHeap_[i].Reset();
    }
    if (brightCbData_) { brightCb_->Unmap(0, nullptr); brightCbData_ = nullptr; }
    brightCb_.Reset();
    combinePSO_.Reset(); combineRS_.Reset();
    blurPSO_.Reset();   blurRS_.Reset();
    brightPSO_.Reset(); brightRS_.Reset();
    sceneTexture_.Reset(); sceneRtvHeap_.Reset();
}

// ============================================================
// BeginScene / EndScene
// ============================================================

void BloomEffect::BeginScene()
{
    auto* cmd = dxCommon_->GetCommandList();

    if (!sceneFirstFrame_) {
        Barrier(cmd, sceneTexture_.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    sceneFirstFrame_ = false;

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(1, &sceneRtvHandle_, FALSE, &dsv);

    constexpr float kClear[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
    cmd->ClearRenderTargetView(sceneRtvHandle_, kClear, 0, nullptr);

    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)WinApp::kClientWidth, (float)WinApp::kClientHeight, 0.f, 1.f };
    D3D12_RECT sc     = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
}

void BloomEffect::EndScene()
{
    auto* cmd = dxCommon_->GetCommandList();
    Barrier(cmd, sceneTexture_.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

// ============================================================
// Apply（4 パス）
// ============================================================

void BloomEffect::Apply(SrvManager* srvManager)
{
    auto* cmd     = dxCommon_->GetCommandList();
    const UINT W  = WinApp::kClientWidth;
    const UINT H  = WinApp::kClientHeight;

    D3D12_VIEWPORT vp = { 0.f, 0.f, (float)W, (float)H, 0.f, 1.f };
    D3D12_RECT     sc = { 0, 0, (LONG)W, (LONG)H };
    constexpr float kBlack[4] = { 0.f, 0.f, 0.f, 0.f };

    // ===========================================
    // Pass 1: Bright Pass  scene → bloomTex_[0]
    // ===========================================
    {
        // bloomTex_[0] を RTV 状態にする
        if (bloomInSrv_[0]) {
            Barrier(cmd, bloomTex_[0].Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
            bloomInSrv_[0] = false;
        }

        cmd->OMSetRenderTargets(1, &bloomRtvHandle_[0], FALSE, nullptr);
        cmd->ClearRenderTargetView(bloomRtvHandle_[0], kBlack, 0, nullptr);
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);

        cmd->SetGraphicsRootSignature(brightRS_.Get());
        cmd->SetPipelineState(brightPSO_.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootConstantBufferView(0, brightCb_->GetGPUVirtualAddress());
        cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(sceneSrvIndex_));
        cmd->DrawInstanced(3, 1, 0, 0);

        // bloomTex_[0] → SRV
        Barrier(cmd, bloomTex_[0].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        bloomInSrv_[0] = true;
    }

    // ===========================================
    // Pass 2: Blur H  bloomTex_[0] → bloomTex_[1]
    // ===========================================
    {
        if (bloomInSrv_[1]) {
            Barrier(cmd, bloomTex_[1].Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
            bloomInSrv_[1] = false;
        }

        cmd->OMSetRenderTargets(1, &bloomRtvHandle_[1], FALSE, nullptr);
        cmd->ClearRenderTargetView(bloomRtvHandle_[1], kBlack, 0, nullptr);
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);

        cmd->SetGraphicsRootSignature(blurRS_.Get());
        cmd->SetPipelineState(blurPSO_.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootConstantBufferView(0, blurCb_[0]->GetGPUVirtualAddress()); // H
        cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(bloomSrvIndex_[0]));
        cmd->DrawInstanced(3, 1, 0, 0);

        Barrier(cmd, bloomTex_[1].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        bloomInSrv_[1] = true;
    }

    // ===========================================
    // Pass 3: Blur V  bloomTex_[1] → bloomTex_[0]
    // ===========================================
    {
        // bloomTex_[0] は SRV → RTV に戻す
        Barrier(cmd, bloomTex_[0].Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        bloomInSrv_[0] = false;

        cmd->OMSetRenderTargets(1, &bloomRtvHandle_[0], FALSE, nullptr);
        cmd->ClearRenderTargetView(bloomRtvHandle_[0], kBlack, 0, nullptr);
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);

        cmd->SetGraphicsRootSignature(blurRS_.Get());
        cmd->SetPipelineState(blurPSO_.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->SetGraphicsRootConstantBufferView(0, blurCb_[1]->GetGPUVirtualAddress()); // V
        cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(bloomSrvIndex_[1]));
        cmd->DrawInstanced(3, 1, 0, 0);

        Barrier(cmd, bloomTex_[0].Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        bloomInSrv_[0] = true;
    }

    // ===========================================
    // Pass 4: Combine  scene + bloomTex_[0] → バックバッファ
    // ===========================================
    {
        D3D12_CPU_DESCRIPTOR_HANDLE backBuffer = dxCommon_->GetCurrentBackBufferHandle();
        cmd->OMSetRenderTargets(1, &backBuffer, FALSE, nullptr);
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);

        cmd->SetGraphicsRootSignature(combineRS_.Get());
        cmd->SetPipelineState(combinePSO_.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // t0 = scene, t1 = bloom
        cmd->SetGraphicsRootDescriptorTable(0, srvManager->GetGPUDescriptorHandle(sceneSrvIndex_));
        cmd->SetGraphicsRootDescriptorTable(1, srvManager->GetGPUDescriptorHandle(bloomSrvIndex_[0]));
        cmd->DrawInstanced(3, 1, 0, 0);
    }
}
