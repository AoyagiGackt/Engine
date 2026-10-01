/**
 * @file AfterImageRenderer.cpp
 * @brief プレイヤーの残像エフェクトの生成・更新・描画（AfterImageRenderer）の実装
 */
#include "AfterImageRenderer.h"
#include "GameConstants.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr float kAfterImageFadeSpeed = 4.0f; // 1秒あたりに減らす不透明度
constexpr float kDenseAfterImageAlpha = 0.75f; // 乱舞中など密に出す時の初期不透明度
constexpr float kSparseAfterImageAlpha = 0.55f;
constexpr Vector3 kAfterImageColor = { 0.05f, 0.35f, 1.0f };
constexpr float kAfterImageAlphaScale = 0.65f;
}

void AfterImageRenderer::Initialize(ModelCommon* modelCommon, Model* model, float scale)
{
    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model);
    object_->SetEnableLighting(false);
    object_->SetScale({ scale, scale, scale });
    object_->Update();
}

void AfterImageRenderer::SetModel(Model* model, float scale)
{
    object_->SetModel(model);
    object_->SetScale({ scale, scale, scale });
    for (auto& img : images_) {
        img.alpha = 0.0f;
    }
}

void AfterImageRenderer::Update(bool active, bool dense, const Vector3& pos, float yaw, float spinZ)
{
    for (auto& img : images_) {
        if (img.alpha > 0.0f) {
            img.alpha -= GameConstants::kFrameDeltaTime * kAfterImageFadeSpeed;
            if (img.alpha < 0.0f) {
                img.alpha = 0.0f;
            }
        }
    }

    if (!active) {
        return;
    }

    timer_ -= GameConstants::kFrameDeltaTime;
    if (timer_ <= 0.0f) {
        timer_ = dense ? kFastInterval : kSlowInterval;
        auto& img = images_[idx_];
        img.pos = pos;
        img.yaw = yaw;
        img.spinZ = spinZ;
        img.alpha = dense ? kDenseAfterImageAlpha : kSparseAfterImageAlpha;
        idx_ = (idx_ + 1) % kMaxImages;
    }
}

void AfterImageRenderer::Draw()
{
    constexpr float kDegToRad = GameConstants::kDegToRad;
    for (const auto& img : images_) {
        if (img.alpha <= 0.0f) {
            continue;
        }
        object_->SetPosition(img.pos);
        object_->SetRotation({ 0.0f, img.yaw, img.spinZ * kDegToRad });
        object_->SetColor({ kAfterImageColor.x, kAfterImageColor.y, kAfterImageColor.z, img.alpha * kAfterImageAlphaScale });
        object_->Update();
        object_->Draw();
    }
}
