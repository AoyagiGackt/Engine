/**
 * @file EnemyEntity.cpp
 * @brief EnemyEntityのプレイヤーの操作、戦闘、状態遷移に関する具体的な処理を実装するファイル
 */
#include "EnemyEntity.h"
#include "GameConstants.h"
#include "GravityBody.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

void EnemyEntity::Initialize(ModelCommon* modelCommon, const Vector3& startPos, WeaponType weaponType)
{
    pos_ = startPos;
    weaponType_ = weaponType;

    model_ = std::make_unique<Model>();
    model_->Initialize(modelCommon,
        "Resources/Knight/OBJ/KnightCharacter.obj",
        "Resources/Knight/OBJ/KnightCharacterPalette.png");

    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model_.get());
    object_->SetEnableLighting(true);
    object_->SetScale({ 0.2f, 0.2f, 0.2f });
    object_->SetPosition(pos_);
    object_->Update();

    const char* weaponPath = "Resources/Knight/OBJ/Sword.obj";
    const char* weaponTexture = "Resources/Knight/OBJ/SwordPalette.png";
    if (weaponType == WeaponType::Spear) {
        weaponPath = "Resources/Knight/OBJ/Katana.obj";
        weaponTexture = "Resources/Knight/OBJ/KatanaPalette.png";
        weaponScale_ = { 0.11f, 0.11f, 0.22f };
    } else if (weaponType == WeaponType::Hammer || weaponType == WeaponType::Axe) {
        weaponPath = "Resources/Knight/OBJ/Club.obj";
        weaponTexture = "Resources/Knight/OBJ/KnightCharacterPalette.png";
        weaponScale_ = { 0.16f, 0.16f, 0.16f };
    }
    weaponModel_ = std::make_unique<Model>();
    weaponModel_->Initialize(modelCommon, weaponPath, weaponTexture);
    weaponObject_ = std::make_unique<Object3d>();
    weaponObject_->Initialize(modelCommon);
    weaponObject_->SetModel(weaponModel_.get());
    weaponObject_->SetEnableLighting(true);
    weaponObject_->SetScale(weaponScale_);
    weaponObject_->SetPosition({ pos_.x + 0.35f, pos_.y + 0.75f, pos_.z + 0.15f });
    weaponObject_->SetRotation({ 0.0f, 0.0f, 0.4f });
    weaponObject_->Update();
}

void EnemyEntity::Update()
{
    justLanded_ = false;
    slowTimer_ = (std::max)(slowTimer_ - GameConstants::kFrameDeltaTime, 0.0f);

    if (std::abs(knockVelX_) > 0.001f) {
        pos_.x += knockVelX_ * (slowTimer_ > 0.0f ? 0.45f : 1.0f);
        knockVelX_ *= 0.82f;
        object_->SetPosition(pos_);
        weaponObject_->SetPosition({ pos_.x + 0.35f, pos_.y + 0.75f, pos_.z + 0.15f });
    }

    if (isLaunched_) {
        airComboTimer_ = (std::max)(airComboTimer_ - GameConstants::kFrameDeltaTime, 0.0f);
        const float gravity = airComboTimer_ > 0.0f ? kGravity_ * 0.28f : kGravity_;
        if (ApplyGravityAndClampY(pos_.y, velY_, gravity, kGroundY_, kCeilingY_, -0.1f)) {
            isLaunched_ = false;
            justLanded_ = true;
        }

        object_->SetPosition(pos_);
        weaponObject_->SetPosition({ pos_.x + 0.35f, pos_.y + 0.75f, pos_.z + 0.15f });
    }

    UpdateAttack();

    object_->Update();
    weaponObject_->Update();
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
        attackTimer_ = weaponType_ == WeaponType::Dagger ? 0.20f
            : weaponType_ == WeaponType::Spear ? 0.38f
            : (weaponType_ == WeaponType::Hammer || weaponType_ == WeaponType::Axe) ? 0.75f
            : kAttackTelegraph_;
        break;
    case AttackState::Telegraph:
        attackState_ = AttackState::Active;
        attackTimer_ = kAttackActive_;
        justFiredAttack_ = true;
        break;
    case AttackState::Active:
        attackState_ = AttackState::Idle;
        attackTimer_ = weaponType_ == WeaponType::Dagger ? 1.25f
            : weaponType_ == WeaponType::Spear ? 2.0f
            : (weaponType_ == WeaponType::Hammer || weaponType_ == WeaponType::Axe) ? 3.2f
            : kAttackInterval_;
        break;
    }
}

void EnemyEntity::Draw()
{
    if (!visible_) {
        return;
    }
    object_->Draw();
    weaponObject_->Draw();
}

void EnemyEntity::Launch(float velY)
{
    isLaunched_ = true;
    velY_ = velY;
    airComboTimer_ = kAirComboHold_;
}

void EnemyEntity::ApplyComboReaction(float knockDirX, float knockY,
    bool switchPull, float playerX)
{
    if (defeated_) return;
    if (switchPull) {
        const float toPlayer = playerX - pos_.x;
        knockVelX_ = std::clamp(toPlayer * 0.18f, -0.32f, 0.32f);
        airComboTimer_ = kAirComboHold_ + 0.18f;
    } else {
        knockVelX_ += knockDirX * 0.055f;
    }
    if (knockY > 0.08f) Launch(knockY);
}
