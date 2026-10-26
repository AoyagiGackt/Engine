/**
 * @file Object3dCommon.cpp
 * @brief 3Dオブジェクト共通のライト・描画設定（Object3dCommon）の実装
 */
#include "Object3dCommon.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "MakeAffine.h"
#include <cassert>
#include <cmath>
#include <numbers>

namespace engine::graphics {
namespace {

// ライトの初期方向
constexpr engine::Vector3 kDefaultLightDirection = { 0.5f, -0.8f, 0.3f };

// 時間帯によるライト方向
constexpr float kLightHorizontalScale = 0.7f;
constexpr float kLightVerticalScale = 0.7f;
constexpr float kLightVerticalBias = 0.3f;
constexpr float kLightDirectionZ = 0.3f;

}

using namespace Microsoft::WRL;

// 初期化

/** @brief 平行光源とポイントライトの定数バッファを作り、既定値を書き込んで初期化する */
void Object3dCommon::Initialize(DirectXCommon* dxCommon) {
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // ライト定数バッファの作成
    lightResource_ = pipelineHelper::CreateUploadBuffer(device, kConstantBufferAlignment);

    // Map したまま保持（毎フレーム UpdateLight() で書き換える）
    lightResource_->Map(0, nullptr, reinterpret_cast<void**>(&lightData_));

    // 固定ライト（白昼光）
    lightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    lightData_->direction = kDefaultLightDirection;
    lightData_->intensity = 1.0f;
    lightData_->ambientColor = { 1.0f, 1.0f, 1.0f };
    lightData_->ambientIntensity = kDefaultAmbientIntensity;

    // ポイントライト定数バッファ
    // CBV は 256 バイトアライン必須なので切り上げる
    pointLightResource_ = pipelineHelper::CreateUploadBuffer(
        device, AlignConstantBufferSize(sizeof(PointLightBuffer)));
    pointLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&pointLightData_));
    // ライト数 0 で初期化（シェーダーはループしない）
    pointLightData_->count = 0;
    pointLightData_->pad[0] = pointLightData_->pad[1] = pointLightData_->pad[2] = 0.0f;
}

// ライト更新（時刻に合わせて方向・色・アンビエントを変える）

/** @brief 時刻の割合に合わせてライトの方向・色・環境光を更新する（手動設定中は何もしない） */
void Object3dCommon::UpdateLight(float timeRatio) {
    // 手動オーバーライード中は時刻に基づく自動更新をスキップする
    if (manualLightOverride_) {
        return;
    }

    // ライト方向アニメーション
    // timeRatio 0.0 = 18:00（夕方）、0.5 = 0:00（真夜中）、1.0 = 6:00（朝方）
    // X 成分  夕方は右から(+)、朝は左から(-)
    float angle = std::numbers::pi_v<float> * timeRatio; // 0 → π
    float dirX = std::cos(angle) * kLightHorizontalScale; // +0.7 → 0 → -0.7
    float dirY = -(std::abs(std::sin(angle)) * kLightVerticalScale + kLightVerticalBias); // 常に下向き
    float dirZ = kLightDirectionZ;

    // 正規化
    float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    lightData_->direction = { dirX / len, dirY / len, dirZ / len };

    // 光源色・強度
    // 夕焼け(0.0) → 月明かり(0.5) → 朝焼け(1.0)
    struct ColorKey {
        float t;
        Vector4 color;
        float intensity;
    };
    static constexpr ColorKey kLightKeys[] = {
        { 0.00f, { 1.0f, 0.70f, 0.35f, 1.0f }, 1.1f }, // 18:00 夕焼け
        { 0.17f, { 0.60f, 0.65f, 0.90f, 1.0f }, 0.5f }, // 20:00 夜へ
        { 0.42f, { 0.55f, 0.60f, 0.85f, 1.0f }, 0.3f }, // 23:00 深夜
        { 0.58f, { 0.55f, 0.60f, 0.85f, 1.0f }, 0.3f }, // 3:00 深夜
        { 0.83f, { 0.70f, 0.60f, 0.75f, 1.0f }, 0.6f }, // 5:00 夜明け前
        { 1.00f, { 1.0f, 0.65f, 0.30f, 1.0f }, 1.1f }, // 6:00 朝焼け
    };

    // アンビエント色
    struct AmbientKey {
        float t;
        Vector3 color;
        float intensity;
    };
    static constexpr AmbientKey kAmbientKeys[] = {
        { 0.00f, { 0.25f, 0.18f, 0.12f }, 0.40f }, // 18:00 暖色アンビエント
        { 0.17f, { 0.08f, 0.10f, 0.20f }, 0.25f }, // 20:00 青みがかった夜
        { 0.42f, { 0.05f, 0.07f, 0.15f }, 0.20f }, // 23:00 深夜
        { 0.58f, { 0.05f, 0.07f, 0.15f }, 0.20f }, // 3:00 深夜
        { 0.83f, { 0.12f, 0.10f, 0.20f }, 0.25f }, // 5:00 夜明け前
        { 1.00f, { 0.25f, 0.15f, 0.10f }, 0.40f }, // 6:00 朝の暖色
    };

    constexpr int kCount = 6;
    for (int i = 0; i + 1 < kCount; ++i) {
        if (timeRatio <= kLightKeys[i + 1].t) {
            float span = kLightKeys[i + 1].t - kLightKeys[i].t;
            float t = (timeRatio - kLightKeys[i].t) / span;

            // 光源色補間
            const Vector4& a = kLightKeys[i].color;
            const Vector4& b = kLightKeys[i + 1].color;
            lightData_->color = { a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t, 1.0f };
            lightData_->intensity = kLightKeys[i].intensity
                + (kLightKeys[i + 1].intensity - kLightKeys[i].intensity) * t;

            // アンビエント補間
            const Vector3& ac = kAmbientKeys[i].color;
            const Vector3& bc = kAmbientKeys[i + 1].color;
            lightData_->ambientColor = { ac.x + (bc.x - ac.x) * t,
                ac.y + (bc.y - ac.y) * t,
                ac.z + (bc.z - ac.z) * t };
            lightData_->ambientIntensity = kAmbientKeys[i].intensity
                + (kAmbientKeys[i + 1].intensity - kAmbientKeys[i].intensity) * t;
            break;
        }
    }
}

// ライト方向の取得（ShadowManager に渡す用）

/** @brief 現在のライト方向を返す（未初期化なら真下） */
Vector3 Object3dCommon::GetLightDirection() const {
    if (!lightData_) {
        return { 0.0f, -1.0f, 0.0f };
    }

    return lightData_->direction;
}

// ライトをコマンドリストにセット（スロット 3）

/** @brief 平行光源とポイントライト群をモデル用のルートスロットへセットする */
void Object3dCommon::SetDefaultLight(ID3D12GraphicsCommandList* commandList) {
    // スロット 3: 平行光源（DirectionalLight）
    commandList->SetGraphicsRootConstantBufferView(pipelineHelper::modelRootSlot::kDirectionalLight, lightResource_->GetGPUVirtualAddress());
    // スロット 6: ポイントライト群（ModelCommon パイプライン用）
    // SkinCommon パイプラインではスロット 6 がスキニングパレットになるため、
    // SkinnedObject3d::Draw 内で SetPointLights(cmd, pipelineHelper::SkinnedRootSlot::kPointLights) を別途呼び直す
    SetPointLights(commandList, pipelineHelper::modelRootSlot::kPointLights);
}

/** @brief ポイントライトを指定スロットにセットする */
void Object3dCommon::SetPointLights(ID3D12GraphicsCommandList* commandList, UINT rootParamSlot) {
    if (!pointLightData_) {
        return;
    }
    pointLightData_->count = pointLightCount_;
    commandList->SetGraphicsRootConstantBufferView(rootParamSlot,
        pointLightResource_->GetGPUVirtualAddress());
}

} // namespace engine::graphics
