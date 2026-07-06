#include "EnemyEntity.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

void EnemyEntity::Initialize(ModelCommon* modelCommon, const Vector3& startPos)
{
    pos_ = startPos;

    model_ = std::make_unique<Model>();
    model_->Initialize(modelCommon,
        "Resources/block/block.obj",
        "Resources/monsterBall.png");

    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model_.get());
    object_->SetEnableLighting(false);
    object_->SetPosition(pos_);
    object_->Update();
}

void EnemyEntity::Update()
{
    justLanded_ = false;

    if (isLaunched_) {
        velY_ -= kGravity_;
        pos_.y += velY_;

        if (pos_.y <= kGroundY_) {
            pos_.y      = kGroundY_;
            velY_       = 0.0f;
            isLaunched_ = false;
            justLanded_ = true;
        }
        if (pos_.y > kCeilingY_) {
            pos_.y  = kCeilingY_;
            velY_  *= -0.1f;
        }

        object_->SetPosition(pos_);
    }

    object_->Update();
}

void EnemyEntity::Draw()
{
    if (!visible_) { return; }
    object_->Draw();
}

void EnemyEntity::Launch(float velY)
{
    isLaunched_ = true;
    velY_       = velY;
}
