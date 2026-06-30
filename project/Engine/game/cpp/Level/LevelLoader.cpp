#include "LevelLoader.h"
#include "JsonHelper.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <cctype>
#include <unordered_map>

using namespace engine::graphics;

namespace engine::game {

namespace {

Vector3 ReadVec3(const nlohmann::json& arr, Vector3 def = {})
{
    if (arr.is_array() && arr.size() >= 3) {
        return { arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>() };
    }
    return def;
}

}

LevelData LevelLoader::Load(const std::string& path)
{
    auto j = JsonHelper::Load(path);
    LevelData data;

    if (j.contains("playerSpawn")) {
        data.playerSpawn = ReadVec3(j["playerSpawn"], data.playerSpawn);
    }
    if (j.contains("enemySpawn")) {
        data.enemySpawn  = ReadVec3(j["enemySpawn"],  data.enemySpawn);
    }

    for (const auto& obj : j.value("objects", nlohmann::json::array())) {
        ObjectDesc desc;
        desc.type     = obj.value("type",    "static");
        desc.model    = obj.value("model",   "");
        desc.texture  = obj.value("texture", "");
        desc.position = ReadVec3(obj.value("position", nlohmann::json::array()));
        desc.rotation = ReadVec3(obj.value("rotation", nlohmann::json::array()));
        desc.scale    = ReadVec3(obj.value("scale", nlohmann::json::array()), { 1.0f, 1.0f, 1.0f });
        desc.lighting = obj.value("lighting", true);

        std::string ax = obj.value("axis", "x");
        desc.axis  = ax.empty() ? 'x' : static_cast<char>(std::tolower(static_cast<unsigned char>(ax[0])));
        desc.count = obj.value("count", 1);
        desc.step  = obj.value("step",  1.0f);

        data.objects.push_back(std::move(desc));
    }
    return data;
}

LevelSpawnResult LevelLoader::Spawn(const LevelData& data, ModelCommon* modelCommon)
{
    LevelSpawnResult result;
    std::unordered_map<std::string, Model*> modelCache;

    auto getModel = [&](const std::string& modelPath, const std::string& texPath) -> Model* {
        std::string key = modelPath + '|' + texPath;
        auto it = modelCache.find(key);
        if (it != modelCache.end()) { return it->second; }

        auto model = std::make_unique<Model>();
        model->Initialize(modelCommon, modelPath, texPath);
        Model* ptr = model.get();
        result.models.push_back(std::move(model));
        modelCache[key] = ptr;
        return ptr;
    };

    auto spawnOne = [&](Model* model, const ObjectDesc& desc, const Vector3& pos) {
        auto obj = std::make_unique<Object3d>();
        obj->Initialize(modelCommon);
        obj->SetModel(model);
        obj->SetPosition(pos);
        obj->SetRotation(desc.rotation);
        obj->SetScale(desc.scale);
        obj->SetEnableLighting(desc.lighting);
        obj->Update();
        result.objects.push_back(std::move(obj));
    };

    for (const auto& desc : data.objects) {
        if (desc.model.empty()) { continue; }
        Model* model = getModel(desc.model, desc.texture);

        if (desc.type == "static") {
            spawnOne(model, desc, desc.position);
        } else if (desc.type == "row") {
            for (int i = 0; i < desc.count; ++i) {
                Vector3 pos = desc.position;
                float offset = desc.step * static_cast<float>(i);
                if      (desc.axis == 'y') { pos.y += offset; }
                else if (desc.axis == 'z') { pos.z += offset; }
                else                       { pos.x += offset; }
                spawnOne(model, desc, pos);
            }
        }
    }
    return result;
}

}
