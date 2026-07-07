#pragma once
#include <memory>
#include <string>
#include <vector>
#include "MakeAffine.h"

namespace engine::graphics {
class ModelCommon;
class Object3d;
class Model;
}

namespace engine::game {

// JSON の1エントリに対応するオブジェクト定義
struct ObjectDesc {
    std::string type;               // "static" | "row"
    std::string model;              // OBJ ファイルパス
    std::string texture;            // テクスチャパス
    Vector3     position = {};
    Vector3     rotation = {};
    Vector3     scale    = { 1.0f, 1.0f, 1.0f };
    bool        lighting = true;
    // "row" 専用
    char        axis  = 'x';       // 並べる軸: 'x' | 'y' | 'z'
    int         count = 1;         // 個数
    float       step  = 1.0f;      // 間隔
};

// ファイルから読み込んだレベル全体のデータ
struct LevelData {
    std::vector<ObjectDesc> objects;
    Vector3 playerSpawn = { 8.0f, 0.4f, 0.0f };
    Vector3 enemySpawn  = { 28.0f, 0.4f, 0.0f };
};

// Spawn() の戻り値Model と Object3d の所有権を持つ
struct LevelSpawnResult {
    std::vector<std::unique_ptr<engine::graphics::Model>>    models;
    std::vector<std::unique_ptr<engine::graphics::Object3d>> objects;
};

namespace LevelLoader {
    // JSON ファイルを読んで LevelData を返す
    LevelData        Load (const std::string& path);

    // LevelData の静的オブジェクトを生成して返す
    LevelSpawnResult Spawn(const LevelData& data, engine::graphics::ModelCommon* modelCommon);
}

} // namespace engine::game
