/**
 * @file EnemyTypeLibrary.cpp
 * @brief 敵の種類の一覧の読み書きと、配置物への反映を実装するファイル
 */
#include "EnemyTypeLibrary.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "LevelLoader.h"
#include "Logger.h"

namespace engine::game {
namespace {
constexpr const char* kEnemyTypesPath = "Resources/Config/enemy_types.json";
constexpr const char* kComment = "エディタの「敵を作る」で作った敵の種類。behavior=動き方（basic=歩く flying=飛ぶ healer=回復役） "
                                 "look=見た目（enemy_params.json の visuals の名前。空なら騎士） weapon=持たせて奪わせる武器（空なら持たない） "
                                 "hpScale / damageScale / speedScale = HP・攻撃力・近づく速さの倍率";

constexpr JsonField<EnemyTypeDef> kTypeFields[] = {
    { nullptr, "id", &EnemyTypeDef::id },
    { nullptr, "label", &EnemyTypeDef::label },
    { nullptr, "behavior", &EnemyTypeDef::behavior },
    { nullptr, "look", &EnemyTypeDef::look },
    { nullptr, "weapon", &EnemyTypeDef::weapon },
    { nullptr, "hpScale", &EnemyTypeDef::hpScale },
    { nullptr, "damageScale", &EnemyTypeDef::damageScale },
    { nullptr, "speedScale", &EnemyTypeDef::speedScale },
};
} // namespace

EnemyTypeLibrary* EnemyTypeLibrary::GetInstance() {
    static EnemyTypeLibrary sInstance;
    return &sInstance;
}

EnemyTypeLibrary::EnemyTypeLibrary() {
    Reload();
}

/** @brief ファイルから読み直す */
void EnemyTypeLibrary::Reload() {
    types_.clear();
    const nlohmann::json root = JsonHelper::Load(kEnemyTypesPath);
    const auto types = root.is_object() ? root.find("types") : root.end();
    if (!root.is_object() || types == root.end() || !types->is_array()) {
        return;
    }
    for (const nlohmann::json& value : *types) {
        EnemyTypeDef type = ReadJsonFields(value, kTypeFields);
        if (type.id.empty()) {
            Logger::LogWarning(std::string("EnemyTypeLibrary: id の無い敵の種類を飛ばしました（") + kEnemyTypesPath + "）");
            continue;
        }
        types_.push_back(std::move(type));
    }
}

/** @brief 今の一覧をファイルへ書き出す */
void EnemyTypeLibrary::Save() const {
    nlohmann::json root = nlohmann::json::object();
    root["_comment"] = kComment;
    root["types"] = nlohmann::json::array();
    for (const EnemyTypeDef& type : types_) {
        nlohmann::json value;
        WriteJsonFields(value, kTypeFields, type);
        root["types"].push_back(std::move(value));
    }
    JsonHelper::Save(kEnemyTypesPath, root);
}

/** @brief 名前から種類を探す（無ければ nullptr） */
const EnemyTypeDef* EnemyTypeLibrary::Find(const std::string& id) const {
    for (const EnemyTypeDef& type : types_) {
        if (type.id == id) {
            return &type;
        }
    }
    return nullptr;
}

/** @brief 種類の値を配置物へ写す（動き方・見た目・武器・強さの倍率と、種類の名前） */
void EnemyTypeLibrary::ApplyTo(const EnemyTypeDef& type, ObjectDesc& desc) {
    desc.enemyType = type.id;
    desc.spawnType = type.behavior;
    desc.enemyModel = type.look;
    desc.weaponType = type.weapon;
    desc.enemyHpScale = type.hpScale;
    desc.enemyDamageScale = type.damageScale;
    desc.enemySpeedScale = type.speedScale;
}

} // namespace engine::game
