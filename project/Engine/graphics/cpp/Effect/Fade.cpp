/**
 * @file Fade.cpp
 * @brief 濃紺の帯と金色のラインによる画面遷移演出
 */
#include "Fade.h"
#include "GraphicsTuning.h"
#include "GameConstants.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>

namespace {

// 縁の配色と補間係数
constexpr float kEdgeRed = 0.82f;
constexpr float kEdgeGreen = 0.66f;
constexpr float kEdgeBlue = 0.38f;
constexpr float kSmootherstepQuintic = 6.0f;
constexpr float kSmootherstepQuartic = 15.0f;
constexpr float kSmootherstepCubic = 10.0f;

}
using namespace engine;
using namespace engine::graphics;

namespace {

// 帯と縁の描画設定
constexpr float kBandStagger = 0.22f;
constexpr float kEdgeWidth = 3.0f;
constexpr float kBandShadeStep = 0.002f;
constexpr float kBandRed = 0.018f;
constexpr float kBandGreen = 0.026f;
constexpr float kBandBlue = 0.045f;
constexpr float kEdgeOpacity = 0.85f;
constexpr float kMaxBandStagger = 0.99f;

struct FadeMotion {
    float (*position)(float, float, bool);
    float (*edgeOffset)(float, float);
};

// Status順の位置と縁の計算
constexpr FadeMotion kFadeMotions[] = {
    { [](float width, float, bool covered) { return covered ? 0.0f : -width; },
      [](float, float) { return 0.0f; } },
    { [](float width, float eased, bool) { return width * eased; },
      [](float, float) { return 0.0f; } },
    { [](float width, float eased, bool) { return -width * (1.0f - eased); },
      [](float width, float edgeWidth) { return width - edgeWidth; } },
};

}

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
    const float bandStagger = GraphicsTuning::Float("fade.bandStagger", kBandStagger, 0.0f, kMaxBandStagger);
    const float edgeWidth = GraphicsTuning::Float("fade.edgeWidth", kEdgeWidth);
    const float bandShadeStep = GraphicsTuning::Float("fade.bandShadeStep", kBandShadeStep);
    const Vector4 bandColor = GraphicsTuning::Color("fade.bandColor", { kBandRed, kBandGreen, kBandBlue, 1.0f });
    const Vector4 edgeColor = GraphicsTuning::Color("fade.edgeColor", { kEdgeRed, kEdgeGreen, kEdgeBlue, kEdgeOpacity });
    const float width = static_cast<float>(WinApp::kClientWidth);
    const float height = static_cast<float>(WinApp::kClientHeight) / kBandCount;
    const float progress = duration_ > 0.0f ? std::clamp(counter_ / duration_, 0.0f, 1.0f) : 1.0f;
    const auto& motion = kFadeMotions[static_cast<size_t>(status_)];
    for (size_t i = 0; i < kBandCount; ++i) {
        // 上から順に開始をずらし、加速と減速を滑らかにする。
        const float delay = bandStagger * static_cast<float>(i) / (kBandCount - 1);
        const float t = std::clamp((progress - delay) / (1.0f - bandStagger), 0.0f, 1.0f);
        const float eased = t * t * t * (t * (t * kSmootherstepQuintic - kSmootherstepQuartic) + kSmootherstepCubic);
        const float x = motion.position(width, eased, covered_);
        const float shade = static_cast<float>(i) * bandShadeStep;
        bands_[i]->SetPosition({ x, height * i });
        bands_[i]->SetSize({ width, height + 1.0f });
        bands_[i]->SetColor({ bandColor.x + shade, bandColor.y + shade, bandColor.z + shade, bandColor.w });
        bands_[i]->Update();

        // 金色の縁は帯の内側に置き、開始・終了時には消す。
        const float edgeX = x + motion.edgeOffset(width, edgeWidth);
        edges_[i]->SetPosition({ edgeX, height * i });
        edges_[i]->SetSize({ edgeWidth, height + 1.0f });
        edges_[i]->SetColor({ edgeColor.x, edgeColor.y, edgeColor.z, (t > 0.0f && t < 1.0f) ? edgeColor.w : 0.0f });
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
