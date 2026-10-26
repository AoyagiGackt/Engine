/**
 * @file OutlineEffect.cpp
 * @brief モデルの輪郭線（OutlineEffect）の実装
 */
#include "OutlineEffect.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "EngineAssert.h"

namespace engine::graphics {

using namespace Microsoft::WRL;

/** @brief アウトライン用のルートシグネチャ・PSO・定数バッファを生成して初期化する */
void OutlineEffect::Initialize(DirectXCommon* dxCommon) {
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();

    // ルートシグネチャの設定

    // slot 0 (ALL shaders, b0)  OutlineParams
    //   VS では width を使って頂点を法線方向に押し出す
    //   PS では color を使ってピクセルを単一色で塗る
    //   ALL にすることで VS と PS が同じスロットで同じ定数バッファを参照できる
    // slot 1 (VS only, b1)  TransformationMatrix
    //   Object3d が持つ WVP 行列などをここで受け取る
    D3D12_ROOT_PARAMETER rootParams[2] = { };
    rootParams[0] = pipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[1] = pipelineHelper::MakeCbvParam(1, D3D12_SHADER_VISIBILITY_VERTEX);

    rootSignature_ = pipelineHelper::CreateRootSignature(device, rootParams, 2,
        nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rootSignature_);

    // OutlineVS  法線方向にクリップ空間で頂点を膨らませる
    // OutlinePS  単色で塗りつぶすだけ

    // 頂点レイアウト（Model::VertexData と一致させること）

    D3D12_INPUT_ELEMENT_DESC inputElem[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // PSO（パイプラインステートオブジェクト）の設定

    // ブレンド  アルファブレンド（半透明アウトラインも対応）

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineHelper::MakeEffectMeshPsoDesc(BlendMode::kAlpha, D3D12_DEPTH_WRITE_MASK_ALL);
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { inputElem, _countof(inputElem) };

    // 前面カリング（CULL_FRONT）がアウトライン手法の核心
    // 通常は背面（CULL_BACK）をカリングするが、ここでは前面をカリングすることで
    // 法線方向に膨らんだメッシュの外側に見える裏面だけが描画される = アウトライン
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;

    pipelineState_ = pipelineHelper::CreateGraphicsPipeline(dxCommon, psoDesc,
        L"Resources/shaders/outline/OutlineVS.hlsl", L"Resources/shaders/outline/OutlinePS.hlsl");

    // 定数バッファ（アウトライン色と幅）

    // 256 バイト確保するのは CBV が 256 バイトアライメント必須のため
    cbResource_ = dxCommon->CreateBufferResource(kConstantBufferAlignment);
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&cbData_));
    *cbData_ = OutlineParams { }; // デフォルト値で初期化（黒、幅 0.02）
}

/** @brief GPU リソースを解放する */
void OutlineEffect::Finalize() {
    // Map 済みのバッファを解放
    pipelineHelper::ReleaseEffectResources(cbResource_, cbData_, pipelineState_, rootSignature_);
}

/** @brief アウトライン描画パスを開始する */
void OutlineEffect::BeginOutlinePass() {
    auto* cmdList = dxCommon_->GetCommandList();

    // アウトライン用のルートシグネチャと PSO にコマンドリストを切り替える
    pipelineHelper::SetTrianglePipeline(cmdList, rootSignature_.Get(), pipelineState_.Get());

    // slot 0 (b0)  OutlineParams（色と幅）を定数バッファとしてバインド
    // この後 Object3d::DrawOutline(this) を呼ぶと slot 1 (b1) に変換行列がバインドされる
    cmdList->SetGraphicsRootConstantBufferView(pipelineHelper::outlineRootSlot::kParameters, cbResource_->GetGPUVirtualAddress());
}

} // namespace engine::graphics
