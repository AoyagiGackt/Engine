/**
 * @file LevelLoader.h
 * @brief レベルデータの型と、レベルファイルの読み込み・保存・実体の生成（LevelLoader）の宣言
 */
#pragma once
#include "MakeAffine.h"
#include "ObjectKind.h"
#include <cmath>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <type_traits>

namespace engine::graphics {
class ModelCommon;
class Object3d;
class Model;
}

namespace engine::game {

/** @brief 配置物の並べ方 */
enum class PlacementType { kStatic, ///< 1個だけ置く
    kRow }; ///< axis方向へcount個並べる
/** @brief カスタム動作の往復方式 */
enum class MotionMode { kLoop, ///< sin波で往復
    kPingPong, ///< 等速で往復
    kOnce }; ///< 一度だけ進んで止まる
/** @brief カスタム動作の加減速 */
enum class MotionEase { kLinear,
    kSmooth };
/** @brief ui_textの座標系 */
enum class TextSpace { kScreen, ///< position.x/yをスクリーンpx座標として使う
    kWorld }; ///< ワールド座標をカメラ基準で画面へ投影する

// レベルJSONに書く文字列（enum の並び順と一致させる。エディタのコンボ表示にも使う）
inline constexpr const char* kPlacementTypeNames[] = { "static", "row" };
inline constexpr const char* kMotionModeNames[] = { "loop", "pingpong", "once" };
inline constexpr const char* kMotionEaseNames[] = { "linear", "smooth" };
inline constexpr const char* kTextSpaceNames[] = { "screen", "world" };

/** @brief spawn_point が出す敵の種類（name は JSON の spawnType の値。敵の性格名としてそのまま EnemyEntity へ渡す） */
struct SpawnTypeDef {
    const char* name;
    const char* label; ///< エディタでの表示名
    bool knight; ///< ナイトの実体を作るか（false なら一般敵）
};
inline constexpr SpawnTypeDef kSpawnTypes[] = {
    { "basic", "汎用エネミー", false },
    { "knight", "ナイト", true },
};
/** @brief spawnType の文字列から kSpawnTypes の添字を引く（無ければ -1） */
inline int FindSpawnTypeIndex(const std::string_view& name) {
    for (int i = 0; i < static_cast<int>(std::size(kSpawnTypes)); ++i) {
        if (name == kSpawnTypes[i].name) {
            return i;
        }
    }
    return -1;
}

/** @brief event_condition が成立した時に立てる GameFlags の接頭辞（後ろに条件の名前が付く） */
inline constexpr std::string_view kConditionFlagPrefix = "condition_";
/** @brief 条件の名前から、成立時に立つフラグ名を作る */
inline std::string ConditionFlagName(const std::string& conditionName) {
    return std::string(kConditionFlagPrefix) + conditionName;
}
/** @brief 親をたどる深さの上限（循環参照で止まらなくならないように） */
inline constexpr int kMaxParentDepth = 16;

/**
 * @brief レベルJSONの文字列を enum へ変換する
 * @param name     JSONに書かれた文字列
 * @param names    enum の並び順に並べた文字列表
 * @param fallback 未知の文字列だった時の値
 */
/** @brief 名前の表から列挙値を探す（無ければ fallback を返す） */
template <class E, size_t N>
E ParseLevelEnum(const std::string& name, const char* const (&names)[N], E fallback) {
    static_assert(std::is_enum_v<E>);
    for (size_t i = 0; i < N; ++i) {
        if (name == names[i]) {
            return static_cast<E>(i);
        }
    }
    return fallback;
}

/** @brief enum をレベルJSONに書く文字列へ変換する */
template <class E, size_t N>
const char* LevelEnumName(E value, const char* const (&names)[N]) {
    static_assert(std::is_enum_v<E>);
    const size_t index = static_cast<size_t>(value);
    return index < N ? names[index] : names[0];
}

// JSON の1エントリに対応するオブジェクト定義
/** @brief レベルJSONの配置物1件ぶんの編集データ（見た目・当たり判定・ギミック・敵生成設定をすべて保持） */
struct ObjectDesc {
    bool enabled = true; // falseなら保存は維持するが生成・更新・描画・当たり判定から除外する
    std::string name; // 親子参照・エディタ表示用の一意な名前（空ならロード時に自動命名）
    std::string parent; // 親オブジェクトのname（空なら親なし）子のpositionは親からの相対位置になる
    PlacementType type = PlacementType::kStatic;
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
    MotionMode motionMode = MotionMode::kLoop;
    MotionEase motionEase = MotionEase::kLinear;
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
    std::string enemyModel; // 敵の見た目。空なら騎士、enemy_params.json の visuals の名前（"Slime" など）ならそのモンスター（武器を持たない敵になる）
    std::string enemyType; // エディタの「敵を作る」で作った種類の名前（表示用。値は配置時に下の項目へ写してある）
    float enemyHpScale = 1.0f; // HPの倍率
    float enemyDamageScale = 1.0f; // 攻撃力の倍率
    float enemySpeedScale = 1.0f; // 近づく速さの倍率

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
    Vector4 breakableColor = { 1.0f, 0.92f, 0.88f, 1.0f }; // 表示色（脈動の基準色。モデルの色に掛けるので白寄りにする）
    // PlacementType::kRow 専用
    char axis = 'x'; // 並べる軸  'x' | 'y' | 'z'
    int count = 1; // 個数
    float step = 1.0f; // 間隔

    // "ui_text" 専用（画面またはワールドに文字列を表示する）
    std::string text; // 表示文字列（UTF-8、複数行可）
    Vector4 textColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool textBold = false;
    bool textShadow = true; // 明るい背景でも文章を読めるようにする
    float textScale = 1.5f;
    TextSpace textSpace = TextSpace::kScreen;
};

/**
 * @brief desc から親を順にたどり、見つかった親ごとに visit を呼ぶ（循環参照は深さ上限で打ち切る）
 * @param findParent 親の名前から配置情報を引く関数（見つからなければ nullptr を返す）
 * @param visit      たどった親ごとに呼ぶ関数（近い親から順）
 */
template <class FindParent, class Visit>
void VisitParentChain(const ObjectDesc& desc, const FindParent& findParent, const Visit& visit) {
    const ObjectDesc* cur = &desc;
    for (int guard = 0; guard < kMaxParentDepth && !cur->parent.empty(); ++guard) {
        const ObjectDesc* parent = findParent(cur->parent);
        if (parent == nullptr) {
            break;
        }
        visit(*parent);
        cur = parent;
    }
}

/** @brief local を Z 軸まわりに angleZ だけ回し、offset を足した位置を返す（親1段ぶんの座標の移し替え） */
inline Vector3 RotateZAndOffset(const Vector3& local, float angleZ, const Vector3& offset) {
    const float c = std::cos(angleZ);
    const float s = std::sin(angleZ);
    return { local.x * c - local.y * s + offset.x, local.x * s + local.y * c + offset.y, local.z + offset.z };
}

/**
 * @brief 親をたどって desc のワールド位置を求める（親ごとに、親のZ回転で回してから親の位置を足す）
 * @param findParent 親の名前から配置情報を引く関数（見つからなければ nullptr を返す）
 */
template <class FindParent>
Vector3 ResolveWorldPosition(const ObjectDesc& desc, const FindParent& findParent) {
    Vector3 result = desc.position;
    VisitParentChain(desc, findParent, [&](const ObjectDesc& parent) {
        result = RotateZAndOffset(result, parent.rotation.z, parent.position);
    });
    return result;
}

/**
 * @brief ワールド位置 world を、desc の親から見た位置へ直す（ResolveWorldPosition の逆。親が無ければそのまま返す）
 * @param findParent 親の名前から配置情報を引く関数（見つからなければ nullptr を返す）
 */
template <class FindParent>
Vector3 ResolveLocalPosition(const ObjectDesc& desc, const Vector3& world, const FindParent& findParent) {
    const ObjectDesc* parent = desc.parent.empty() ? nullptr : findParent(desc.parent);
    if (parent == nullptr) {
        return world;
    }
    // 親から見た向きは、先祖すべての Z 回転の合計になる（Z軸まわりだけなので足し合わせればよい）
    float angleZ = 0.0f;
    VisitParentChain(desc, findParent, [&](const ObjectDesc& ancestor) {
        angleZ += ancestor.rotation.z;
    });
    // 親のワールド位置からのずれを、その向きの分だけ逆に回す
    const Vector3 parentWorld = ResolveWorldPosition(*parent, findParent);
    const Vector3 offset = { world.x - parentWorld.x, world.y - parentWorld.y, world.z - parentWorld.z };
    return RotateZAndOffset(offset, -angleZ, { });
}

/** @brief 配列配置の軸名と位置ベクトル内の対象成分 */
struct PlacementAxis {
    char name;
    float Vector3::* member;
};
inline constexpr PlacementAxis kPlacementAxes[] = {
    { 'x', &Vector3::x }, { 'y', &Vector3::y }, { 'z', &Vector3::z },
};

/** @brief 配列配置に使う軸の添字を返す。未知の軸はXとして扱う */
inline size_t PlacementAxisIndex(char axis) {
    for (size_t i = 0; i < std::size(kPlacementAxes); ++i) {
        if (axis == kPlacementAxes[i].name) {
            return i;
        }
    }
    return 0;
}

/** @brief 配置方式の定義表から生成個数を求める */
inline int PlacementCopyCount(const ObjectDesc& desc) {
    using Count = int (*)(const ObjectDesc&);
    static constexpr Count kCounts[] = {
        [](const ObjectDesc&) {
            return 1;
        },
        [](const ObjectDesc& row) {
            return row.count;
        },
    };
    const size_t index = static_cast<size_t>(desc.type);
    return index < std::size(kCounts) ? kCounts[index](desc) : 1;
}

/** @brief 配置方式と軸の定義表から指定インスタンスのワールド座標を求める */
inline Vector3 PlacementInstancePosition(const ObjectDesc& desc, const Vector3& basePosition, int instanceIndex) {
    using Offset = float (*)(const ObjectDesc&, int);
    static constexpr Offset kOffsets[] = {
        [](const ObjectDesc&, int) {
            return 0.0f;
        },
        [](const ObjectDesc& row, int index) {
            return row.step * static_cast<float>(index);
        },
    };
    const size_t type = static_cast<size_t>(desc.type);
    Vector3 position = basePosition;
    const auto member = kPlacementAxes[PlacementAxisIndex(desc.axis)].member;
    position.*member += type < std::size(kOffsets) ? kOffsets[type](desc, instanceIndex) : 0.0f;
    return position;
}

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
    void Initialize(const std::vector<CheckpointDesc>* checkpoints, const Vector3& fallback) {
        checkpoints_ = checkpoints;
        respawnPosition_ = fallback;
        activeIndex_ = -1;
    }

    /**
     * @brief プレイヤー位置から通過判定を更新する
     * @param playerPosition 現在のプレイヤー座標
     * @return 新しいチェックポイントを有効化した場合はtrue
     */
    bool Update(const Vector3& playerPosition) {
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
    const Vector3& GetRespawnPosition() const {
        return respawnPosition_;
    }

    /** @brief 最後に有効化したチェックポイント番号を返す */
    int GetActiveIndex() const {
        return activeIndex_;
    }

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
/** @brief levelLoader::Spawn() が生成したModel/Object3dの所有権を保持する（シーン側が寿命を管理する） */
struct LevelSpawnResult {
    std::vector<std::unique_ptr<engine::graphics::Model>> models;
    std::vector<std::unique_ptr<engine::graphics::Object3d>> objects;
};

namespace levelLoader {
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

    /**
     * @brief 配置情報の回転・拡縮・ライティングで、見た目のオブジェクトを1つ作る
     * @param modelCommon モデル描画の共通設定
     * @param model 表示するモデル
     * @param desc 回転・拡縮・ライティングを取る配置情報
     * @param position 置く位置（列配置の各実体や親子の合成後の位置）
     */
    std::unique_ptr<engine::graphics::Object3d> CreatePlacedObject(engine::graphics::ModelCommon* modelCommon,
        engine::graphics::Model* model, const ObjectDesc& desc, const Vector3& position);
}

} // namespace engine::game
