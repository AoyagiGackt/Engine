/**
 * @file StageEditorPrefabService.cpp
 * @brief ステージエディタのプレハブ保存と読み込みを実装するファイル
 */
#include "StageEditorPrefabService.h"
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace engine::game {

std::string StageEditorPrefabService::Save(const std::string& name, const ObjectDesc& object)
{
    std::filesystem::create_directories("Resources/Prefabs");

    ObjectDesc prefabObject = object;
    prefabObject.parent.clear();
    prefabObject.position = { };

    LevelData prefab;
    prefab.objects.push_back(std::move(prefabObject));
    const std::string path = MakePath(name);
    LevelLoader::Save(path, prefab);
    return path;
}

std::vector<ObjectDesc> StageEditorPrefabService::Load(const std::string& name)
{
    return LevelLoader::Load(MakePath(name)).objects;
}

std::string StageEditorPrefabService::SanitizeName(const std::string& name)
{
    std::string safeName = name;
    safeName.erase(std::remove_if(safeName.begin(), safeName.end(), [](unsigned char character) {
        return !(std::isalnum(character) || character == '_' || character == '-');
    }), safeName.end());
    return safeName.empty() ? "stage_part" : safeName;
}

std::string StageEditorPrefabService::MakePath(const std::string& name)
{
    return "Resources/Prefabs/" + SanitizeName(name) + ".json";
}

} // namespace engine::game
