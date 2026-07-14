#include "SaveData.h"
#include "JsonHelper.h"
using namespace engine::game;

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

    if (j.contains("continue") && j["continue"].is_object()) {
        const auto& c = j["continue"];
        continue_.valid = c.value("valid", false);
        continue_.hp = c.value("hp", 0);
        continue_.maxHp = c.value("max_hp", 0);
        continue_.gold = c.value("gold", 0);
        continue_.floor = c.value("floor", 0);
        continue_.currentNode = static_cast<RunData::NodeType>(c.value("current_node", 0));

        continue_.skills.clear();
        if (c.contains("skills") && c["skills"].is_array()) {
            for (const auto& s : c["skills"]) {
                continue_.skills.push_back(static_cast<RunData::Skill>(s.get<int>()));
            }
        }
    }

    if (j.contains("records") && j["records"].is_object()) {
        const auto& r = j["records"];
        records_.bestFloorReached = r.value("best_floor", 0);
        records_.totalRuns = r.value("total_runs", 0);
        records_.totalClears = r.value("total_clears", 0);
        records_.totalGoldEarned = r.value("total_gold_earned", 0LL);
    }
}

void SaveDataManager::Save()
{
    nlohmann::json j;

    j["continue"]["valid"] = continue_.valid;
    j["continue"]["hp"] = continue_.hp;
    j["continue"]["max_hp"] = continue_.maxHp;
    j["continue"]["gold"] = continue_.gold;
    j["continue"]["floor"] = continue_.floor;
    j["continue"]["current_node"] = static_cast<int>(continue_.currentNode);

    nlohmann::json skillsArray = nlohmann::json::array();
    for (auto s : continue_.skills) {
        skillsArray.push_back(static_cast<int>(s));
    }
    j["continue"]["skills"] = skillsArray;

    j["records"]["best_floor"] = records_.bestFloorReached;
    j["records"]["total_runs"] = records_.totalRuns;
    j["records"]["total_clears"] = records_.totalClears;
    j["records"]["total_gold_earned"] = records_.totalGoldEarned;

    engine::JsonHelper::Save(kFilePath, j);
}

void SaveDataManager::SaveContinue(const RunData& rd)
{
    continue_.valid = true;
    continue_.hp = rd.GetHp();
    continue_.maxHp = rd.GetMaxHp();
    continue_.gold = rd.GetGold();
    continue_.floor = rd.GetFloor();
    continue_.currentNode = rd.GetCurrentNode();
    continue_.skills = rd.GetSkills();
    Save();
}

void SaveDataManager::LoadContinue(RunData& rd) const
{
    rd.RestoreFromSave(continue_.hp, continue_.maxHp, continue_.gold, continue_.floor,
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
