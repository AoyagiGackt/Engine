/**
 * @file AfterImageRenderer.cpp
 * @brief プレイヤーの残像エフェクトの生成・更新・描画（AfterImageRenderer）の実装
 */
#include "AfterImageRenderer.h"
#include "GameConstants.h"
#include "GraphicsTuning.h"
using namespace engine::graphics;

namespace engine::game {
namespace {
/** @brief 残像の発生間隔・色・不透明度と減衰速度 */
struct AfterImageStyle {
    float fadeSpeed = 4.0f;
    float denseAlpha = 0.75f;
    float sparseAlpha = 0.55f;
    Vector4 color = { 0.05f, 0.35f, 1.0f, 1.0f };
    float alphaScale = 0.65f;
    float fastInterval = 0.03f;
    float slowInterval = 0.05f;
};

/** @brief 残像の見た目を初回にgraphics_tuning.jsonから読み込む */
const AfterImageStyle& ImageStyle() {
    static const AfterImageStyle style = [] {
        AfterImageStyle value;
        value.fadeSpeed = GraphicsTuning::Float("afterImage.fadeSpeed", value.fadeSpeed);
        value.denseAlpha = GraphicsTuning::Float("afterImage.denseAlpha", value.denseAlpha, 0.0f, 1.0f);
        value.sparseAlpha = GraphicsTuning::Float("afterImage.sparseAlpha", value.sparseAlpha, 0.0f, 1.0f);
        value.color = GraphicsTuning::Color("afterImage.color", value.color);
        value.alphaScale = GraphicsTuning::Float("afterImage.alphaScale", value.alphaScale);
        value.fastInterval = GraphicsTuning::Positive("afterImage.fastInterval", value.fastInterval);
        value.slowInterval = GraphicsTuning::Positive("afterImage.slowInterval", value.slowInterval);
        return value;
    }();
    return style;
}
}

/** @brief 描画に使うモデルを設定して初期化する */
void AfterImageRenderer::Initialize(ModelCommon* modelCommon, Model* model, float scale) {
    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model);
    object_->SetEnableLighting(false);
    object_->SetScale({ scale, scale, scale });
    object_->Update();
}

/** @brief 残像モデルを差し替える（覚醒フォーム切り替え用） */
void AfterImageRenderer::SetModel(Model* model, float scale) {
    object_->SetModel(model);
    object_->SetScale({ scale, scale, scale });
    for (auto& img : images_) {
        img.alpha = 0.0f;
    }
}

/** @brief 毎フレーム残像を更新する */
void AfterImageRenderer::Update(bool active, bool dense, const Vector3& pos, float yaw, float spinZ) {
    const AfterImageStyle& style = ImageStyle();
    for (auto& img : images_) {
        if (img.alpha > 0.0f) {
            img.alpha -= gameConstants::kFrameDeltaTime * style.fadeSpeed;
            if (img.alpha < 0.0f) {
                img.alpha = 0.0f;
            }
        }
    }

    if (!active) {
        return;
    }

    timer_ -= gameConstants::kFrameDeltaTime;
    if (timer_ <= 0.0f) {
        timer_ = dense ? style.fastInterval : style.slowInterval;
        auto& img = images_[idx_];
        img.pos = pos;
        img.yaw = yaw;
        img.spinZ = spinZ;
        img.alpha = dense ? style.denseAlpha : style.sparseAlpha;
        idx_ = (idx_ + 1) % kMaxImages;
    }
}

/** @brief 全残像を描画する（半透明のため通常の Draw() の後に呼ぶ） */
void AfterImageRenderer::Draw() {
    const AfterImageStyle& style = ImageStyle();
    constexpr float kDegToRad = gameConstants::kDegToRad;
    for (const auto& img : images_) {
        if (img.alpha <= 0.0f) {
            continue;
        }
        object_->SetPosition(img.pos);
        object_->SetRotation({ 0.0f, img.yaw, img.spinZ * kDegToRad });
        object_->SetColor({ style.color.x, style.color.y, style.color.z, img.alpha * style.alphaScale });
        object_->Update();
        object_->Draw();
    }
}

} // namespace engine::game
