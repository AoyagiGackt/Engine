/**
 * @file DebugButton.cpp
 * @brief 演出を確かめる確認用ボタン（DebugButton）の配置・押し判定・描画の実装
 */
#include "DebugButton.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "SceneShared.h"
#include "UILayout.h"
using namespace engine::graphics;

namespace engine::game {
namespace {
// 既定の見た目（F2のエディタで変えた値は Resources/Config/UI/gameplay.json に保存される）
constexpr const char* kLayoutName = "gameplay";
constexpr float kLabelScale = 1.0f;
constexpr Vector2 kPadding = { 12.0f, 8.0f }; ///< 文字の周りに取る余白
constexpr Vector4 kPanelColor = { 0.08f, 0.08f, 0.1f, 0.65f };
constexpr Vector4 kLabelColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr int kLeftMouseButton = 0;
}

/** @brief ボタンの下地のスプライトを作る（シーンの Initialize で1回） */
void DebugButton::Initialize(SpriteCommon* spriteCommon, const Desc& desc) {
    desc_ = desc;
    panel_ = std::make_unique<Sprite>();
    panel_->Initialize(spriteCommon, "Resources/white.png");
}

/** @brief game_rules.json でこのボタンを出す設定になっているか */
bool DebugButton::IsShown() const {
    return desc_.shownFlag != nullptr && GameRules::GetInstance()->Get().*desc_.shownFlag;
}

/** @brief UIレイアウトでのこのボタンの項目名（グループ名.項目） */
std::string DebugButton::Key(const char* item) const {
    return std::string(desc_.layoutGroup) + "." + item;
}

/** @brief 配置を読み直し、マウスが乗っているかと押されたかを調べる */
bool DebugButton::Update(Input* input, const DirectXCommon* dxCommon) {
    if (!IsShown()) {
        hovered_ = false;
        return false;
    }
    RefreshLayout();

    // マウスがボタンの上にあるか（デバッグUIがマウスを使っている間は反応させない）
    const Vector2 mouse = sceneShared::MouseScreenPosition(input, dxCommon);
    hovered_ = sceneShared::IsMouseFreeForGame()
        && mouse.x >= position_.x && mouse.x <= position_.x + size_.x
        && mouse.y >= position_.y && mouse.y <= position_.y + size_.y;

    // キーはマウスの位置に関係なく押せる
    const bool clicked = hovered_ && input->TriggerMouseButton(kLeftMouseButton);
    return input->TriggerKey(desc_.key) || clicked;
}

/** @brief UIレイアウトから位置と大きさを読み直す */
void DebugButton::RefreshLayout() {
    // 出さない設定の時は、F2 のエディタにも位置の印を出さない
    if (!IsShown()) {
        return;
    }
    // 大きさは文字の幅に余白を足して決め、既定の位置は画面の上端の左右中央にする
    UILayout& layout = UILayout::Get(kLayoutName);
    labelScale_ = layout.Float(Key("scale"), kLabelScale);
    const Vector2 labelSize = FontRenderer::MeasureStringW(desc_.label, labelScale_);
    size_ = { labelSize.x + kPadding.x * 2.0f, labelSize.y + kPadding.y * 2.0f };
    position_ = layout.Pos(Key("pos"), { gameConstants::kScreenCenterX - size_.x * 0.5f, desc_.defaultTop });
}

/** @brief ボタンの下地を描く（マウスが乗っている間は色を変える） */
void DebugButton::Draw() {
    if (!IsShown()) {
        return;
    }
    UILayout& layout = UILayout::Get(kLayoutName);
    panel_->SetPosition(position_);
    panel_->SetSize(size_);
    panel_->SetColor(hovered_
        ? layout.Color(Key("hover_color"), desc_.hoverColor)
        : layout.Color(Key("color"), kPanelColor));
    panel_->Update();
    panel_->Draw();
}

/** @brief ボタンの文字を積む（fontRenderer.Reset() の後に呼ぶ） */
void DebugButton::QueueText(FontRenderer& font) const {
    if (!IsShown()) {
        return;
    }
    UILayout& layout = UILayout::Get(kLayoutName);
    font.DrawStringW(desc_.label, position_.x + kPadding.x, position_.y + kPadding.y,
        labelScale_, layout.Color(Key("label_color"), kLabelColor));
}

} // namespace engine::game
