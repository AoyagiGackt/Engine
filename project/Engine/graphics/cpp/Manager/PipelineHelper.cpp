/**
 * @file PipelineHelper.cpp
 * @brief ルートシグネチャ・PSO を作る時の共通の補助関数の実装
 */
#include "PipelineHelper.h"
#include <algorithm>
#include <iterator>
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include "SrvManager.h"
#include "Object3dCommon.h"
#include "ShadowManager.h"
#include "WinApp.h"

namespace engine::graphics::pipelineHelper {
/** @brief 単一要素・単一ミップの行優先バッファ記述を作る */
D3D12_RESOURCE_DESC MakeBufferDesc(UINT64 size) {
    D3D12_RESOURCE_DESC desc { };
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}


/** @brief t{baseRegister} を1つ参照するSRVディスクリプタレンジを作る */
D3D12_DESCRIPTOR_RANGE MakeSrvRange(UINT baseRegister) {
    D3D12_DESCRIPTOR_RANGE range { };
    range.BaseShaderRegister = baseRegister;
    range.NumDescriptors = 1;
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return range;
}

/** @brief CBV1つのルートパラメータを作る */
D3D12_ROOT_PARAMETER MakeCbvParam(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER param { };
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility = visibility;
    param.Descriptor.ShaderRegister = shaderRegister;
    return param;
}

/** @brief ディスクリプタレンジ1つのテーブル型ルートパラメータを作る */
D3D12_ROOT_PARAMETER MakeSrvTableParam(const D3D12_DESCRIPTOR_RANGE* range, D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER param { };
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.ShaderVisibility = visibility;
    param.DescriptorTable.pDescriptorRanges = range;
    param.DescriptorTable.NumDescriptorRanges = 1;
    return param;
}

/** @brief 標準の静的サンプラー2本（s0: リニアWRAP、s1: シャドウマップPCF比較）を作る */
std::array<D3D12_STATIC_SAMPLER_DESC, 2> MakeDefaultSamplers(float s0MaxLod) {
    std::array<D3D12_STATIC_SAMPLER_DESC, 2> samplers { };
    // s0: 通常テクスチャ用リニアフィルタ
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].MaxLOD = s0MaxLod;
    samplers[0].ShaderRegister = 0; // s0
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s1: シャドウマップ PCF 比較サンプラー
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1; // s1
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    return samplers;
}

/** @brief ルートシグネチャをシリアライズして生成する */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(
    ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc) {
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    if (FAILED(hr)) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    if (FAILED(hr)) {
        return nullptr;
    }
    return rootSignature;
}

/** @brief ブレンドモード別のレンダーターゲットブレンド設定を作る */
D3D12_RENDER_TARGET_BLEND_DESC MakeBlendDesc(BlendMode mode) {
    /** @brief ブレンドモード1つぶんの色の合成式 */
    struct BlendFactors {
        BOOL enable;
        D3D12_BLEND src;
        D3D12_BLEND dest;
        D3D12_BLEND_OP op;
    };
    // BlendMode の並び順
    static constexpr BlendFactors kBlendFactors[static_cast<size_t>(BlendMode::kCount)] = {
        { FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD }, // None
        { TRUE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD }, // Alpha
        { TRUE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE, D3D12_BLEND_OP_ADD }, // Add
        { TRUE, D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE, D3D12_BLEND_OP_REV_SUBTRACT }, // Subtract
        { TRUE, D3D12_BLEND_DEST_COLOR, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD }, // Multiply
    };
    const size_t index = static_cast<size_t>(mode);
    const BlendFactors& factors = kBlendFactors[index < std::size(kBlendFactors) ? index : 0];

    D3D12_RENDER_TARGET_BLEND_DESC blend { };
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.BlendEnable = factors.enable;
    blend.SrcBlend = factors.src;
    blend.DestBlend = factors.dest;
    blend.BlendOp = factors.op;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    return blend;
}

/** @brief 3D描画の標準PSO設定を埋めたひな形を作る */
D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeDefault3dPsoDesc() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc { };
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.SampleDesc.Count = 1;
    return psoDesc;
}

/** @brief 深度を使わない全画面パスの共通設定を作る */
D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeFullscreenPsoDesc() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc { };
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    return desc;
}

/** @brief 呼び出し側で確定した設定からPSOを生成し、失敗を検出する */
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipeline(
    ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline));
    ENGINE_ASSERT(SUCCEEDED(hr));
    return pipeline;
}

/** @brief 指定したCSとルートシグネチャからコンピュートPSOを作る */
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateComputePipeline(
    DirectXCommon* dxCommon, ID3D12RootSignature* rootSignature, const wchar_t* shaderPath) {
    // シェーダーの生存期間をPSO生成が完了するまで保つ。
    const auto shader = dxCommon->CompileShader(shaderPath, L"cs_6_0");
    ENGINE_ASSERT(shader);
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc { };
    desc.pRootSignature = rootSignature;
    desc.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = dxCommon->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline));
    ENGINE_ASSERT(SUCCEEDED(hr));
    return pipeline;
}

/** @brief アドレス方式以外をピクセル用の既定値で埋める */
D3D12_STATIC_SAMPLER_DESC MakePixelSampler(D3D12_FILTER filter,
    D3D12_TEXTURE_ADDRESS_MODE addressU, D3D12_TEXTURE_ADDRESS_MODE addressV,
    D3D12_TEXTURE_ADDRESS_MODE addressW) {
    D3D12_STATIC_SAMPLER_DESC sampler { };
    sampler.Filter = filter;
    sampler.AddressU = addressU;
    sampler.AddressV = addressV;
    sampler.AddressW = addressW;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    return sampler;
}

/** @brief 全画面パスでよく使う、リニア補間・端を伸ばす（クランプ）ピクセル用サンプラー s0 を作る */
D3D12_STATIC_SAMPLER_DESC MakeLinearClampSampler() {
    return MakePixelSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
}

/** @brief 三角形パスの共通状態を順に設定する */
void SetTrianglePipeline(ID3D12GraphicsCommandList* cmd,
    ID3D12RootSignature* rootSignature, ID3D12PipelineState* pipelineState) {
    cmd->SetGraphicsRootSignature(rootSignature);
    cmd->SetPipelineState(pipelineState);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

/** @brief 頂点バッファを使わない全画面三角形を描く */
void DrawFullscreenTriangle(ID3D12GraphicsCommandList* cmd) {
    constexpr UINT kFullscreenVertexCount = 3;
    cmd->DrawInstanced(kFullscreenVertexCount, 1, 0, 0);
}

/** @brief ビューポートとクリップ範囲を同じパスへ適用する */
void SetViewportAndScissor(ID3D12GraphicsCommandList* cmd,
    const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor) {
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
}

/** @brief 今のバックバッファを深度なしの描画先にし、ビューポートとシザーもバックバッファに合わせる */
void BindBackBufferTarget(DirectXCommon* dxCommon, ID3D12GraphicsCommandList* cmd) {
    // 固定解像度の深度は使わず、リサイズ後のバックバッファの大きさで描く
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon->GetCurrentBackBufferHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    SetViewportAndScissor(cmd, dxCommon->GetBackBufferViewport(), dxCommon->GetBackBufferScissorRect());
}

/** @brief ビューポートとシザーを内部解像度（WinApp::kClientWidth × kClientHeight）全体に合わせる */
void SetClientViewportAndScissor(ID3D12GraphicsCommandList* cmd) {
    // オフスクリーンのテクスチャは内部解像度で作っているので、その全面に描く
    const D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight), 0.0f, 1.0f };
    const D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    SetViewportAndScissor(cmd, viewport, scissor);
}

/** @brief バックバッファと同じ形式・大きさの、画面を写し取るためのテクスチャと SRV を作る */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBackBufferCopyTexture(DirectXCommon* dxCommon,
    SrvManager* srvManager, uint32_t& outSrvIndex) {
    // 形式はバックバッファに合わせ、大きさはクライアント領域にする
    const DXGI_FORMAT format = dxCommon->GetCurrentBackBufferResource()->GetDesc().Format;
    D3D12_RESOURCE_DESC desc = MakeRenderTextureDesc(WinApp::kClientWidth, WinApp::kClientHeight, format);
    desc.Flags = D3D12_RESOURCE_FLAG_NONE; // コピー先なので描画先にはしない

    D3D12_HEAP_PROPERTIES heap { };
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    const HRESULT hr = dxCommon->GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&texture));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // シェーダーから読めるよう SRV を作る
    outSrvIndex = srvManager->Allocate();
    srvManager->CreateSrvForTexture2D(outSrvIndex, texture.Get(), format, 1);
    return texture;
}

/** @brief コピー前後にソースとコピー先のリソース状態を戻す */
void CaptureRenderTarget(ID3D12GraphicsCommandList* cmd,
    ID3D12Resource* source, ID3D12Resource* destination) {
    constexpr UINT kBarrierCount = 2;
    const D3D12_RESOURCE_BARRIER toCopy[kBarrierCount] = {
        DirectXCommon::MakeTransitionBarrier(source, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
        DirectXCommon::MakeTransitionBarrier(destination, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
    };
    cmd->ResourceBarrier(kBarrierCount, toCopy);
    cmd->CopyResource(destination, source);
    const D3D12_RESOURCE_BARRIER toRestore[kBarrierCount] = {
        DirectXCommon::MakeTransitionBarrier(source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        DirectXCommon::MakeTransitionBarrier(destination, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    };
    cmd->ResourceBarrier(kBarrierCount, toRestore);
}

/** @brief シェーダーの実体をPSO生成まで保持し、バイトコードを設定する */
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipeline(
    DirectXCommon* dxCommon, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
    const wchar_t* vertexShaderPath, const wchar_t* pixelShaderPath) {
    const auto vertexShader = dxCommon->CompileShader(vertexShaderPath, L"vs_6_0");
    const auto pixelShader = dxCommon->CompileShader(pixelShaderPath, L"ps_6_0");
    auto shaderDesc = desc;
    shaderDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    shaderDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    return CreateGraphicsPipeline(dxCommon->GetDevice(), shaderDesc);
}

/** @brief コンピュートシェーダーが書き込むバッファ（DEFAULT ヒープ・UAV 可）を COMMON 状態で確保する */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateUnorderedAccessBuffer(ID3D12Device* device, UINT64 size) {
    D3D12_HEAP_PROPERTIES heap { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC desc = MakeBufferDesc(size);
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    // バッファは常に COMMON で作られる（別の状態を指定すると警告 #1328 になる）
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
    ENGINE_ASSERT(SUCCEEDED(hr));
    return resource;
}

/** @brief アップロードヒープ上に単一の行優先バッファを生成する */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device* device, UINT64 size) {
    D3D12_HEAP_PROPERTIES heap { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC desc = MakeBufferDesc(size);
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
    ENGINE_ASSERT(SUCCEEDED(hr));
    return resource;
}

/** @brief テクスチャ範囲の寿命を呼び出し側に保ったままモデル共通のスロットを作る */
std::array<D3D12_ROOT_PARAMETER, kModelRootParameterCount> MakeModelRootParameters(
    const std::array<D3D12_DESCRIPTOR_RANGE, kModelTextureRangeCount>& ranges) {
    return {
        MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL), // 0: マテリアル (PS, b0)
        MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // 1: 変換行列 (VS, b0)
        MakeSrvTableParam(&ranges[0], D3D12_SHADER_VISIBILITY_PIXEL), // 2: テクスチャ (PS, t0)
        MakeCbvParam(1, D3D12_SHADER_VISIBILITY_PIXEL), // 3: 平行光源 (PS, b1)
        MakeSrvTableParam(&ranges[1], D3D12_SHADER_VISIBILITY_PIXEL), // 4: シャドウマップ (PS, t1)
        MakeSrvTableParam(&ranges[2], D3D12_SHADER_VISIBILITY_PIXEL), // 5: キューブマップ (PS, t2)
        MakeCbvParam(2, D3D12_SHADER_VISIBILITY_PIXEL), // 6: 点光源の配列 (PS, b2)
        MakeSrvTableParam(&ranges[3], D3D12_SHADER_VISIBILITY_PIXEL), // 7: 法線マップ (PS, t3)
    };
}

/** @brief レンダーターゲットとして使う2Dテクスチャの共通項目を埋める */
D3D12_RESOURCE_DESC MakeRenderTextureDesc(UINT width, UINT height, DXGI_FORMAT format) {
    D3D12_RESOURCE_DESC desc { };
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    return desc;
}

/** @brief 描画先テクスチャを作り、RTV と SRV を1つずつ作る */
void CreateRenderTarget(ID3D12Device* device, SrvManager* srvManager, UINT width, UINT height,
    const RenderTargetFormats& formats, const float (&clearColor)[4], const RenderTargetViews& out,
    D3D12_RESOURCE_STATES initialState) {
    // 消去値を付けて、指定の状態でテクスチャを作る（消去値を登録しておくとドライバのクリア最適化が効く）
    D3D12_HEAP_PROPERTIES heapProps = { };
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clearValue = { };
    clearValue.Format = formats.rtv;
    std::copy(std::begin(clearColor), std::end(clearColor), clearValue.Color);
    const D3D12_RESOURCE_DESC desc = MakeRenderTextureDesc(width, height, formats.resource);
    const HRESULT hr = device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &desc, initialState,
        &clearValue, IID_PPV_ARGS(&out.texture));
    ENGINE_ASSERT(SUCCEEDED(hr));

    // RTV 1つぶんのヒープを作り、そこに RTV を作る
    out.rtvHeap = DirectXCommon::CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    out.rtvHandle = out.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = { };
    rtvDesc.Format = formats.rtv;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(out.texture.Get(), &rtvDesc, out.rtvHandle);

    // シェーダーから読むための SRV を SrvManager で確保する
    out.srvIndex = srvManager->Allocate();
    srvManager->CreateSrvForTexture2D(out.srvIndex, out.texture.Get(), formats.srv, 1);
}

/** @brief 一時記述を組み立て、共通のシリアライズ処理へ渡す */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(
    ID3D12Device* device, const D3D12_ROOT_PARAMETER* parameters, UINT parameterCount,
    const D3D12_STATIC_SAMPLER_DESC* samplers, UINT samplerCount,
    D3D12_ROOT_SIGNATURE_FLAGS flags) {
    D3D12_ROOT_SIGNATURE_DESC desc { };
    desc.pParameters = parameters;
    desc.NumParameters = parameterCount;
    desc.pStaticSamplers = samplers;
    desc.NumStaticSamplers = samplerCount;
    desc.Flags = flags;
    return CreateRootSignature(device, desc);
}

/** @brief 3D演出の共通深度設定にブレンドと書き込み可否を適用する */
D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeEffectMeshPsoDesc(
    BlendMode blendMode, D3D12_DEPTH_WRITE_MASK depthWriteMask) {
    auto desc = MakeDefault3dPsoDesc();
    desc.BlendState.RenderTarget[0] = MakeBlendDesc(blendMode);
    desc.DepthStencilState.DepthWriteMask = depthWriteMask;
    return desc;
}

/** @brief CreateVertexColorEffectPipeline で作ったパイプラインで、頂点色の三角形の並びを描く */
void DrawVertexColorEffect(ID3D12GraphicsCommandList* cmd, ID3D12RootSignature* rootSignature,
    ID3D12PipelineState* pipelineState, const D3D12_VERTEX_BUFFER_VIEW& vertexBuffer,
    D3D12_GPU_VIRTUAL_ADDRESS sceneConstants, UINT vertexCount) {
    SetTrianglePipeline(cmd, rootSignature, pipelineState);
    cmd->IASetVertexBuffers(0, 1, &vertexBuffer);
    cmd->SetGraphicsRootConstantBufferView(vertexColorRootSlot::kScene, sceneConstants);
    cmd->DrawInstanced(vertexCount, 1, 0, 0);
}

/** @brief 位置と頂点色だけを使い、深度を更新せず加算描画する資源を生成する */
void CreateVertexColorEffectPipeline(DirectXCommon* dxCommon,
    Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) {
    const auto parameter = MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX);
    rootSignature = CreateRootSignature(dxCommon->GetDevice(), &parameter, 1, nullptr, 0,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rootSignature);
    constexpr D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    auto desc = MakeEffectMeshPsoDesc(BlendMode::kAdd, D3D12_DEPTH_WRITE_MASK_ZERO);
    desc.pRootSignature = rootSignature.Get();
    desc.InputLayout = { kInputLayout, static_cast<UINT>(std::size(kInputLayout)) };
    pipelineState = CreateGraphicsPipeline(dxCommon, desc,
        L"Resources/shaders/bladeflash/BladeFlash.VS.hlsl", L"Resources/shaders/bladeflash/BladeFlash.PS.hlsl");
}

/** @brief 有効なライトとシャドウマップだけを標準スロットへ登録する */
void BindCommonLighting(ID3D12GraphicsCommandList* cmd, Object3dCommon* objectCommon, ShadowManager* shadowManager) {
    if (objectCommon) {
        objectCommon->SetDefaultLight(cmd);
    }
    if (shadowManager) {
        shadowManager->SetShadowMap(cmd, SrvManager::GetInstance());
    }
}

/** @brief 全画面キャプチャ用の共通スロットを設定してバックバッファへ描き戻す */
void DrawCapturedFullscreen(engine::DirectXCommon* dxCommon, SrvManager* srvManager,
    ID3D12RootSignature* signature, ID3D12PipelineState* pipeline,
    D3D12_GPU_VIRTUAL_ADDRESS constants, uint32_t textureSrvIndex) {
    auto* cmd = dxCommon->GetCommandList();
    BindBackBufferTarget(dxCommon, cmd);
    ID3D12DescriptorHeap* heaps[] = { srvManager->GetSrvDescriptorHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
    SetTrianglePipeline(cmd, signature, pipeline);
    cmd->SetGraphicsRootConstantBufferView(fullscreenRootSlot::kConstants, constants);
    cmd->SetGraphicsRootDescriptorTable(fullscreenRootSlot::kTexture, srvManager->GetGPUDescriptorHandle(textureSrvIndex));
    DrawFullscreenTriangle(cmd);
}

/** @brief 画面キャプチャを読む共通のルートシグネチャを構築する */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateCapturedFullscreenSignature(ID3D12Device* device) {
    return CreateConstantTextureSignature(device, D3D12_SHADER_VISIBILITY_PIXEL,
        MakeLinearClampSampler(), D3D12_ROOT_SIGNATURE_FLAG_NONE);
}

/** @brief 定数とテクスチャのスロット契約を共有し、用途ごとのサンプラーとフラグを適用する */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateConstantTextureSignature(ID3D12Device* device,
    D3D12_SHADER_VISIBILITY constantVisibility, const D3D12_STATIC_SAMPLER_DESC& sampler,
    D3D12_ROOT_SIGNATURE_FLAGS flags) {
    const auto range = MakeSrvRange(0);
    const D3D12_ROOT_PARAMETER parameters[] = {
        MakeCbvParam(0, constantVisibility),
        MakeSrvTableParam(&range, D3D12_SHADER_VISIBILITY_PIXEL),
    };
    return CreateRootSignature(device, parameters, static_cast<UINT>(std::size(parameters)),
        &sampler, 1, flags);
}

/** @brief 深度なしの全画面描画にシェーダーと合成方式を設定する */
Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateFullscreenPipeline(DirectXCommon* dxCommon,
    ID3D12RootSignature* signature, const wchar_t* pixelShaderPath, BlendMode blendMode) {
    auto desc = MakeFullscreenPsoDesc();
    desc.pRootSignature = signature;
    desc.BlendState.RenderTarget[0] = MakeBlendDesc(blendMode);
    return CreateGraphicsPipeline(dxCommon, desc,
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", pixelShaderPath);
}

/** @brief リソース状態を戻してからキャプチャ画像を全画面へ合成する */
void CaptureAndDrawFullscreen(bool active, DirectXCommon* dxCommon, SrvManager* srvManager,
    ID3D12Resource* captureTexture, ID3D12RootSignature* signature, ID3D12PipelineState* pipeline,
    ID3D12Resource* constantsResource, uint32_t textureSrvIndex) {
    if (!active || !captureTexture) {
        return;
    }
    CaptureRenderTarget(dxCommon->GetCommandList(), dxCommon->GetCurrentBackBufferResource(), captureTexture);
    DrawCapturedFullscreen(dxCommon, srvManager, signature, pipeline,
        constantsResource->GetGPUVirtualAddress(), textureSrvIndex);
}

/** @brief シェーダーと描画設定を保ち、合成方式だけを変えたPSO一式を作る */
void CreateBlendPipelines(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc,
    const std::wstring& namePrefix,
    Microsoft::WRL::ComPtr<ID3D12PipelineState> (&pipelines)[static_cast<size_t>(BlendMode::kCount)]) {
    auto blendDesc = desc;
    for (size_t i = 0; i < static_cast<size_t>(BlendMode::kCount); ++i) {
        blendDesc.BlendState.RenderTarget[0] = MakeBlendDesc(static_cast<BlendMode>(i));
        pipelines[i] = CreateGraphicsPipeline(device, blendDesc);
        const std::wstring name = namePrefix + std::to_wstring(i);
        pipelines[i]->SetName(name.c_str());
    }
}

/** @brief 指定した記述と初期状態で既定ヒープのリソースを作る */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultResource(ID3D12Device* device,
    const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* clearValue) {
    // 記述と消去値をそのまま使い、既定ヒープに確保する
    const D3D12_HEAP_PROPERTIES heap = { D3D12_HEAP_TYPE_DEFAULT };
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, state, clearValue, IID_PPV_ARGS(&resource));
    ENGINE_ASSERT(SUCCEEDED(hr));
    return resource;
}

/** @brief 深度書き込み用の単一ミップテクスチャを作る */
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthTexture(ID3D12Device* device,
    UINT width, UINT height, UINT16 arraySize, DXGI_FORMAT format) {
    // 配列数とリソース形式を保って深度テクスチャを用意する
    auto desc = MakeRenderTextureDesc(width, height, format);
    desc.DepthOrArraySize = arraySize;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    return CreateDefaultResource(device, desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue);
}

/** @brief 深度付きシーン描画先を設定して既定色で消去する */
void BindSceneTarget(DirectXCommon* dxCommon, const D3D12_CPU_DESCRIPTOR_HANDLE& rtv) {
    // 共通の深度と消去色でシーン描画を開始する
    auto* cmd = dxCommon->GetCommandList();
    const auto dsv = dxCommon->GetDsvHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    cmd->ClearRenderTargetView(rtv, kOffscreenClearColor, 0, nullptr);
    SetClientViewportAndScissor(cmd);
}

/** @brief 描画先と全画面パスの定数・入力画像を設定する */
void BindFullscreenPass(ID3D12GraphicsCommandList* cmd, ID3D12RootSignature* signature,
    ID3D12PipelineState* pipeline, const D3D12_CPU_DESCRIPTOR_HANDLE& rtv,
    const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor,
    D3D12_GPU_VIRTUAL_ADDRESS constants, const D3D12_GPU_DESCRIPTOR_HANDLE& texture) {
    // 補助テクスチャを足す呼び出し元もあるので、描画の発行は呼び出し元に任せる
    SetTrianglePipeline(cmd, signature, pipeline);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    SetViewportAndScissor(cmd, viewport, scissor);
    cmd->SetGraphicsRootConstantBufferView(fullscreenRootSlot::kConstants, constants);
    cmd->SetGraphicsRootDescriptorTable(fullscreenRootSlot::kTexture, texture);
}

/** @brief 指定レジスタを参照する全段用のルートUAVを作る */
D3D12_ROOT_PARAMETER MakeUavParam(UINT shaderRegister) {
    // UAVの可視範囲とレジスタを揃える
    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter.Descriptor.ShaderRegister = shaderRegister;
    return parameter;
}

/** @brief 標準モデルが使うテクスチャ範囲の配列を作る */
std::array<D3D12_DESCRIPTOR_RANGE, kModelTextureRangeCount> MakeModelTextureRanges() {
    // テクスチャ・影・環境・法線の順を保つ
    std::array<D3D12_DESCRIPTOR_RANGE, kModelTextureRangeCount> ranges = {};
    for (UINT index = 0; index < ranges.size(); ++index) {
        ranges[index] = MakeSrvRange(index);
    }
    return ranges;
}

} // namespace engine::graphics::PipelineHelper
