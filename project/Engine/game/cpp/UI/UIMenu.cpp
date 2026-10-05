/**
 * @file UIMenu.cpp
 * @brief カーソル移動で選択する縦一列メニュー（UIMenu）の実装
 */
#include "UIMenu.h"
#include "StringUtility.h"
#include "UILayout.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr Vector4 kSelectedColor = { 0.2f, 0.8f, 0.2f, 0.9f };
constexpr Vector4 kIdleColor = { 0.4f, 0.4f, 0.4f, 0.7f };
constexpr Vector4 kDisabledColor = { 0.25f, 0.25f, 0.25f, 0.5f };
constexpr Vector4 kTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kTextDisabledColor = { 0.5f, 0.5f, 0.5f, 0.8f };
constexpr float kLabelTopRatio = 0.3f; // 項目の高さに対する文字の上端位置
constexpr float kLabelPaddingX = 24.0f;
constexpr float kHalf = 0.5f;
constexpr wchar_t kAsciiLimit = 128;

/** @brief DrawStringWで描いたときの横幅（ASCIIは半角、それ以外は全角幅） */
float MeasureTextWidth(const std::wstring& text, float scale)
{
    float width = 0.0f;
    for (wchar_t c : text) {
        width += static_cast<float>(c < kAsciiLimit ? FontRenderer::kCharW : FontRenderer::kJpCharW) * scale;
    }
    return width;
}
constexpr float kCursorGap = 8.0f;
} // namespace

void UIMenu::Initialize(SpriteCommon* spriteCommon, FontRenderer* fontRenderer, Audio* audio)
{
    spriteCommon_ = spriteCommon;
    fontRenderer_ = fontRenderer;
    audio_ = audio;
}

void UIMenu::BindLayout(const std::string& layoutName, const std::string& group,
    const Vector2& defaultPosition, const Vector2& defaultItemSize, const UIMenuStyle& style)
{
    layoutName_ = layoutName;
    group_ = group;
    defaultPosition_ = defaultPosition;
    defaultItemSize_ = defaultItemSize;
    defaultStyle_ = style;
    ApplyBoundLayout();
}

void UIMenu::ApplyBoundLayout()
{
    if (layoutName_.empty()) {
        return;
    }
    UILayout& layout = UILayout::Get(layoutName_);
    const Vector2 position = layout.Pos(group_ + ".pos", defaultPosition_);
    const Vector2 itemSize = layout.Vec2(group_ + ".item_size", defaultItemSize_);
    selectedColor_ = layout.Color(group_ + ".selected_color", kSelectedColor);
    idleColor_ = layout.Color(group_ + ".idle_color", kIdleColor);
    disabledColor_ = layout.Color(group_ + ".disabled_color", kDisabledColor);
    textColor_ = layout.Color(group_ + ".text_color", kTextColor);
    textDisabledColor_ = layout.Color(group_ + ".text_disabled_color", kTextDisabledColor);
    labelScale_ = layout.Float(group_ + ".label_scale", defaultStyle_.labelScale);
    centerLabels_ = layout.Float(group_ + ".label_center", defaultStyle_.centerLabels ? 1.0f : 0.0f) >= kHalf;
    labelPaddingX_ = layout.Float(group_ + ".label_padding_x", kLabelPaddingX);

    // 位置や大きさが変わった時だけハイライト枠を作り直す（毎フレーム作り直さない）
    const bool moved = position.x != x_ || position.y != y_ || itemSize.x != itemWidth_ || itemSize.y != itemHeight_;
    x_ = position.x;
    y_ = position.y;
    itemWidth_ = itemSize.x;
    itemHeight_ = itemSize.y;
    if (moved || boxes_.size() != items_.size()) {
        RebuildBoxes();
    } else {
        RefreshBoxColors();
    }
}

void UIMenu::SetItems(const std::vector<UIButton>& items)
{
    items_ = items;
    labels_.clear();
    for (const UIButton& item : items_) {
        labels_.push_back(StringUtility::ConvertString(item.label));
    }
    cursor_ = 0;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].enabled) {
            cursor_ = static_cast<int>(i);
            break;
        }
    }
    RebuildBoxes();
}

void UIMenu::RebuildBoxes()
{
    if (items_.empty() || itemWidth_ <= 0.0f || itemHeight_ <= 0.0f || !spriteCommon_) {
        return;
    }

    boxes_.clear();
    boxes_.reserve(items_.size());
    for (size_t i = 0; i < items_.size(); ++i) {
        auto box = std::make_unique<Sprite>();
        box->Initialize(spriteCommon_, "Resources/white.png");
        box->SetPosition({ x_, y_ + itemHeight_ * static_cast<float>(i) });
        box->SetSize({ itemWidth_, itemHeight_ });
        boxes_.push_back(std::move(box));
    }
    RefreshBoxColors();
}

void UIMenu::RefreshBoxColors()
{
    for (size_t i = 0; i < boxes_.size(); ++i) {
        Vector4 color = idleColor_;
        if (!items_[i].enabled) {
            color = disabledColor_;
        } else if (static_cast<int>(i) == cursor_) {
            color = selectedColor_;
        }
        boxes_[i]->SetColor(color);
        boxes_[i]->Update();
    }
}

void UIMenu::Update(Input* input)
{
    if (items_.empty()) {
        return;
    }

    const int previousCursor = cursor_;
    if (input->TriggerKey(DIK_W) || input->TriggerKey(DIK_UP)) {
        for (int i = cursor_ - 1; i >= 0; --i) {
            if (items_[i].enabled) {
                cursor_ = i;
                break;
            }
        }
    }
    if (input->TriggerKey(DIK_S) || input->TriggerKey(DIK_DOWN)) {
        for (int i = cursor_ + 1; i < static_cast<int>(items_.size()); ++i) {
            if (items_[i].enabled) {
                cursor_ = i;
                break;
            }
        }
    }

    if (audio_ && cursor_ != previousCursor) {
        audio_->PlayMenuChoice();
    }
    RefreshBoxColors();
}

bool UIMenu::ConsumeConfirm(Input* input)
{
    if (items_.empty() || !items_[cursor_].enabled) {
        return false;
    }
    const bool confirmed = input->TriggerKey(DIK_SPACE) || input->TriggerKey(DIK_RETURN);
    if (confirmed && audio_) {
        audio_->PlayMenuSelect();
    }
    return confirmed;
}

void UIMenu::Draw()
{
    // エディタで位置・色を変えた結果を、ゲームが一時停止していてもその場で反映する
    ApplyBoundLayout();

    for (auto& box : boxes_) {
        box->Draw();
    }

    for (size_t i = 0; i < items_.size(); ++i) {
        const Vector4 textColor = items_[i].enabled ? textColor_ : textDisabledColor_;
        const float labelY = y_ + itemHeight_ * static_cast<float>(i) + itemHeight_ * kLabelTopRatio;
        const float labelX = centerLabels_
            ? x_ + (itemWidth_ - MeasureTextWidth(labels_[i], labelScale_)) * kHalf
            : x_ + labelPaddingX_;
        fontRenderer_->DrawStringW(labels_[i], labelX, labelY, labelScale_, textColor);

        if (static_cast<int>(i) == cursor_) {
            fontRenderer_->DrawStringW(L">", x_ - labelPaddingX_, labelY, labelScale_, selectedColor_);
            fontRenderer_->DrawStringW(L"<", x_ + itemWidth_ + kCursorGap, labelY, labelScale_, selectedColor_);
        }
    }
}
