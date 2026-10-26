/**
 * @file SaveData.cpp
 * @brief ローグライトのコンティニューデータ・通算記録の読み込み・保存とバージョン移行（SaveDataManager）の実装
 */
#include "SaveData.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <algorithm>

namespace engine::game {
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

/** @brief 共有インスタンスを取得する */
SaveDataManager* SaveDataManager::GetInstance() {
    static SaveDataManager sInstance;
    return &sInstance;
}

/** @brief save/save.json から読み込む（ファイルがなければデフォルト値のまま） */
void SaveDataManager::Load() {
    // セーブファイルが無い・壊れている場合は初期状態のままにする
    auto j = engine::JsonHelper::Load(kFilePath);
    if (!j.is_object()) {
        return;
    }

    // 古い形式なら今の形式へ移し替えてから読む（新しい形式は警告だけ出して読める項目を読む）
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
        // 手で書き換えられた値でも壊れないよう、負の値やHPの上限超えを丸める
        continue_.maxHp = (std::max)(continue_.maxHp, 0);
        continue_.hp = std::clamp(continue_.hp, 0, continue_.maxHp);
        continue_.gold = (std::max)(continue_.gold, 0);
        continue_.score = (std::max)(continue_.score, 0);
        continue_.floor = (std::max)(continue_.floor, 0);
        continue_.currentNode = static_cast<RunData::NodeType>(c.value("current_node", 0));

        // 取得済みスキルは番号の配列で保存している
        continue_.skills.clear();
        if (c.contains("skills") && c["skills"].is_array()) {
            for (const auto& s : c["skills"]) {
                continue_.skills.push_back(static_cast<RunData::Skill>(s.get<int>()));
            }
        }
    }

    // 最高記録などのラン外の記録
    if (j.contains("records") && j["records"].is_object()) {
        records_ = SaveRecords {};
        ReadJsonFields(j, kRecordFields, records_);
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
    // バージョン導入前の形式はフィールド構成が同じため、番号だけ補完する
    // 今後形式を変更する場合はsourceVersionごとに変換処理を追加する
    if (sourceVersion <= 0) {
        data["version"] = 1;
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
    Save();
}

/** @brief 保存済みのコンティニューデータを RunData へ復元する */
void SaveDataManager::LoadContinue(RunData& rd) const {
    rd.RestoreFromSave(continue_.hp, continue_.maxHp, continue_.gold, continue_.score, continue_.floor,
        continue_.currentNode, continue_.skills);
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
