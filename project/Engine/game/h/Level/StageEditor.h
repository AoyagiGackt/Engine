/**
 * @file StageEditor.h
 * @brief レベル実体、編集パネル、中央ビュー制御を統括するステージエディタ
 * @note オブジェクトの生成・毎フレームUpdate/Drawは通常ビルドでも動くレベルの実体そのものであり、
 * F2で開くImGuiパネル（Hierarchy/Inspector）だけがUSE_IMGUIビルド限定のデバッグ機能
 * ロジックはノードグラフ（GraphEditor）側の役目なので、ここではトリガーのフラグを立てるまでしかやらない
 */
#pragma once
#include "CollisionConfig.h"
#include "EditorHistory.h"
#include "LevelLoader.h"
#include "StageEditorContentFactory.h"
#include "StageEditorEventConnection.h"
#include "StageEditorViewport.h"
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
class StageEditorSelectionService;
class StageEditorHierarchyPanel;
class StageEditorInspectorPanel;

/**
 * @brief レベルデータの読み書きと配置物の実行および編集UIを統括する
 *
 * JSONに保存する編集データとゲーム中に動作する実体を対応付ける。
 * 編集入力と表示は専用サービスへ委譲し、配置物の所有権とライフサイクルを管理する。
 */
class StageEditor {
    friend class StageEditorSelectionService;
    friend class StageEditorHierarchyPanel;
    friend class StageEditorInspectorPanel;

public:
    // unique_ptr<KnightEnemy>/<EnemyEntity>をObjectEntryが持つため、それらの完全な定義が無い
    // 翻訳単位（BaseScene経由でStageEditorを持つ全シーン等）でも安全にコンパイルできるよう、
    // コンストラクタ/デストラクタは両方とも.cpp側（KnightEnemy.h/EnemyEntity.hをインクルード済みの場所）で
    // 定義する（暗黙生成に任せると、生成先の翻訳単位でobjects_絡みの完全性チェックが走ってしまうため）
    /** @brief 空のステージエディタを構築する */
    StageEditor();
    /** @brief 保持しているレベル実体と外部参照を破棄する */
    ~StageEditor();

    /**
     * @brief レベルJSONを読み込み、配置物とトリガーを生成する
     * @param levelPath 読み込むレベルJSONのパス
     * @param modelCommon 配置物のモデル生成に使用する共通処理
     * @param camera 編集ビューとカメラポイントに使用するカメラ
     */
    void Open(const std::string& levelPath, engine::graphics::ModelCommon* modelCommon, engine::graphics::Camera* camera);

    /**
     * @brief GPUの使用完了を待ってからレベル実体と外部参照を破棄する
     * @note 複数回呼んでも安全で、シーンの終了処理から明示的に呼び出す
     */
    void Finalize();

    /** @brief 現在の内容をOpenで指定したパスへ書き戻す */
    void Save();

    /**
     * @brief 毎フレーム呼ぶF2でパネルの表示/非表示を切り替える
     * @param input 編集操作に使用する入力
     * @param playerPos トリガー判定に使用するプレイヤー位置
     * @note トリガー判定（フラグを立てる処理）はパネルの表示状態に関係なく常に行う
     */
    void Update(engine::Input* input, const Vector3& playerPos);

    /**
     * @brief 生成済みオブジェクトのトランスフォームを反映する（Draw前に毎フレーム呼ぶ）
     * @param pm        敵エンティティのパーティクル演出に使う（enemy_knight配置が無ければnullptrでよい）
     * @param playerPos 敵のAIターゲットに使う（enemy_knight配置が無ければ既定値でよい）
     * @note enemy系配置は、エディタ表示中（IsVisible）は編集用に位置を固定し、非表示中は
     * 実際のAI/重力Update()を回す（PlayerRefreshVisualTransforms系と同じ編集中は静止規約）
     */
    void UpdateObjects(engine::graphics::ParticleManager* pm = nullptr, const Vector3& playerPos = { });

    /** @brief 生成済みオブジェクトを3D描画パスへ描画する */
    void DrawObjects();

    /**
     * @brief このフレーム中にDrawObjects()が呼ばれ済みかどうか
     * @return 描画済みの場合はtrue
     * @note Scene::Draw()内でHUDより前に自分でDrawObjects()を呼んだ場合、
     * BaseScene::Render()側の自動呼び出しをスキップしてブロックの二重描画/UI上乗せを防ぐために使う
     */
    bool WasObjectsDrawnThisFrame() const { return objectsDrawnThisFrame_; }

    /**
     * @brief 配置済みのナイト敵一覧を返す
     * @return エディタが所有する生存期間限定のナイト敵ポインタ一覧
     * @note 戦闘判定はシーン側が一覧を走査して行う
     */
    std::vector<KnightEnemy*> GetKnights();

    /**
     * @brief solid=trueのオブジェクトのワールドAABB一覧を返す（毎フレーム呼ぶ想定）
     * @return 現在の配置状態から構築したワールドAABB一覧
     * @note ブロックの追加・移動・削除がそのまま次フレームの当たり判定に反映される
     */
    std::vector<engine::AABB> GetSolidColliders() const;

    /**
     * @brief 編集UIが表示中か返す
     * @return 編集UIが表示中の場合はtrue
     */
    bool IsVisible() const { return visible_; }

    /**
     * @brief エディタ表示中にゲーム更新を停止すべきか返す
     * @return 編集停止モードの場合はtrue
     */
    bool ShouldPauseGame() const { return visible_ && !playTestMode_; }

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
    /**
     * @brief ObjectEntry に関する型を提供する
     * @details ObjectEntry が扱うデータと操作の責務をまとめる
     */
    struct ObjectEntry {
        ObjectDesc desc;
        std::vector<std::unique_ptr<engine::graphics::Object3d>> instances;
        std::unique_ptr<KnightEnemy> knight;
        std::unique_ptr<EnemyEntity> enemy;
        int patrolTargetIndex = 0; // patrolRoute上で現在向かっているWaypoint番号を保持する
        Vector3 authoredPosition = { }; // ギミック演出やテスト終了後に戻す編集時の基準位置
        float runtimeTimer = 0.0f; // 条件成立後の遅延と演出経過時間を保持する
        bool conditionWasMet = false;
        bool runtimeActive = true;
    };

    /**
     * @brief GetOrLoadModel の結果を取得する
     * @param modelPath 処理に使用する値
     * @param texPath 処理に使用する値
     * @return 処理結果
     */
    engine::graphics::Model* GetOrLoadModel(const std::string& modelPath, const std::string& texPath);

    /** @brief モデル/軸/個数など構造が変わったときの再構築（instances/knight/enemyを作り直す） */
    void RegenerateInstances(ObjectEntry& entry);
    /** @brief ファクトリが生成した配置データをレベル実体へ追加する */
    void AppendGeneratedContent(StageEditorGeneratedContent content);
    /** @brief 位置/回転/スケールだけを既存instancesへ反映する軽量パス（kind=="prop"専用） */
    void RefreshTransforms(ObjectEntry& entry);
    /**
     * @brief enemy系エントリ1つぶんの毎フレーム処理
     * @note エディタ表示中はdesc.positionを実体へ書き戻して静止表示（RefreshVisualTransforms相当）、
     * 非表示中は実体の本物のUpdate()（AI/重力）を回し、逆にdesc.positionへ現在地を書き戻す（表示専用、保存はしない）
     */
    void UpdateEnemyEntry(ObjectEntry& entry, engine::graphics::ParticleManager* pm, const Vector3& playerPos);

    /** @brief 削除・Open()の再読み込み・破棄の前に、enemy_basic配置分をEnemyRegistryから解除する（ダングリングポインタ防止） */
    void UnregisterEnemyEntity(const ObjectEntry& entry);
    /** @brief 配置物が所有する描画実体と敵実体を安全な順序で破棄する */
    void DestroyObjectRuntime(ObjectEntry& entry, bool waitForGpu);
    /** @brief enabledとactivationFlagを評価してゲーム側で有効な配置か返す */
    bool IsRuntimeActive(const ObjectDesc& desc) const;
    /** @brief ノーコード条件を評価して対応するゲームフラグへ反映する */
    void EvaluateEventConditions(float dt);
    /** @brief 有効化フラグと遅延から全配置物の実行状態を更新する */
    void UpdateRuntimeActivation(float dt);
    /** @brief 実行中の配置物一件へ種別固有の更新を適用する */
    void UpdateRuntimeEntry(ObjectEntry& entry, engine::graphics::ParticleManager* pm,
        const Vector3& playerPos, float dt);
    /** @brief 敵に設定された巡回ルートへ沿って位置を更新する */
    bool UpdatePatrol(ObjectEntry& entry);

    /** @brief F2による編集セッションの開始と終了を処理する */
    void UpdateEditorVisibility(engine::Input* input);
    /** @brief 編集操作のキーボードショートカットを処理する */
    void HandleEditorShortcuts();
    /** @brief 現在のレイアウト状態に応じて編集パネルを表示する */
    void RenderEditorPanels();
    /** @brief 編集停止とゲーム動作テストを切り替えて時間倍率を同期する */
    void SetPlayTestMode(bool enabled);

    void RenderHierarchy();
    /** @brief 階層パネルの実際の編集内容を描画する */
    /** @brief 中央シーンビューの上部に編集モードと補助パネルの操作を表示する */
    void RenderEditorToolbar();
    /** @brief ゲーム画面を広く確認するための最小操作バーを表示する */
    void RenderViewportFocusBar();
    void RenderInspector();
    /** @brief 詳細パネルの実際の編集内容を描画する */
    /** @brief モデル/テクスチャをプリセットから選んで置ける一覧パネル選択中の配置物があればそれに適用、無ければ新規追加する */
    void RenderAssetPalette();
    void RenderFlagsPanel();
    /** @brief プレハブ、検証、自動保存、編集とテストの切り替えをまとめて表示する */
    void RenderWorkflowPanel();
    /** @brief トリガーと配置対象を選ぶだけでイベント接続を構築する */
    void RenderNoCodeEventPanel();
    /** @brief 敵Wave用のSpawnPoint群を表形式の設定から生成する */
    void RenderWavePanel();
    /** @brief 配置・接続・到達性の問題を解析して一覧表示する */
    void RenderStageAnalysisPanel();
    /** @brief 最後に保存した状態との差分を一覧表示する */
    void RenderDiffPanel();
    /** @brief 制作手順と確認項目をエディタ内に表示する */
    void RenderEditorHelpPanel();
    void DrawGizmos();

    /** @brief 画面中央(z=0平面)に新規の配置物(prop)を1つ追加して選択状態にする（+ボタン/アセットパレット共通） */
    void AddPropAtScreenCenter(const std::string& model, const std::string& texture);
    /** @brief WASD(+QEで奥/手前)でカメラを移動するImGuiのテキスト入力中は無効化する */
    void UpdateFreeCamera(engine::Input* input, float dt);

    /** @brief 3Dビュー上での左クリック選択とドラッグ移動（ImGuiウィンドウ上のマウスは無視する） */
    void UpdateViewportInteraction();

    /** @brief マウススクリーン座標をゲーム平面(z=0)上のワールド座標へ変換する */
    bool MouseToGround(float mouseX, float mouseY, Vector3& outWorld) const;

    /** @brief 親チェーンを解決したワールド位置を返す（親のpositionを順に加算循環は深さ上限で打ち切り） */
    Vector3 WorldPositionOf(const ObjectDesc& desc) const;
    /** @brief 親のワールド位置を返す（親なしなら原点）ドラッグ時のローカル座標逆算に使う */
    Vector3 ParentWorldPositionOf(const ObjectDesc& desc) const;
    /** @brief candidateName が selfName の子孫かどうか（親に設定すると循環になる相手の判定） */
    bool IsDescendantOf(const std::string& candidateName, const std::string& selfName) const;

    /** @brief 空の名前・重複した名前に一意な自動名を振る（Open直後に呼ぶ） */
    void EnsureUniqueNames();

    /** @brief Hierarchyツリーに1エントリ＋その子を再帰的に描く */
    void DrawHierarchyEntry(int index, int depthLevel);

    std::string levelPath_;
    engine::graphics::ModelCommon* modelCommon_ = nullptr;
    engine::graphics::Camera* camera_ = nullptr;
    StageEditorViewport viewport_;

    std::vector<ObjectEntry> objects_;
    std::vector<TriggerVolume> triggers_;
    std::vector<CheckpointDesc> checkpoints_;

    // レベルJSONに属さないランタイム実体（Player/Enemy等）への参照RegisterExternalEntity()で登録される
    /**
     * @brief ExternalEntityRef に関する型を提供する
     * @details ExternalEntityRef が扱うデータと操作の責務をまとめる
     */
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
    bool viewportFocusMode_ = false; // 編集パネルを隠してゲーム画面とギズモの確認領域を広げる
    bool playTestMode_ = false; // パネルを表示したままゲームを動かすテスト状態を保持する
    float savedTimeScale_ = 1.0f;

    enum class SelKind { None,
        Object,
        Trigger,
        External };
    SelKind selKind_ = SelKind::None;
    int selIndex_ = -1;
    std::vector<int> selectedObjectIndices_; // Ctrl選択した配置物を一括削除・複製するために保持する

    int nextSerial_ = 0; // 新規オブジェクト/トリガーの名前生成用

    bool viewportDragging_ = false; // 3Dビュー上で選択物をドラッグ移動中か

    bool objectsDrawnThisFrame_ = false; // DrawObjects()の二重呼び出し防止用（UpdateObjects()で毎フレームリセット）

    std::string statusMessage_;
    float statusTimer_ = 0.0f;

    /** @brief 現在の編集内容を指定パスへ書き出す */
    void SaveToPath(const std::string& path) const;

    /** @brief 読み込み済みレベルの実体を依存関係に沿った順序で破棄する */
    void ReleaseLevelResources(bool releaseExternalEntities);

#ifdef USE_IMGUI
    // Undo/Redo（GraphEditorと同じスナップショット方式、Ctrl+Z/Ctrl+Y）
    // ObjectEntryは実体(unique_ptr)を持ちコピーできないため、Save()の保存対象と同じdescだけを控え、
    // 復元時はRegenerateInstances()で実体を作り直す（modelCache_は生きているので再構築は軽い）
    struct LevelSnapshot {
        std::vector<ObjectDesc> objects;
        std::vector<TriggerDesc> triggers;
        std::vector<CheckpointDesc> checkpoints;
        Vector3 playerSpawn;
        Vector3 enemySpawn;
    };
    /**
     * @brief MakeSnapshot の結果を取得する
     * @return 処理結果
     */
    LevelSnapshot MakeSnapshot() const;
    /** @brief スナップショットの内容へ丸ごと戻す（敵のHPやトリガーの成立済み状態はリセットされる） */
    void ApplySnapshot(const LevelSnapshot& snap);
    /** @brief 現在の状態を即座にUndoスタックへ積む（追加/削除など単発で完結する変更の直前に呼ぶ） */
    void RecordUndoSnapshotNow();
    /** @brief ドラッグ/テキスト編集の開始時に変更前を仮記録するIsItemActivated()の直後に呼ぶ */
    void BeginUndoCapture();
    /** @brief BeginUndoCapture()後、実際に値が変わったことを記録する（変更が無ければCommit時に捨てられる） */
    void MarkUndoDirty();
    /** @brief ドラッグ/テキスト編集の終了時に呼ぶ実際に変化していた場合のみUndoスタックへ確定する */
    void CommitUndoCapture();
    void Undo();
    void Redo();

    /** @brief 選択中のオブジェクト/トリガーを削除する（削除ボタンとDeleteキー共用） */
    void DeleteSelected();
    /** @brief 選択中のオブジェクト/トリガーを複製して選択を移す（複製ボタンとCtrl+D共用） */
    void DuplicateSelected();
    /** @brief 選択中の配置物をエディタ内クリップボードへコピーする */
    void CopySelected();
    /** @brief エディタ内クリップボードの配置物を複製して貼り付ける */
    void PasteClipboard();

    /** @brief スナップ有効時、値をsnapStep_の倍数へ丸める（無効時はそのまま返す） */
    float SnapValue(float v) const;

    std::vector<ObjectDesc> objectClipboard_;
    LevelSnapshot lastSavedSnapshot_;
    EditorHistory<LevelSnapshot> history_;

    bool dirty_ = false; // 最後の保存以降に編集があるか（未保存マーカーと開く時の破棄確認に使う）

    char hierarchySearch_[64] = { };

    bool snapEnabled_ = false; // グリッドスナップ（ドラッグ移動・新規配置・複製に効く）
    float snapStep_ = 1.0f;

    int paletteMode_ = 0; // アセットパレットの動作 0=新規配置 1=選択中の配置物へ差し替え

    float dragRawZ_ = 0.0f; // Shift+ドラッグ(奥行き移動)中のスナップ前のZ累積値
    int gizmoAxis_ = 0; // 0は自由移動、1から3はX・Y・Z軸へ移動を制限する

    /** @brief 保存前検証を実行し、修正が必要な項目を返す */
    std::vector<std::string> ValidateLevel() const;
    /** @brief 未保存内容を一定間隔で復旧用ファイルへ退避する */
    void UpdateAutoSave(float realDt);
    /** @brief 選択中の配置物を名前付きプレハブへ保存する */
    void SaveSelectedPrefab();
    /** @brief 名前付きプレハブを画面中央へ生成する */
    void InstantiatePrefab();

    float autoSaveElapsed_ = 0.0f;
    static constexpr float kAutoSaveIntervalSeconds = 30.0f;
    bool autoSaveEnabled_ = true;
    bool recoveryAvailable_ = false;
    std::string recoveryPath_;
    std::vector<std::string> validationIssues_;
    char prefabName_[64] = "stage_part";
    StageEditorEventConnection eventConnection_;
    int validationFocusIndex_ = -1;
    char waveGroupName_[64] = "wave_1";
    int waveEnemyType_ = 0;
    int waveEnemyCount_ = 3;
    float waveSpacing_ = 2.0f;
    int waveStartTrigger_ = -1;
    bool showStageAnalysis_ = false;
    bool showSavedDiff_ = false;
    bool showEditorHelp_ = false;
    bool showFlagsPanel_ = false;
    bool showWorkflowPanel_ = false;
    bool showNoCodeEventPanel_ = false;
    bool showWavePanel_ = false;
    bool helpChecklist_[5] = { false, false, false, false, false };
#endif
};

} // namespace engine::game
