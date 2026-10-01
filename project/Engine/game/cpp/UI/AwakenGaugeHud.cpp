#include "AwakenGaugeHud.h"
#include "FontRenderer.h"
#include <algorithm>
#include <cmath>
namespace engine::game {
namespace {
// ゲージの配置（画面下中央）
constexpr Vector2 kGaugePosition = { 500.0f, 700.0f };
constexpr Vector2 kGaugeSize = { 280.0f, 14.0f };
constexpr Vector4 kGaugeBackgroundColor = { 0.05f, 0.05f, 0.15f, 0.75f };
constexpr Vector4 kGaugeChargingColor = { 0.1f, 0.45f, 0.95f, 0.85f };
constexpr float kAwakenedRedScale = 0.05f; // 覚醒中の色（明滅に合わせて赤・緑成分を揺らす）
constexpr float kAwakenedGreenScale = 0.6f;
constexpr float kAwakenedAlpha = 0.95f;

// 覚醒中の明滅
constexpr float kPulseBase = 0.7f;
constexpr float kPulseAmplitude = 0.3f;
constexpr float kPulseSpeed = 8.0f;

// ゲージ上の文字
constexpr float kLabelY = 682.0f;
constexpr float kLabelScale = 1.5f;
constexpr Vector4 kTitleColor = { 0.6f, 0.8f, 1.0f, 0.9f };
constexpr float kActiveLabelX = 710.0f;
constexpr float kHintLabelX = 570.0f;
constexpr Vector4 kHintColor = { 0.8f, 0.9f, 1.0f, 0.8f };
constexpr float kActivationHintThreshold = 0.3f; // 覚醒を発動できるゲージ量（Player側の発動条件と同じ値）
}

void AwakenGaugeHud::Initialize(const HudServices& services)
{
    background_ = std::make_unique<engine::graphics::Sprite>();
    background_->Initialize(&services.sprites, "Resources/white.png");
    background_->SetColor(kGaugeBackgroundColor);
    foreground_ = std::make_unique<engine::graphics::Sprite>();
    foreground_->Initialize(&services.sprites, "Resources/white.png");
}
void AwakenGaugeHud::Update(const HudFrame& frame)
{
    gauge_ = std::clamp(frame.awakenGauge, 0.0f, 1.0f);
    awakened_ = frame.awakened;
    pulse_ = awakened_ ? (kPulseBase + kPulseAmplitude * std::sin(frame.pulseTime * kPulseSpeed)) : 1.0f;
    background_->SetPosition(kGaugePosition);
    background_->SetSize(kGaugeSize);
    background_->Update();
    foreground_->SetPosition(kGaugePosition);
    foreground_->SetSize({ kGaugeSize.x * gauge_, kGaugeSize.y });
    foreground_->SetColor(awakened_ ? Vector4 { kAwakenedRedScale * pulse_, kAwakenedGreenScale * pulse_, 1.0f, kAwakenedAlpha }
        : kGaugeChargingColor);
    foreground_->Update();
}
void AwakenGaugeHud::QueueText(FontRenderer& font) const
{
    font.DrawString("AWAKEN", kGaugePosition.x, kLabelY, kLabelScale, kTitleColor);
    if (awakened_) {
        font.DrawString("ACTIVE", kActiveLabelX, kLabelY, kLabelScale, { pulse_, pulse_, 1.0f, 1.0f });
    } else if (gauge_ >= kActivationHintThreshold) {
        font.DrawString("[R] Activate", kHintLabelX, kLabelY, kLabelScale, kHintColor);
    }
}
void AwakenGaugeHud::Draw()
{
    background_->Draw();
    if (gauge_ > 0.0f) { foreground_->Draw(); }
}
}
