/**
 * @file ShadowManager.cpp
 * @brief シャドウマップの生成と管理（ShadowManager）の実装
 */
#include "ShadowManager.h"
#include "PipelineHelper.h"
#include "EngineAssert.h"
#include "SrvManager.h"
#include "ModelCommon.h"
#include "GraphicsTuning.h"
#include <limits>
#include <cmath>

namespace engine::graphics {
namespace {
/** @brief シャドウマップの投影中心・ライト距離・描画範囲 */
struct ShadowProjectionTuning {
    Vector3 center = { 15.0f, 5.0f, 0.0f };
    float lightDistance = 40.0f;
    float width = 40.0f;
    float height = 25.0f;
    float nearClip = 0.1f;
    float farClip = 80.0f;
};

/** @brief 影の投影設定を初回にgraphics_tuning.jsonから読み込む */
const ShadowProjectionTuning& ProjectionTuning() {
    static const ShadowProjectionTuning kTuning = [] {
        ShadowProjectionTuning value;
        const float lower = -(std::numeric_limits<float>::max)();
        value.center.x = GraphicsTuning::Float("shadow.centerX", value.center.x, lower);
        value.center.y = GraphicsTuning::Float("shadow.centerY", value.center.y, lower);
        value.center.z = GraphicsTuning::Float("shadow.centerZ", value.center.z, lower);
        value.lightDistance = GraphicsTuning::Positive("shadow.lightDistance", value.lightDistance);
        value.width = GraphicsTuning::Positive("shadow.width", value.width);
        value.height = GraphicsTuning::Positive("shadow.height", value.height);
        value.nearClip = GraphicsTuning::Positive("shadow.nearClip", value.nearClip);
        value.farClip = GraphicsTuning::Positive("shadow.farClip", value.farClip);
        if (value.farClip <= value.nearClip) {
            const ShadowProjectionTuning defaults;
            value.nearClip = defaults.nearClip;
            value.farClip = defaults.farClip;
        }
        return value;
    }();
    return kTuning;
}
} // namespace

/** @brief 通常の影とカスケードの影で共通の投影中心を取得する */
Vector3 ShadowManager::SceneCenter() {
    return ProjectionTuning().center;
}

/** @brief 投影中心からライト方向と逆に設定距離だけ離れた位置を求める */
Vector3 ShadowManager::LightPosition(const Vector3& lightDirection) {
    const ShadowProjectionTuning& tuning = ProjectionTuning();
    return { tuning.center.x - lightDirection.x * tuning.lightDistance,
        tuning.center.y - lightDirection.y * tuning.lightDistance,
        tuning.center.z - lightDirection.z * tuning.lightDistance };
}


using namespace Microsoft::WRL;

/** @brief 確保した SRV を返却する（GPU が使い終わるまで SrvManager 側で再利用を待たせる） */
ShadowManager::~ShadowManager() {
    // 初期化済みのときだけ返却する
    if (srvManager_) {
        srvManager_->Free(shadowSrvIndex_);
    }
}

// 初期化

/** @brief シャドウマップ用の深度テクスチャと DSV・SRV を作成して初期化する */
void ShadowManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager) {
    // 作り直す場合は、前回確保した SRV を先に返却して二重確保を防ぐ
    if (srvManager_) {
        srvManager_->Free(shadowSrvIndex_);
    }
    srvManager_ = nullptr;
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // 深度を書き込んで後から読むため R32_TYPELESS で作る（DSV: D32_FLOAT, SRV: R32_FLOAT）
    shadowMapResource_ = pipelineHelper::CreateDepthTexture(device, kShadowMapSize, kShadowMapSize, 1, DXGI_FORMAT_R32_TYPELESS);

    // DSV ヒープ（シャドウマップ専用）
    shadowDsvHeap_ = DirectXCommon::CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc { };
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(shadowMapResource_.Get(), &dsvDesc,
        shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart());

    // SRV（PS でシャドウマップを読む用）
    shadowSrvIndex_ = srvManager->Allocate();
    srvManager_ = srvManager;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc { };
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(shadowMapResource_.Get(), &srvDesc,
        srvManager->GetCPUDescriptorHandle(shadowSrvIndex_));
}

// ライト方向 → LightVP 行列を更新

/** @brief ライト方向から影用のビュー射影行列を求めて保持する */
void ShadowManager::Update(const Vector3& lightDir) {
    const ShadowProjectionTuning& tuning = ProjectionTuning();
    const Vector3 sceneCenter = SceneCenter();
    const Vector3 lightEye = LightPosition(lightDir);

    Matrix4x4 view {};
    if (!TryMakeLightViewMatrix(lightEye, sceneCenter, view)) {
        return;
    }
    const Matrix4x4 proj = MakeCenteredOrthographicMatrix(tuning.width, tuning.height, tuning.nearClip, tuning.farClip);

    lightVP_ = Multiply(view, proj);
}

// シャドウパス開始・終了

/** @brief シャドウパスを開始する（深度書き込みへの遷移・DSV のクリア・ビューポート設定） */
void ShadowManager::BeginShadowPass(ID3D12GraphicsCommandList* commandList) {
    if (!shadowMapInDepthWrite_) {
        DirectXCommon::TransitionBarrier(commandList, shadowMapResource_.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    shadowMapInDepthWrite_ = true;

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = shadowDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT vp = { 0, 0, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize) };
    commandList->RSSetViewports(1, &vp);
    commandList->RSSetScissorRects(1, &scissor);
}

/** @brief シャドウパス終了（バリア遷移: DEPTH_WRITE → SRV） */
void ShadowManager::EndShadowPass(ID3D12GraphicsCommandList* commandList) {
    DirectXCommon::TransitionBarrier(commandList, shadowMapResource_.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    shadowMapInDepthWrite_ = false;
}

/** @brief シャドウパスを1回分まとめて行う（開始 → モデル側の影用設定 → 影を落とす物の描画 → 終了） */
void ShadowManager::RunShadowPass(ID3D12GraphicsCommandList* commandList, ModelCommon* modelCommon,
    const std::function<void()>& drawCasters) {
    BeginShadowPass(commandList);
    modelCommon->BeginShadowPass();
    if (drawCasters) {
        drawCasters();
    }
    EndShadowPass(commandList);
}

// SRV セット（スロット 4）

/** @brief シャドウマップの SRV を影用のルートスロットへセットする */
void ShadowManager::SetShadowMap(ID3D12GraphicsCommandList* commandList, SrvManager* srvManager) {
    commandList->SetGraphicsRootDescriptorTable(pipelineHelper::modelRootSlot::kShadow,
        srvManager->GetGPUDescriptorHandle(shadowSrvIndex_));
}

} // namespace engine::graphics
