#pragma once
#include <string>
#include "json.hpp"

// JSON ファイルの読み書きユーティリティ
//
// 基本的な使い方:
//   // 保存
//   nlohmann::json j;
//   j["camera_pos"] = { pos.x, pos.y, pos.z };
//   j["bgm_volume"] = 0.7f;
//   JsonHelper::Save("save/settings.json", j);
//
//   // 読み込み
//   auto j = JsonHelper::Load("save/settings.json");
//   float vol = j.value("bgm_volume", 0.7f); // キーがなければデフォルト値
namespace JsonHelper {
    // ファイルから JSON を読み込む（ファイルが存在しない・破損の場合は空オブジェクトを返す）
    nlohmann::json Load(const std::string& path);

    // JSON をファイルへ保存する（親ディレクトリがなければ作成する）
    void Save(const std::string& path, const nlohmann::json& j, int indent = 2);

    // --- 後方互換（既存コードはそのまま動く）---
    // 新規コードでは Load/Save + nlohmann::json を使うこと
    float       ReadFloat (const std::string& src, const std::string& key, float def);
    int         ReadInt   (const std::string& src, const std::string& key, int def);
    std::string ReadString(const std::string& src, const std::string& key, const std::string& def);
}
