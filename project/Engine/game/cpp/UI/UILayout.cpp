/**
 * @file UILayout.cpp
 * @brief UIレイアウト調整値の読み込み・登録・保存と編集パネル（UILayout）の実装
 */
#include "UILayout.h"
#include "JsonHelper.h"
#include <algorithm>
#include <memory>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine;
using namespace engine::game;

namespace {
constexpr const char* kLayoutDirectory = "Resources/Config/UI/";
constexpr const char* kLayoutExtension = ".json";
constexpr int kScalarComponents = 1;
constexpr int kVectorComponents = 2;
constexpr int kColorComponents = 4;
constexpr float kDragSpeed = 1.0f;
#ifdef USE_IMGUI
constexpr ImVec4 kSelectedLabelColor = { 1.0f, 0.85f, 0.2f, 1.0f };
#endif

std::unordered_map<std::string, std::unique_ptr<UILayout>>& Layouts()
{
    static std::unordered_map<std::string, std::unique_ptr<UILayout>> layouts;
    return layouts;
}

/** @brief 現在のシーンで参照された順のレイアウト名（パネルや印の並び順） */
std::vector<std::string>& UsedOrder()
{
    static std::vector<std::string> order;
    return order;
}

const UILayout* gSelectedLayout = nullptr;
size_t gSelectedIndex = 0;

/** @brief キーの "." より前（グループ名）。無ければ空文字 */
std::string GroupOf(const std::string& key)
{
    const size_t dot = key.find('.');
    return dot == std::string::npos ? std::string() : key.substr(0, dot);
}

/** @brief キーの "." より後（パネルに出す項目名） */
std::string LabelOf(const std::string& key)
{
    const size_t dot = key.find('.');
    return dot == std::string::npos ? key : key.substr(dot + 1);
}
} // namespace

UILayout& UILayout::Get(const std::string& name)
{
    auto& layouts = Layouts();
    auto it = layouts.find(name);
    if (it == layouts.end()) {
        it = layouts.emplace(name, std::unique_ptr<UILayout>(new UILayout(name))).first;
    }
    UILayout& layout = *it->second;
    if (!layout.usedInScene_) {
        layout.usedInScene_ = true;
        UsedOrder().push_back(name);
    }
    return layout;
}

void UILayout::ResetUsedLayouts()
{
    for (auto& [name, layout] : Layouts()) {
        layout->usedInScene_ = false;
    }
    UsedOrder().clear();
    gSelectedLayout = nullptr;
}

UILayout::UILayout(const std::string& name)
    : name_(name)
    , path_(std::string(kLayoutDirectory) + name + kLayoutExtension)
{
    loaded_ = JsonHelper::Load(path_);
    if (!loaded_.is_object()) {
        loaded_ = nlohmann::json::object();
    }
}

int UILayout::ComponentsOf(Kind kind)
{
    switch (kind) {
    case Kind::Float:
        return kScalarComponents;
    case Kind::Color:
        return kColorComponents;
    default:
        return kVectorComponents;
    }
}

UILayout::Entry& UILayout::FindOrRegister(const std::string& key, Kind kind, const float* defaultValue)
{
    const int count = ComponentsOf(kind);
    auto it = indexOf_.find(key);
    if (it != indexOf_.end()) {
        Entry& entry = entries_[it->second];
        // コード側の既定値が変わっても、保存時の差分判定が新しい既定値で行われるようにする
        std::copy(defaultValue, defaultValue + count, entry.defaultValue);
        if (entry.kind != kind) {
            // 同じキーを別の型で使い直した場合は登録し直す
            entry.kind = kind;
            std::copy(defaultValue, defaultValue + count, entry.value);
            ApplyLoadedValue(entry);
        }
        return entry;
    }

    Entry entry;
    entry.key = key;
    entry.kind = kind;
    std::copy(defaultValue, defaultValue + count, entry.defaultValue);
    std::copy(defaultValue, defaultValue + count, entry.value);
    ApplyLoadedValue(entry);
    indexOf_.emplace(key, entries_.size());
    entries_.push_back(entry);
    return entries_.back();
}

void UILayout::ApplyLoadedValue(Entry& entry) const
{
    auto it = loaded_.find(entry.key);
    if (it == loaded_.end()) {
        return;
    }
    const int count = ComponentsOf(entry.kind);
    if (count == kScalarComponents) {
        if (it->is_number()) {
            entry.value[0] = it->get<float>();
        }
        return;
    }
    if (!it->is_array() || static_cast<int>(it->size()) != count) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        if ((*it)[i].is_number()) {
            entry.value[i] = (*it)[i].get<float>();
        }
    }
}

float UILayout::Float(const std::string& key, float defaultValue)
{
    return FindOrRegister(key, Kind::Float, &defaultValue).value[0];
}

Vector2 UILayout::Vec2(const std::string& key, const Vector2& defaultValue)
{
    const float values[kVectorComponents] = { defaultValue.x, defaultValue.y };
    const Entry& entry = FindOrRegister(key, Kind::Vec2, values);
    return { entry.value[0], entry.value[1] };
}

Vector2 UILayout::Pos(const std::string& key, const Vector2& defaultValue)
{
    const float values[kVectorComponents] = { defaultValue.x, defaultValue.y };
    const Entry& entry = FindOrRegister(key, Kind::Pos, values);
    return { entry.value[0], entry.value[1] };
}

Vector4 UILayout::Color(const std::string& key, const Vector4& defaultValue)
{
    const float values[kColorComponents] = { defaultValue.x, defaultValue.y, defaultValue.z, defaultValue.w };
    const Entry& entry = FindOrRegister(key, Kind::Color, values);
    return { entry.value[0], entry.value[1], entry.value[2], entry.value[3] };
}

void UILayout::SetPosition(size_t index, const Vector2& position)
{
    if (index >= entries_.size() || entries_[index].kind != Kind::Pos) {
        return;
    }
    Entry& entry = entries_[index];
    if (entry.value[0] == position.x && entry.value[1] == position.y) {
        return;
    }
    entry.value[0] = position.x;
    entry.value[1] = position.y;
    MarkChanged();
}

void UILayout::MarkChanged()
{
    unsaved_ = true;
}

std::vector<UILayout::PositionHandle> UILayout::CollectPositionHandles()
{
    std::vector<PositionHandle> handles;
    for (const std::string& name : UsedOrder()) {
        UILayout& layout = *Layouts()[name];
        for (size_t i = 0; i < layout.entries_.size(); ++i) {
            const Entry& entry = layout.entries_[i];
            if (entry.kind != Kind::Pos) {
                continue;
            }
            handles.push_back({ &layout, i, name + ":" + entry.key, { entry.value[0], entry.value[1] } });
        }
    }
    return handles;
}

void UILayout::SetSelectedHandle(const UILayout* layout, size_t index)
{
    gSelectedLayout = layout;
    gSelectedIndex = index;
}

bool UILayout::IsSelectedHandle(const UILayout* layout, size_t index)
{
    return gSelectedLayout == layout && gSelectedIndex == index;
}

void UILayout::Save()
{
    // まだ参照されていないキー（別条件でだけ出るUI等）は読み込んだ値をそのまま残す
    nlohmann::json root = loaded_;
    for (const Entry& entry : entries_) {
        const int count = ComponentsOf(entry.kind);
        const bool isDefault = std::equal(entry.value, entry.value + count, entry.defaultValue);
        if (isDefault) {
            root.erase(entry.key);
            continue;
        }
        if (count == kScalarComponents) {
            root[entry.key] = entry.value[0];
        } else {
            root[entry.key] = std::vector<float>(entry.value, entry.value + count);
        }
    }
    JsonHelper::Save(path_, root);
    loaded_ = root;
    unsaved_ = false;
}

void UILayout::Reload()
{
    loaded_ = JsonHelper::Load(path_);
    if (!loaded_.is_object()) {
        loaded_ = nlohmann::json::object();
    }
    for (Entry& entry : entries_) {
        const int count = ComponentsOf(entry.kind);
        std::copy(entry.defaultValue, entry.defaultValue + count, entry.value);
        ApplyLoadedValue(entry);
    }
    unsaved_ = false;
}

void UILayout::DrawEditorPanel()
{
#ifdef USE_IMGUI
    if (UsedOrder().empty()) {
        return;
    }
    if (!ImGui::Begin("UIレイアウト")) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("画面上の四角い印はドラッグで動かせる。保存すると Resources/Config/UI/ に書き出す");
    ImGui::TextDisabled("項目を右クリックすると既定値に戻せる");
    for (const std::string& name : UsedOrder()) {
        Layouts()[name]->DrawEntries();
    }
    ImGui::End();
#endif
}

void UILayout::DrawEntries()
{
#ifdef USE_IMGUI
    const std::string header = name_ + (unsaved_ ? " *" : "") + "###" + name_;
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::PushID(name_.c_str());
    if (ImGui::Button("保存")) {
        Save();
    }
    ImGui::SameLine();
    if (ImGui::Button("読み直し")) {
        Reload();
    }
    ImGui::SameLine();
    if (ImGui::Button("すべて既定値に戻す")) {
        for (Entry& entry : entries_) {
            const int count = ComponentsOf(entry.kind);
            std::copy(entry.defaultValue, entry.defaultValue + count, entry.value);
        }
        MarkChanged();
    }

    std::vector<std::string> groups;
    for (const Entry& entry : entries_) {
        const std::string group = GroupOf(entry.key);
        if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
            groups.push_back(group);
        }
    }
    for (const std::string& group : groups) {
        const bool open = group.empty() || ImGui::TreeNodeEx(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (!open) {
            continue;
        }
        for (size_t i = 0; i < entries_.size(); ++i) {
            Entry& entry = entries_[i];
            if (GroupOf(entry.key) != group) {
                continue;
            }
            const std::string label = LabelOf(entry.key);
            const bool selected = IsSelectedHandle(this, i);
            ImGui::PushID(entry.key.c_str());
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Text, kSelectedLabelColor);
            }
            bool changed = false;
            switch (entry.kind) {
            case Kind::Float:
                changed = ImGui::DragFloat(label.c_str(), entry.value, kDragSpeed);
                break;
            case Kind::Vec2:
            case Kind::Pos:
                changed = ImGui::DragFloat2(label.c_str(), entry.value, kDragSpeed);
                break;
            case Kind::Color:
                changed = ImGui::ColorEdit4(label.c_str(), entry.value, ImGuiColorEditFlags_AlphaBar);
                break;
            }
            if (selected) {
                ImGui::PopStyleColor();
            }
            if (ImGui::BeginPopupContextItem("reset")) {
                if (ImGui::MenuItem("既定値に戻す")) {
                    const int count = ComponentsOf(entry.kind);
                    std::copy(entry.defaultValue, entry.defaultValue + count, entry.value);
                    changed = true;
                }
                ImGui::EndPopup();
            }
            if (changed) {
                MarkChanged();
            }
            ImGui::PopID();
        }
        if (!group.empty()) {
            ImGui::TreePop();
        }
    }
    ImGui::PopID();
#endif
}
