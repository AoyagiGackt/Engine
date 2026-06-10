#include "Skydome.h"

void Skydome::Initialize(ModelCommon* modelCommon, Model* model)
{
    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model);

    // SkyDome.obj の球半径は約500unit。farClip=100 内に収めるため 0.19 倍(半径≒95) にする
    object_->SetScale({ 0.19f, 0.19f, 0.19f });

    object_->SetEnableLighting(false);
    object_->SetUseCubemap(false);
}

void Skydome::Update(Camera* camera)
{
    if (camera != nullptr) {
        Vector3 camPos = camera->GetTransform().translate;
        object_->SetPosition(camPos);
    }

    object_->SetRotation({ 0.0f, rotationOffsetY_, 0.0f });
    object_->Update();
}

void Skydome::Draw()
{
     if (object_) {
        object_->Draw();
    }
}

void Skydome::SetSkyColor(const Vector4& color)
{
    object_->SetColor(color);
}