/**
 * @file AwakenGaugeHud.cpp
 * @brief 覚醒ゲージの HUD 表示（AwakenGaugeHud）の実装
 */
#include "AwakenGaugeHud.h"
#include "FontRenderer.h"
#include "Input.h"
#include "PlayerTuning.h"
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
constexpr Vector4 kGaugeReadyColor = { 0.25f, 0.7f, 1.0f, 0.95f }; // 覚醒できる量まで溜まった時の色
constexpr Vector4 kGaugeFullColor = { 1.0f, 0.78f, 0.2f, 1.0f }; // 満タン（フィニッシャーも使える）の時の色
constexpr float kAwakenedRedScale = 0.05f; // 覚醒中の色（明滅に合わせて赤・緑成分を揺らす）
constexpr float kAwakenedGreenScale = 0.6f;
constexpr float kAwakenedAlpha = 0.95f;

// 覚醒中と満タンの時の明滅
constexpr float kPulseBase = 0.7f;
constexpr float kPulseAmplitude = 0.3f;
constexpr float kPulseSpeed = 8.0f;

// 覚醒できる量を示す印（ゲージの上下に少しはみ出す縦線）
constexpr float kMarkerWidth = 3.0f;
constexpr float kMarkerOverhang = 4.0f;
constexpr Vector4 kMarkerColor = { 1.0f, 1.0f, 1.0f, 0.9f };

// ゲージ上の文字（ゲージ左上からの位置）
constexpr float kLabelOffsetY = -24.0f;
constexpr float kLabelScale = 1.2f;
constexpr float kPromptGap = 18.0f; // 見出しと操作の案内、案内どうしの間隔
constexpr Vector4 kTitleColor = { 0.6f, 0.8f, 1.0f, 0.9f };
constexpr Vector4 kPromptReadyColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kPromptLockedColor = { 0.45f, 0.5f, 0.6f, 1.0f };
constexpr const wchar_t* kTitleText = L"覚醒ゲージ";
constexpr const wchar_t* kAwakenPrompt = L"[{Awaken}] 覚醒";
constexpr const wchar_t* kFinisherPrompt = L"[{Finisher}] フィニッシャー";
constexpr const wchar_t* kAwakenedText = L"覚醒中";
constexpr float kFull = 1.0f;
constexpr float kHalf = 0.5f;
constexpr float kBothSides = 2.0f; // 上下両方にはみ出す分

/** @brief 明滅の強さを色の明るさへ掛ける（不透明度はそのまま） */
Vector4 Pulsed(const Vector4& color, float pulse) {
    return { color.x * pulse, color.y * pulse, color.z * pulse, color.w };
}
}

/** @brief 覚醒ゲージと、覚醒できる量の印のスプライトを用意する */
void AwakenGaugeHud::Initialize(const HudServices& services) {
    background_ = std::make_unique<engine::graphics::Sprite>();
    background_->Initialize(&services.sprites, "Resources/white.png");
    background_->SetColor(kGaugeBackgroundColor);
    foreground_ = std::make_unique<engine::graphics::Sprite>();
    foreground_->Initialize(&services.sprites, "Resources/white.png");
    marker_ = std::make_unique<engine::graphics::Sprite>();
    marker_->Initialize(&services.sprites, "Resources/white.png");
    marker_->SetColor(kMarkerColor);
}
/** @brief 覚醒ゲージの量と表示、R/F の使える・使えないを更新する */
void AwakenGaugeHud::Update(const HudFrame& frame) {
    gauge_ = std::clamp(frame.awakenGauge, 0.0f, kFull);
    awakened_ = frame.awakened;
    threshold_ = std::clamp(PlayerTuning::Get().awakenActivationThreshold, 0.0f, kFull);
    // 覚醒とフィニッシャーは同時には使えず、どちらも覚醒中は押せない（Player 側の発動条件と同じ）
    awakenReady_ = !awakened_ && gauge_ >= threshold_;
    finisherReady_ = !awakened_ && gauge_ >= kFull;
    const bool pulsing = awakened_ || finisherReady_;
    pulse_ = pulsing ? (kPulseBase + kPulseAmplitude * std::sin(frame.pulseTime * kPulseSpeed)) : kFull;
    if (frame.input != nullptr) {
        awakenPrompt_ = frame.input->ExpandPrompts(kAwakenPrompt);
        finisherPrompt_ = frame.input->ExpandPrompts(kFinisherPrompt);
    }

    UILayout& layout = UILayout::Get(kLayoutName);
    position_ = layout.Pos("awaken_gauge.pos", kGaugePosition);
    const Vector2 size = layout.Vec2("awaken_gauge.size", kGaugeSize);
    background_->SetPosition(position_);
    background_->SetSize(size);
    background_->SetColor(layout.Color("awaken_gauge.background_color", kGaugeBackgroundColor));
    background_->Update();

    // 溜まり具合で色を変える（覚醒中 → 満タン → 覚醒できる → 溜めている途中）
    Vector4 fillColor = layout.Color("awaken_gauge.charging_color", kGaugeChargingColor);
    if (awakened_) {
        fillColor = { kAwakenedRedScale * pulse_, kAwakenedGreenScale * pulse_, 1.0f, kAwakenedAlpha };
    } else if (finisherReady_) {
        fillColor = Pulsed(layout.Color("awaken_gauge.full_color", kGaugeFullColor), pulse_);
    } else if (awakenReady_) {
        fillColor = layout.Color("awaken_gauge.ready_color", kGaugeReadyColor);
    }
    foreground_->SetPosition(position_);
    foreground_->SetSize({ size.x * gauge_, size.y });
    foreground_->SetColor(fillColor);
    foreground_->Update();

    marker_->SetPosition({ position_.x + size.x * threshold_ - kMarkerWidth * kHalf, position_.y - kMarkerOverhang });
    marker_->SetSize({ kMarkerWidth, size.y + kMarkerOverhang * kBothSides });
    marker_->SetColor(layout.Color("awaken_gauge.marker_color", kMarkerColor));
    marker_->Update();
}
/** @brief 見出しと、覚醒中の表示または R/F の案内（使える時だけ明るく）を描画予約する */
void AwakenGaugeHud::QueueText(FontRenderer& font) const {
    // 暗い影を先に重ねて明るい床から文字の輪郭を分離する。
    constexpr Vector4 kTextShadowColor = { 0.02f, 0.02f, 0.04f, 1.0f };
    constexpr float kTextShadowOffset = 2.0f;
    const auto drawShadowed = [&](const std::wstring& text, float x, float y, const Vector4& color) {
        font.DrawStringW(text, x + kTextShadowOffset, y + kTextShadowOffset, kLabelScale, kTextShadowColor);
        font.DrawStringW(text, x, y, kLabelScale, color);
    };
    const float labelY = position_.y + kLabelOffsetY;
    drawShadowed(kTitleText, position_.x, labelY, kTitleColor);
    float x = position_.x + FontRenderer::MeasureStringW(kTitleText, kLabelScale).x + kPromptGap;
    if (awakened_) {
        drawShadowed(kAwakenedText, x, labelY, { pulse_, pulse_, kFull, kFull });
        return;
    }
    drawShadowed(awakenPrompt_, x, labelY, awakenReady_ ? kPromptReadyColor : kPromptLockedColor);
    x += FontRenderer::MeasureStringW(awakenPrompt_, kLabelScale).x + kPromptGap;
    const Vector4 finisherColor = finisherReady_ ? Pulsed(kGaugeFullColor, pulse_) : kPromptLockedColor;
    drawShadowed(finisherPrompt_, x, labelY, finisherColor);
}
/** @brief 覚醒ゲージと覚醒できる量の印を描画する */
void AwakenGaugeHud::Draw() {
    background_->Draw();
    if (gauge_ > 0.0f) {
        foreground_->Draw();
    }
    // 覚醒中は量の目安がいらないので印を出さない
    if (!awakened_) {
        marker_->Draw();
    }
}
}
