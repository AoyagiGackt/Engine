/**
 * @file TrainingHud.cpp
 * @brief トレーニングの説明・ナビゲーション表示を担当するHUD要素の実装
 */
#include "TrainingHud.h"
#include "FontRenderer.h"
#include "SceneShared.h"
#include <algorithm>
namespace engine::game {
namespace {
constexpr const char* kPanelTexture = "Resources/white.png";
constexpr Vector4 kPanelBackgroundColor = { 0.025f, 0.035f, 0.06f, 0.94f };

// 右下のナビゲーション枠（左端にアクセントの縦線を重ねる）
constexpr Vector2 kNavigationPosition = { 852.0f, 654.0f };
constexpr Vector2 kNavigationSize = { 404.0f, 58.0f };
constexpr Vector2 kAccentSize = { 3.0f, 58.0f };
constexpr Vector4 kAccentColor = { 0.95f, 0.72f, 0.3f, 1.0f };

// 武器一覧と操作説明の背景パネル（初期位置。毎フレームアンカーに追従させる）
constexpr Vector2 kWeaponPanelPosition = { 4.0f, 4.0f };
constexpr Vector2 kWeaponPanelSize = { 520.0f, 228.0f };
constexpr Vector2 kControlsPanelPosition = { 792.0f, 4.0f };
constexpr Vector2 kControlsPanelSize = { 480.0f, 254.0f };
constexpr float kPanelPadding = 8.0f; ///< アンカーから背景パネルを外側へ広げる量

// 操作説明パネルが画面外へはみ出さないためのアンカー可動範囲
constexpr float kControlsAnchorMin = 8.0f;
constexpr float kControlsAnchorMaxX = 800.0f;
constexpr float kControlsAnchorMaxY = 450.0f;

// 文字の影
constexpr float kShadowOffset = 1.5f;
constexpr Vector4 kShadowColor = { 0.0f, 0.0f, 0.0f, 1.0f };

// 武器一覧の下に出す操作ヒント
constexpr float kHintScale = 1.05f;
constexpr float kHintLineHeight = 23.0f;
constexpr Vector4 kHintColor = { 0.88f, 0.9f, 0.96f, 1.0f };

// ナビゲーション枠内の文字
constexpr Vector2 kStageSelectTextPosition = { 872.0f, 666.0f };
constexpr float kStageSelectTextScale = 1.1f;
constexpr Vector4 kStageSelectTextColor = { 1.0f, 0.9f, 0.65f, 1.0f };
constexpr Vector2 kTitleTextPosition = { 872.0f, 689.0f };
constexpr float kTitleTextScale = 0.9f;
constexpr Vector4 kTitleTextColor = { 0.82f, 0.86f, 0.95f, 1.0f };
}

void TrainingHud::Initialize(const HudServices& services)
{
    weapons_ = &services.weapons;
    auto create = [&](Vector2 position, Vector2 size, Vector4 color) {
        auto sprite = std::make_unique<engine::graphics::Sprite>();
        sprite->Initialize(&services.sprites, kPanelTexture);
        sprite->SetPosition(position);
        sprite->SetSize(size);
        sprite->SetColor(color);
        sprite->Update();
        return sprite;
    };
    navigation_ = create(kNavigationPosition, kNavigationSize, kPanelBackgroundColor);
    accent_ = create(kNavigationPosition, kAccentSize, kAccentColor);
    weaponPanel_ = create(kWeaponPanelPosition, kWeaponPanelSize, kPanelBackgroundColor);
    controlsPanel_ = create(kControlsPanelPosition, kControlsPanelSize, kPanelBackgroundColor);
}
void TrainingHud::Update(const HudFrame& frame)
{
    weaponAnchor_ = frame.weaponAnchor;
    controlsAnchor_ = { std::clamp(frame.controlsAnchor.x, kControlsAnchorMin, kControlsAnchorMaxX),
        std::clamp(frame.controlsAnchor.y, kControlsAnchorMin, kControlsAnchorMaxY) };
    weaponPanel_->SetPosition({ weaponAnchor_.x - kPanelPadding, weaponAnchor_.y - kPanelPadding });
    weaponPanel_->Update();
    controlsPanel_->SetPosition({ controlsAnchor_.x - kPanelPadding, controlsAnchor_.y - kPanelPadding });
    controlsPanel_->Update();
}
void TrainingHud::QueueText(FontRenderer& font) const
{
    const float y = SceneShared::DrawWeaponListHud(font, weapons_, L"トレーニングルーム", weaponAnchor_);
    auto hint = [&](const wchar_t* text, float py) {
        font.DrawStringW(text, weaponAnchor_.x + kShadowOffset, py + kShadowOffset, kHintScale, kShadowColor);
        font.DrawStringW(text, weaponAnchor_.x, py, kHintScale, kHintColor);
    };
    hint(L"L：コンボ　S+L：打ち上げ　空中L：追撃", y);
    hint(L"I：回避　Space：武器固有技", y + kHintLineHeight);
    SceneShared::DrawControlsHud(font, controlsAnchor_, L": バトルテストへ移動");
    font.DrawStringW(L"[ TAB ] ステージ選択へ", kStageSelectTextPosition.x + kShadowOffset,
        kStageSelectTextPosition.y + kShadowOffset, kStageSelectTextScale, kShadowColor);
    font.DrawStringW(L"[ TAB ] ステージ選択へ", kStageSelectTextPosition.x, kStageSelectTextPosition.y,
        kStageSelectTextScale, kStageSelectTextColor);
    font.DrawStringW(L"[ Backspace ] タイトルへ", kTitleTextPosition.x, kTitleTextPosition.y,
        kTitleTextScale, kTitleTextColor);
}
void TrainingHud::Draw()
{
    weaponPanel_->Draw();
    controlsPanel_->Draw();
    navigation_->Draw();
    accent_->Draw();
}
}
