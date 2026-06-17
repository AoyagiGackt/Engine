#include "ScreenFlash.h"
#include "DirectXCommon.h"
#include "WinApp.h"

ScreenFlash* ScreenFlash::GetInstance() {
    static ScreenFlash instance;
    return &instance;
}

void ScreenFlash::Initialize(DirectXCommon* dxCommon) {
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon);

    sprite_ = std::make_unique<Sprite>();
    sprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    sprite_->SetPosition({ 0.0f, 0.0f });
    sprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
                       static_cast<float>(WinApp::kClientHeight) });
    sprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
}

void ScreenFlash::Request(const Vector4& color, float duration) {
    baseColor_ = color;
    duration_  = duration;
    timer_     = duration;
}

void ScreenFlash::Update(float dt) {
    if (timer_ <= 0.0f) { return; }
    timer_ -= dt;
    if (timer_ < 0.0f) { timer_ = 0.0f; }
    float ratio = (duration_ > 0.0f) ? (timer_ / duration_) : 0.0f;
    sprite_->SetColor({ baseColor_.x, baseColor_.y, baseColor_.z, baseColor_.w * ratio });
    sprite_->Update();
}

void ScreenFlash::Draw() {
    if (timer_ <= 0.0f) { return; }
    spriteCommon_->CommonDrawSettings();
    sprite_->Draw();
}
