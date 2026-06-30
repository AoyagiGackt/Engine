#include "OutlineEffect.h"
#include <cassert>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

void OutlineEffect::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();

    // =====================================================
    // ルートシグネチャの設定
    // =====================================================
    // slot 0 (ALL shaders, b0): OutlineParams
    //   VS では width を使って頂点を法線方向に押し出す
    //   PS では color を使ってピクセルを単一色で塗る
    //   ALL にすることで VS と PS が同じスロットで同じ定数バッファを参照できる
    // slot 1 (VS only, b1): TransformationMatrix
    //   Object3d が持つ WVP 行列などをここで受け取る
    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL; // VS と PS 両方で使う
    rootParams[0].Descriptor.ShaderRegister = 0; // b0
    rootParams[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX; // VS のみ
    rootParams[1].Descriptor.ShaderRegister = 1; // b1

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rsDesc.NumParameters = 2;
    rsDesc.pParameters   = rootParams;

    ComPtr<ID3DBlob> sigBlob, errBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(
        0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    assert(SUCCEEDED(hr));

    // =====================================================
    // シェーダーのコンパイル
    // =====================================================
    // OutlineVS: 法線方向にクリップ空間で頂点を膨らませる
    // OutlinePS: 単色で塗りつぶすだけ
    IDxcBlob* vsBlob = dxCommon->CompileShader(L"Resources/shaders/outline/OutlineVS.hlsl", L"vs_6_0");
    IDxcBlob* psBlob = dxCommon->CompileShader(L"Resources/shaders/outline/OutlinePS.hlsl", L"ps_6_0");

    // =====================================================
    // 頂点レイアウト（Model::VertexData と一致させること）
    // =====================================================
    D3D12_INPUT_ELEMENT_DESC inputElem[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // =====================================================
    // PSO（パイプラインステートオブジェクト）の設定
    // =====================================================
    // ブレンド: アルファブレンド（半透明アウトラインも対応）
    D3D12_RENDER_TARGET_BLEND_DESC blendRT = {};
    blendRT.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendRT.BlendEnable           = TRUE;
    blendRT.SrcBlend              = D3D12_BLEND_SRC_ALPHA;
    blendRT.DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
    blendRT.BlendOp               = D3D12_BLEND_OP_ADD;
    blendRT.SrcBlendAlpha         = D3D12_BLEND_ONE;
    blendRT.DestBlendAlpha        = D3D12_BLEND_ZERO;
    blendRT.BlendOpAlpha          = D3D12_BLEND_OP_ADD;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0]  = blendRT;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature  = rootSignature_.Get();
    psoDesc.InputLayout     = { inputElem, _countof(inputElem) };
    psoDesc.VS              = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS              = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState      = blendDesc;

    // 前面カリング（CULL_FRONT）がアウトライン手法の核心
    // 通常は背面（CULL_BACK）をカリングするが、ここでは前面をカリングすることで
    // 法線方向に膨らんだメッシュの「外側に見える裏面」だけが描画される = アウトライン
    psoDesc.RasterizerState.FillMode  = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode  = D3D12_CULL_MODE_FRONT;

    psoDesc.DepthStencilState.DepthEnable    = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DSVFormat             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.NumRenderTargets      = 1;
    psoDesc.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleMask            = D3D12_DEFAULT_SAMPLE_MASK;
    psoDesc.SampleDesc.Count      = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));

    // =====================================================
    // 定数バッファ（アウトライン色と幅）
    // =====================================================
    // 256 バイト確保するのは CBV が 256 バイトアライメント必須のため
    cbResource_ = dxCommon->CreateBufferResource(256);
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = OutlineParams{}; // デフォルト値で初期化（黒、幅 0.02）
}

void OutlineEffect::Finalize()
{
    // Map 済みのバッファを解放
    if (cbData_) {
        cbResource_->Unmap(0, nullptr);
        cbData_ = nullptr;
    }
    cbResource_.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();
}

void OutlineEffect::BeginOutlinePass()
{
    auto* cmdList = dxCommon_->GetCommandList();

    // アウトライン用のルートシグネチャと PSO にコマンドリストを切り替える
    cmdList->SetGraphicsRootSignature(rootSignature_.Get());
    cmdList->SetPipelineState(pipelineState_.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // slot 0 (b0): OutlineParams（色と幅）を定数バッファとしてバインド
    // この後 Object3d::DrawOutline(this) を呼ぶと slot 1 (b1) に変換行列がバインドされる
    cmdList->SetGraphicsRootConstantBufferView(0, cbResource_->GetGPUVirtualAddress());
}
