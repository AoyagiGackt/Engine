/**
 * @file SaveData.cpp
 * @brief ローグライトのコンティニューデータ・通算記録の読み込み・保存とバージョン移行（SaveDataManager）の実装
 */
#include "SaveData.h"
#include "WeaponManager.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>
#include <limits>
#include <type_traits>

namespace engine::game {
namespace {
// セーブファイルの1つの値で済む項目（種類とスキル一覧は数値の変換が要るので個別に読み書きする）
constexpr JsonField<ContinueData> kContinueFields[] = {
    { "continue", "weapon_index", &ContinueData::weaponIndex },
    { "continue", "selected_weapon_slot", &ContinueData::selectedWeaponSlot },
    { "continue", "pending_weapon_index", &ContinueData::pendingWeaponIndex },
    { "continue", "ranged_weapon_index", &ContinueData::rangedWeaponIndex },
    { "continue", "valid", &ContinueData::valid },
    { "continue", "hp", &ContinueData::hp },
    { "continue", "max_hp", &ContinueData::maxHp },
    { "continue", "gold", &ContinueData::gold },
    { "continue", "score", &ContinueData::score },
    { "continue", "floor", &ContinueData::floor },
};

constexpr JsonField<SaveRecords> kRecordFields[] = {
    { "records", "best_floor", &SaveRecords::bestFloorReached },
    { "records", "total_runs", &SaveRecords::totalRuns },
    { "records", "total_clears", &SaveRecords::totalClears },
    { "records", "total_gold_earned", &SaveRecords::totalGoldEarned },
};

/** @brief 型と整数範囲を検証し、不正な値は既定値のまま残す */
template<class Value>
void ReadSaveValue(const nlohmann::json& value, Value& target, const std::string& name) {
    if constexpr (std::is_same_v<Value, bool>) {
        if (value.is_boolean()) {
            target = value.get<bool>();
            return;
        }
    } else {
        if (value.is_number_integer()) {
            if (value.is_number_unsigned()) {
                const auto number = value.get<uint64_t>();
                if (number <= static_cast<uint64_t>((std::numeric_limits<Value>::max)())) {
                    target = static_cast<Value>(number);
                    return;
                }
            } else {
                const auto number = value.get<int64_t>();
                if (number >= (std::numeric_limits<Value>::lowest)() && number <= (std::numeric_limits<Value>::max)()) {
                    target = static_cast<Value>(number);
                    return;
                }
            }
        }
    }
    Logger::LogWarning("セーブの型または範囲が不正です: " + name);
}

/** @brief 対応表の各項目を検証して読み込む */
template<class Data, size_t Count>
void ReadSaveFields(const nlohmann::json& root, const JsonField<Data> (&fields)[Count], Data& data) {
    for (const auto& field : fields) {
        const auto section = root.find(field.section);
        if (section == root.end()) {
            continue;
        }
        if (!section->is_object()) {
            Logger::LogWarning(std::string("セーブの節が不正です: ") + field.section);
            continue;
        }
        const auto value = section->find(field.key);
        if (value != section->end()) {
            std::visit([&](const auto& member) {
                using Value = std::decay_t<decltype(data.*member)>;
                if constexpr (std::is_integral_v<Value>) {
                    ReadSaveValue(*value, data.*member, field.key);
                }
            }, field.member);
        }
    }
}
/** @brief 型と値域を確認して、装備番号の配列を読み込む */
void ReadEquipmentArray(const nlohmann::json& section, const std::string& key, std::vector<int32_t>& target) {
    const auto values = section.find(key);
    if (values == section.end()) {
        return;
    }
    if (!values->is_array()) {
        Logger::LogWarning("セーブの装備配列が不正です: " + key);
        return;
    }
    for (const auto& value : *values) {
        int32_t number = -1;
        ReadSaveValue(value, number, key);
        target.push_back(number);
    }
}
} // namespace

/** @brief 共有インスタンスを取得する */
SaveDataManager* SaveDataManager::GetInstance() {
    static SaveDataManager sInstance;
    return &sInstance;
}

/** @brief save/save.json から読み込む（ファイルがなければデフォルト値のまま） */
void SaveDataManager::Load() {
    continue_ = ContinueData {};
    records_ = SaveRecords {};
    // セーブファイルが無い・壊れている場合は初期状態のままにする
    auto j = engine::JsonHelper::Load(kFilePath);
    if (!j.is_object()) {
        return;
    }

    // 節がオブジェクトでなければ読まずに既定値のままにし、理由をログに残す
    for (const char* section : { "continue", "records" }) {
        if (j.contains(section) && !j[section].is_object()) {
            Logger::LogWarning(std::string("セーブの節が不正です: ") + section);
        }
    }
    // 古い形式なら今の形式へ移し替えてから読む（新しい形式は警告だけ出して読める項目を読む）
    // 版番号が数値でなければ、最も古い形式として扱う
    int version = 0;
    if (j.contains("version")) {
        ReadSaveValue(j["version"], version, "version");
    }
    if (version > kCurrentVersion) {
        engine::Logger::LogWarning("Save data is newer than this build. Unknown fields are preserved only until the next save.");
    } else if (version < kCurrentVersion) {
        Migrate(j, version);
    }

    if (j.contains("continue") && j["continue"].is_object()) {
        const auto& c = j["continue"];
        // 書かれていない項目は0扱いにする（前回のセーブの値を持ち越さない）
        continue_ = ContinueData {};
        ReadSaveFields(j, kContinueFields, continue_);
        ReadEquipmentArray(c, "weapon_slots", continue_.weaponSlots);
        ReadEquipmentArray(c, "unlocked_weapons", continue_.unlockedWeapons);
        // 手で書き換えられた値でも壊れないよう、負の値やHPの上限超えを丸める
        continue_.maxHp = (std::max)(continue_.maxHp, 0);
        continue_.hp = std::clamp(continue_.hp, 0, continue_.maxHp);
        continue_.gold = (std::max)(continue_.gold, 0);
        continue_.score = (std::max)(continue_.score, 0);
        continue_.floor = (std::max)(continue_.floor, 0);
        int currentNode = 0;
        if (c.contains("current_node")) {
            ReadSaveValue(c["current_node"], currentNode, "current_node");
        }
        continue_.currentNode = static_cast<RunData::NodeType>(currentNode);

        // 取得済みスキルは番号の配列で保存している
        continue_.skills.clear();
        if (c.contains("skills") && !c["skills"].is_array()) {
            Logger::LogWarning("セーブのスキル配列が不正です");
        }
        if (c.contains("skills") && c["skills"].is_array()) {
            for (const auto& s : c["skills"]) {
                int skill = -1;
                ReadSaveValue(s, skill, "skills");
                if (skill >= 0) {
                    continue_.skills.push_back(static_cast<RunData::Skill>(skill));
                }
            }
        }
    }

    // 最高記録などのラン外の記録
    if (j.contains("records") && j["records"].is_object()) {
        records_ = SaveRecords {};
        ReadSaveFields(j, kRecordFields, records_);
    }
}

/** @brief save/save.json へ書き込む */
void SaveDataManager::Save() {
    if (!writesEnabled_) {
        return;
    }
    nlohmann::json j;

    j["version"] = kCurrentVersion;

    WriteJsonFields(j, kContinueFields, continue_);
    j["continue"]["weapon_slots"] = continue_.weaponSlots;
    j["continue"]["unlocked_weapons"] = continue_.unlockedWeapons;
    j["continue"]["current_node"] = static_cast<int>(continue_.currentNode);

    nlohmann::json skillsArray = nlohmann::json::array();
    for (auto s : continue_.skills) {
        skillsArray.push_back(static_cast<int>(s));
    }
    j["continue"]["skills"] = skillsArray;

    WriteJsonFields(j, kRecordFields, records_);

    engine::JsonHelper::Save(kFilePath, j);
}

/** @brief 現在のセーブ形式へ古いデータを補完する */
void SaveDataManager::Migrate(nlohmann::json& data, int sourceVersion) const {
    // 装備情報が無い旧形式に、空の配列と現在の版番号を補完する
    if (sourceVersion < kCurrentVersion) {
        // 装備を保存していない形式では、空の装備として読み込む
        if (data.contains("continue") && data["continue"].is_object()) {
            data["continue"]["weapon_slots"] = nlohmann::json::array();
            data["continue"]["unlocked_weapons"] = nlohmann::json::array();
        }
        data["version"] = kCurrentVersion;
    }
}

/** @brief RunData の現在状態をコンティニューデータとして保存する */
void SaveDataManager::SaveContinue(const RunData& rd) {
    continue_.valid = true;
    continue_.hp = rd.GetHp();
    continue_.maxHp = rd.GetMaxHp();
    continue_.gold = rd.GetGold();
    continue_.score = rd.GetScore();
    continue_.floor = rd.GetFloor();
    continue_.currentNode = rd.GetCurrentNode();
    continue_.skills = rd.GetSkillSnapshot();
    // ラン情報と同じタイミングで、武器管理が保持する装備を退避する
    const auto equipment = WeaponManager::GetInstance()->SaveSnapshot();
    continue_.weaponSlots.assign(equipment.slots.begin(), equipment.slots.end());
    continue_.unlockedWeapons.assign(equipment.unlocked.begin(), equipment.unlocked.end());
    continue_.weaponIndex = equipment.index;
    continue_.selectedWeaponSlot = equipment.selectedSlot;
    continue_.pendingWeaponIndex = equipment.pendingWeaponIndex;
    continue_.rangedWeaponIndex = equipment.rangedIndex;
    Save();
}

/** @brief 保存済みのコンティニューデータを RunData へ復元する */
void SaveDataManager::LoadContinue(RunData& rd) const {
    rd.RestoreFromSave(continue_.hp, continue_.maxHp, continue_.gold, continue_.score, continue_.floor,
        continue_.currentNode, continue_.skills);
    // 再起動後もセーブの装備を使用し、タイトルデモ中の装備を引き継がない
    auto* manager = WeaponManager::GetInstance();
    // 装備を記録していない古い版のセーブは、新しいランと同じ初期武器で再開する
    if (continue_.unlockedWeapons.empty()) {
        manager->ResetForNewRun();
        return;
    }
    manager->Reset();
    auto equipment = manager->SaveSnapshot();
    for (size_t i = 0; i < equipment.slots.size() && i < continue_.weaponSlots.size(); ++i) {
        equipment.slots[i] = continue_.weaponSlots[i];
    }
    for (size_t i = 0; i < equipment.unlocked.size() && i < continue_.unlockedWeapons.size(); ++i) {
        equipment.unlocked[i] = continue_.unlockedWeapons[i] == 1;
    }
    equipment.index = continue_.weaponIndex;
    equipment.selectedSlot = continue_.selectedWeaponSlot;
    equipment.pendingWeaponIndex = continue_.pendingWeaponIndex;
    equipment.rangedIndex = continue_.rangedWeaponIndex;
    manager->RestoreSnapshot(equipment);
}

/** @brief コンティニューデータを破棄する（ラン終了時に呼ぶ） */
void SaveDataManager::ClearContinue() {
    continue_.valid = false;
    Save();
}

/** @brief ラン終了結果を通算記録へ反映する */
void SaveDataManager::RecordRunResult(bool cleared, int floorReached, int goldEarned) {
    records_.totalRuns++;
    if (cleared) {
        records_.totalClears++;
    }
    if (floorReached > records_.bestFloorReached) {
        records_.bestFloorReached = floorReached;
    }
    records_.totalGoldEarned += goldEarned;
    Save();
}

} // namespace engine::game
