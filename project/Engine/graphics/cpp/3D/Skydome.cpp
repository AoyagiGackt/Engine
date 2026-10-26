/**
 * @file Skydome.cpp
 * @brief 空の球（Skydome）の読み込みと描画の実装
 */
#include "Skydome.h"
#include "ModelManager.h"

namespace engine::graphics {
namespace {

// 天球モデルのスケール
constexpr float kSkydomeScale = 0.19f;
// 既定の空のモデルとテクスチャ
constexpr const char* kDefaultModelPath = "Resources/SkyDome/SkyDome.obj";
constexpr const char* kDefaultTexturePath = "Resources/SkyDome/skySphere.png";

}

/** @brief 天球オブジェクトの生成と設定 */
void Skydome::Initialize(ModelCommon* modelCommon, Model* model) {
    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model);

    // SkyDome.obj の球半径は約500unitfarClip=100 内に収めるため 0.19 倍(半径≒95) にする
    object_->SetScale({ kSkydomeScale, kSkydomeScale, kSkydomeScale });

    object_->SetEnableLighting(false);
    object_->SetUseCubemap(false);
}

/** @brief 既定の空のモデル（Resources/SkyDome）を読み込んで天球を作る */
void Skydome::InitializeDefault(ModelCommon* modelCommon) {
    // モデルは ModelManager が持ち、同じ空を使う画面どうしで共有する
    Initialize(modelCommon, ModelManager::GetInstance()->GetOrLoad(modelCommon, kDefaultModelPath, kDefaultTexturePath));
}

/** @brief カメラの位置を取得し、天球をその場所に移動させる */
void Skydome::Update(Camera* camera) {
    if (camera != nullptr) {
        Vector3 camPos = camera->GetTransform().translate;
        object_->SetPosition(camPos);
    }

    object_->SetRotation({ 0.0f, rotationOffsetY_, 0.0f });
    object_->Update();
}

/** @brief 天球を描画 */
void Skydome::Draw() {
    if (object_) {
        object_->Draw();
    }
}

/** @brief 天球全体に色味を乗算する（デフォルトは白 = 無変化） */
void Skydome::SetSkyColor(const Vector4& color) {
    object_->SetColor(color);
}
} // namespace engine::graphics
