/**
 * @file UILayout.cpp
 * @brief UIレイアウト調整値の読み込み・登録・保存と編集パネル（UILayout）の実装
 */
#include "UILayout.h"
#include "JsonHelper.h"
#include <algorithm>
#include <iterator>
#include <memory>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace engine::game {
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

/** @brief 名前ごとのUIレイアウトの表を返す */
std::unordered_map<std::string, std::unique_ptr<UILayout>>& Layouts() {
    static std::unordered_map<std::string, std::unique_ptr<UILayout>> sLayouts;
    return sLayouts;
}

/** @brief 現在のシーンで参照された順のレイアウト名（パネルや印の並び順） */
std::vector<std::string>& UsedOrder() {
    static std::vector<std::string> sOrder;
    return sOrder;
}

/** @brief 画面上で選択中の位置項目（どのレイアウトの何番目か） */
struct HandleSelection {
    const UILayout* layout = nullptr;
    size_t index = 0;
};

/** @brief 選択中の位置項目を返す */
HandleSelection& SelectedHandle() {
    static HandleSelection sSelection;
    return sSelection;
}

/** @brief キーの "." より前（グループ名）。無ければ空文字 */
std::string GroupOf(const std::string& key) {
    const size_t dot = key.find('.');
    return dot == std::string::npos ? std::string() : key.substr(0, dot);
}

/** @brief キーの "." より後（パネルに出す項目名） */
std::string LabelOf(const std::string& key) {
    const size_t dot = key.find('.');
    return dot == std::string::npos ? key : key.substr(dot + 1);
}
} // namespace

/** @brief 名前ごとの唯一のインスタンスを取得する（初回は Resources/Config/UI/<name>.json を読む） */
UILayout& UILayout::Get(const std::string& name) {
    auto& layouts = Layouts();
    auto it = layouts.find(name);
    if (it == layouts.end()) {
        it = layouts.emplace(name, std::make_unique<UILayout>(UILayout(name))).first;
    }
    UILayout& layout = *it->second;
    if (!layout.usedInScene_) {
        layout.usedInScene_ = true;
        UsedOrder().push_back(name);
    }
    return layout;
}

/** @brief 編集パネルに並べる対象をリセットする（シーン切り替え時にSceneManagerが呼ぶ） */
void UILayout::ResetUsedLayouts() {
    for (auto& [name, layout] : Layouts()) {
        layout->usedInScene_ = false;
    }
    UsedOrder().clear();
    SelectedHandle().layout = nullptr;
}

/** @brief レイアウト名に対応するJSONファイルを読み込んで初期化する */
UILayout::UILayout(const std::string& name)
    : name_(name)
    , path_(std::string(kLayoutDirectory) + name + kLayoutExtension) {
    loaded_ = JsonHelper::Load(path_);
    if (!loaded_.is_object()) {
        loaded_ = nlohmann::json::object();
    }
}

/** @brief 種類ごとの要素数（Floatは1、Vec2/Posは2、Colorは4） */
int UILayout::ComponentsOf(Kind kind) {
    // Kind の並び順
    static constexpr int kComponents[] = {
        kScalarComponents, // Float
        kVectorComponents, // Vec2
        kVectorComponents, // Pos
        kColorComponents, // Color
    };
    const size_t index = static_cast<size_t>(kind);
    return index < std::size(kComponents) ? kComponents[index] : kVectorComponents;
}

/** @brief キーに対応する項目を探し、無ければ既定値とJSONの値で登録する */
UILayout::Entry& UILayout::FindOrRegister(const std::string& key, Kind kind, const float* defaultValue) {
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

/** @brief 読み込み済みJSONの値を項目へ反映する（形が合わなければ既定値のまま） */
void UILayout::ApplyLoadedValue(Entry& entry) const {
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

/** @brief 数値を取得する（未登録なら既定値で登録する） */
float UILayout::Float(const std::string& key, float defaultValue) {
    return FindOrRegister(key, Kind::kFloat, &defaultValue).value[0];
}

/** @brief 2次元の値（サイズ・間隔など）を取得する（未登録なら既定値で登録する） */
Vector2 UILayout::Vec2(const std::string& key, const Vector2& defaultValue) {
    const float values[kVectorComponents] = { defaultValue.x, defaultValue.y };
    const Entry& entry = FindOrRegister(key, Kind::kVec2, values);
    return { entry.value[0], entry.value[1] };
}

/** @brief 画面上の位置を取得する。エディタ表示中は印が出てドラッグで動かせる */
Vector2 UILayout::Pos(const std::string& key, const Vector2& defaultValue) {
    const float values[kVectorComponents] = { defaultValue.x, defaultValue.y };
    const Entry& entry = FindOrRegister(key, Kind::kPos, values);
    return { entry.value[0], entry.value[1] };
}

/** @brief 色を取得する（未登録なら既定値で登録する） */
Vector4 UILayout::Color(const std::string& key, const Vector4& defaultValue) {
    const float values[kColorComponents] = { defaultValue.x, defaultValue.y, defaultValue.z, defaultValue.w };
    const Entry& entry = FindOrRegister(key, Kind::kColor, values);
    return { entry.value[0], entry.value[1], entry.value[2], entry.value[3] };
}

/** @brief 位置項目をエディタから書き換える */
void UILayout::SetPosition(size_t index, const Vector2& position) {
    if (index >= entries_.size() || entries_[index].kind != Kind::kPos) {
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

/** @brief 値が変わったことを記録する（未保存表示と再配置判定用の番号を進める） */
void UILayout::MarkChanged() {
    unsaved_ = true;
}

/** @brief 現在のシーンで参照された位置項目をすべて集める */
std::vector<UILayout::PositionHandle> UILayout::CollectPositionHandles() {
    std::vector<PositionHandle> handles;
    for (const std::string& name : UsedOrder()) {
        UILayout& layout = *Layouts()[name];
        for (size_t i = 0; i < layout.entries_.size(); ++i) {
            const Entry& entry = layout.entries_[i];
            if (entry.kind != Kind::kPos) {
                continue;
            }
            handles.push_back({ &layout, i, name + ":" + entry.key, { entry.value[0], entry.value[1] } });
        }
    }
    return handles;
}

/** @brief 画面上で選択中の位置項目を設定する（パネル上で強調表示する） */
void UILayout::SetSelectedHandle(const UILayout* layout, size_t index) {
    SelectedHandle() = { layout, index };
}

/** @brief 画面上で選択中の位置項目か */
bool UILayout::IsSelectedHandle(const UILayout* layout, size_t index) {
    const HandleSelection& selection = SelectedHandle();
    return selection.layout == layout && selection.index == index;
}

/** @brief 既定値と違う項目だけをJSONへ書き出す */
void UILayout::Save() {
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

/** @brief JSONを読み直して登録済みの項目へ反映する */
void UILayout::Reload() {
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

/** @brief 現在のシーンで参照されたレイアウトの編集パネルを描く（ImGuiのUpdateフェーズで呼ぶ） */
void UILayout::DrawEditorPanel() {
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

/** @brief パネル内に自分の項目を並べる */
void UILayout::DrawEntries() {
#ifdef USE_IMGUI
    const std::string header = name_ + (unsaved_ ? " *" : "") + "###" + name_;
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::PushID(name_.c_str());
    // 項目の値を、コード側で決めた既定値へ戻す
    auto resetToDefault = [](Entry& entry) {
        const int count = ComponentsOf(entry.kind);
        std::copy(entry.defaultValue, entry.defaultValue + count, entry.value);
    };
    // 保存・読み直し・全項目を既定値へ戻す
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
            resetToDefault(entry);
        }
        MarkChanged();
    }

    // キーの . より前をグループ名とし、登録順にグループを集める
    std::vector<std::string> groups;
    for (const Entry& entry : entries_) {
        const std::string group = GroupOf(entry.key);
        if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
            groups.push_back(group);
        }
    }
    // グループごとに折りたたみ、項目の種類に合った編集欄を出す（右クリックで既定値へ戻す）
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
            // Kind の並び順に、値を編集するウィジェット
            using ValueWidget = bool (*)(const char*, float*);
            // 大きさと位置はどちらも2つの数値を動かす同じ欄で編集する
            static constexpr ValueWidget kDragPair = [](const char* l, float* v) {
                return ImGui::DragFloat2(l, v, kDragSpeed);
            };
            static constexpr ValueWidget kValueWidgets[] = {
                [](const char* l, float* v) {
                    return ImGui::DragFloat(l, v, kDragSpeed);
                }, // Float
                kDragPair, // Vec2
                kDragPair, // Pos
                [](const char* l, float* v) {
                    return ImGui::ColorEdit4(l, v, ImGuiColorEditFlags_AlphaBar);
                }, // Color
            };
            const size_t kindIndex = static_cast<size_t>(entry.kind);
            bool changed = kindIndex < std::size(kValueWidgets)
                && kValueWidgets[kindIndex](label.c_str(), entry.value);
            if (selected) {
                ImGui::PopStyleColor();
            }
            if (ImGui::BeginPopupContextItem("reset")) {
                if (ImGui::MenuItem("既定値に戻す")) {
                    resetToDefault(entry);
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

} // namespace engine::game
