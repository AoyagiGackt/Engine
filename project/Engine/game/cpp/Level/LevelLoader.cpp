/**
 * @file LevelLoader.cpp
 * @brief レベルファイル（JSON）の読み込み・保存と、配置物の実体の生成の実装
 */
#include "LevelLoader.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_map>

using namespace engine::graphics;

namespace engine::game {

namespace {

    constexpr float kMinCheckpointRadius = 0.1f;
    constexpr char kDefaultAxis = 'x';

    // 1つの値で済む項目の対応表（省略された項目は各構造体の既定値のまま）。
    // 名前で書く種類（type / motionMode / motionEase / textSpace）と軸の文字（axis）は個別に読み書きする
    constexpr JsonField<ObjectDesc> kObjectFields[] = {
        { nullptr, "enabled", &ObjectDesc::enabled },
        { nullptr, "name", &ObjectDesc::name },
        { nullptr, "parent", &ObjectDesc::parent },
        { nullptr, "kind", &ObjectDesc::kind },
        { nullptr, "model", &ObjectDesc::model },
        { nullptr, "texture", &ObjectDesc::texture },
        { nullptr, "position", &ObjectDesc::position },
        { nullptr, "rotation", &ObjectDesc::rotation },
        { nullptr, "scale", &ObjectDesc::scale },
        { nullptr, "lighting", &ObjectDesc::lighting },
        { nullptr, "solid", &ObjectDesc::solid },
        { nullptr, "activationFlag", &ObjectDesc::activationFlag },
        { nullptr, "activeWhenFlag", &ObjectDesc::activeWhenFlag },
        { nullptr, "activationDelay", &ObjectDesc::activationDelay },
        { nullptr, "enemyGroup", &ObjectDesc::enemyGroup },
        { nullptr, "conditionType", &ObjectDesc::conditionType },
        { nullptr, "conditionSeconds", &ObjectDesc::conditionSeconds },
        { nullptr, "gimmickMotion", &ObjectDesc::gimmickMotion },
        { nullptr, "motionAmount", &ObjectDesc::motionAmount },
        { nullptr, "motionSpeed", &ObjectDesc::motionSpeed },
        { nullptr, "motionAxis", &ObjectDesc::motionAxis },
        { nullptr, "motionRotation", &ObjectDesc::motionRotation },
        { nullptr, "cameraBlendSeconds", &ObjectDesc::cameraBlendSeconds },
        { nullptr, "cameraHoldSeconds", &ObjectDesc::cameraHoldSeconds },
        { nullptr, "spawnType", &ObjectDesc::spawnType },
        { nullptr, "patrolRoute", &ObjectDesc::patrolRoute },
        { nullptr, "routeOrder", &ObjectDesc::routeOrder },
        { nullptr, "patrolSpeed", &ObjectDesc::patrolSpeed },
        { nullptr, "meshCollider", &ObjectDesc::meshCollider },
        { nullptr, "weaponType", &ObjectDesc::weaponType },
        { nullptr, "isStageBoss", &ObjectDesc::isStageBoss },
        { nullptr, "enemyModel", &ObjectDesc::enemyModel },
        { nullptr, "pickupRadius", &ObjectDesc::pickupRadius },
        { nullptr, "pickupGaugeAmount", &ObjectDesc::pickupGaugeAmount },
        { nullptr, "pickupColor", &ObjectDesc::pickupColor },
        { nullptr, "breakableHp", &ObjectDesc::breakableHp },
        { nullptr, "breakableRadius", &ObjectDesc::breakableRadius },
        { nullptr, "breakablePlayerDamage", &ObjectDesc::breakablePlayerDamage },
        { nullptr, "breakableEnemyDamage", &ObjectDesc::breakableEnemyDamage },
        { nullptr, "breakableWeapon", &ObjectDesc::breakableWeapon },
        { nullptr, "breakableColor", &ObjectDesc::breakableColor },
        { nullptr, "count", &ObjectDesc::count },
        { nullptr, "step", &ObjectDesc::step },
        { nullptr, "text", &ObjectDesc::text },
        { nullptr, "textColor", &ObjectDesc::textColor },
        { nullptr, "textBold", &ObjectDesc::textBold },
        { nullptr, "textShadow", &ObjectDesc::textShadow },
        { nullptr, "textScale", &ObjectDesc::textScale },
    };

    constexpr JsonField<TriggerDesc> kTriggerFields[] = {
        { nullptr, "name", &TriggerDesc::name },
        { nullptr, "position", &TriggerDesc::position },
        { nullptr, "radius", &TriggerDesc::radius },
        { nullptr, "flag", &TriggerDesc::flag },
        { nullptr, "value", &TriggerDesc::value },
        { nullptr, "once", &TriggerDesc::once },
        { nullptr, "spawnsWaterSplash", &TriggerDesc::spawnsWaterSplash },
    };

    constexpr JsonField<CheckpointDesc> kCheckpointFields[] = {
        { nullptr, "name", &CheckpointDesc::name },
        { nullptr, "position", &CheckpointDesc::position },
        { nullptr, "activationRadius", &CheckpointDesc::activationRadius },
    };

    constexpr JsonField<FlagGraphBinding> kFlagGraphFields[] = {
        { nullptr, "flag", &FlagGraphBinding::flag },
        { nullptr, "graphPath", &FlagGraphBinding::graphPath },
    };

    constexpr JsonField<LevelData> kLevelFields[] = {
        { nullptr, "playerSpawn", &LevelData::playerSpawn },
        { nullptr, "enemySpawn", &LevelData::enemySpawn },
        { nullptr, "graphPath", &LevelData::graphPath },
    };

    /** @brief JSON配列の各要素を対応表で読み、読んだ後に extra で個別の項目を足す */
    template <class Desc, size_t N, class Extra>
    std::vector<Desc> ReadList(const nlohmann::json& list, const JsonField<Desc> (&fields)[N], const Extra& extra)
    {
        std::vector<Desc> result;
        if (!list.is_array())
        {
            return result;
        }
        for (const nlohmann::json& item : list)
        {
            Desc desc;
            ReadJsonFields(item, fields, desc);
            extra(item, desc);
            result.push_back(std::move(desc));
        }
        return result;
    }

    /** @brief 一覧の各要素を対応表で書き出し、extra で個別の項目を足したJSON配列を返す */
    template <class Desc, size_t N, class Extra>
    nlohmann::json WriteList(const std::vector<Desc>& list, const JsonField<Desc> (&fields)[N], const Extra& extra)
    {
        nlohmann::json result = nlohmann::json::array();
        for (const Desc& desc : list)
        {
            nlohmann::json item = nlohmann::json::object();
            WriteJsonFields(item, fields, desc);
            extra(item, desc);
            result.push_back(std::move(item));
        }
        return result;
    }

    /** @brief 追加で読み書きする項目が無い時に使う何もしない処理 */
    template <class Item, class Desc>
    void NoExtra(const Item&, const Desc&)
    {
    }


}

/** @brief レベルファイルを読み込む（項目が無いものは既定値のまま） */
LevelData LevelLoader::Load(const std::string& path)
{
    auto j = JsonHelper::Load(path);
    LevelData data;

    // ファイルが存在しない/壊れている場合、JsonHelper::Load()はnull型のjsonを返す
    // （空オブジェクト{}ではない）。.value()/operator[]はobject型以外に呼ぶとtype_errorを投げるため、
    // ここで空のLevelDataとして扱う（新規レベルを起動時に自動作成するようなシーンで必須のガード）
    if (!j.is_object())
    {
        return data;
    }

    ReadJsonFields(j, kLevelFields, data);
    data.objects = ReadList(j.value("objects", nlohmann::json::array()), kObjectFields,
        [](const nlohmann::json& item, ObjectDesc& desc) {
            desc.type = ParseLevelEnum(item.value("type", std::string()), kPlacementTypeNames, desc.type);
            desc.motionMode = ParseLevelEnum(item.value("motionMode", std::string()), kMotionModeNames, desc.motionMode);
            desc.motionEase = ParseLevelEnum(item.value("motionEase", std::string()), kMotionEaseNames, desc.motionEase);
            desc.textSpace = ParseLevelEnum(item.value("textSpace", std::string()), kTextSpaceNames, desc.textSpace);
            const std::string axis = item.value("axis", std::string(1, kDefaultAxis));
            desc.axis = axis.empty() ? kDefaultAxis : static_cast<char>(std::tolower(static_cast<unsigned char>(axis[0])));
        });
    data.triggers = ReadList(j.value("triggers", nlohmann::json::array()), kTriggerFields, NoExtra<nlohmann::json, TriggerDesc>);
    data.checkpoints = ReadList(j.value("checkpoints", nlohmann::json::array()), kCheckpointFields,
        [](const nlohmann::json&, CheckpointDesc& desc) {
            desc.activationRadius = (std::max)(desc.activationRadius, kMinCheckpointRadius);
        });
    data.flagGraphs = ReadList(j.value("flagGraphs", nlohmann::json::array()), kFlagGraphFields, NoExtra<nlohmann::json, FlagGraphBinding>);
    return data;
}

/** @brief レベルデータをファイルへ保存する */
void LevelLoader::Save(const std::string& path, const LevelData& data)
{
    nlohmann::json j = nlohmann::json::object();
    WriteJsonFields(j, kLevelFields, data);
    j["objects"] = WriteList(data.objects, kObjectFields, [](nlohmann::json& item, const ObjectDesc& desc) {
        item["type"] = LevelEnumName(desc.type, kPlacementTypeNames);
        item["motionMode"] = LevelEnumName(desc.motionMode, kMotionModeNames);
        item["motionEase"] = LevelEnumName(desc.motionEase, kMotionEaseNames);
        item["textSpace"] = LevelEnumName(desc.textSpace, kTextSpaceNames);
        item["axis"] = std::string(1, desc.axis);
    });
    j["triggers"] = WriteList(data.triggers, kTriggerFields, NoExtra<nlohmann::json, TriggerDesc>);
    j["checkpoints"] = WriteList(data.checkpoints, kCheckpointFields, NoExtra<nlohmann::json, CheckpointDesc>);
    j["flagGraphs"] = WriteList(data.flagGraphs, kFlagGraphFields, NoExtra<nlohmann::json, FlagGraphBinding>);

    JsonHelper::Save(path, j);
}

/** @brief 配置情報の回転・拡縮・ライティングで、見た目のオブジェクトを1つ作る */
std::unique_ptr<Object3d> LevelLoader::CreatePlacedObject(ModelCommon* modelCommon, Model* model,
    const ObjectDesc& desc, const Vector3& position)
{
    auto obj = std::make_unique<Object3d>();
    obj->Initialize(modelCommon);
    obj->SetModel(model);
    obj->SetPosition(position);
    obj->SetRotation(desc.rotation);
    obj->SetScale(desc.scale);
    obj->SetEnableLighting(desc.lighting);
    obj->Update();
    return obj;
}

/** @brief レベルデータから配置物の実体を生成する */
LevelSpawnResult LevelLoader::Spawn(const LevelData& data, ModelCommon* modelCommon)
{
    // 同じモデルとテクスチャの組は1回だけ読み込んで使い回す
    LevelSpawnResult result;
    std::unordered_map<std::string, Model*> modelCache;

    // 親チェーンをたどってワールド位置を求める（エディタと同じく親のZ回転も反映し、循環参照は深さ上限で打ち切る）
    std::unordered_map<std::string, const ObjectDesc*> byName;
    for (const auto& desc : data.objects)
    {
        if (!desc.name.empty())
        {
            byName[desc.name] = &desc;
        }
    }
    auto findParent = [&](const std::string& name) -> const ObjectDesc* {
        auto it = byName.find(name);
        return it != byName.end() ? it->second : nullptr;
    };
    auto worldPositionOf = [&](const ObjectDesc& desc) {
        return ResolveWorldPosition(desc, findParent);
    };

    // モデルを読み込み、所有権は result に持たせて生ポインタをキャッシュする
    auto getModel = [&](const std::string& modelPath, const std::string& texPath) -> Model* {
        std::string key = modelPath + '|' + texPath;
        auto it = modelCache.find(key);
        if (it != modelCache.end())
        {
            return it->second;
        }

        auto model = std::make_unique<Model>();
        model->Initialize(modelCommon, modelPath, texPath);
        Model* ptr = model.get();
        result.models.push_back(std::move(model));
        modelCache[key] = ptr;
        return ptr;
    };

    // 配置情報どおりの位置・回転・拡縮で見た目のオブジェクトを1つ作る
    auto spawnOne = [&](Model* model, const ObjectDesc& desc, const Vector3& pos) {
        result.objects.push_back(CreatePlacedObject(modelCommon, model, desc, pos));
    };

    for (const auto& desc : data.objects)
    {
        // enemy系はStageEditor側が実体を生成する担当なので、この単純な見た目専用スポナーでは無視する
        if (!desc.enabled || !ObjectKind::Of(desc.kind).IsVisual() || desc.model.empty())
        {
            continue;
        }
        Model* model = getModel(desc.model, desc.texture);
        Vector3 basePos = worldPositionOf(desc);

        // 1個置くか、axis方向へ count 個並べる
        const int copies = PlacementCopyCount(desc);
        for (int i = 0; i < copies; ++i)
        {
            const Vector3 pos = PlacementInstancePosition(desc, basePos, i);
            spawnOne(model, desc, pos);
        }
    }
    return result;
}

}
