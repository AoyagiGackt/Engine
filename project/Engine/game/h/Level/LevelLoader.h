/**
 * @file LevelLoader.h
 * @brief LevelLoaderのレベルデータの読込、編集、実体生成に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "MakeAffine.h"
#include <memory>
#include <string>
#include <vector>

namespace engine::graphics {
class ModelCommon;
class Object3d;
class Model;
}

namespace engine::game {

// JSON の1エントリに対応するオブジェクト定義
/**
 * @brief ObjectDesc に関する型を提供する
 * @details ObjectDesc が扱うデータと操作の責務をまとめる
 */
struct ObjectDesc {
    bool enabled = true; // falseなら保存は維持するが生成・更新・描画・当たり判定から除外する
    std::string name; // 親子参照・エディタ表示用の一意な名前（空ならロード時に自動命名）
    std::string parent; // 親オブジェクトのname（空なら親なし）子のpositionは親からの相対位置になる
    std::string type; // "static" | "row"
    // "prop"（既定、見た目のみのObject3d）| "enemy_knight"（KnightEnemy実体を生成）| "enemy_basic"（EnemyEntity実体を生成）
    // enemy系はStageEditorが実際にHPを持つ敵インスタンスとして生成する（model/texture/type/axis/count/stepは無視される）
    std::string kind = "prop";
    std::string model; // OBJ ファイルパス
    std::string texture; // テクスチャパス
    Vector3 position = { }; // 親がいる場合は親位置からのオフセット、いなければワールド座標
    Vector3 rotation = { };
    Vector3 scale = { 1.0f, 1.0f, 1.0f };
    bool lighting = true;
    bool solid = false; // trueならプレイヤーの当たり判定あり（壁として塞ぐ／上に乗れる）
    std::string activationFlag; // 指定時はGameFlagsがtrueの間だけギミックやカメラを有効にする
    bool activeWhenFlag = true; // falseならactivationFlagがfalseの間だけ対象を有効にする
    float activationDelay = 0.0f; // 条件成立から動作を反映するまでの秒数
    std::string enemyGroup; // 敵生成と全滅条件をまとめるグループ名
    std::string conditionType = "manual"; // event_conditionの条件 manual、timer、enemy_group_defeated
    float conditionSeconds = 0.0f; // timer条件が成立するまでの秒数
    std::string gimmickMotion = "none"; // none、move_y、rotate_y、fall、blink
    float motionAmount = 3.0f; // ギミック移動量または回転量をワールド単位またはラジアンで指定する
    float motionSpeed = 1.0f; // ギミック演出速度を毎秒単位で指定する
    float cameraBlendSeconds = 0.5f; // カメラポイントへ補間する秒数
    float cameraHoldSeconds = 2.0f; // カメラポイントを維持する秒数
    std::string spawnType = "basic"; // spawn_pointが生成する敵種類 basicまたはknight
    std::string patrolRoute; // 敵とpatrol_pointを結ぶ巡回ルート名
    int routeOrder = 0; // patrol_pointを並べる順序
    float patrolSpeed = 1.5f; // 巡回速度をワールド単位毎秒で指定する
    bool meshCollider = false; // terrainの表示メッシュから三角形単位AABBを同期する
    // "row" 専用
    char axis = 'x'; // 並べる軸  'x' | 'y' | 'z'
    int count = 1; // 個数
    float step = 1.0f; // 間隔
};

// JSON の1エントリに対応するトリガー定義
// プレイヤーが半径radius以内に入ると、flagで指定した名前のフラグをvalueにする（GameFlags参照）
// 実際の分岐ロジックはノードグラフ側（GetFlag→If）が担当し、トリガーはフラグを立てるだけに徹する
/**
 * @brief TriggerDesc に関する型を提供する
 * @details TriggerDesc が扱うデータと操作の責務をまとめる
 */
struct TriggerDesc {
    std::string name; // ステージエディタのHierarchy表示用（省略可）
    Vector3 position = { };
    float radius = 2.0f;
    std::string flag; // 立てる／倒すフラグ名（GameFlagsのキー）
    bool value = true; // トリガー成立時にflagへ設定する値
    bool once = true; // true  一度成立したら以降は判定しない
};

/** @brief ステージ内の復帰地点を定義する */
struct CheckpointDesc {
    std::string name; ///< セーブやデバッグ表示に使う一意な名前
    Vector3 position = { }; ///< 復帰時にプレイヤーを配置する座標
    float activationRadius = 2.0f; ///< 有効化する距離をワールド単位で指定する
};

/**
 * @brief プレイヤーが最後に通過したチェックポイントを管理する
 * @note シーン開始時にInitializeし、毎フレームUpdateを呼ぶ
 */
class CheckpointRuntime {
public:
    /**
     * @brief チェックポイント一覧と初期復帰地点を設定する
     * @param checkpoints ステージが保持するチェックポイント一覧
     * @param fallback チェックポイント未通過時の復帰地点
     */
    void Initialize(const std::vector<CheckpointDesc>* checkpoints, const Vector3& fallback)
    {
        checkpoints_ = checkpoints;
        respawnPosition_ = fallback;
        activeIndex_ = -1;
    }

    /**
     * @brief プレイヤー位置から通過判定を更新する
     * @param playerPosition 現在のプレイヤー座標
     * @return 新しいチェックポイントを有効化した場合はtrue
     */
    bool Update(const Vector3& playerPosition)
    {
        if (!checkpoints_) {
            return false;
        }
        for (size_t i = 0; i < checkpoints_->size(); ++i) {
            const CheckpointDesc& checkpoint = (*checkpoints_)[i];
            const Vector3 delta = {
                playerPosition.x - checkpoint.position.x,
                playerPosition.y - checkpoint.position.y,
                playerPosition.z - checkpoint.position.z
            };
            const float distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
            if (distanceSquared <= checkpoint.activationRadius * checkpoint.activationRadius
                && activeIndex_ != static_cast<int>(i)) {
                activeIndex_ = static_cast<int>(i);
                respawnPosition_ = checkpoint.position;
                return true;
            }
        }
        return false;
    }

    /** @brief 現在の復帰地点を返す */
    const Vector3& GetRespawnPosition() const { return respawnPosition_; }

    /** @brief 最後に有効化したチェックポイント番号を返す */
    int GetActiveIndex() const { return activeIndex_; }

private:
    const std::vector<CheckpointDesc>* checkpoints_ = nullptr;
    Vector3 respawnPosition_ = { };
    int activeIndex_ = -1;
};

// ファイルから読み込んだレベル全体のデータ
/**
 * @brief LevelData に関する型を提供する
 * @details LevelData が扱うデータと操作の責務をまとめる
 */
struct LevelData {
    std::vector<ObjectDesc> objects;
    std::vector<TriggerDesc> triggers;
    std::vector<CheckpointDesc> checkpoints;
    Vector3 playerSpawn = { 8.0f, 0.4f, 0.0f };
    Vector3 enemySpawn = { 28.0f, 0.4f, 0.0f };
};

// Spawn() の戻り値Model と Object3d の所有権を持つ
/**
 * @brief LevelSpawnResult に関する型を提供する
 * @details LevelSpawnResult が扱うデータと操作の責務をまとめる
 */
struct LevelSpawnResult {
    std::vector<std::unique_ptr<engine::graphics::Model>> models;
    std::vector<std::unique_ptr<engine::graphics::Object3d>> objects;
};

namespace LevelLoader {
    // JSON ファイルを読んで LevelData を返す
    /**
     * @brief Load の結果を取得する
     * @param path 処理に使用する値
     * @return 処理結果
     */
    LevelData Load(const std::string& path);

    // LevelData を JSON ファイルへ書き出す（StageEditorの保存機能から使う）
    /**
     * @brief Save に対応する処理を実行する
     * @param path 処理に使用する値
     * @param data 処理に使用する値
     * @return なし
     */
    void Save(const std::string& path, const LevelData& data);

    // LevelData の静的オブジェクトを生成して返す
    /**
     * @brief Spawn に対応する処理を実行する
     * @param data 処理に使用する値
     * @param modelCommon 処理に使用する値
     * @return 処理結果
     */
    LevelSpawnResult Spawn(const LevelData& data, engine::graphics::ModelCommon* modelCommon);
}

} // namespace engine::game
