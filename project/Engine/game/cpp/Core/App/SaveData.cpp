/**
 * @file SaveData.cpp
 * @brief ローグライトのコンティニューデータ・通算記録の読み込み・保存とバージョン移行（SaveDataManager）の実装
 */
#include "SaveData.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>
using namespace engine::game;

namespace {
// セーブファイルの1つの値で済む項目（種類とスキル一覧は数値の変換が要るので個別に読み書きする）
constexpr JsonField<ContinueData> kContinueFields[] = {
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
} // namespace

SaveDataManager* SaveDataManager::GetInstance()
{
    static SaveDataManager instance;
    return &instance;
}

void SaveDataManager::Load()
{
    auto j = engine::JsonHelper::Load(kFilePath);
    if (!j.is_object()) {
        return;
    }

    const int version = j.value("version", 0);
    if (version > kCurrentVersion) {
        engine::Logger::LogWarning("Save data is newer than this build. Unknown fields are preserved only until the next save.");
    } else if (version < kCurrentVersion) {
        Migrate(j, version);
    }

    if (j.contains("continue") && j["continue"].is_object()) {
        const auto& c = j["continue"];
        // 書かれていない項目は0扱いにする（前回のセーブの値を持ち越さない）
        continue_ = ContinueData {};
        ReadJsonFields(j, kContinueFields, continue_);
        continue_.maxHp = (std::max)(continue_.maxHp, 0);
        continue_.hp = std::clamp(continue_.hp, 0, continue_.maxHp);
        continue_.gold = (std::max)(continue_.gold, 0);
        continue_.score = (std::max)(continue_.score, 0);
        continue_.floor = (std::max)(continue_.floor, 0);
        continue_.currentNode = static_cast<RunData::NodeType>(c.value("current_node", 0));

        continue_.skills.clear();
        if (c.contains("skills") && c["skills"].is_array()) {
            for (const auto& s : c["skills"]) {
                continue_.skills.push_back(static_cast<RunData::Skill>(s.get<int>()));
            }
        }
    }

    if (j.contains("records") && j["records"].is_object()) {
        records_ = SaveRecords {};
        ReadJsonFields(j, kRecordFields, records_);
    }
}

void SaveDataManager::Save()
{
    nlohmann::json j;

    j["version"] = kCurrentVersion;

    WriteJsonFields(j, kContinueFields, continue_);
    j["continue"]["current_node"] = static_cast<int>(continue_.currentNode);

    nlohmann::json skillsArray = nlohmann::json::array();
    for (auto s : continue_.skills) {
        skillsArray.push_back(static_cast<int>(s));
    }
    j["continue"]["skills"] = skillsArray;

    WriteJsonFields(j, kRecordFields, records_);

    engine::JsonHelper::Save(kFilePath, j);
}

void SaveDataManager::Migrate(nlohmann::json& data, int sourceVersion) const
{
    // バージョン導入前の形式はフィールド構成が同じため、番号だけ補完する
    // 今後形式を変更する場合はsourceVersionごとに変換処理を追加する
    if (sourceVersion <= 0) {
        data["version"] = 1;
    }
}

void SaveDataManager::SaveContinue(const RunData& rd)
{
    continue_.valid = true;
    continue_.hp = rd.GetHp();
    continue_.maxHp = rd.GetMaxHp();
    continue_.gold = rd.GetGold();
    continue_.score = rd.GetScore();
    continue_.floor = rd.GetFloor();
    continue_.currentNode = rd.GetCurrentNode();
    continue_.skills = rd.GetSkills();
    Save();
}

void SaveDataManager::LoadContinue(RunData& rd) const
{
    rd.RestoreFromSave(continue_.hp, continue_.maxHp, continue_.gold, continue_.score, continue_.floor,
        continue_.currentNode, continue_.skills);
}

void SaveDataManager::ClearContinue()
{
    continue_.valid = false;
    Save();
}

void SaveDataManager::RecordRunResult(bool cleared, int floorReached, int goldEarned)
{
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
