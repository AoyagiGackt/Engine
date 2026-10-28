/**
 * @file JsonConfigEditor.cpp
 * @brief 設定JSONの一覧・木の編集・保存と、ファイルの場所の候補の自動検出を実装するファイル
 */
#ifdef USE_IMGUI
#include "JsonConfigEditor.h"
#include "EditorUI.h"
#include "JsonHelper.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <imgui.h>

namespace engine::graphics {
namespace {

constexpr const char* kWindowTitle = "調整値エディタ";
constexpr float kFileListWidth = 210.0f;
constexpr float kLabelWidthRatio = 0.45f; // 項目名に使う横幅の割合
constexpr double kAssetRescanSeconds = 3.0; // 素材フォルダを調べ直す間隔
constexpr float kDragSpeedRatio = 0.01f; // ドラッグ1ピクセルで値の何割動かすか
constexpr float kMinDragSpeed = 0.001f;
constexpr float kIntDragSpeed = 0.2f;
constexpr float kVectorDragSpeedScale = 10.0f; // ベクトルは単体の数値よりこの倍だけ速く動かす
constexpr ImVec2 kInitialWindowSize = { 760.0f, 560.0f }; // 初めて開いた時のウィンドウの大きさ
constexpr size_t kVector3Size = 3;
constexpr size_t kColorSize = 4;
constexpr ImVec4 kDirtyColor = { 1.0f, 0.75f, 0.2f, 1.0f };
constexpr ImVec4 kLiveColor = { 0.45f, 0.9f, 0.5f, 1.0f };
constexpr ImVec4 kNextBootColor = { 0.75f, 0.75f, 0.75f, 1.0f };
constexpr const char* kNoFileLabel = "（なし）";

/** @brief 拡張子ごとの素材の種類（プルダウンの候補を同じ種類に絞るのに使う） */
struct AssetCategory {
    const char* name;
    std::array<const char*, 5> extensions;
};
constexpr AssetCategory kAssetCategories[] = {
    { "image", { ".png", ".jpg", ".jpeg", ".dds", ".tga" } },
    { "model", { ".obj", ".gltf", ".glb", ".fbx", nullptr } },
    { "sound", { ".mp3", ".wav", ".ogg", nullptr, nullptr } },
};

/** @brief パスをUTF-8の文字列にする（JSONとImGuiはUTF-8なので、日本語のファイル名でも化けないようにする） */
std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

/** @brief 小文字にした文字列を返す（英字だけ。日本語はそのまま） */
std::string ToLower(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower;
}

/** @brief 拡張子から素材の種類を返す（素材でなければ空） */
std::string CategoryOf(const std::string& path) {
    const std::string extension = ToLower(ToUtf8(std::filesystem::path(std::u8string(path.begin(), path.end())).extension()));
    for (const AssetCategory& category : kAssetCategories) {
        for (const char* candidate : category.extensions) {
            if (candidate != nullptr && extension == candidate) {
                return category.name;
            }
        }
    }
    return {};
}

/** @brief 文字列がファイルの場所に見えるか（素材の拡張子で終わる、または Resources/ から始まる） */
bool LooksLikeAssetPath(const std::string& text) {
    return !CategoryOf(text).empty() || text.rfind("Resources/", 0) == 0;
}

/** @brief "グループ.キー" の形で子の場所を作る */
std::string JoinPath(const std::string& parent, const std::string& key) {
    return parent.empty() ? key : parent + "." + key;
}

/** @brief 3つ・4つの数値だけの配列か */
bool IsNumberArray(const nlohmann::json& value, size_t size) {
    if (!value.is_array() || value.size() != size) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const nlohmann::json& v) { return v.is_number(); });
}

/** @brief 色として編集する配列か（キーに color を含む4つの数値） */
bool IsColor(const nlohmann::json& value, const std::string& key) {
    return IsNumberArray(value, kColorSize) && ToLower(key).find("color") != std::string::npos;
}

/** @brief 配列の要素の見出し（name / id / type のどれかがあればそれを添える） */
std::string ElementCaption(const nlohmann::json& element, size_t index) {
    std::string caption = "[" + std::to_string(index) + "]";
    if (element.is_object()) {
        for (const char* key : { "name", "id", "type" }) {
            const auto found = element.find(key);
            if (found != element.end() && found->is_string()) {
                return caption + " " + found->get<std::string>();
            }
        }
    } else if (element.is_string()) {
        return caption + " " + element.get<std::string>();
    }
    return caption;
}

} // namespace

/** @brief ただ1つのエディタを返す（初回の呼び出しで作る） */
JsonConfigEditor* JsonConfigEditor::GetInstance() {
    static JsonConfigEditor sInstance;
    return &sInstance;
}

/** @brief 対象フォルダと名前の表を設定する */
void JsonConfigEditor::Initialize(const std::string& configDirectory, const std::string& labelsPath, const std::string& assetDirectory) {
    configDirectory_ = configDirectory;
    labelsPath_ = labelsPath;
    assetDirectory_ = assetDirectory;
    labels_ = JsonHelper::Load(labelsPath_);
    ScanConfigFiles();
}

/** @brief 指定ファイルを保存した直後に呼ぶ処理を登録する */
void JsonConfigEditor::SetReloadCallback(const std::string& fileName, std::function<void()> onSaved) {
    reloadCallbacks_[fileName] = std::move(onSaved);
}

/** @brief フォルダを調べてファイルの一覧を作り直す（開いている内容は残す） */
void JsonConfigEditor::ScanConfigFiles() {
    std::error_code error;
    if (!std::filesystem::is_directory(configDirectory_, error)) {
        return;
    }
    const nlohmann::json hidden = labels_.value("_hidden", nlohmann::json::array());
    const nlohmann::json titles = labels_.value("_files", nlohmann::json::object());
    std::vector<FileEntry> scanned;
    for (const auto& item : std::filesystem::directory_iterator(configDirectory_, error)) {
        if (!item.is_regular_file() || ToLower(ToUtf8(item.path().extension())) != ".json") {
            continue;
        }
        const std::string fileName = ToUtf8(item.path().filename());
        if (std::find(hidden.begin(), hidden.end(), fileName) != hidden.end()) {
            continue;
        }
        // 開いたことのあるファイルは編集中の内容を引き継ぐ
        const auto existing = std::find_if(files_.begin(), files_.end(), [&](const FileEntry& e) { return e.fileName == fileName; });
        if (existing != files_.end()) {
            scanned.push_back(std::move(*existing));
            continue;
        }
        FileEntry entry;
        entry.fileName = fileName;
        entry.path = ToUtf8(item.path());
        entry.title = titles.value(fileName, fileName);
        scanned.push_back(std::move(entry));
    }
    // 日本語の名前があるファイルを先に、その中は名前順に並べる
    std::sort(scanned.begin(), scanned.end(), [&](const FileEntry& a, const FileEntry& b) {
        const bool aNamed = titles.contains(a.fileName);
        const bool bNamed = titles.contains(b.fileName);
        if (aNamed != bNamed) {
            return aNamed;
        }
        return a.fileName < b.fileName;
    });
    files_ = std::move(scanned);
    selected_ = std::clamp(selected_, 0, (std::max)(static_cast<int>(files_.size()) - 1, 0));
}

/** @brief 素材フォルダを調べてファイルの場所の候補を作り直す */
void JsonConfigEditor::ScanAssets() {
    assetsByCategory_.clear();
    std::error_code error;
    if (!std::filesystem::is_directory(assetDirectory_, error)) {
        return;
    }
    // ゲームが読むのと同じ "Resources/..." の形にそろえる
    const std::filesystem::path base = std::filesystem::path(assetDirectory_).parent_path();
    for (auto it = std::filesystem::recursive_directory_iterator(assetDirectory_, error);
        it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (error || !it->is_regular_file()) {
            continue;
        }
        const std::string category = CategoryOf(ToUtf8(it->path()));
        if (category.empty()) {
            continue;
        }
        assetsByCategory_[category].push_back(ToUtf8(std::filesystem::relative(it->path(), base, error)));
    }
    for (auto& [category, paths] : assetsByCategory_) {
        std::sort(paths.begin(), paths.end());
    }
}

/** @brief ファイルを読み込んで編集を始める */
void JsonConfigEditor::LoadEntry(FileEntry& entry) {
    entry.data = JsonHelper::Load(entry.path);
    entry.saved = entry.data;
    entry.loaded = true;
    entry.dirty = false;
}

/** @brief 編集中の内容を書き出し、登録された読み直しを呼ぶ */
void JsonConfigEditor::SaveEntry(FileEntry& entry) {
    if (!JsonHelper::Save(entry.path, entry.data)) {
        return;
    }
    entry.saved = entry.data;
    entry.dirty = false;
    const auto callback = reloadCallbacks_.find(entry.fileName);
    if (callback != reloadCallbacks_.end() && callback->second) {
        callback->second();
    }
}

/** @brief 表示中ならウィンドウを描く */
void JsonConfigEditor::Draw() {
    if (!open_) {
        return;
    }
    // ファイルを足したら出るよう、開いている間は素材を定期的に調べ直す
    const double now = ImGui::GetTime();
    if (lastAssetScanTime_ < 0.0 || now - lastAssetScanTime_ > kAssetRescanSeconds) {
        ScanAssets();
        lastAssetScanTime_ = now;
    }

    ImGui::SetNextWindowSize(kInitialWindowSize, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWindowTitle, &open_)) {
        ImGui::End();
        return;
    }
    DrawFileList();
    ImGui::SameLine();
    ImGui::BeginChild("##contents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (selected_ >= 0 && selected_ < static_cast<int>(files_.size())) {
        DrawFileContents(files_[selected_]);
    } else {
        ImGui::TextDisabled("設定ファイルがありません（%s）", configDirectory_.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
}

/** @brief 左側のファイル一覧を描く */
void JsonConfigEditor::DrawFileList() {
    ImGui::BeginChild("##files", ImVec2(kFileListWidth, 0.0f), ImGuiChildFlags_Borders);
    if (ImGui::Button("一覧を更新")) {
        ScanConfigFiles();
        labels_ = JsonHelper::Load(labelsPath_);
    }
    editorUI::HelpMarker("フォルダに足したJSONや、名前の表（tuning_labels.json）の変更を読み込み直します");
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(files_.size()); ++i) {
        const FileEntry& entry = files_[i];
        const std::string caption = (entry.dirty ? "* " : "") + entry.title + "##" + entry.fileName;
        if (ImGui::Selectable(caption.c_str(), selected_ == i)) {
            selected_ = i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", entry.fileName.c_str());
        }
    }
    ImGui::EndChild();
}

/** @brief 右側に選んだファイルの中身を描く */
void JsonConfigEditor::DrawFileContents(FileEntry& entry) {
    if (!entry.loaded) {
        LoadEntry(entry);
    }
    currentFile_ = entry.fileName;

    // 保存・元に戻すと、保存した時にすぐ効くかの表示
    ImGui::BeginDisabled(!entry.dirty);
    if (ImGui::Button("保存")) {
        SaveEntry(entry);
    }
    ImGui::SameLine();
    if (ImGui::Button("元に戻す")) {
        entry.data = entry.saved;
        entry.dirty = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("ファイルから読み直す")) {
        LoadEntry(entry);
    }
    ImGui::SameLine();
    const bool live = reloadCallbacks_.count(entry.fileName) > 0;
    ImGui::TextColored(live ? kLiveColor : kNextBootColor, live ? "保存するとすぐ反映" : "保存すると次回起動時に反映");
    if (entry.dirty) {
        ImGui::SameLine();
        ImGui::TextColored(kDirtyColor, "未保存");
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "項目を探す（日本語の名前かキー）", filter_, sizeof(filter_));

    if (!entry.data.is_object()) {
        ImGui::TextDisabled("中身を読めませんでした（%s）", entry.path.c_str());
        return;
    }
    // ファイル先頭の説明は畳んで見せる
    const auto comment = entry.data.find("_comment");
    if (comment != entry.data.end() && comment->is_string() && ImGui::CollapsingHeader("このファイルの説明")) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", comment->get<std::string>().c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    if (DrawValue(entry.data, "", "", entry.title, 0)) {
        entry.dirty = true;
    }
    ImGui::EndChild();
}

/** @brief JSONの1つの値を編集部品として描く */
bool JsonConfigEditor::DrawValue(nlohmann::json& value, const std::string& path, const std::string& key, const std::string& label, int depth) {
    if (!MatchesFilter(value, path, key)) {
        return false;
    }
    bool changed = false;
    if (value.is_object()) {
        // 直下はそのまま並べ、入れ子は畳める見出しにする
        const bool opened = depth == 0
            || (depth == 1 ? ImGui::CollapsingHeader((label + "##" + path).c_str(), ImGuiTreeNodeFlags_DefaultOpen)
                           : ImGui::TreeNode((label + "##" + path).c_str()));
        if (!opened) {
            return false;
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            // _comment などの "_" で始まるキーは説明用なので編集対象にしない
            if (it.key().empty() || it.key()[0] == '_') {
                continue;
            }
            const std::string childPath = JoinPath(path, it.key());
            ImGui::PushID(it.key().c_str());
            changed |= DrawValue(it.value(), childPath, it.key(), LabelFor(childPath, it.key()), depth + 1);
            ImGui::PopID();
        }
        if (depth >= 2) {
            ImGui::TreePop();
        }
        return changed;
    }
    if (value.is_array()) {
        return DrawArray(value, path, key, label, depth);
    }

    BeginRow(label, path);
    if (value.is_boolean()) {
        bool flag = value.get<bool>();
        if (ImGui::Checkbox("##value", &flag)) {
            value = flag;
            changed = true;
        }
    } else if (value.is_number()) {
        changed = DrawNumber(value, path, key);
    } else if (value.is_string()) {
        changed = DrawString(value);
    } else {
        ImGui::TextDisabled("(空)");
    }
    return changed;
}

/** @brief 数値を整数か小数のドラッグで編集する */
bool JsonConfigEditor::DrawNumber(nlohmann::json& value, const std::string& path, const std::string& key) {
    // 名前の表の型を優先し、無ければJSONに書かれた形（小数点の有無）で決める
    const std::string type = TypeFor(path, key);
    const bool isInt = type == "int" || (type.empty() && value.is_number_integer());
    if (isInt) {
        int number = value.get<int>();
        if (ImGui::DragInt("##value", &number, kIntDragSpeed)) {
            value = number;
            return true;
        }
        return false;
    }
    float number = value.get<float>();
    const float speed = (std::max)(std::fabs(number) * kDragSpeedRatio, kMinDragSpeed);
    if (ImGui::DragFloat("##value", &number, speed, 0.0f, 0.0f, "%.4g")) {
        value = number;
        return true;
    }
    return false;
}

/** @brief 文字列を編集する（ファイルの場所なら、素材フォルダから自動で集めた候補のプルダウンにする） */
bool JsonConfigEditor::DrawString(nlohmann::json& value) {
    std::string text = value.get<std::string>();
    const std::string category = CategoryOf(text);
    if (LooksLikeAssetPath(text) && !category.empty()) {
        bool changed = false;
        if (ImGui::BeginCombo("##value", text.empty() ? kNoFileLabel : text.c_str(), ImGuiComboFlags_HeightLarge)) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##assetFilter", "絞り込み", assetFilter_, sizeof(assetFilter_));
            if (ImGui::Selectable(kNoFileLabel, text.empty())) {
                value = std::string();
                changed = true;
            }
            const std::string filter = ToLower(assetFilter_);
            for (const std::string& candidate : assetsByCategory_[category]) {
                if (!filter.empty() && ToLower(candidate).find(filter) == std::string::npos) {
                    continue;
                }
                if (ImGui::Selectable(candidate.c_str(), candidate == text)) {
                    value = candidate;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
    std::array<char, 512> buffer {};
    text.copy(buffer.data(), buffer.size() - 1);
    if (ImGui::InputText("##value", buffer.data(), buffer.size())) {
        value = std::string(buffer.data());
        return true;
    }
    return false;
}

/** @brief 配列を編集する（3つの数値=ベクトル、色=カラーピッカー、それ以外は要素の一覧と追加・削除） */
bool JsonConfigEditor::DrawArray(nlohmann::json& value, const std::string& path, const std::string& key, const std::string& label, int depth) {
    if (IsColor(value, key)) {
        BeginRow(label, path);
        float color[kColorSize];
        for (size_t i = 0; i < kColorSize; ++i) {
            color[i] = value[i].get<float>();
        }
        if (ImGui::ColorEdit4("##value", color, ImGuiColorEditFlags_Float)) {
            value = nlohmann::json::array({ color[0], color[1], color[2], color[3] });
            return true;
        }
        return false;
    }
    if (IsNumberArray(value, kVector3Size)) {
        BeginRow(label, path);
        float vector[kVector3Size];
        for (size_t i = 0; i < kVector3Size; ++i) {
            vector[i] = value[i].get<float>();
        }
        if (ImGui::DragFloat3("##value", vector, kMinDragSpeed * kVectorDragSpeedScale, 0.0f, 0.0f, "%.4g")) {
            value = nlohmann::json::array({ vector[0], vector[1], vector[2] });
            return true;
        }
        return false;
    }

    const std::string caption = label + " (" + std::to_string(value.size()) + ")##" + path;
    const bool opened = depth <= 1 ? ImGui::CollapsingHeader(caption.c_str()) : ImGui::TreeNode(caption.c_str());
    if (!opened) {
        return false;
    }
    bool changed = false;
    int removeIndex = -1;
    for (size_t i = 0; i < value.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        nlohmann::json& element = value[i];
        if (element.is_object() || element.is_array()) {
            // 要素の中身は配列の場所と同じ名前の表で引く（例: 各武器の damage は "damage"）
            if (ImGui::TreeNode("##element", "%s", ElementCaption(element, i).c_str())) {
                for (auto it = element.begin(); element.is_object() && it != element.end(); ++it) {
                    if (it.key().empty() || it.key()[0] == '_') {
                        continue;
                    }
                    ImGui::PushID(it.key().c_str());
                    changed |= DrawValue(it.value(), JoinPath(path, it.key()), it.key(), LabelFor(JoinPath(path, it.key()), it.key()), depth + 2);
                    ImGui::PopID();
                }
                if (element.is_array()) {
                    changed |= DrawArray(element, path, key, ElementCaption(element, i), depth + 2);
                }
                if (ImGui::SmallButton("この要素を消す")) {
                    removeIndex = static_cast<int>(i);
                }
                ImGui::TreePop();
            }
        } else {
            // 入力欄は右端まで伸びるので、消すボタンは左に置く
            if (ImGui::SmallButton("x")) {
                removeIndex = static_cast<int>(i);
            }
            ImGui::SameLine();
            changed |= DrawValue(element, path, key, ElementCaption(element, i), depth + 1);
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        value.erase(static_cast<size_t>(removeIndex));
        changed = true;
    }
    // 追加は最後の要素を複製する（形をそろえたまま増やせる）
    if (!value.empty() && ImGui::SmallButton("最後の要素を複製して追加")) {
        value.push_back(value.back());
        changed = true;
    }
    if (depth > 1) {
        ImGui::TreePop();
    }
    return changed;
}

/** @brief 項目名を左側に出し、続く編集部品を右側に置く準備をする */
void JsonConfigEditor::BeginRow(const std::string& label, const std::string& path) {
    const float labelWidth = ImGui::GetContentRegionAvail().x * kLabelWidthRatio;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    if (ImGui::IsItemHovered()) {
        const std::string help = HelpFor(path);
        ImGui::SetTooltip("%s%sキー: %s", help.c_str(), help.empty() ? "" : "\n", path.c_str());
    }
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

/** @brief 検索語に当てはまる項目を含むか（入れ子の中も調べる） */
bool JsonConfigEditor::MatchesFilter(const nlohmann::json& value, const std::string& path, const std::string& key) const {
    const std::string filter = ToLower(filter_);
    if (filter.empty()) {
        return true;
    }
    if (ToLower(path).find(filter) != std::string::npos || ToLower(LabelFor(path, key)).find(filter) != std::string::npos) {
        return true;
    }
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (MatchesFilter(it.value(), JoinPath(path, it.key()), it.key())) {
                return true;
            }
        }
    }
    if (value.is_array()) {
        for (const nlohmann::json& element : value) {
            if (element.is_object() && MatchesFilter(element, path, key)) {
                return true;
            }
        }
    }
    return false;
}

/** @brief 名前の表から表示名を引く（ファイルの表 → どのファイルでも使う表 → グループ名 → キーの順） */
std::string JsonConfigEditor::LabelFor(const std::string& path, const std::string& key) const {
    const auto file = labels_.find(currentFile_);
    if (file != labels_.end() && file->is_object()) {
        const auto entry = file->find(path);
        if (entry != file->end() && entry->is_object() && entry->contains("name")) {
            return entry->value("name", key);
        }
    }
    for (const char* table : { "_keys", "_sections" }) {
        const auto common = labels_.find(table);
        if (common == labels_.end() || !common->is_object()) {
            continue;
        }
        const auto entry = common->find(key);
        if (entry == common->end()) {
            continue;
        }
        if (entry->is_string()) {
            return entry->get<std::string>();
        }
        if (entry->is_object() && entry->contains("name")) {
            return entry->value("name", key);
        }
    }
    return key;
}

/** @brief 名前の表から説明を引く */
std::string JsonConfigEditor::HelpFor(const std::string& path) const {
    const auto file = labels_.find(currentFile_);
    if (file == labels_.end() || !file->is_object()) {
        return {};
    }
    const auto entry = file->find(path);
    return entry != file->end() && entry->is_object() ? entry->value("help", std::string()) : std::string();
}

/** @brief 名前の表から型（"int" / "float"）を引く */
std::string JsonConfigEditor::TypeFor(const std::string& path, const std::string& key) const {
    const auto file = labels_.find(currentFile_);
    if (file != labels_.end() && file->is_object()) {
        const auto entry = file->find(path);
        if (entry != file->end() && entry->is_object() && entry->contains("type")) {
            return entry->value("type", std::string());
        }
    }
    const auto common = labels_.find("_keys");
    if (common != labels_.end() && common->is_object()) {
        const auto entry = common->find(key);
        if (entry != common->end() && entry->is_object()) {
            return entry->value("type", std::string());
        }
    }
    return {};
}

} // namespace engine::graphics

#endif // USE_IMGUI
