/**
 * @file UIMenu.cpp
 * @brief UIMenuが担当する処理を実装するファイル
 */
#include "UIMenu.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr Vector4 kSelectedColor = { 0.2f, 0.8f, 0.2f, 0.9f };
constexpr Vector4 kIdleColor = { 0.4f, 0.4f, 0.4f, 0.7f };
constexpr Vector4 kDisabledColor = { 0.25f, 0.25f, 0.25f, 0.5f };
constexpr Vector4 kTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kTextDisabledColor = { 0.5f, 0.5f, 0.5f, 0.8f };
} // namespace

void UIMenu::Initialize(SpriteCommon* spriteCommon, FontRenderer* fontRenderer)
{
    spriteCommon_ = spriteCommon;
    fontRenderer_ = fontRenderer;
}

void UIMenu::SetLayout(float x, float y, float itemWidth, float itemHeight)
{
    x_ = x;
    y_ = y;
    itemWidth_ = itemWidth;
    itemHeight_ = itemHeight;
    RebuildBoxes();
}

void UIMenu::SetItems(const std::vector<UIButton>& items)
{
    items_ = items;
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
}

void UIMenu::Update(Input* input)
{
    if (items_.empty()) {
        return;
    }

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

    for (size_t i = 0; i < boxes_.size(); ++i) {
        Vector4 color = kIdleColor;
        if (!items_[i].enabled) {
            color = kDisabledColor;
        } else if (static_cast<int>(i) == cursor_) {
            color = kSelectedColor;
        }
        boxes_[i]->SetColor(color);
        boxes_[i]->Update();
    }
}

bool UIMenu::ConsumeConfirm(Input* input)
{
    if (items_.empty() || !items_[cursor_].enabled) {
        return false;
    }
    return input->TriggerKey(DIK_SPACE) || input->TriggerKey(DIK_RETURN);
}

void UIMenu::Draw()
{
    for (auto& box : boxes_) {
        box->Draw();
    }

    for (size_t i = 0; i < items_.size(); ++i) {
        const Vector4 textColor = items_[i].enabled ? kTextColor : kTextDisabledColor;
        const float labelY = y_ + itemHeight_ * static_cast<float>(i) + itemHeight_ * 0.3f;
        fontRenderer_->DrawString(items_[i].label, x_ + 24.0f, labelY, 1.5f, textColor);

        if (static_cast<int>(i) == cursor_) {
            fontRenderer_->DrawString(">", x_ - 24.0f, labelY, 1.5f, kSelectedColor);
            fontRenderer_->DrawString("<", x_ + itemWidth_ + 8.0f, labelY, 1.5f, kSelectedColor);
        }
    }
}
