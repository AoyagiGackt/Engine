/**
 * @file JsonConfigEditorLabels.cpp
 * @brief 設定JSONの項目の表示名・説明・型を名前の表から引く処理を実装するファイル
 */
#ifdef USE_IMGUI
#include "JsonConfigEditor.h"

namespace engine::graphics {

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
