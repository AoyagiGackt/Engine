/**
 * @file ScreenFlash.cpp
 * @brief 画面全体を単色でフラッシュさせる演出（ScreenFlash）の実装
 */
#include "ScreenFlash.h"
#include "DirectXCommon.h"
#include "WinApp.h"
using namespace engine::graphics;

namespace engine::game {
/** @brief シングルトンインスタンスを返す（初回呼び出しで生成される） */
ScreenFlash* ScreenFlash::GetInstance()
{
    static ScreenFlash instance;
    return &instance;
}

/** @brief 画面全体を覆う不透明スプライトを生成する（Game::Initialize()で1回呼ぶ） */
void ScreenFlash::Initialize(DirectXCommon* dxCommon)
{
    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon);

    sprite_ = std::make_unique<Sprite>();
    sprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    sprite_->SetPosition({ 0.0f, 0.0f });
    sprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });
    sprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });
}

/** @brief フラッシュを開始する（既存のフラッシュ中に呼ぶと上書きされる） */
void ScreenFlash::Request(const Vector4& color, float duration)
{
    baseColor_ = color;
    duration_ = duration;
    timer_ = duration;
}

/** @brief 残り時間を減らし、経過に応じてスプライトの不透明度を線形フェードさせる */
void ScreenFlash::Update(float dt)
{
    if (timer_ <= 0.0f)
    {
        return;
    }
    timer_ -= dt;
    if (timer_ < 0.0f)
    {
        timer_ = 0.0f;
    }
    float ratio = (duration_ > 0.0f) ? (timer_ / duration_) : 0.0f;
    sprite_->SetColor({ baseColor_.x, baseColor_.y, baseColor_.z, baseColor_.w * ratio });
    sprite_->Update();
}

/** @brief フラッシュ中（timer_>0）ならスプライトを描画する */
void ScreenFlash::Draw()
{
    if (timer_ <= 0.0f)
    {
        return;
    }
    spriteCommon_->CommonDrawSettings();
    sprite_->Draw();
}

} // namespace engine::game
