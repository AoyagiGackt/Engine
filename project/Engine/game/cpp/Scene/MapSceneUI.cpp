/**
 * @file MapSceneUI.cpp
 * @brief MapScene のうち、画面の文字（入口の番号と案内・操作説明の下敷き・選んだマスの説明）を描く部分
 * @note 分割ファイル。クラスは MapScene のまま、画面表示の定義だけをここに置く
 */
#include "MapScene.h"
#include "MapNodeInfo.h"
#include "TextBufferCapacity.h"
#include "GameConstants.h"
#include "UILayout.h"
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string>
using namespace engine::graphics;

namespace engine::game {
using namespace mapNodeInfo;
namespace {
// 既定の配置（F2のエディタで動かした値は Resources/Config/UI/map.json に保存される）
constexpr float kHintPanelPadding = 8.0f;
constexpr Vector4 kHintPanelColor = { 0.0f, 0.0f, 0.05f, 0.65f };
constexpr float kLabelOffsetX = 42.0f;
constexpr float kLabelY = 185.0f;
constexpr float kLabelScale = 1.55f;
constexpr Vector2 kLabelShadowOffset = { 2.0f, 3.0f };
constexpr Vector4 kLabelShadowColor = { 0.02f, 0.02f, 0.04f, 0.95f };
constexpr Vector4 kLabelNearColor = { 1.0f, 1.0f, 0.2f, 1.0f };
constexpr Vector4 kLabelColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr float kEnterHintOffsetX = 43.0f;
constexpr float kEnterHintY = 225.0f;
constexpr float kEnterHintScale = 1.35f;
constexpr Vector4 kEnterHintShadowColor = { 0.02f, 0.02f, 0.03f, 0.95f };
constexpr Vector4 kEnterHintColor = { 1.0f, 0.92f, 0.22f, 1.0f };
constexpr Vector2 kInfoPanelPosition = { 875.0f, 350.0f };
constexpr Vector2 kInfoPanelSize = { 365.0f, 118.0f };
constexpr Vector4 kInfoPanelColor = { 0.015f, 0.02f, 0.04f, 0.84f };
constexpr Vector2 kInfoTitleOffset = { 25.0f, 20.0f }; // 説明パネル左上からの位置
constexpr float kInfoTitleScale = 1.8f;
constexpr Vector4 kInfoTitleColor = { 1.0f, 0.85f, 0.2f, 1.0f };
constexpr Vector2 kInfoDescOffset = { 25.0f, 60.0f };
constexpr float kInfoDescLineHeight = 25.0f;
constexpr float kInfoDescScale = 1.15f;
constexpr Vector4 kInfoDescColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr size_t kInfoDescMinWrapIndex = 10; // これより手前の全角スペースでは折り返さない
} // namespace

/** @brief 操作説明の後ろに暗い板を敷く（明るい背景でも読めるように） */
void MapScene::DrawHintPanel(const std::wstring& text, const Vector2& position, float scale) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float hintPanelPadding = layout.Float("hint_panel.padding", kHintPanelPadding);
    const Vector4 hintPanelColor = layout.Color("hint_panel.color", kHintPanelColor);
    const Vector2 size = FontRenderer::MeasureStringW(text, scale);
    fontRenderer_.DrawPanel(position.x - hintPanelPadding, position.y - hintPanelPadding,
        size.x + hintPanelPadding * 2.0f, size.y + hintPanelPadding * 2.0f, hintPanelColor);
}

/** @brief Draw()の下請け 各ステージ入口ポータルの上に番号ラベルとENTER案内を描画する */
void MapScene::DrawStagePortalLabels(int floor) {
    // 表示位置・大きさ・色は UI レイアウトから読む
    UILayout& layout = UILayout::Get(kLayoutName);
    // 影の付け方と、決定ボタン案内の位置
    const float labelOffsetX = layout.Float("portal_label.offset_x", kLabelOffsetX);
    const Vector2 labelShadowOffset = layout.Vec2("portal_label.shadow_offset", kLabelShadowOffset);
    const Vector4 labelShadowColor = layout.Color("portal_label.shadow_color", kLabelShadowColor);
    const float enterHintOffsetX = layout.Float("enter_hint.offset_x", kEnterHintOffsetX);
    const Vector4 enterHintShadowColor = layout.Color("enter_hint.shadow_color", kEnterHintShadowColor);
    // 入口の数と各入口のワールドX座標、画面へ直すためのカメラ位置
    const int count = static_cast<int>(floors_.size());
    const float* portalXs = stageWorldX_.data();
    const Vector3& cameraPos = camera_->GetTranslate();
    // ステージ名ラベルの高さ・大きさ・通常色と選択中の色
    const float labelY = layout.Float("portal_label.y", kLabelY);
    const float labelScale = layout.Float("portal_label.scale", kLabelScale);
    const Vector4 labelColor = layout.Color("portal_label.color", kLabelColor);
    const Vector4 labelNearColor = layout.Color("portal_label.near_color", kLabelNearColor);
    // 決定ボタン案内の高さ・大きさ・色。案内の文字はゲームパッドかキーボードかで変える
    const float enterHintY = layout.Float("enter_hint.y", kEnterHintY);
    const float enterHintScale = layout.Float("enter_hint.scale", kEnterHintScale);
    const Vector4 enterHintColor = layout.Color("enter_hint.color", kEnterHintColor);
    const char* enterHint = input_->IsUsingGamepad() ? "A" : "ENTER";
    for (int i = 0; i < count; ++i) {
        const bool isNear = i == selectedCol_;
        // 入口のワールド座標を画面のX座標へ直す
        const float screenX = (portalXs[i] - cameraPos.x)
                / gameConstants::kCameraHalfW * gameConstants::kScreenCenterX
            + gameConstants::kScreenCenterX;

        // ステージ名は影を少しずらして重ね、選択中は色を変える
        wchar_t stageLabel[textBufferCapacity::kShortText];
        swprintf_s(stageLabel, L"ステージ %d", i + 1);
        const wchar_t* label = stageLabel;
        fontRenderer_.DrawStringW(label, screenX - labelOffsetX + labelShadowOffset.x, labelY + labelShadowOffset.y,
            labelScale, labelShadowColor);
        fontRenderer_.DrawStringW(label, screenX - labelOffsetX, labelY, labelScale,
            isNear ? labelNearColor : labelColor);
        // 選択中の入口だけ、決定ボタンの案内と説明パネルを出す
        if (isNear) {
            // 案内もラベルと同じく影を先に描いてから本体を重ねる
            fontRenderer_.DrawString(enterHint, screenX - enterHintOffsetX + labelShadowOffset.x,
                enterHintY + labelShadowOffset.y, enterHintScale, enterHintShadowColor);
            fontRenderer_.DrawString(enterHint, screenX - enterHintOffsetX, enterHintY, enterHintScale,
                enterHintColor);
            DrawSelectedNodeInfo(floor, floors_[i][0]);
        }
    }
}

/** @brief 選択中ノードの説明パネル（右側）を描画する */
void MapScene::DrawSelectedNodeInfo(int curFloor, RunData::NodeType hoveredNode) {
    // 説明パネルの下地を置いてから、見出しと説明文を重ねる
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 infoTitleOffset = layout.Vec2("info_title.offset", kInfoTitleOffset);
    const float infoTitleScale = layout.Float("info_title.scale", kInfoTitleScale);
    const Vector4 infoTitleColor = layout.Color("info_title.color", kInfoTitleColor);
    const Vector2 infoDescOffset = layout.Vec2("info_desc.offset", kInfoDescOffset);
    const float infoDescLineHeight = layout.Float("info_desc.line_height", kInfoDescLineHeight);
    const float infoDescScale = layout.Float("info_desc.scale", kInfoDescScale);
    const Vector4 infoDescColor = layout.Color("info_desc.color", kInfoDescColor);
    // 折り返した本文と見出しを測り、枠の左右に余白を確保する。
    constexpr float kRightMargin = 16.0f;
    constexpr size_t kSeparatorLength = 1;
    const NodeInfo& panelInfo = GetNodeInfo(hoveredNode);
    const std::wstring panelTitle = panelInfo.entersCombat ? L"ステージ入口" : panelInfo.label;
    const std::wstring panelDesc(panelInfo.desc);
    const size_t panelBreak = panelDesc.find(L'　');
    const bool panelWraps = panelBreak != std::wstring::npos && panelBreak > kInfoDescMinWrapIndex;
    const std::wstring firstLine = panelWraps ? panelDesc.substr(0, panelBreak) : panelDesc;
    const std::wstring secondLine = panelWraps ? panelDesc.substr(panelBreak + kSeparatorLength) : L"";
    const float descWidth = (std::max)(FontRenderer::MeasureStringW(firstLine, infoDescScale).x,
        FontRenderer::MeasureStringW(secondLine, infoDescScale).x);
    Vector2 panelSize = layout.Vec2("info_panel.size", kInfoPanelSize);
    panelSize.x = (std::max)(panelSize.x, (std::max)(
        infoTitleOffset.x + FontRenderer::MeasureStringW(panelTitle, infoTitleScale).x + infoTitleOffset.x,
        infoDescOffset.x + descWidth + infoDescOffset.x));
    // 枠全体を画面右端の余白より内側に配置する。
    Vector2 panelPosition = layout.Pos("info_panel.pos", kInfoPanelPosition);
    panelPosition.x = (std::min)(panelPosition.x, gameConstants::kScreenWidth - kRightMargin - panelSize.x);
    nodeSprite_->SetPosition(panelPosition);
    nodeSprite_->SetSize(panelSize);
    nodeSprite_->SetColor(layout.Color("info_panel.color", kInfoPanelColor));
    nodeSprite_->Update();
    nodeSprite_->Draw();
    // ── 選択中ノードの説明（右側）──
    if (curFloor >= static_cast<int>(floors_.size())) {
        return;
    }

    // 戦闘に入るマスはステージの入口として、それ以外はマスの名前で見出しを出す
    const NodeInfo& info = GetNodeInfo(hoveredNode);
    const wchar_t* desc = info.desc;
    fontRenderer_.DrawStringW(info.entersCombat ? L"ステージ入口" : info.label,
        panelPosition.x + infoTitleOffset.x, panelPosition.y + infoTitleOffset.y, infoTitleScale, infoTitleColor);
    const float descX = panelPosition.x + infoDescOffset.x;
    const float descY = panelPosition.y + infoDescOffset.y;
    // 説明を2行に折り返して表示
    std::wstring descStr(desc);
    size_t br = descStr.find(L'　'); // 全角スペースで折り返しポイントを探す
    if (br != std::wstring::npos && br > kInfoDescMinWrapIndex) {
        fontRenderer_.DrawStringW(descStr.substr(0, br).c_str(), descX, descY, infoDescScale, infoDescColor);
        fontRenderer_.DrawStringW(descStr.substr(br + 1).c_str(), descX, descY + infoDescLineHeight,
            infoDescScale, infoDescColor);
    } else {
        fontRenderer_.DrawStringW(desc, descX, descY, infoDescScale, infoDescColor);
    }
}

} // namespace engine::game
