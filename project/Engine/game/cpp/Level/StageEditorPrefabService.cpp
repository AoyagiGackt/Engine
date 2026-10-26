/**
 * @file StageEditorPrefabService.cpp
 * @brief ステージエディタのプレハブ保存と読み込みを実装するファイル
 */
#include "StageEditorPrefabService.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace engine::game {

/** @brief 配置物を原点基準のプレハブとして保存し、保存先を返す */
std::string StageEditorPrefabService::Save(const std::string& name, const ObjectDesc& object) {
    std::filesystem::create_directories("Resources/Prefabs");

    ObjectDesc prefabObject = object;
    prefabObject.parent.clear();
    prefabObject.position = { };

    LevelData prefab;
    prefab.objects.push_back(std::move(prefabObject));
    const std::string path = MakePath(name);
    levelLoader::Save(path, prefab);
    return path;
}

/** @brief 名前に対応するプレハブ内の配置物を読み込む */
std::vector<ObjectDesc> StageEditorPrefabService::Load(const std::string& name) {
    return levelLoader::Load(MakePath(name)).objects;
}

/** @brief ファイル名に利用できる安全なプレハブ名へ変換する */
std::string StageEditorPrefabService::SanitizeName(const std::string& name) {
    std::string safeName = name;
    safeName.erase(std::remove_if(safeName.begin(), safeName.end(), [](unsigned char character) {
        return !(std::isalnum(character) || character == '_' || character == '-');
    }),
        safeName.end());
    return safeName.empty() ? "stage_part" : safeName;
}

/** @brief プレハブ名から保存先の相対パスを構築する */
std::string StageEditorPrefabService::MakePath(const std::string& name) {
    return "Resources/Prefabs/" + SanitizeName(name) + ".json";
}

} // namespace engine::game
