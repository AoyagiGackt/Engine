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
/** @brief レベルJSONの配置物1件ぶんの編集データ（見た目・当たり判定・ギミック・敵生成設定をすべて保持） */
struct ObjectDesc {
    bool enabled = true; // falseなら保存は維持するが生成・更新・描画・当たり判定から除外する
    std::string name; // 親子参照・エディタ表示用の一意な名前（空ならロード時に自動命名）
    std::string parent; // 親オブジェクトのname（空なら親なし）子のpositionは親からの相対位置になる
    std::string type; // "static" | "row"
    // "prop"（既定、見た目のみのObject3d）| "enemy_knight"（KnightEnemy実体を生成）| "enemy_basic"（EnemyEntity実体を生成）
    // | "ui_text"（Object3dを生成せず、StageEditor::DrawUITextがFontRendererで文字列を描画する。以下のtext系フィールド専用）
    // | "pickup"（触れると回収される収集物。pickup系フィールド専用）
    // | "breakable"（攻撃で壊れて周囲にダメージを撒く設置物。breakable系フィールド専用）
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
    std::string gimmickMotion = "none"; // none、move_x、move_y、rotate_y、rotate_z、fall、blink、custom
    float motionAmount = 3.0f; // ギミック移動量または回転量をワールド単位またはラジアンで指定する
    float motionSpeed = 1.0f; // ギミック演出速度を毎秒単位で指定する
    // "custom"専用（任意方向の移動と回転を組み合わせた汎用モーション。新しい動きをコード無しで作る）
    Vector3 motionAxis = { 1.0f, 0.0f, 0.0f }; // 移動方向（motionAmount倍して往復・周回する）
    Vector3 motionRotation = { }; // 進行度1.0に対する回転量（ラジアン）
    std::string motionMode = "loop"; // loop（sin波で往復）| pingpong（等速で往復）| once（一度だけ進んで止まる）
    std::string motionEase = "linear"; // linear | smooth（加減速）
    float cameraBlendSeconds = 0.5f; // カメラポイントへ補間する秒数
    float cameraHoldSeconds = 2.0f; // カメラポイントを維持する秒数
    std::string spawnType = "basic"; // spawn_pointが生成する敵種類 basicまたはknight
    std::string patrolRoute; // 敵とpatrol_pointを結ぶ巡回ルート名
    int routeOrder = 0; // patrol_pointを並べる順序
    float patrolSpeed = 1.5f; // 巡回速度をワールド単位毎秒で指定する
    bool meshCollider = false; // terrainの表示メッシュから三角形単位AABBを同期する
    // "enemy_basic" 専用（GamePlayScene等が武器奪取ギミックの対象を判別するのに使う）
    std::string weaponType; // 空なら武器を持たない一般敵。"Sword"等ならその武器を持ち、倒してJキーで奪取できる
    bool isStageBoss = false; // trueならこの敵を倒して奪取するとステージクリア条件が成立する（HPはRunDataのノード種別で自動調整）

    // "pickup" 専用（プレイヤーが触れると回収され、覚醒ゲージが増える収集物。回収時にpickup_<name>フラグが立つ）
    float pickupRadius = 1.0f; // 回収判定の半径
    float pickupGaugeAmount = 0.2f; // 回収時に増える覚醒ゲージ量
    Vector4 pickupColor = { 0.3f, 0.9f, 1.0f, 1.0f }; // 表示色（脈動の基準色）

    // "breakable" 専用（攻撃で壊すと周囲にダメージを撒く設置物。破壊時にbroken_<name>フラグが立つ）
    int breakableHp = 2; // 何回攻撃を当てると壊れるか
    float breakableRadius = 3.0f; // 爆発が届く範囲の半径
    int breakablePlayerDamage = 4; // 範囲内のプレイヤーが受けるダメージ
    int breakableEnemyDamage = 3; // 範囲内の敵が受けるダメージ
    std::string breakableWeapon; // 空なら何でも壊せる。"Hammer"等を指定するとその武器の近接攻撃でしか壊れない（壁ギミック用、solid=trueと組み合わせる）
    Vector4 breakableColor = { 1.0f, 0.35f, 0.1f, 1.0f }; // 表示色（脈動の基準色）
    // "row" 専用
    char axis = 'x'; // 並べる軸  'x' | 'y' | 'z'
    int count = 1; // 個数
    float step = 1.0f; // 間隔

    // "ui_text" 専用（画面またはワールドに文字列を表示する）
    std::string text; // 表示文字列（UTF-8、複数行可）
    Vector4 textColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool textBold = false;
    bool textShadow = true; // 明るい背景でも文章を読めるようにする
    float textScale = 1.5f;
    std::string textSpace = "screen"; // "screen"（position.x/yをスクリーンpx座標として使う）| "world"（ワールド座標をカメラ基準で画面へ投影する）
};

// JSON の1エントリに対応するトリガー定義
// プレイヤーが半径radius以内に入ると、flagで指定した名前のフラグをvalueにする（GameFlags参照）
// 実際の分岐ロジックはノードグラフ側（GetFlag→If）が担当し、トリガーはフラグを立てるだけに徹する
/** @brief プレイヤーが半径radius以内に入るとflagをvalueにするトリガー1件の定義 */
struct TriggerDesc {
    std::string name; // ステージエディタのHierarchy表示用（省略可）
    Vector3 position = { };
    float radius = 2.0f;
    std::string flag; // 立てる／倒すフラグ名（GameFlagsのキー）
    bool value = true; // トリガー成立時にflagへ設定する値
    bool once = true; // true  一度成立したら以降は判定しない
    bool spawnsWaterSplash = false; // trueなら成立した瞬間、この位置に水しぶきを出す（ノードグラフ不要の単体演出）
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

/**
 * @brief GameFlagsのフラグがfalse→trueになった瞬間に起動するノードグラフの紐付け1件
 * @note ステージのトリガー/条件がフラグを立てる → このグラフが走る、という分業でロジックをコード無しで組む
 */
struct FlagGraphBinding {
    std::string flag; // 監視するGameFlagsのキー
    std::string graphPath; // 起動するグラフJSONのパス
};

/**
 * @brief 見た目のモデル（Object3d）を生成する配置種類かどうか
 * @note prop/background/gimmick/terrainに加えて、pickup/breakableもモデルを持つ
 */
inline bool IsVisualKind(const std::string& kind)
{
    return kind == "prop" || kind == "background" || kind == "gimmick" || kind == "terrain"
        || kind == "pickup" || kind == "breakable";
}

// ファイルから読み込んだレベル全体のデータ
/** @brief レベルJSON1ファイルぶんの内容（配置物・トリガー・チェックポイント・プレイヤー/敵の初期スポーン位置・グラフ紐付け） */
struct LevelData {
    std::vector<ObjectDesc> objects;
    std::vector<TriggerDesc> triggers;
    std::vector<CheckpointDesc> checkpoints;
    Vector3 playerSpawn = { 8.0f, 0.4f, 0.0f };
    Vector3 enemySpawn = { 28.0f, 0.4f, 0.0f };
    std::string graphPath; // レベル読込直後から常駐で走るグラフJSONのパス（空なら無し）
    std::vector<FlagGraphBinding> flagGraphs; // フラグの立ち上がりで起動するグラフ一覧
};

// Spawn() の戻り値Model と Object3d の所有権を持つ
/** @brief LevelLoader::Spawn() が生成したModel/Object3dの所有権を保持する（シーン側が寿命を管理する） */
struct LevelSpawnResult {
    std::vector<std::unique_ptr<engine::graphics::Model>> models;
    std::vector<std::unique_ptr<engine::graphics::Object3d>> objects;
};

namespace LevelLoader {
    /**
     * @brief JSON ファイルを読み込みLevelDataへ変換する
     * @param path 読み込むレベルJSONのパス
     * @return 読み込んだレベルデータファイルが存在しない/壊れている場合は空のLevelData
     */
    LevelData Load(const std::string& path);

    /**
     * @brief LevelData を JSON ファイルへ書き出す（StageEditorの保存機能から使う）
     * @param path 書き出し先のパス
     * @param data 書き出すレベルデータ
     */
    void Save(const std::string& path, const LevelData& data);

    /**
     * @brief kindがprop/gimmick/terrainの配置物からModel/Object3dを生成する（enemy系の実体生成はStageEditor側が担当）
     * @param data 生成元のレベルデータ
     * @param modelCommon モデル生成に使用する共通処理
     * @return 生成したModel/Object3dの所有権を持つ結果
     */
    LevelSpawnResult Spawn(const LevelData& data, engine::graphics::ModelCommon* modelCommon);
}

} // namespace engine::game
