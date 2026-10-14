/**
 * @file CommandListView.cpp
 * @brief CommandListViewのJSON読み込み、武器と技の選択、一覧と説明欄の描画を実装するファイル
 */
#include "CommandListView.h"
#include "Audio.h"
#include "FontRenderer.h"
#include "Input.h"
#include "JsonHelper.h"
#include "StringUtility.h"
#include "UILayout.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
using namespace engine;
using namespace engine::game;

namespace {

constexpr const char* kDataPath = "Resources/Config/command_list.json";
constexpr const char* kLayoutName = "command_list";

// 画面の配置（UILayout の command_list で F2 から動かせる）
constexpr Vector2 kTitlePos = { 60.0f, 36.0f };
constexpr float kTitleScale = 2.0f;
constexpr Vector2 kTabsPos = { 60.0f, 96.0f };
constexpr float kTabScale = 1.4f;
constexpr float kTabGap = 28.0f; // 武器名どうしの間隔
constexpr float kTabPadding = 8.0f; // 選択中の武器名の下敷きの余白
constexpr Vector2 kTaglinePos = { 60.0f, 144.0f };
constexpr float kTaglineScale = 1.2f;
constexpr Vector2 kListPos = { 60.0f, 184.0f };
constexpr float kListWidth = 1160.0f;
constexpr float kRowHeight = 42.0f;
constexpr int kVisibleRows = 9;
constexpr float kRowScale = 1.4f;
constexpr float kRowTextInset = 16.0f; // 行の左右の余白
constexpr float kNameColumn = 150.0f; // 技名を書き始める位置（行の左端から）
constexpr float kRowTextOffsetY = 10.0f; // 行の上端から文字までの余白
constexpr Vector2 kDetailPos = { 60.0f, 576.0f };
constexpr float kDetailHeight = 92.0f;
constexpr float kDetailScale = 1.3f;
constexpr float kDetailPadding = 14.0f;
constexpr Vector2 kHelpPos = { 60.0f, 686.0f };
constexpr float kHelpScale = 1.0f;

constexpr Vector4 kTitleColor = { 1.0f, 0.78f, 0.15f, 1.0f };
constexpr Vector4 kTabColor = { 0.6f, 0.62f, 0.7f, 1.0f };
constexpr Vector4 kTabSelectedColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kTabSelectedPanel = { 0.85f, 0.25f, 0.2f, 0.9f };
constexpr Vector4 kTaglineColor = { 0.85f, 0.85f, 0.9f, 1.0f };
constexpr Vector4 kListPanel = { 0.08f, 0.08f, 0.12f, 0.92f };
constexpr Vector4 kRowStripe = { 0.12f, 0.12f, 0.17f, 0.92f }; // 1行おきの縞（行を目で追いやすくする）
constexpr Vector4 kRowSelectedPanel = { 0.85f, 0.25f, 0.2f, 0.85f };
constexpr Vector4 kCategoryColor = { 1.0f, 0.78f, 0.15f, 1.0f };
constexpr Vector4 kNameColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kInputColor = { 0.55f, 0.85f, 1.0f, 1.0f };
constexpr Vector4 kDetailPanel = { 0.05f, 0.05f, 0.08f, 0.95f };
constexpr Vector4 kDetailColor = { 0.95f, 0.92f, 0.85f, 1.0f };
constexpr Vector4 kHelpColor = { 0.7f, 0.7f, 0.7f, 1.0f };
constexpr Vector4 kLockedColor = { 0.45f, 0.45f, 0.5f, 1.0f };

const std::wstring kLockedName = L"？？？";
const std::wstring kLockedTagline = L"まだ手に入れていない武器";

/** @brief JSON の文字列項目を表示用の文字列で返す（無ければ空） */
std::wstring ReadText(const nlohmann::json& j, const char* key)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) {
        return { };
    }
    return StringUtility::ConvertString(j[key].get<std::string>());
}

std::vector<CommandMove> ReadMoves(const nlohmann::json& list)
{
    std::vector<CommandMove> moves;
    if (!list.is_array()) {
        return moves;
    }
    for (const nlohmann::json& m : list) {
        CommandMove move { ReadText(m, "category"), ReadText(m, "name"), ReadText(m, "input"), ReadText(m, "detail") };
        if (!move.name.empty()) {
            moves.push_back(std::move(move));
        }
    }
    return moves;
}

} // namespace

void CommandListView::Initialize(FontRenderer* fontRenderer, Audio* audio)
{
    fontRenderer_ = fontRenderer;
    audio_ = audio;

    sheets_.clear();
    commonMoves_.clear();
    const nlohmann::json j = JsonHelper::Load(kDataPath);
    if (j.is_object() && j.contains("weapons") && j["weapons"].is_object()) {
        const nlohmann::json& weapons = j["weapons"];
        for (auto it = weapons.begin(); it != weapons.end(); ++it) {
            CommandSheet sheet;
            sheet.tagline = ReadText(it.value(), "tagline");
            sheet.moves = ReadMoves(it.value().is_object() && it.value().contains("moves") ? it.value()["moves"] : nlohmann::json { });
            sheets_[ParseWeaponTypeName(it.key())] = std::move(sheet);
        }
    }
    if (j.is_object() && j.contains("common")) {
        commonMoves_ = ReadMoves(j["common"]);
    }
}

void CommandListView::Open()
{
    weaponIndex_ = WeaponManager::GetInstance()->GetIndex();
    RebuildRows();
}

void CommandListView::RebuildRows()
{
    rows_.clear();
    cursor_ = 0;
    scroll_ = 0;
    const auto& list = WeaponManager::GetInstance()->GetList();
    if (weaponIndex_ < 0 || weaponIndex_ >= static_cast<int>(list.size())) {
        return;
    }
    // 持っていない武器は技を伏せ、共通の操作だけを見せる
    if (WeaponManager::GetInstance()->IsUnlocked(weaponIndex_)) {
        const auto sheet = sheets_.find(list[weaponIndex_].type);
        if (sheet != sheets_.end()) {
            for (const CommandMove& move : sheet->second.moves) {
                rows_.push_back(&move);
            }
        }
    }
    for (const CommandMove& move : commonMoves_) {
        rows_.push_back(&move);
    }
}

void CommandListView::MoveWeapon(int step)
{
    const int count = static_cast<int>(WeaponManager::GetInstance()->GetList().size());
    if (count <= 0) {
        return;
    }
    weaponIndex_ = (weaponIndex_ + step + count) % count;
    RebuildRows();
    if (audio_ != nullptr) {
        audio_->PlayMenuSelect();
    }
}

bool CommandListView::Update(Input* input)
{
    if (input->TriggerMenuCancel()) {
        if (audio_ != nullptr) {
            audio_->PlayMenuSelect();
        }
        return true;
    }
    if (input->TriggerMenuLeft()) {
        MoveWeapon(-1);
    }
    if (input->TriggerMenuRight()) {
        MoveWeapon(1);
    }
    const int rowCount = static_cast<int>(rows_.size());
    if (rowCount > 0) {
        if (input->TriggerMenuUp()) {
            cursor_ = (cursor_ - 1 + rowCount) % rowCount;
        }
        if (input->TriggerMenuDown()) {
            cursor_ = (cursor_ + 1) % rowCount;
        }
        // 選んでいる行が一覧の外へ出たら、見える位置までずらす
        scroll_ = std::clamp(scroll_, (std::max)(cursor_ - kVisibleRows + 1, 0), cursor_);
    }
    return false;
}

std::wstring CommandListView::WrapText(const std::wstring& text, float maxWidth, float scale)
{
    std::wstring result;
    std::wstring line;
    for (wchar_t ch : text) {
        const std::wstring candidate = line + ch;
        if (!line.empty() && FontRenderer::MeasureStringW(candidate, scale).x > maxWidth) {
            result += line + L'\n';
            line = std::wstring(1, ch);
        } else {
            line = candidate;
        }
    }
    return result + line;
}

void CommandListView::Draw(const Input* input)
{
    if (fontRenderer_ == nullptr) {
        return;
    }
    FontRenderer& font = *fontRenderer_;
    UILayout& layout = UILayout::Get(kLayoutName);
    auto expand = [input](const std::wstring& text) { return input != nullptr ? input->ExpandPrompts(text) : text; };

    // ── 見出し ──
    const Vector2 titlePos = layout.Pos("title.pos", kTitlePos);
    font.DrawStringW(L"コマンド表", titlePos.x, titlePos.y, layout.Float("title.scale", kTitleScale), layout.Color("title.color", kTitleColor));

    // ── 武器の切り替え（持っていない武器は伏せる） ──
    const auto& list = WeaponManager::GetInstance()->GetList();
    const Vector2 tabsPos = layout.Pos("tabs.pos", kTabsPos);
    const float tabScale = layout.Float("tabs.scale", kTabScale);
    float tabX = tabsPos.x;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const bool unlocked = WeaponManager::GetInstance()->IsUnlocked(i);
        const std::wstring name = unlocked ? list[i].styleNameJp : kLockedName;
        const Vector2 size = FontRenderer::MeasureStringW(name, tabScale);
        if (i == weaponIndex_) {
            font.DrawPanel(tabX - kTabPadding, tabsPos.y - kTabPadding, size.x + kTabPadding * 2.0f, size.y + kTabPadding * 2.0f,
                layout.Color("tabs.selected_panel", kTabSelectedPanel));
        }
        const Vector4 color = (i == weaponIndex_) ? kTabSelectedColor : (unlocked ? kTabColor : kLockedColor);
        font.DrawStringW(name, tabX, tabsPos.y, tabScale, color);
        tabX += size.x + kTabGap;
    }

    // ── 武器の一言説明 ──
    const bool weaponValid = weaponIndex_ >= 0 && weaponIndex_ < static_cast<int>(list.size());
    std::wstring tagline;
    if (weaponValid && WeaponManager::GetInstance()->IsUnlocked(weaponIndex_)) {
        const auto sheet = sheets_.find(list[weaponIndex_].type);
        tagline = (sheet != sheets_.end()) ? sheet->second.tagline : std::wstring { };
    } else {
        tagline = kLockedTagline;
    }
    const Vector2 taglinePos = layout.Pos("tagline.pos", kTaglinePos);
    font.DrawStringW(tagline, taglinePos.x, taglinePos.y, layout.Float("tagline.scale", kTaglineScale), layout.Color("tagline.color", kTaglineColor));

    // ── 技の一覧 ──
    const Vector2 listPos = layout.Pos("list.pos", kListPos);
    const float listWidth = layout.Float("list.width", kListWidth);
    const float rowHeight = layout.Float("list.row_height", kRowHeight);
    const float rowScale = layout.Float("list.scale", kRowScale);
    font.DrawPanel(listPos.x, listPos.y, listWidth, rowHeight * kVisibleRows, layout.Color("list.panel", kListPanel));
    const int lastRow = (std::min)(scroll_ + kVisibleRows, static_cast<int>(rows_.size()));
    for (int i = scroll_; i < lastRow; ++i) {
        const CommandMove& move = *rows_[i];
        const float rowY = listPos.y + rowHeight * static_cast<float>(i - scroll_);
        if (i == cursor_) {
            font.DrawPanel(listPos.x, rowY, listWidth, rowHeight, layout.Color("list.selected_panel", kRowSelectedPanel));
        } else if ((i % 2) == 1) {
            font.DrawPanel(listPos.x, rowY, listWidth, rowHeight, kRowStripe);
        }
        const float textY = rowY + kRowTextOffsetY;
        font.DrawStringW(L"[" + move.category + L"]", listPos.x + kRowTextInset, textY, rowScale, kCategoryColor);
        font.DrawStringW(move.name, listPos.x + kNameColumn, textY, rowScale, kNameColor);
        // 入力は右端にそろえる（{Attack} などは最後に使った機器のボタン名になる）
        const std::wstring inputText = expand(move.input);
        const float inputWidth = FontRenderer::MeasureStringW(inputText, rowScale).x;
        font.DrawStringW(inputText, listPos.x + listWidth - kRowTextInset - inputWidth, textY, rowScale, kInputColor);
    }

    // ── 選んでいる技の説明 ──
    const Vector2 detailPos = layout.Pos("detail.pos", kDetailPos);
    const float detailScale = layout.Float("detail.scale", kDetailScale);
    font.DrawPanel(detailPos.x, detailPos.y, listWidth, kDetailHeight, layout.Color("detail.panel", kDetailPanel));
    if (cursor_ >= 0 && cursor_ < static_cast<int>(rows_.size())) {
        const std::wstring detail = WrapText(expand(rows_[cursor_]->detail), listWidth - kDetailPadding * 2.0f, detailScale);
        font.DrawStringW(detail, detailPos.x + kDetailPadding, detailPos.y + kDetailPadding, detailScale, kDetailColor);
    }

    // ── 操作の案内 ──
    const Vector2 helpPos = layout.Pos("help.pos", kHelpPos);
    // 戻るは Backspace（パッドは B）。ESC はアプリの終了に使われているので案内しない
    const bool gamepad = (input != nullptr) && input->IsUsingGamepad();
    const std::wstring backLabel = gamepad ? L"B" : L"Backspace";
    font.DrawStringW(L"←→: 武器を切り替え    ↑↓: 技を選ぶ    " + backLabel + L": 戻る", helpPos.x, helpPos.y,
        layout.Float("help.scale", kHelpScale), kHelpColor);
}
