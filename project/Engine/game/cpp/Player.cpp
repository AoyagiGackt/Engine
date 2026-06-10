#include "Player.h"
#include "Input.h"
#include "ModelCommon.h"
#include <algorithm>

void Player::Initialize(ModelCommon* modelCommon)
{
    model_ = std::make_unique<Model>();
    model_->Initialize(modelCommon,
        "Resources/player/player.obj",
        "Resources/player/player.png");

    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model_.get());
    object_->SetEnableLighting(false);
    object_->SetPosition(pos_);
    object_->Update();
}

void Player::Update(Input* input)
{
    justJumped_   = false;
    justLanded_   = false;
    prevOnGround_ = onGround_;

    // 横移動
    if (input->PushKey(DIK_A) || input->PushKey(DIK_LEFT))  { pos_.x -= kSpeed_; }
    if (input->PushKey(DIK_D) || input->PushKey(DIK_RIGHT)) { pos_.x += kSpeed_; }

    // ジャンプ（地面にいるときだけ）
    if (onGround_) {
        if (input->TriggerKey(DIK_W) || input->TriggerKey(DIK_UP) || input->TriggerKey(DIK_SPACE)) {
            velocityY_  = kJumpPower_;
            onGround_   = false;
            justJumped_ = true;
        }
    }

    // 重力・落下
    velocityY_ -= kGravity_;
    pos_.y     += velocityY_;

    // 着地（床クランプ）
    if (pos_.y <= kGroundY_) {
        pos_.y     = kGroundY_;
        velocityY_ = 0.0f;
        onGround_  = true;
    }

    // 天井クランプ
    if (pos_.y > kCeilingY_) {
        pos_.y     = kCeilingY_;
        velocityY_ = 0.0f;
    }

    // 左右クランプ
    pos_.x = std::clamp(pos_.x, kMinX_, kMaxX_);

    // 着地検出（前フレーム空中 → 今フレーム着地）
    justLanded_ = !prevOnGround_ && onGround_;

    object_->SetPosition(pos_);
    object_->Update();
}

void Player::Draw()
{
    object_->Draw();
}
