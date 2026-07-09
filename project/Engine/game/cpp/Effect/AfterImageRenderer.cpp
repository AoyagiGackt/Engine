#include "AfterImageRenderer.h"
#include "GameConstants.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

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
    for (auto& img : images_) { img.alpha = 0.0f; }
}

void AfterImageRenderer::Update(bool active, bool dense, const Vector3& pos, float yaw, float spinZ)
{
    for (auto& img : images_) {
        if (img.alpha > 0.0f) {
            img.alpha -= GameConstants::kFrameDeltaTime * 4.0f;
            if (img.alpha < 0.0f) {
                img.alpha = 0.0f;
            }
        }
    }

    if (!active) { return; }

    timer_ -= GameConstants::kFrameDeltaTime;
    if (timer_ <= 0.0f) {
        timer_    = dense ? kFastInterval : kSlowInterval;
        auto& img = images_[idx_];
        img.pos   = pos;
        img.yaw   = yaw;
        img.spinZ = spinZ;
        img.alpha = dense ? 0.75f : 0.55f;
        idx_      = (idx_ + 1) % kMaxImages;
    }
}

void AfterImageRenderer::Draw()
{
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    for (const auto& img : images_) {
        if (img.alpha <= 0.0f) { continue; }
        object_->SetPosition(img.pos);
        object_->SetRotation({ 0.0f, img.yaw, img.spinZ * kDegToRad });
        object_->SetColor({ 0.05f, 0.35f, 1.0f, img.alpha * 0.65f });
        object_->Update();
        object_->Draw();
    }
}
