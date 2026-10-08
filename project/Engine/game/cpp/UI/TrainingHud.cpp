/**
 * @file TrainingHud.cpp
 * @brief トレーニングの説明・ナビゲーション表示を担当するHUD要素の実装
 */
#include "TrainingHud.h"
#include "FontRenderer.h"
#include "Input.h"
#include "SceneShared.h"
#include "UILayout.h"
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

// ナビゲーション枠内の文字（枠の左上からの位置）
constexpr Vector2 kStageSelectTextOffset = { 20.0f, 12.0f };
constexpr float kStageSelectTextScale = 1.1f;
constexpr Vector4 kStageSelectTextColor = { 1.0f, 0.9f, 0.65f, 1.0f };
constexpr Vector2 kTitleTextOffset = { 20.0f, 35.0f };
constexpr float kTitleTextScale = 0.9f;
constexpr Vector4 kTitleTextColor = { 0.82f, 0.86f, 0.95f, 1.0f };

// 既定の配置（F2のエディタで動かした値は Resources/Config/UI/training.json に保存される）
constexpr const char* kLayoutName = "training";
constexpr Vector2 kControlsAnchor = { 800.0f, 12.0f };
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
void TrainingHud::Update(const HudFrame&)
{
    // 位置はエディタで動かされても止まった画面に反映できるよう、毎フレームUILayoutから読む
    UILayout& layout = UILayout::Get(kLayoutName);
    weaponAnchor_ = layout.Pos("weapon_list.pos", kDefaultHudWeaponAnchor);
    const Vector2 controls = layout.Pos("controls.pos", kControlsAnchor);
    controlsAnchor_ = { std::clamp(controls.x, kControlsAnchorMin, kControlsAnchorMaxX),
        std::clamp(controls.y, kControlsAnchorMin, kControlsAnchorMaxY) };
    navigationPosition_ = layout.Pos("navigation.pos", kNavigationPosition);

    const float padding = layout.Float("panel.padding", kPanelPadding);
    const Vector4 panelColor = layout.Color("panel.color", kPanelBackgroundColor);
    weaponPanel_->SetPosition({ weaponAnchor_.x - padding, weaponAnchor_.y - padding });
    weaponPanel_->SetSize(layout.Vec2("weapon_list.panel_size", kWeaponPanelSize));
    weaponPanel_->SetColor(panelColor);
    weaponPanel_->Update();
    controlsPanel_->SetPosition({ controlsAnchor_.x - padding, controlsAnchor_.y - padding });
    controlsPanel_->SetSize(layout.Vec2("controls.panel_size", kControlsPanelSize));
    controlsPanel_->SetColor(panelColor);
    controlsPanel_->Update();
    navigation_->SetPosition(navigationPosition_);
    navigation_->SetSize(layout.Vec2("navigation.size", kNavigationSize));
    navigation_->SetColor(panelColor);
    navigation_->Update();
    accent_->SetPosition(navigationPosition_);
    accent_->SetSize(layout.Vec2("navigation.accent_size", kAccentSize));
    accent_->SetColor(layout.Color("navigation.accent_color", kAccentColor));
    accent_->Update();
}
void TrainingHud::QueueText(FontRenderer& font) const
{
    UILayout& layout = UILayout::Get(kLayoutName);
    const float hintScale = layout.Float("weapon_list.hint_scale", kHintScale);
    const Vector4 hintColor = layout.Color("weapon_list.hint_color", kHintColor);
    const float y = SceneShared::DrawWeaponListHud(font, weapons_, L"トレーニングルーム", weaponAnchor_);
    const Input* input = Input::GetCurrent();
    auto prompt = [&](const wchar_t* text) { return input ? input->ExpandPrompts(text) : std::wstring(text); };
    auto hint = [&](const std::wstring& text, float py) {
        font.DrawStringW(text, weaponAnchor_.x + kShadowOffset, py + kShadowOffset, hintScale, kShadowColor);
        font.DrawStringW(text, weaponAnchor_.x, py, hintScale, hintColor);
    };
    hint(prompt(L"{Attack}：コンボ　{Down}+{Attack}：打ち上げ　空中{Attack}：追撃"), y);
    hint(prompt(L"{Dodge}：回避　{Skill}：武器固有技"), y + kHintLineHeight);
    SceneShared::DrawControlsHud(font, controlsAnchor_, L": バトルテストへ移動");

    const Vector2 stageSelect = { navigationPosition_.x + kStageSelectTextOffset.x, navigationPosition_.y + kStageSelectTextOffset.y };
    const float stageSelectScale = layout.Float("navigation.stage_select_scale", kStageSelectTextScale);
    font.DrawStringW(input && input->IsUsingGamepad() ? L"[ BACK ] ステージ選択へ" : L"[ TAB ] ステージ選択へ", stageSelect.x + kShadowOffset, stageSelect.y + kShadowOffset,
        stageSelectScale, kShadowColor);
    font.DrawStringW(input && input->IsUsingGamepad() ? L"[ BACK ] ステージ選択へ" : L"[ TAB ] ステージ選択へ", stageSelect.x, stageSelect.y,
        stageSelectScale, layout.Color("navigation.stage_select_color", kStageSelectTextColor));
    const Vector2 title = { navigationPosition_.x + kTitleTextOffset.x, navigationPosition_.y + kTitleTextOffset.y };
    font.DrawStringW(L"[ Backspace ] タイトルへ", title.x, title.y,
        layout.Float("navigation.title_scale", kTitleTextScale), layout.Color("navigation.title_color", kTitleTextColor));
}
void TrainingHud::Draw()
{
    weaponPanel_->Draw();
    controlsPanel_->Draw();
    navigation_->Draw();
    accent_->Draw();
}
}
