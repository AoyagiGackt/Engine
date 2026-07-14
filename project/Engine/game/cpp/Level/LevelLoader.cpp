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

nlohmann::json WriteVec3(const Vector3& v)
{
    return nlohmann::json::array({ v.x, v.y, v.z });
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
        desc.name     = obj.value("name",    "");
        desc.parent   = obj.value("parent",  "");
        desc.type     = obj.value("type",    "static");
        desc.model    = obj.value("model",   "");
        desc.texture  = obj.value("texture", "");
        desc.position = ReadVec3(obj.value("position", nlohmann::json::array()));
        desc.rotation = ReadVec3(obj.value("rotation", nlohmann::json::array()));
        desc.scale    = ReadVec3(obj.value("scale", nlohmann::json::array()), { 1.0f, 1.0f, 1.0f });
        desc.lighting = obj.value("lighting", true);
        desc.solid    = obj.value("solid",    false);

        std::string ax = obj.value("axis", "x");
        desc.axis  = ax.empty() ? 'x' : static_cast<char>(std::tolower(static_cast<unsigned char>(ax[0])));
        desc.count = obj.value("count", 1);
        desc.step  = obj.value("step",  1.0f);

        data.objects.push_back(std::move(desc));
    }

    for (const auto& trg : j.value("triggers", nlohmann::json::array())) {
        TriggerDesc desc;
        desc.name     = trg.value("name",     "");
        desc.position = ReadVec3(trg.value("position", nlohmann::json::array()));
        desc.radius   = trg.value("radius",   2.0f);
        desc.flag     = trg.value("flag",     "");
        desc.value    = trg.value("value",    true);
        desc.once     = trg.value("once",     true);
        data.triggers.push_back(std::move(desc));
    }
    return data;
}

void LevelLoader::Save(const std::string& path, const LevelData& data)
{
    nlohmann::json j;
    j["playerSpawn"] = WriteVec3(data.playerSpawn);
    j["enemySpawn"]  = WriteVec3(data.enemySpawn);

    nlohmann::json objectsJson = nlohmann::json::array();
    for (const auto& desc : data.objects) {
        nlohmann::json oj;
        oj["name"]     = desc.name;
        oj["parent"]   = desc.parent;
        oj["type"]     = desc.type;
        oj["model"]    = desc.model;
        oj["texture"]  = desc.texture;
        oj["position"] = WriteVec3(desc.position);
        oj["rotation"] = WriteVec3(desc.rotation);
        oj["scale"]    = WriteVec3(desc.scale);
        oj["lighting"] = desc.lighting;
        oj["solid"]    = desc.solid;
        oj["axis"]     = std::string(1, desc.axis);
        oj["count"]    = desc.count;
        oj["step"]     = desc.step;
        objectsJson.push_back(std::move(oj));
    }
    j["objects"] = std::move(objectsJson);

    nlohmann::json triggersJson = nlohmann::json::array();
    for (const auto& desc : data.triggers) {
        nlohmann::json tj;
        tj["name"]     = desc.name;
        tj["position"] = WriteVec3(desc.position);
        tj["radius"]   = desc.radius;
        tj["flag"]     = desc.flag;
        tj["value"]    = desc.value;
        tj["once"]     = desc.once;
        triggersJson.push_back(std::move(tj));
    }
    j["triggers"] = std::move(triggersJson);

    JsonHelper::Save(path, j);
}

LevelSpawnResult LevelLoader::Spawn(const LevelData& data, ModelCommon* modelCommon)
{
    LevelSpawnResult result;
    std::unordered_map<std::string, Model*> modelCache;

    // 親チェーンをたどってワールド位置を求める（親のpositionを順に加算循環参照は深さ上限で打ち切り）
    std::unordered_map<std::string, const ObjectDesc*> byName;
    for (const auto& desc : data.objects) {
        if (!desc.name.empty()) { byName[desc.name] = &desc; }
    }
    auto worldPositionOf = [&](const ObjectDesc& desc) {
        Vector3 pos = desc.position;
        const ObjectDesc* cur = &desc;
        for (int guard = 0; guard < 16 && !cur->parent.empty(); ++guard) {
            auto it = byName.find(cur->parent);
            if (it == byName.end()) { break; }
            cur = it->second;
            pos = pos + cur->position;
        }
        return pos;
    };

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
        Vector3 basePos = worldPositionOf(desc);

        if (desc.type == "static") {
            spawnOne(model, desc, basePos);
        } else if (desc.type == "row") {
            for (int i = 0; i < desc.count; ++i) {
                Vector3 pos = basePos;
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
