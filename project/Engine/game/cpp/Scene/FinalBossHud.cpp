/** @file FinalBossHud.cpp
 * @brief 体力バー、黒帯、白い幕と日本語表示を構成する。
 */
#include "FinalBossHud.h"
#include "FontRenderer.h"
#include "GuidePanel.h"
#include "UILayout.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
namespace engine::game {
namespace {
constexpr const char* kLayout = "lastboss";
constexpr const char* kWhite = "Resources/white.png";
constexpr float kScreenWidth = static_cast<float>(WinApp::kClientWidth);
constexpr float kScreenHeight = static_cast<float>(WinApp::kClientHeight);
constexpr float kCenterX = kScreenWidth * 0.5f;
constexpr std::size_t kBackground = 0;
constexpr std::size_t kDelayed = 1;
constexpr std::size_t kHealth = 2;
constexpr std::size_t kMark = 3;
constexpr std::size_t kTop = 4;
constexpr std::size_t kBottom = 5;
constexpr std::size_t kVeil = 6;
constexpr Vector2 kBarPosition = { kCenterX, kScreenHeight - 64.0f };
constexpr float kBarWidth = 720.0f;
constexpr float kBarHeight = 14.0f;
constexpr float kBorder = 3.0f;
constexpr float kMarkWidth = 3.0f;
constexpr float kMarkHeight = 22.0f;
constexpr float kDelaySeconds = 0.35f;
constexpr float kFollowRate = 3.0f;
constexpr float kLetterboxHeight = 84.0f;
constexpr Vector2 kBossNameOffset = { 0.0f, -32.0f };
constexpr Vector2 kRevealPosition = { kCenterX, kScreenHeight * 0.5f + 80.0f };
constexpr Vector2 kLinePosition = { kCenterX, kScreenHeight - 114.0f };
constexpr float kNameScale = 1.3f;
constexpr float kRevealScale = 2.8f;
constexpr float kLineScale = 1.4f;
constexpr Vector4 kBarBackground = { 0.035f, 0.025f, 0.055f, 0.9f };
constexpr Vector4 kDelayedColor = { 1.0f, 1.0f, 1.0f, 0.85f };
constexpr Vector4 kHealthColor = { 0.8f, 0.73f, 1.0f, 1.0f };
constexpr Vector4 kRageColor = { 0.95f, 0.12f, 0.2f, 1.0f };
constexpr Vector4 kMarkColor = { 1.0f, 0.65f, 0.7f, 1.0f };
constexpr Vector4 kBlack = { 0.0f, 0.0f, 0.0f, 1.0f };
constexpr Vector4 kWhiteColor = { 1.0f, 1.0f, 1.0f, 1.0f };
/** @brief 不透明度を指定倍率にする。 */
Vector4 Fade(const Vector4& color, float alpha) {
    return { color.x, color.y, color.z, color.w * std::clamp(alpha, 0.0f, 1.0f) };
}
/** @brief 不正な割合をゼロにし、有効な値を単位区間へ収める。 */
float Unit(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
} // namespace

/** @brief 白画像の全スプライトを初期化時に生成する。 */
void FinalBossHud::Initialize(engine::graphics::SpriteCommon* spriteCommon, FontRenderer* font) {
    font_ = font;
    if (initialized_ || !spriteCommon) {
        return;
    }
    for (auto& sprite : sprites_) {
        sprite = std::make_unique<engine::graphics::Sprite>();
        sprite->Initialize(spriteCommon, kWhite);
    }
    initialized_ = true;
    Update(State {}, 0.0f);
}

/** @brief 既存スプライトの描画データだけを変更する。 */
void FinalBossHud::SetPanel(std::size_t index, const Vector2& position, const Vector2& size, const Vector4& color) {
    auto& sprite = sprites_[index];
    sprite->SetPosition(position);
    sprite->SetSize({ (std::max)(size.x, 0.0f), (std::max)(size.y, 0.0f) });
    sprite->SetColor(color);
    sprite->Update();
}

/** @brief 遅延バーと画面配置を状態に追従させる。 */
void FinalBossHud::Update(const State& state, float deltaTime) {
    if (!initialized_) {
        return;
    }
    auto& layout = UILayout::Get(kLayout);
    const float hp = Unit(state.hpRatio);
    const float dt = std::isfinite(deltaTime) ? (std::max)(deltaTime, 0.0f) : 0.0f;
    // 初回表示と回復は即座に合わせ、減少後だけ追従を遅らせる。
    if (!state.visibleBar || !state_.visibleBar || hp >= delayedHp_) {
        delayedHp_ = hp;
        delayLeft_ = 0.0f;
    } else {
        if (hp < state_.hpRatio) {
            delayLeft_ = (std::max)(layout.Float("bar.delaySeconds", kDelaySeconds), 0.0f);
        }
        const float followDt = (std::max)(dt - delayLeft_, 0.0f);
        delayLeft_ = (std::max)(delayLeft_ - dt, 0.0f);
        const float rate = (std::max)(layout.Float("bar.followRate", kFollowRate), 0.0f);
        delayedHp_ = std::lerp(hp, delayedHp_, std::exp(-rate * followDt));
    }
    state_ = state;
    state_.hpRatio = hp;
    state_.letterbox = Unit(state.letterbox);
    state_.whiteVeil = Unit(state.whiteVeil);
    state_.nameReveal = Unit(state.nameReveal);
    state_.lineAlpha = Unit(state.lineAlpha);
    const Vector2 bar = layout.Pos("bar.pos", kBarPosition);
    const float width = (std::max)(layout.Float("bar.width", kBarWidth), 0.0f);
    const float height = (std::max)(layout.Float("bar.height", kBarHeight), 0.0f);
    const float border = (std::max)(layout.Float("bar.border", kBorder), 0.0f);
    const Vector2 left = { bar.x - width * 0.5f, bar.y };
    const float visible = state_.visibleBar ? 1.0f : 0.0f;
    SetPanel(kBackground, { left.x - border, left.y - border }, { width + border + border, height + border + border },
        Fade(layout.Color("bar.background", kBarBackground), visible));
    SetPanel(kDelayed, left, { width * delayedHp_, height }, Fade(layout.Color("bar.delayedColor", kDelayedColor), visible));
    const Vector4 calm = layout.Color("bar.color", kHealthColor);
    const Vector4 rage = layout.Color("bar.rageColor", kRageColor);
    SetPanel(kHealth, left, { width * hp, height }, Fade(state_.rage ? rage : calm, visible));
    const float markWidth = (std::max)(layout.Float("mark.width", kMarkWidth), 0.0f);
    const float markHeight = (std::max)(layout.Float("mark.height", kMarkHeight), 0.0f);
    const Vector2 markOffset = layout.Pos("mark.offset", { 0.0f, 0.0f });
    SetPanel(kMark, { bar.x - markWidth * 0.5f + markOffset.x, bar.y + (height - markHeight) * 0.5f + markOffset.y },
        { markWidth, markHeight }, Fade(layout.Color("mark.color", kMarkColor), state_.rage ? visible : 0.0f));
    // 黒帯は画面外から上下対称に伸びる。
    const float boxHeight = std::clamp(layout.Float("letterbox.height", kLetterboxHeight), 0.0f, kScreenHeight * 0.5f) * state_.letterbox;
    const Vector4 boxColor = layout.Color("letterbox.color", kBlack);
    SetPanel(kTop, layout.Pos("letterbox.top", { 0.0f, 0.0f }), { kScreenWidth, boxHeight }, boxColor);
    const Vector2 bottom = layout.Pos("letterbox.bottom", { 0.0f, kScreenHeight });
    SetPanel(kBottom, { bottom.x, bottom.y - boxHeight }, { kScreenWidth, boxHeight }, boxColor);
    // 最後のスプライトとして画面全体に白い幕を描く。
    SetPanel(kVeil, { 0.0f, 0.0f }, { kScreenWidth, kScreenHeight }, Fade(kWhiteColor, state_.whiteVeil));
}

/** @brief 配置設定と白い幕を反映し文字を予約する。 */
void FinalBossHud::QueueText(const std::string& key, const std::wstring& text, const Vector2& position,
    float scale, float alpha, float anchor) {
    auto& layout = UILayout::Get(kLayout);
    const Vector2 pos = layout.Pos(key + ".pos", position);
    const float textScale = (std::max)(layout.Float(key + ".scale", scale), 0.0f);
    Vector4 color = Fade(layout.Color(key + ".color", kWhiteColor), alpha);
    // フォントは幕より後に描かれるため、文字色にも同じ白合成を適用する。
    color.x = std::lerp(color.x, 1.0f, state_.whiteVeil);
    color.y = std::lerp(color.y, 1.0f, state_.whiteVeil);
    color.z = std::lerp(color.z, 1.0f, state_.whiteVeil);
    if (!font_ || text.empty() || alpha <= 0.0f) {
        return;
    }
    const float alignment = Unit(layout.Float(key + ".anchor", anchor));
    const Vector2 size = FontRenderer::MeasureStringW(text, textScale);
    font_->DrawStringW(text, pos.x - size.x * alignment, pos.y, textScale, color);
}

/** @brief 背景を描画し、表示する日本語文字を予約する。 */
void FinalBossHud::Draw() {
    if (!initialized_) {
        return;
    }
    for (auto& sprite : sprites_) {
        if (sprite->GetSize().x > 0.0f && sprite->GetSize().y > 0.0f) {
            sprite->Draw();
        }
    }
    auto& layout = UILayout::Get(kLayout);
    const Vector2 bar = layout.Pos("bar.pos", kBarPosition);
    const Vector2 nameOffset = layout.Pos("bossName.offset", kBossNameOffset);
    QueueText("bossName", state_.bossName, { bar.x + nameOffset.x, bar.y + nameOffset.y },
        kNameScale, state_.visibleBar ? 1.0f : 0.0f, 0.5f);
    QueueText("nameReveal", state_.bossName, kRevealPosition, kRevealScale, state_.nameReveal, 0.5f);
    QueueText("line", state_.line, kLinePosition, kLineScale, state_.lineAlpha, 0.5f);
    QueueGuide();
}

/** @brief 戦闘の前の操作説明（下敷きの板・見出し・本文・始め方）を予約する。 */
void FinalBossHud::QueueGuide() {
    // 板の描き方はステージの操作説明と共通（配置は lastboss の guide.*）
    if (font_ != nullptr) {
        GuidePanel::Queue(*font_, kLayout, { state_.guideTitle, state_.guide, state_.guideConfirm }, state_.guideAlpha);
    }
}
} // namespace engine::game
