/**
 * @file JsonConfigEditorLabels.cpp
 * @brief 設定JSONの項目の表示名・説明・型を名前の表から引く処理を実装するファイル
 */
#ifdef USE_IMGUI
#include "JsonConfigEditor.h"
#include <algorithm>
#include <cctype>
#include <iterator>

namespace engine::graphics {
namespace {
/** @brief キーの終わり方と、その時に出す単位 */
struct UnitBySuffix {
    const char* suffix;
    const char* unit;
};
constexpr UnitBySuffix kUnitsBySuffix[] = {
    { "seconds", "秒" },
    { "duration", "秒" },
    { "mult", "倍" },
    { "scale", "倍" },
    { "degrees", "度" },
};
/** @brief キーにこの言葉を含み、値が 0〜1 なら割合としてスライダーにする */
constexpr const char* kRatioWords[] = { "ratio", "alpha", "chance", "opacity" };

/** @brief 英字だけ小文字にした文字列 */
std::string Lowered(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower;
}

/** @brief 名前の表の項目（ファイルの表を先に、無ければどのファイルでも使う表から。どちらも無ければ nullptr） */
const nlohmann::json* FindLabelEntry(const nlohmann::json& labels, const std::string& file, const std::string& path, const std::string& key) {
    const auto fileTable = labels.find(file);
    if (fileTable != labels.end() && fileTable->is_object()) {
        const auto entry = fileTable->find(path);
        if (entry != fileTable->end() && entry->is_object()) {
            return &*entry;
        }
    }
    const auto common = labels.find("_keys");
    if (common != labels.end() && common->is_object()) {
        const auto entry = common->find(key);
        if (entry != common->end() && entry->is_object()) {
            return &*entry;
        }
    }
    return nullptr;
}
} // namespace

/** @brief 名前の表から単位を引く（無ければキーの終わり方から決める。例: ～Seconds は 秒） */
std::string JsonConfigEditor::UnitFor(const std::string& path, const std::string& key) const {
    const nlohmann::json* entry = FindLabelEntry(labels_, currentFile_, path, key);
    if (entry != nullptr && entry->contains("unit")) {
        return entry->value("unit", std::string());
    }
    const std::string lower = Lowered(key);
    for (const UnitBySuffix& rule : kUnitsBySuffix) {
        const std::string suffix = rule.suffix;
        if (lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return rule.unit;
        }
    }
    return {};
}

/** @brief 名前の表からスライダーの範囲を引く（無ければキーと値から 0〜1 の割合かを判断する） */
bool JsonConfigEditor::RangeFor(const std::string& path, const std::string& key, float value, float& outMin, float& outMax) const {
    // 名前の表に min と max の両方が書いてあればその範囲
    const nlohmann::json* entry = FindLabelEntry(labels_, currentFile_, path, key);
    if (entry != nullptr && entry->contains("min") && entry->contains("max")
        && (*entry)["min"].is_number() && (*entry)["max"].is_number()) {
        outMin = (*entry)["min"].get<float>();
        outMax = (*entry)["max"].get<float>();
        return outMin < outMax;
    }
    // 割合らしいキーで、値が 0〜1 に収まっていれば 0〜1 のスライダー
    if (value < 0.0f || value > 1.0f) {
        return false;
    }
    const std::string lower = Lowered(key);
    const bool ratio = std::any_of(std::begin(kRatioWords), std::end(kRatioWords), [&](const char* word) {
        return lower.find(word) != std::string::npos;
    });
    if (ratio) {
        outMin = 0.0f;
        outMax = 1.0f;
    }
    return ratio;
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
