#include "AwakenGaugeHud.h"
#include "FontRenderer.h"
#include "UILayout.h"
#include <algorithm>
#include <cmath>
namespace engine::game {
namespace {
// ゲージの配置（画面下中央。全シーン共通のHUDレイアウト Resources/Config/UI/hud.json で上書きできる）
constexpr const char* kLayoutName = "hud";
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

// ゲージ上の文字（ゲージ左上からの位置）
constexpr float kLabelOffsetY = -18.0f;
constexpr float kLabelScale = 1.5f;
constexpr Vector4 kTitleColor = { 0.6f, 0.8f, 1.0f, 0.9f };
constexpr float kActiveLabelOffsetX = 210.0f;
constexpr float kHintLabelOffsetX = 70.0f;
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
    UILayout& layout = UILayout::Get(kLayoutName);
    position_ = layout.Pos("awaken_gauge.pos", kGaugePosition);
    const Vector2 size = layout.Vec2("awaken_gauge.size", kGaugeSize);
    background_->SetPosition(position_);
    background_->SetSize(size);
    background_->SetColor(layout.Color("awaken_gauge.background_color", kGaugeBackgroundColor));
    background_->Update();
    foreground_->SetPosition(position_);
    foreground_->SetSize({ size.x * gauge_, size.y });
    foreground_->SetColor(awakened_ ? Vector4 { kAwakenedRedScale * pulse_, kAwakenedGreenScale * pulse_, 1.0f, kAwakenedAlpha }
        : layout.Color("awaken_gauge.charging_color", kGaugeChargingColor));
    foreground_->Update();
}
void AwakenGaugeHud::QueueText(FontRenderer& font) const
{
    const float labelY = position_.y + kLabelOffsetY;
    font.DrawString("AWAKEN", position_.x, labelY, kLabelScale, kTitleColor);
    if (awakened_) {
        font.DrawString("ACTIVE", position_.x + kActiveLabelOffsetX, labelY, kLabelScale, { pulse_, pulse_, 1.0f, 1.0f });
    } else if (gauge_ >= kActivationHintThreshold) {
        font.DrawString("[R] Activate", position_.x + kHintLabelOffsetX, labelY, kLabelScale, kHintColor);
    }
}
void AwakenGaugeHud::Draw()
{
    background_->Draw();
    if (gauge_ > 0.0f) { foreground_->Draw(); }
}
}
