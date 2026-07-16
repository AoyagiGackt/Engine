#include "EnemyEntity.h"
#include "GameConstants.h"
#include "GravityBody.h"
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
        if (ApplyGravityAndClampY(pos_.y, velY_, kGravity_, kGroundY_, kCeilingY_, -0.1f)) {
            isLaunched_ = false;
            justLanded_ = true;
        }

        object_->SetPosition(pos_);
    }

    UpdateAttack();

    object_->Update();
}

void EnemyEntity::UpdateAttack()
{
    justFiredAttack_ = false;

    // 打ち上げ中/撃破後はステートマシンを止め、丸腰の演出中に攻撃が発生しないようにする
    if (defeated_ || isLaunched_) {
        return;
    }

    attackTimer_ -= GameConstants::kFrameDeltaTime;
    if (attackTimer_ > 0.0f) {
        return;
    }

    switch (attackState_) {
    case AttackState::Idle:
        attackState_ = AttackState::Telegraph;
        attackTimer_ = kAttackTelegraph_;
        break;
    case AttackState::Telegraph:
        attackState_ = AttackState::Active;
        attackTimer_ = kAttackActive_;
        justFiredAttack_ = true;
        break;
    case AttackState::Active:
        attackState_ = AttackState::Idle;
        attackTimer_ = kAttackInterval_;
        break;
    }
}

void EnemyEntity::Draw()
{
    if (!visible_) {
        return;
    }
    object_->Draw();
}

void EnemyEntity::Launch(float velY)
{
    isLaunched_ = true;
    velY_ = velY;
}
