/**
 * @file StageEditor.h
 * @brief レベルJSON（配置オブジェクト＋トリガー）の読み込み・描画・実行時編集を1つにまとめたステージエディタ
 * @note オブジェクトの生成・毎フレームUpdate/Drawは通常ビルドでも動く「レベルの実体」そのものであり、
 * F2で開くImGuiパネル（Hierarchy/Inspector）だけがUSE_IMGUIビルド限定のデバッグ機能
 * ロジックはノードグラフ（GraphEditor）側の役目なので、ここではトリガーの「フラグを立てる」までしかやらない
 */
#pragma once
#include "CollisionConfig.h"
#include "LevelLoader.h"
#include "TriggerVolume.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine {
class Input;
}
namespace engine::graphics {
class Model;
class ModelCommon;
class Object3d;
class Camera;
class ParticleManager;
}

namespace engine::game {

class KnightEnemy;
class EnemyEntity;

class StageEditor {
public:
    // unique_ptr<KnightEnemy>/<EnemyEntity>をObjectEntryが持つため、それらの完全な定義が無い
    // 翻訳単位（BaseScene経由でStageEditorを持つ全シーン等）でも安全にコンパイルできるよう、
    // コンストラクタ/デストラクタは両方とも.cpp側（KnightEnemy.h/EnemyEntity.hをインクルード済みの場所）で
    // 定義する（暗黙生成に任せると、生成先の翻訳単位でobjects_絡みの完全性チェックが走ってしまうため）
    StageEditor();
    ~StageEditor();

    /// @brief レベルJSONを読み込み、オブジェクト/トリガーを生成する
    void Open(const std::string& levelPath, engine::graphics::ModelCommon* modelCommon, engine::graphics::Camera* camera);

    /// @brief 現在の内容を Open() したパスへ書き戻す
    void Save();

    /**
     * @brief 毎フレーム呼ぶF2でパネルの表示/非表示を切り替える
     * @note トリガー判定（フラグを立てる処理）はパネルの表示状態に関係なく常に行う
     */
    void Update(engine::Input* input, const Vector3& playerPos);

    /**
     * @brief 生成済みオブジェクトのトランスフォームを反映する（Draw前に毎フレーム呼ぶ）
     * @param pm        敵エンティティのパーティクル演出に使う（enemy_knight配置が無ければnullptrでよい）
     * @param playerPos 敵のAIターゲットに使う（enemy_knight配置が無ければ既定値でよい）
     * @note enemy系配置は、エディタ表示中（IsVisible）は編集用に位置を固定し、非表示中は
     * 実際のAI/重力Update()を回す（PlayerRefreshVisualTransforms系と同じ「編集中は静止」規約）
     */
    void UpdateObjects(engine::graphics::ParticleManager* pm = nullptr, const Vector3& playerPos = { });

    /// @brief 生成済みオブジェクトを描画する（3Dパス中に毎フレーム呼ぶ）
    void DrawObjects();

    /**
     * @brief このフレーム中にDrawObjects()が呼ばれ済みかどうか
     * @note Scene::Draw()内でHUDより前に自分でDrawObjects()を呼んだ場合、
     * BaseScene::Render()側の自動呼び出しをスキップしてブロックの二重描画/UI上乗せを防ぐために使う
     */
    bool WasObjectsDrawnThisFrame() const { return objectsDrawnThisFrame_; }

    /// @brief 配置済みのKnightEnemy一覧（enemy_knight配置分）を返す戦闘判定はScene側がこれを走査して行う
    std::vector<KnightEnemy*> GetKnights();

    /**
     * @brief solid=trueのオブジェクトのワールドAABB一覧を返す（毎フレーム呼ぶ想定）
     * @note ブロックの追加・移動・削除がそのまま次フレームの当たり判定に反映される
     */
    std::vector<engine::AABB> GetSolidColliders() const;

    bool IsVisible() const { return visible_; }

    /**
     * @brief Player/EnemyEntity/KnightEnemy等、レベルJSONに属さないランタイム上の実体をエディタで
     * 選択・ドラッグ移動できるようにする（Scene::Initialize等で、対象生成後に1回呼ぶ）
     * @param name     Hierarchy上の表示名（一意にすること同名を渡すと既存の登録を上書きする）
     * @param position 実体側が持つ位置メンバへの参照（Player::GetPositionRef()等）
     * @note ここに渡した参照はJSONへは保存しない位置の永続化は各Sceneが自前で行うこと
     */
    void RegisterExternalEntity(const std::string& name, Vector3* position);

private:
    // 1オブジェクト定義ぶんの編集単位（"row"は複数インスタンスを1エントリにまとめる）
    // kind=="prop"ならinstancesを使い、kindがenemy系ならknight/enemyのどちらかだけが生成される
    struct ObjectEntry {
        ObjectDesc desc;
        std::vector<std::unique_ptr<engine::graphics::Object3d>> instances;
        std::unique_ptr<KnightEnemy> knight;
        std::unique_ptr<EnemyEntity> enemy;
    };

    engine::graphics::Model* GetOrLoadModel(const std::string& modelPath, const std::string& texPath);

    /// @brief モデル/軸/個数など構造が変わったときの再構築（instances/knight/enemyを作り直す）
    void RegenerateInstances(ObjectEntry& entry);
    /// @brief 位置/回転/スケールだけを既存instancesへ反映する軽量パス（kind=="prop"専用）
    void RefreshTransforms(ObjectEntry& entry);
    /**
     * @brief enemy系エントリ1つぶんの毎フレーム処理
     * @note エディタ表示中はdesc.positionを実体へ書き戻して静止表示（RefreshVisualTransforms相当）、
     * 非表示中は実体の本物のUpdate()（AI/重力）を回し、逆にdesc.positionへ現在地を書き戻す（表示専用、保存はしない）
     */
    void UpdateEnemyEntry(ObjectEntry& entry, engine::graphics::ParticleManager* pm, const Vector3& playerPos);

    /// @brief 削除・Open()の再読み込み・破棄の前に、enemy_basic配置分をEnemyRegistryから解除する（ダングリングポインタ防止）
    void UnregisterEnemyEntity(const ObjectEntry& entry);

    void RenderHierarchy();
    void RenderInspector();
    /// @brief モデル/テクスチャをプリセットから選んで置ける一覧パネル選択中の配置物があればそれに適用、無ければ新規追加する
    void RenderAssetPalette();
    void RenderFlagsPanel();
    void DrawGizmos();

    /// @brief 画面中央(z=0平面)に新規の配置物(prop)を1つ追加して選択状態にする（+ボタン/アセットパレット共通）
    void AddPropAtScreenCenter(const std::string& model, const std::string& texture);
    /// @brief WASD(+QEで奥/手前)でカメラを移動するImGuiのテキスト入力中は無効化する
    void UpdateFreeCamera(engine::Input* input, float dt);

    /// @brief 3Dビュー上での左クリック選択とドラッグ移動（ImGuiウィンドウ上のマウスは無視する）
    void UpdateViewportInteraction();

    /// @brief マウススクリーン座標をゲーム平面(z=0)上のワールド座標へ変換する
    bool MouseToGround(float mouseX, float mouseY, Vector3& outWorld) const;

    /// @brief 親チェーンを解決したワールド位置を返す（親のpositionを順に加算循環は深さ上限で打ち切り）
    Vector3 WorldPositionOf(const ObjectDesc& desc) const;
    /// @brief 親のワールド位置を返す（親なしなら原点）ドラッグ時のローカル座標逆算に使う
    Vector3 ParentWorldPositionOf(const ObjectDesc& desc) const;
    /// @brief candidateName が selfName の子孫かどうか（親に設定すると循環になる相手の判定）
    bool IsDescendantOf(const std::string& candidateName, const std::string& selfName) const;

    /// @brief 空の名前・重複した名前に一意な自動名を振る（Open直後に呼ぶ）
    void EnsureUniqueNames();

    /// @brief Hierarchyツリーに1エントリ＋その子を再帰的に描く
    void DrawHierarchyEntry(int index, int depthLevel);

    std::string levelPath_;
    engine::graphics::ModelCommon* modelCommon_ = nullptr;
    engine::graphics::Camera* camera_ = nullptr;

    std::vector<ObjectEntry> objects_;
    std::vector<TriggerVolume> triggers_;

    // レベルJSONに属さないランタイム実体（Player/Enemy等）への参照RegisterExternalEntity()で登録される
    struct ExternalEntityRef {
        std::string name;
        Vector3* position = nullptr;
    };
    std::vector<ExternalEntityRef> externalEntities_;

    std::vector<std::unique_ptr<engine::graphics::Model>> modelStorage_;
    std::map<std::string, engine::graphics::Model*> modelCache_;

    Vector3 playerSpawn_ = { };
    Vector3 enemySpawn_ = { };

    // F2で表示/非表示（GraphEditorのF1と違い、ゲーム画面を隠さない小窓パネル構成）
    bool visible_ = false;
    float savedTimeScale_ = 1.0f;

    enum class SelKind { None,
        Object,
        Trigger,
        External };
    SelKind selKind_ = SelKind::None;
    int selIndex_ = -1;

    int nextSerial_ = 0; // 新規オブジェクト/トリガーの名前生成用

    bool viewportDragging_ = false; // 3Dビュー上で選択物をドラッグ移動中か

    bool objectsDrawnThisFrame_ = false; // DrawObjects()の二重呼び出し防止用（UpdateObjects()で毎フレームリセット）

    std::string statusMessage_;
    float statusTimer_ = 0.0f;

#ifdef USE_IMGUI
    // ---- Undo/Redo（GraphEditorと同じスナップショット方式、Ctrl+Z/Ctrl+Y） ----
    // ObjectEntryは実体(unique_ptr)を持ちコピーできないため、Save()の保存対象と同じdescだけを控え、
    // 復元時はRegenerateInstances()で実体を作り直す（modelCache_は生きているので再構築は軽い）
    struct LevelSnapshot {
        std::vector<ObjectDesc> objects;
        std::vector<TriggerDesc> triggers;
        Vector3 playerSpawn;
        Vector3 enemySpawn;
    };
    static constexpr size_t kMaxUndoHistory = 50;

    LevelSnapshot MakeSnapshot() const;
    /// @brief スナップショットの内容へ丸ごと戻す（敵のHPやトリガーの成立済み状態はリセットされる）
    void ApplySnapshot(const LevelSnapshot& snap);
    void PushUndo(LevelSnapshot snapshot);
    /// @brief 現在の状態を即座にUndoスタックへ積む（追加/削除など単発で完結する変更の直前に呼ぶ）
    void RecordUndoSnapshotNow();
    /// @brief ドラッグ/テキスト編集の開始時に変更前を仮記録するIsItemActivated()の直後に呼ぶ
    void BeginUndoCapture();
    /// @brief BeginUndoCapture()後、実際に値が変わったことを記録する（変更が無ければCommit時に捨てられる）
    void MarkUndoDirty();
    /// @brief ドラッグ/テキスト編集の終了時に呼ぶ実際に変化していた場合のみUndoスタックへ確定する
    void CommitUndoCapture();
    void Undo();
    void Redo();

    /// @brief 選択中のオブジェクト/トリガーを削除する（削除ボタンとDeleteキー共用）
    void DeleteSelected();
    /// @brief 選択中のオブジェクト/トリガーを複製して選択を移す（複製ボタンとCtrl+D共用）
    void DuplicateSelected();

    /// @brief スナップ有効時、値をsnapStep_の倍数へ丸める（無効時はそのまま返す）
    float SnapValue(float v) const;

    std::vector<LevelSnapshot> undoStack_;
    std::vector<LevelSnapshot> redoStack_;
    LevelSnapshot pendingUndoSnapshot_;
    bool hasPendingUndo_ = false;
    bool pendingUndoDirtied_ = false;

    bool dirty_ = false; // 最後の保存以降に編集があるか（未保存マーカーと開く時の破棄確認に使う）

    bool snapEnabled_ = false; // グリッドスナップ（ドラッグ移動・新規配置・複製に効く）
    float snapStep_ = 1.0f;

    int paletteMode_ = 0; // アセットパレットの動作 0=新規配置 1=選択中の配置物へ差し替え

    float dragRawZ_ = 0.0f; // Shift+ドラッグ(奥行き移動)中のスナップ前のZ累積値
#endif
};

} // namespace engine::game
