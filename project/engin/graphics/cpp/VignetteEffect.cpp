#include "VignetteEffect.h"
#include "WinApp.h"
#include <cassert>

void VignetteEffect::Initialize(DirectXCommon* dxCommon)
{
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // 定数バッファの作成
    cbResource_ = dxCommon_->CreateBufferResource(sizeof(VignetteParams));
    cbResource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));

    // パイプラインステートとルートシグネチャの生成処理が必要
    // (GrayscaleEffectと同じくRootSignatureとPSOを生成してください)
}

void VignetteEffect::Apply()
{
    if (!enabled_)
        return;

    auto* cmdList = dxCommon_->GetCommandList();

    // 描画設定（バックバッファへ書き込み）
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon_->GetCurrentBackBufferHandle();
    cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // パイプラインと定数バッファの設定
    cmdList->SetGraphicsRootSignature(rootSignature_.Get());
    cmdList->SetPipelineState(pipelineState_.Get());
    cmdList->SetGraphicsRootConstantBufferView(0, cbResource_->GetGPUVirtualAddress());

    // 描画（VSで頂点IDから四角形を描画）
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);
}

// パラメータ取得/設定用のメソッド例
void VignetteEffect::SetIntensity(float v) { mappedData_->intensity = v; }
float VignetteEffect::GetIntensity() const { return mappedData_->intensity; }