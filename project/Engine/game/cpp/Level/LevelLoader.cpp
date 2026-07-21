/**
 * @file LevelLoader.cpp
 * @brief LevelLoaderが担当する処理を実装するファイル
 */
#include "LevelLoader.h"
#include "JsonHelper.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>

using namespace engine::graphics;

namespace engine::game {

namespace {

    Vector3 ReadVec3(const nlohmann::json& arr, Vector3 def = { })
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

    // ファイルが存在しない/壊れている場合、JsonHelper::Load()はnull型のjsonを返す
    // （空オブジェクト{}ではない）。.value()/operator[]はobject型以外に呼ぶとtype_errorを投げるため、
    // ここで空のLevelDataとして扱う（新規レベルを起動時に自動作成するようなシーンで必須のガード）
    if (!j.is_object()) {
        return data;
    }

    if (j.contains("playerSpawn")) {
        data.playerSpawn = ReadVec3(j["playerSpawn"], data.playerSpawn);
    }
    if (j.contains("enemySpawn")) {
        data.enemySpawn = ReadVec3(j["enemySpawn"], data.enemySpawn);
    }

    for (const auto& obj : j.value("objects", nlohmann::json::array())) {
        ObjectDesc desc;
        desc.enabled = obj.value("enabled", true);
        desc.name = obj.value("name", "");
        desc.parent = obj.value("parent", "");
        desc.type = obj.value("type", "static");
        desc.kind = obj.value("kind", "prop");
        desc.model = obj.value("model", "");
        desc.texture = obj.value("texture", "");
        desc.position = ReadVec3(obj.value("position", nlohmann::json::array()));
        desc.rotation = ReadVec3(obj.value("rotation", nlohmann::json::array()));
        desc.scale = ReadVec3(obj.value("scale", nlohmann::json::array()), { 1.0f, 1.0f, 1.0f });
        desc.lighting = obj.value("lighting", true);
        desc.solid = obj.value("solid", false);
        desc.activationFlag = obj.value("activationFlag", "");
        desc.activeWhenFlag = obj.value("activeWhenFlag", true);
        desc.activationDelay = obj.value("activationDelay", 0.0f);
        desc.enemyGroup = obj.value("enemyGroup", "");
        desc.conditionType = obj.value("conditionType", "manual");
        desc.conditionSeconds = obj.value("conditionSeconds", 0.0f);
        desc.gimmickMotion = obj.value("gimmickMotion", "none");
        desc.motionAmount = obj.value("motionAmount", 3.0f);
        desc.motionSpeed = obj.value("motionSpeed", 1.0f);
        desc.cameraBlendSeconds = obj.value("cameraBlendSeconds", 0.5f);
        desc.cameraHoldSeconds = obj.value("cameraHoldSeconds", 2.0f);
        desc.spawnType = obj.value("spawnType", "basic");
        desc.patrolRoute = obj.value("patrolRoute", "");
        desc.routeOrder = obj.value("routeOrder", 0);
        desc.patrolSpeed = obj.value("patrolSpeed", 1.5f);
        desc.meshCollider = obj.value("meshCollider", false);

        std::string ax = obj.value("axis", "x");
        desc.axis = ax.empty() ? 'x' : static_cast<char>(std::tolower(static_cast<unsigned char>(ax[0])));
        desc.count = obj.value("count", 1);
        desc.step = obj.value("step", 1.0f);

        data.objects.push_back(std::move(desc));
    }

    for (const auto& trg : j.value("triggers", nlohmann::json::array())) {
        TriggerDesc desc;
        desc.name = trg.value("name", "");
        desc.position = ReadVec3(trg.value("position", nlohmann::json::array()));
        desc.radius = trg.value("radius", 2.0f);
        desc.flag = trg.value("flag", "");
        desc.value = trg.value("value", true);
        desc.once = trg.value("once", true);
        data.triggers.push_back(std::move(desc));
    }

    for (const auto& checkpoint : j.value("checkpoints", nlohmann::json::array())) {
        CheckpointDesc desc;
        desc.name = checkpoint.value("name", "");
        desc.position = ReadVec3(checkpoint.value("position", nlohmann::json::array()));
        desc.activationRadius = (std::max)(checkpoint.value("activationRadius", 2.0f), 0.1f);
        data.checkpoints.push_back(std::move(desc));
    }
    return data;
}

void LevelLoader::Save(const std::string& path, const LevelData& data)
{
    nlohmann::json j;
    j["playerSpawn"] = WriteVec3(data.playerSpawn);
    j["enemySpawn"] = WriteVec3(data.enemySpawn);

    nlohmann::json objectsJson = nlohmann::json::array();
    for (const auto& desc : data.objects) {
        nlohmann::json oj;
        oj["enabled"] = desc.enabled;
        oj["name"] = desc.name;
        oj["parent"] = desc.parent;
        oj["type"] = desc.type;
        oj["kind"] = desc.kind;
        oj["model"] = desc.model;
        oj["texture"] = desc.texture;
        oj["position"] = WriteVec3(desc.position);
        oj["rotation"] = WriteVec3(desc.rotation);
        oj["scale"] = WriteVec3(desc.scale);
        oj["lighting"] = desc.lighting;
        oj["solid"] = desc.solid;
        oj["activationFlag"] = desc.activationFlag;
        oj["activeWhenFlag"] = desc.activeWhenFlag;
        oj["activationDelay"] = desc.activationDelay;
        oj["enemyGroup"] = desc.enemyGroup;
        oj["conditionType"] = desc.conditionType;
        oj["conditionSeconds"] = desc.conditionSeconds;
        oj["gimmickMotion"] = desc.gimmickMotion;
        oj["motionAmount"] = desc.motionAmount;
        oj["motionSpeed"] = desc.motionSpeed;
        oj["cameraBlendSeconds"] = desc.cameraBlendSeconds;
        oj["cameraHoldSeconds"] = desc.cameraHoldSeconds;
        oj["spawnType"] = desc.spawnType;
        oj["patrolRoute"] = desc.patrolRoute;
        oj["routeOrder"] = desc.routeOrder;
        oj["patrolSpeed"] = desc.patrolSpeed;
        oj["meshCollider"] = desc.meshCollider;
        oj["axis"] = std::string(1, desc.axis);
        oj["count"] = desc.count;
        oj["step"] = desc.step;
        objectsJson.push_back(std::move(oj));
    }
    j["objects"] = std::move(objectsJson);

    nlohmann::json triggersJson = nlohmann::json::array();
    for (const auto& desc : data.triggers) {
        nlohmann::json tj;
        tj["name"] = desc.name;
        tj["position"] = WriteVec3(desc.position);
        tj["radius"] = desc.radius;
        tj["flag"] = desc.flag;
        tj["value"] = desc.value;
        tj["once"] = desc.once;
        triggersJson.push_back(std::move(tj));
    }
    j["triggers"] = std::move(triggersJson);

    nlohmann::json checkpointsJson = nlohmann::json::array();
    for (const auto& desc : data.checkpoints) {
        nlohmann::json checkpointJson;
        checkpointJson["name"] = desc.name;
        checkpointJson["position"] = WriteVec3(desc.position);
        checkpointJson["activationRadius"] = desc.activationRadius;
        checkpointsJson.push_back(std::move(checkpointJson));
    }
    j["checkpoints"] = std::move(checkpointsJson);

    JsonHelper::Save(path, j);
}

LevelSpawnResult LevelLoader::Spawn(const LevelData& data, ModelCommon* modelCommon)
{
    LevelSpawnResult result;
    std::unordered_map<std::string, Model*> modelCache;

    // 親チェーンをたどってワールド位置を求める（親のpositionを順に加算循環参照は深さ上限で打ち切り）
    std::unordered_map<std::string, const ObjectDesc*> byName;
    for (const auto& desc : data.objects) {
        if (!desc.name.empty()) {
            byName[desc.name] = &desc;
        }
    }
    auto worldPositionOf = [&](const ObjectDesc& desc) {
        Vector3 pos = desc.position;
        const ObjectDesc* cur = &desc;
        for (int guard = 0; guard < 16 && !cur->parent.empty(); ++guard) {
            auto it = byName.find(cur->parent);
            if (it == byName.end()) {
                break;
            }
            cur = it->second;
            pos = pos + cur->position;
        }
        return pos;
    };

    auto getModel = [&](const std::string& modelPath, const std::string& texPath) -> Model* {
        std::string key = modelPath + '|' + texPath;
        auto it = modelCache.find(key);
        if (it != modelCache.end()) {
            return it->second;
        }

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
        // enemy系はStageEditor側が実体を生成する担当なので、この単純な見た目専用スポナーでは無視する
        if (!desc.enabled || (desc.kind != "prop" && desc.kind != "gimmick" && desc.kind != "terrain") || desc.model.empty()) {
            continue;
        }
        Model* model = getModel(desc.model, desc.texture);
        Vector3 basePos = worldPositionOf(desc);

        if (desc.type == "static") {
            spawnOne(model, desc, basePos);
        } else if (desc.type == "row") {
            for (int i = 0; i < desc.count; ++i) {
                Vector3 pos = basePos;
                float offset = desc.step * static_cast<float>(i);
                if (desc.axis == 'y') {
                    pos.y += offset;
                } else if (desc.axis == 'z') {
                    pos.z += offset;
                } else {
                    pos.x += offset;
                }
                spawnOne(model, desc, pos);
            }
        }
    }
    return result;
}

}
