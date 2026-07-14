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
    std::string name;               // 親子参照・エディタ表示用の一意な名前（空ならロード時に自動命名）
    std::string parent;             // 親オブジェクトのname（空なら親なし）子のpositionは親からの相対位置になる
    std::string type;               // "static" | "row"
    std::string model;              // OBJ ファイルパス
    std::string texture;            // テクスチャパス
    Vector3     position = {};      // 親がいる場合は親位置からのオフセット、いなければワールド座標
    Vector3     rotation = {};
    Vector3     scale    = { 1.0f, 1.0f, 1.0f };
    bool        lighting = true;
    bool        solid    = false;  // trueならプレイヤーの当たり判定あり（壁として塞ぐ／上に乗れる）
    // "row" 専用
    char        axis  = 'x';       // 並べる軸: 'x' | 'y' | 'z'
    int         count = 1;         // 個数
    float       step  = 1.0f;      // 間隔
};

// JSON の1エントリに対応するトリガー定義
// プレイヤーが半径radius以内に入ると、flagで指定した名前のフラグをvalueにする（GameFlags参照）
// 実際の分岐ロジックはノードグラフ側（GetFlag→If）が担当し、トリガーは「フラグを立てるだけ」に徹する
struct TriggerDesc {
    std::string name;               // ステージエディタのHierarchy表示用（省略可）
    Vector3     position = {};
    float       radius   = 2.0f;
    std::string flag;               // 立てる／倒すフラグ名（GameFlagsのキー）
    bool        value    = true;    // トリガー成立時にflagへ設定する値
    bool        once      = true;   // true: 一度成立したら以降は判定しない
};

// ファイルから読み込んだレベル全体のデータ
struct LevelData {
    std::vector<ObjectDesc>  objects;
    std::vector<TriggerDesc> triggers;
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

    // LevelData を JSON ファイルへ書き出す（StageEditorの保存機能から使う）
    void              Save(const std::string& path, const LevelData& data);

    // LevelData の静的オブジェクトを生成して返す
    LevelSpawnResult Spawn(const LevelData& data, engine::graphics::ModelCommon* modelCommon);
}

} // namespace engine::game
