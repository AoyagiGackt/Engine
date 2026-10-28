/**
 * @file SoundAssignEditor.cpp
 * @brief 音声の割り当て画面と変更箇所だけの JSON 保存
 */
#include "SoundAssignEditor.h"
#ifdef USE_IMGUI
#include "AudioBridge.h"
#include "GameSounds.h"
#include "JsonHelper.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <exception>
#include <utility>

namespace engine::game {
namespace {
using Json = nlohmann::json;
constexpr const char* kConfigPaths[] = {
    "Resources/Config/sounds.json", "Resources/Config/bgm.json",
    "Resources/Config/clear_finish.json", "Resources/Config/player_defeat.json",
    "Resources/Config/stage_liberation.json"
};
constexpr size_t kSoundsIndex = 0;
constexpr size_t kBgmIndex = 1;
constexpr size_t kEffectsBegin = 2;
constexpr float kMinVolume = 0.0f;
constexpr float kDefaultVolume = 1.0f;
constexpr float kFileWidth = 310.0f;
constexpr float kVolumeWidth = 130.0f;
constexpr int kSoundColumns = 5;
constexpr int kBgmColumns = 3;
constexpr int kEffectColumns = 3;
constexpr size_t kStyleCapacity = 128;
constexpr const char* kWeaponBases[] = { "meleeHit", "meleeFinisher" };

/** @brief 音声フォルダを再帰的に列挙する */
std::vector<std::string> FindFiles(const char* directory) {
    std::vector<std::string> files;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(directory,
        std::filesystem::directory_options::skip_permission_denied, error), end;
    while (!error && it != end) {
        if (it->is_regular_file(error)) {
            std::string extension = it->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (extension == ".mp3" || extension == ".wav") {
                const auto utf8 = it->path().generic_u8string();
                files.emplace_back(utf8.begin(), utf8.end());
            }
        }
        it.increment(error);
    }
    std::sort(files.begin(), files.end());
    return files;
}

/** @brief オブジェクト・配列をたどり演出の音声パスと武器の種類を集める */
void Collect(const Json& value, const Json::json_pointer& pointer,
    std::vector<std::string>& paths, std::set<std::string>& styles) {
    if (value.is_string()) {
        if (value.get_ref<const std::string&>().find("Resources/SE/") == 0) {
            paths.push_back(pointer.to_string());
        }
        return;
    }
    if (!value.is_structured()) {
        return;
    }
    for (const auto& item : value.items()) {
        if (item.key() == "hitStyle" && item.value().is_string()) {
            const auto style = item.value().get<std::string>();
            if (!style.empty()) {
                styles.insert(style);
            }
        }
        Collect(item.value(), pointer / item.key(), paths, styles);
    }
}

/** @brief 既知の場面を日本語にし、未知のキーはそのまま返す */
std::string SceneName(const std::string& key) {
    static const std::pair<const char*, const char*> kNames[] = {
        { "dodge", "回避" }, { "swing", "近接攻撃の各段を振った時" },
        { "shot", "銃を撃った時" }, { "finisherBeat", "フィニッシャーの斬撃1本ごと" },
        { "finisherRelease", "フィニッシャーの溜めた斬撃を解き放った時" },
        { "glassShatter", "画面がガラスのように割れた時" },
        { "meleeHit", "近接が当たった時" }, { "meleeFinisher", "締めの一撃が当たった時" },
        { "title", "タイトル" }, { "stage_select", "ステージセレクト" },
        { "stage", "ステージ本編" }, { "training", "トレーニング" },
        { "battle_test", "バトルテスト" }, { "game_over", "ゲームオーバー" },
        { "game_clear", "ゲームクリア" }
    };
    for (const auto& name : kNames) {
        if (key == name.first) {
            return name.second;
        }
    }
    for (const auto* base : kWeaponBases) {
        const std::string prefix = std::string(base) + "_";
        if (key.find(prefix) == 0) {
            return SceneName(base) + "（" + key.substr(prefix.size()) + "）";
        }
    }
    return key;
}

/** @brief コメントを先頭で折り返して表示する */
void DrawComment(const Json& root) {
    if (root.is_object() && root.contains("_comment") && root["_comment"].is_string()) {
        ImGui::TextWrapped("%s", root["_comment"].get_ref<const std::string&>().c_str());
    }
}

/** @brief 空文字・現在値・フォルダ内のファイルを選択する */
bool FileCombo(const char* id, std::string& path, const std::vector<std::string>& files) {
    bool changed = false;
    ImGui::SetNextItemWidth(kFileWidth);
    if (ImGui::BeginCombo(id, path.empty() ? "（鳴らさない）" : path.c_str())) {
        if (ImGui::Selectable("（鳴らさない）", path.empty())) {
            changed = !path.empty();
            path.clear();
        }
        if (!path.empty() && std::find(files.begin(), files.end(), path) == files.end()) {
            ImGui::Selectable(path.c_str(), true);
        }
        for (const auto& file : files) {
            if (ImGui::Selectable(file.c_str(), path == file)) {
                changed = path != file;
                path = file;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/** @brief 武器別の削除可能なキーか判定する */
bool IsWeaponKey(const std::string& key) {
    for (const auto* base : kWeaponBases) {
        const std::string prefix = std::string(base) + "_";
        if (key.find(prefix) == 0 && key.size() > prefix.size()) {
            return true;
        }
    }
    return false;
}

/** @brief 実在するファイルだけを既存の音声再生先で試聴する */
void Preview(const std::string& path, float volume, bool bgm) {
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(std::filesystem::path(std::u8string(path.begin(), path.end())), error)) {
        return;
    }
    if (bgm) {
        AudioBridge::GetInstance()->PlayBGM(path, true);
    } else {
        AudioBridge::GetInstance()->PlaySE(path, volume);
    }
}

/** @brief 最新の JSON に編集差分だけを適用し、保存結果を確認する */
bool SaveChanges(const char* path, const Json& original, const Json& edited) {
    Json current = JsonHelper::Load(path);
    if (!current.is_object() || current.empty()) {
        return false;
    }
    const Json changes = Json::diff(original, edited);
    // 同じ項目が外部で変わっていたら上書きせず、未保存のままにする。
    for (const auto& change : changes) {
        const Json::json_pointer pointer(change.at("path").get<std::string>());
        if (original.contains(pointer)) {
            if (!current.contains(pointer) || current.at(pointer) != original.at(pointer)) {
                return false;
            }
        } else if (current.contains(pointer)) {
            return false;
        }
    }
    current = current.patch(changes);
    JsonHelper::Save(path, current);
    return JsonHelper::Load(path) == current;
}
} // namespace

/** @brief 開いている間だけ音の割り当て画面を描く */
void SoundAssignEditor::Draw(bool* open) {
    if (!open || !*open) {
        return;
    }
    if (!ImGui::Begin("音の割り当て", open)) {
        ImGui::End();
        return;
    }
    if (!initialized_) {
        std::set<std::string> styles;
        std::set<std::string> unusedStyles;
        std::vector<std::string> unused;
        for (const auto* path : kConfigPaths) {
            original_.push_back(JsonHelper::Load(path));
            std::vector<std::string> paths;
            Collect(original_.back(), Json::json_pointer(), paths, unusedStyles);
            effectPaths_.push_back(std::move(paths));
        }
        Collect(JsonHelper::Load("Resources/Config/weapons.json"), Json::json_pointer(), unused, styles);
        styles_.assign(styles.begin(), styles.end());
        styleInput_.assign(kStyleCapacity, '\0');
        edited_ = original_;
        seFiles_ = FindFiles("Resources/SE");
        bgmFiles_ = FindFiles("Resources/bgm");
        initialized_ = true;
    }
    if (ImGui::Button("保存")) {
        bool success = true;
        for (size_t i = 0; i < edited_.size(); ++i) {
            if (original_[i] == edited_[i]) {
                continue;
            }
            try {
                if (!SaveChanges(kConfigPaths[i], original_[i], edited_[i])) {
                    status_ = std::string("保存失敗（書き込み失敗または外部変更）: ") + kConfigPaths[i];
                    success = false;
                    break;
                }
                original_[i] = edited_[i];
                if (i == kSoundsIndex) {
                    gameSounds::Reload();
                } else if (i == kBgmIndex) {
                    Audio::ReloadBGMConfig();
                }
            } catch (const std::exception& error) {
                status_ = std::string("保存失敗: ") + kConfigPaths[i] + " / " + error.what();
                success = false;
                break;
            }
        }
        if (success) {
            status_ = "保存しました";
        }
    }
    if (original_ != edited_) {
        ImGui::SameLine();
        ImGui::TextUnformatted("未保存");
    }
    ImGui::SameLine();
    if (ImGui::Button("ファイル一覧を更新")) {
        seFiles_ = FindFiles("Resources/SE");
        bgmFiles_ = FindFiles("Resources/bgm");
    }
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
    ImGui::TextWrapped("試聴は現在のシーンの Audio を使用します。BGM の試聴は再生中の曲を置き換えます。");
    if (ImGui::BeginTabBar("SoundAssignTabs")) {
        if (ImGui::BeginTabItem("効果音")) {
            auto& root = edited_[kSoundsIndex];
            DrawComment(root);
            std::string removeKey;
            if (!root.is_object() || root.empty()) {
                ImGui::TextUnformatted("sounds.json を読み込めませんでした。");
            } else if (ImGui::BeginTable("Sounds", kSoundColumns, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("場面");
                ImGui::TableSetupColumn("ファイル");
                ImGui::TableSetupColumn("音量");
                ImGui::TableSetupColumn("試聴");
                ImGui::TableSetupColumn("武器別");
                ImGui::TableHeadersRow();
                for (auto& item : root.items()) {
                    if (item.key() == "_comment") {
                        continue;
                    }
                    ImGui::PushID(item.key().c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", SceneName(item.key()).c_str());
                    auto& entry = item.value();
                    if (!entry.is_object()) {
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted("未対応の形式（保持します）");
                        ImGui::PopID();
                        continue;
                    }
                    std::string path = entry.contains("path") && entry["path"].is_string()
                        ? entry["path"].get<std::string>() : std::string();
                    float volume = entry.contains("volume") && entry["volume"].is_number()
                        ? entry["volume"].get<float>() : kDefaultVolume;
                    ImGui::TableNextColumn();
                    if (FileCombo("##file", path, seFiles_)) {
                        entry["path"] = path;
                    }
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(kVolumeWidth);
                    if (ImGui::SliderFloat("##volume", &volume, kMinVolume, kDefaultVolume)) {
                        entry["volume"] = volume;
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("▶")) {
                        Preview(path, std::clamp(volume, kMinVolume, kDefaultVolume), false);
                    }
                    ImGui::TableNextColumn();
                    if (IsWeaponKey(item.key()) && ImGui::Button("削除")) {
                        removeKey = item.key();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (!removeKey.empty()) {
                root.erase(removeKey);
            }
            if (root.is_object() && !root.empty() && ImGui::CollapsingHeader("武器ごとの音を追加")) {
                if (ImGui::BeginCombo("hitStyle の候補", styleInput_.data())) {
                    for (const auto& style : styles_) {
                        if (ImGui::Selectable(style.c_str())) {
                            const size_t length = (std::min)(style.size(), styleInput_.size() - 1);
                            std::copy_n(style.data(), length, styleInput_.data());
                            styleInput_[length] = '\0';
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::InputText("hitStyle（手入力可）", styleInput_.data(), styleInput_.size());
                for (const auto* base : kWeaponBases) {
                    ImGui::PushID(base);
                    if (ImGui::Button((SceneName(base) + "を追加").c_str())) {
                        const std::string style(styleInput_.data());
                        const std::string key = std::string(base) + "_" + style;
                        if (!style.empty() && !root.contains(key)) {
                            root[key] = { { "path", "" }, { "volume", kDefaultVolume } };
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("BGM")) {
            auto& root = edited_[kBgmIndex];
            DrawComment(root);
            if (!root.is_object() || root.empty()) {
                ImGui::TextUnformatted("bgm.json を読み込めませんでした。");
            } else if (ImGui::BeginTable("Bgm", kBgmColumns, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("場面");
                ImGui::TableSetupColumn("ファイル");
                ImGui::TableSetupColumn("試聴");
                ImGui::TableHeadersRow();
                for (auto& item : root.items()) {
                    if (item.key() == "_comment" || !item.value().is_string()) {
                        continue;
                    }
                    ImGui::PushID(item.key().c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextWrapped("%s", SceneName(item.key()).c_str());
                    ImGui::TableNextColumn();
                    std::string path = item.value().get<std::string>();
                    if (FileCombo("##file", path, bgmFiles_)) {
                        item.value() = path;
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("▶ 再生")) {
                        Preview(path, kDefaultVolume, true);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("■ 停止")) {
                        AudioBridge::GetInstance()->StopBGM();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("演出ごとの音")) {
            ImGui::TextWrapped("演出ごとの音は保存後、次回起動時に反映されます。");
            for (size_t i = kEffectsBegin; i < edited_.size(); ++i) {
                ImGui::PushID(kConfigPaths[i]);
                if (ImGui::CollapsingHeader(kConfigPaths[i])) {
                    DrawComment(edited_[i]);
                    if (effectPaths_[i].empty()) {
                        ImGui::TextUnformatted("Resources/SE/ で始まる音声項目がありません（未読込の場合も含みます）。");
                    } else if (ImGui::BeginTable("Effects", kEffectColumns, ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("JSON 内の場所");
                        ImGui::TableSetupColumn("ファイル");
                        ImGui::TableSetupColumn("試聴");
                        ImGui::TableHeadersRow();
                        for (const auto& pointer : effectPaths_[i]) {
                            auto& value = edited_[i].at(Json::json_pointer(pointer));
                            std::string path = value.get<std::string>();
                            ImGui::PushID(pointer.c_str());
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextWrapped("%s", pointer.c_str());
                            ImGui::TableNextColumn();
                            if (FileCombo("##file", path, seFiles_)) {
                                value = path;
                            }
                            ImGui::TableNextColumn();
                            if (ImGui::Button("▶")) {
                                Preview(path, kDefaultVolume, false);
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
} // namespace engine::game
#else
namespace engine::game {
/** @brief ImGui を使わない構成では何もしない */
void SoundAssignEditor::Draw(bool* open) {
    (void)open;
}
} // namespace engine::game
#endif
