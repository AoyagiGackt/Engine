/**
 * @file Fade.cpp
 * @brief 濃紺の帯と金色のラインによる画面遷移演出
 */
#include "Fade.h"
#include "GameConstants.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;

void Fade::Initialize(SpriteCommon* spriteCommon)
{
    spriteCommon_ = spriteCommon;
    status_ = Status::None;
    covered_ = false;
    for (size_t i = 0; i < kBandCount; ++i) {
        bands_[i] = std::make_unique<Sprite>();
        bands_[i]->Initialize(spriteCommon, "Resources/white.png");
        edges_[i] = std::make_unique<Sprite>();
        edges_[i]->Initialize(spriteCommon, "Resources/white.png");
    }
    UpdateSprites();
}

void Fade::Start(Status status, float duration)
{
    status_ = status;
    duration_ = std::isfinite(duration) ? (std::max)(duration, 0.0f) : 0.0f;
    counter_ = 0.0f;
    if (status == Status::None) {
        covered_ = false;
    }
    UpdateSprites();
}

void Fade::Update()
{
    if (status_ == Status::None) {
        return;
    }
    counter_ = (std::min)(counter_ + GameConstants::kFrameDeltaTime, duration_);
    UpdateSprites();
    if (counter_ >= duration_) {
        covered_ = status_ == Status::FadeOut;
        status_ = Status::None;
    }
}

void Fade::UpdateSprites()
{
    const float width = static_cast<float>(WinApp::kClientWidth);
    const float height = static_cast<float>(WinApp::kClientHeight) / kBandCount;
    const float progress = duration_ > 0.0f ? std::clamp(counter_ / duration_, 0.0f, 1.0f) : 1.0f;
    constexpr float stagger = 0.22f;
    constexpr float edgeWidth = 3.0f;
    for (size_t i = 0; i < kBandCount; ++i) {
        // 上から順に開始をずらし、加速と減速を滑らかにする。
        const float delay = stagger * static_cast<float>(i) / (kBandCount - 1);
        const float t = std::clamp((progress - delay) / (1.0f - stagger), 0.0f, 1.0f);
        const float eased = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        float x = covered_ ? 0.0f : -width;
        if (status_ == Status::FadeOut) {
            x = -width * (1.0f - eased);
        } else if (status_ == Status::FadeIn) {
            x = width * eased;
        }
        const float shade = static_cast<float>(i) * 0.002f;
        bands_[i]->SetPosition({ x, height * i });
        bands_[i]->SetSize({ width, height + 1.0f });
        bands_[i]->SetColor({ 0.018f + shade, 0.026f + shade, 0.045f + shade, 1.0f });
        bands_[i]->Update();

        // 金色の縁は帯の内側に置き、開始・終了時には消す。
        const float edgeX = status_ == Status::FadeOut ? x + width - edgeWidth : x;
        edges_[i]->SetPosition({ edgeX, height * i });
        edges_[i]->SetSize({ edgeWidth, height + 1.0f });
        edges_[i]->SetColor({ 0.82f, 0.66f, 0.38f, (t > 0.0f && t < 1.0f) ? 0.85f : 0.0f });
        edges_[i]->Update();
    }
}

void Fade::Draw()
{
    if (!spriteCommon_ || (status_ == Status::None && !covered_)) {
        return;
    }
    spriteCommon_->CommonDrawSettings();
    for (size_t i = 0; i < kBandCount; ++i) {
        bands_[i]->Draw();
    }
    for (size_t i = 0; i < kBandCount; ++i) {
        if (edges_[i]->GetColor().w > 0.0f) {
            edges_[i]->Draw();
        }
    }
}
