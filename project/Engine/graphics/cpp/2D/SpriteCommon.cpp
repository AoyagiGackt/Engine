#include "SpriteCommon.h"
#include "EngineAssert.h"
#include "PipelineHelper.h"
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

namespace PH = engine::graphics::PipelineHelper;

void SpriteCommon::Initialize(DirectXCommon* dxCommon)
{
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // --- ルートシグネチャの作成（Object3dPS.hlsl のバインディングに合わせて8スロット） ---
    D3D12_DESCRIPTOR_RANGE texRange = PH::MakeSrvRange(0); // t0
    // t1〜t3 は Object3dPS.hlsl が要求するため宣言だけ必要（スプライトは実際にはアクセスしない）
    D3D12_DESCRIPTOR_RANGE shadowRange = PH::MakeSrvRange(1); // t1
    D3D12_DESCRIPTOR_RANGE cubemapRange = PH::MakeSrvRange(2); // t2
    D3D12_DESCRIPTOR_RANGE normalMapRange = PH::MakeSrvRange(3); // t3

    D3D12_ROOT_PARAMETER rootParameters[8] = {
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL), // 0: マテリアル (PS, b0)
        PH::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // 1: 変換行列 (VS, b0)
        PH::MakeSrvTableParam(&texRange, D3D12_SHADER_VISIBILITY_PIXEL), // 2: テクスチャ (PS, t0)
        PH::MakeCbvParam(1, D3D12_SHADER_VISIBILITY_PIXEL), // 3: 平行光源 (PS, b1)
        PH::MakeSrvTableParam(&shadowRange, D3D12_SHADER_VISIBILITY_PIXEL), // 4: シャドウマップ (PS, t1) — enableLighting=false なので未使用
        PH::MakeSrvTableParam(&cubemapRange, D3D12_SHADER_VISIBILITY_PIXEL), // 5: キューブマップ (PS, t2) — useCubemap=false なので未使用
        PH::MakeCbvParam(2, D3D12_SHADER_VISIBILITY_PIXEL), // 6: ポイントライト配列 (PS, b2) — count=0 なので未使用
        PH::MakeSrvTableParam(&normalMapRange, D3D12_SHADER_VISIBILITY_PIXEL), // 7: 法線マップ (PS, t3) — useNormalMap=0 なので未使用
    };

    // スプライトは全ミップを使用する
    auto staticSamplers = PH::MakeDefaultSamplers(D3D12_FLOAT32_MAX);

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature { };
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);
    descriptionRootSignature.pStaticSamplers = staticSamplers.data();
    descriptionRootSignature.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
    rootSignature_ = PH::CreateRootSignature(device, descriptionRootSignature);
    ENGINE_ASSERT(rootSignature_);

    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dPS.hlsl", L"ps_6_0");

    // --- PSO の作成（アルファブレンド固定・深度テストなし） ---
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc = PH::MakeDefault3dPsoDesc();
    graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
    graphicsPipelineStateDesc.InputLayout = { PH::kStandardInputLayout, _countof(PH::kStandardInputLayout) };
    graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.BlendState.RenderTarget[0] = PH::MakeBlendDesc(BlendMode::Alpha);
    graphicsPipelineStateDesc.DepthStencilState.DepthEnable = FALSE; // 2Dは常に最前面に描く

    HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
        IID_PPV_ARGS(&graphicsPipelineState_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    defaultLightResource_ = dxCommon_->CreateBufferResource(256);

    // PointLightBuffer (b2) 用ダミーバッファ
    // struct サイズ: uint count(4) + float3 pad(12) + PointLight[8]*48 = 400 → 256 アライン → 512 bytes
    // count=0 でゼロ初期化するので GPU はライト配列を読まない
    defaultPointLightResource_ = dxCommon_->CreateBufferResource(512);
}

void SpriteCommon::CommonDrawSettings()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(graphicsPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ルートパラメータ3（DirectionalLight CBV b1）にダミーバッファをバインドする
    // ルートシグネチャ切り替え時に全パラメータがリセットされるため、
    // スプライトが enableLighting=false であっても有効なアドレスを渡す必要がある
    commandList->SetGraphicsRootConstantBufferView(3, defaultLightResource_->GetGPUVirtualAddress());

    // ルートパラメータ6（PointLightBuffer CBV b2）にダミーバッファをバインドする
    // スプライトはポイントライトを使用しないが、Object3dPS.hlsl が b2 を宣言しているため
    // Root Signature と整合させるために有効なアドレスが必要
    commandList->SetGraphicsRootConstantBufferView(6, defaultPointLightResource_->GetGPUVirtualAddress());
}