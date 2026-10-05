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
#include "LevelGraphRunner.h"
#include "LevelLoader.h"
#include "StageEditorContentFactory.h"
#include "StageEditorEventConnection.h"
#include "StageEditorViewport.h"
#include "TriggerVolume.h"
#include <functional>
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
class FontRenderer;
class UILayout;
class StageEditorSelectionService;
class StageEditorHierarchyPanel;
class StageEditorInspectorPanel;
enum class WeaponType; // Weapon.h で定義

/** @brief kind=="enemy_basic"かつ武器種別を持つ配置物1件ぶんの参照（GetCombatEnemies()の戻り値） */
struct CombatEnemyRef {
    std::string name;
    WeaponType weaponType;
    bool isStageBoss = false;
    EnemyEntity* enemy = nullptr; // 非所有。StageEditorのobjects_が生存させる
};

/** @brief kind=="breakable"の配置物1件ぶんの参照（GetBreakables()の戻り値。ヒット判定と破壊処理はシーン側が行う） */
struct BreakableRef {
    const ObjectDesc* desc = nullptr; // 非所有。HP・爆風半径・ダメージ量の設定元
    Vector3 position = { }; // 親チェーン解決済みのワールド位置
    int* hp = nullptr; // 残りHP（シーン側が減らす）
    bool* destroyed = nullptr; // trueにすると以降は描画・判定から外れる
    engine::graphics::Object3d* object = nullptr; // 非所有。脈動色などの見た目更新用
};

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
    // 編集パネルと中央ビューの操作判定で共有するレイアウト定数。
    // 画面座標のテキストがパネルに隠れるかの判定にも使う。
    static constexpr float kToolbarHeight = 82.0f;
    static constexpr float kLeftPanelWidth = 280.0f; // ヒエラルキー＋アセットパレットの列
    static constexpr float kRightPanelWidth = 300.0f; // 詳細設定(インスペクタ)の列

    // ステータスバーへ出すメッセージの表示秒数（内容の重要度に応じて使い分ける）
    static constexpr float kStatusBriefSeconds = 1.5f;
    static constexpr float kStatusShortSeconds = 2.0f;
    static constexpr float kStatusNormalSeconds = 3.0f;
    static constexpr float kStatusLongSeconds = 4.0f;
    static constexpr float kStatusVeryLongSeconds = 5.0f;

    // unique_ptr<KnightEnemy>/<EnemyEntity>をObjectEntryが持つため、それらの完全な定義が無い
    // 翻訳単位（BaseScene経由でStageEditorを持つ全シーン等）でも安全にコンパイルできるよう、
    // コンストラクタ/デストラクタは両方とも.cpp側（KnightEnemy.h/EnemyEntity.hをインクルード済みの場所）で
    // 定義する（暗黙生成に任せると、生成先の翻訳単位でobjects_絡みの完全性チェックが走ってしまうため）
    /** @brief 空のステージエディタを構築する */
    StageEditor();
    /** @brief 保持しているレベル実体と外部参照を破棄する */
    ~StageEditor();

    /**
     * @brief 現在レベルを開いているStageEditorを返す（ノードグラフの配置物操作ノードが名前引きに使う）
     * @return Open()済みのStageEditor。無ければnullptr
     * @note シーンは同時に1つしか動かないため、最後にOpen()したものを有効とする。Finalize()で解除する
     */
    static StageEditor* GetActive() { return activeEditor_; }

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
     * @brief 配置物の見た目モデルをシャドウマップへ描く
     * @note シーンのシャドウパス内（ModelCommon::BeginShadowPass()の後）で呼ぶこと。敵は対象外
     */
    void DrawObjectShadows();

    /**
     * @brief kind=="ui_text"の配置物をFontRendererへ描画コマンドとして積む
     * @param font 呼び出し元シーンが所有するFontRendererfontRenderer_.Reset()後、Draw()前に呼ぶこと
     * @note textSpace=="world"の配置物はcamera_の現在位置を基準にスクリーン座標へ投影する（SceneShared::WorldToScreenと同じ変換）
     */
    void DrawUIText(FontRenderer& font) const;

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
     * @brief 武器種別が設定されたkind=="enemy_basic"の配置物一覧を返す（毎フレーム呼ぶ想定）
     * @return エディタが所有する生存期間限定のEnemyEntityポインタと武器種別/ボス指定の一覧
     * @note 倒して奪取できる武器持ち敵をコード側で決め打ちせず、レベル側の配置とInspector設定だけで
     * 増減・変更できるようにするための仕組み（GetKnights()と同じ規約）
     */
    std::vector<CombatEnemyRef> GetCombatEnemies() const;

    /**
     * @brief kind=="breakable"で未破壊の配置物一覧を返す（毎フレーム呼ぶ想定）
     * @return HP/破壊フラグへの可変参照を含む一覧。ヒット判定と爆風ダメージはシーン側が行う
     * @note 破壊時はシーン側がdestroyedをtrueにし、あわせてGameFlagsのbroken_<name>を立てること
     */
    std::vector<BreakableRef> GetBreakables();

    /**
     * @brief kind=="pickup"の回収状況を返す（HUDの「x / y」表示用）
     * @param outCollected 回収済み個数
     * @param outTotal 有効な収集物の総数
     */
    void GetPickupCounts(int& outCollected, int& outTotal) const;

    // 名前引きの配置物操作（ノードグラフのSetObjectVisible/TeleportObject/MoveObject/SetObjectEnabledノードから使う）
    /** @brief 配置物の表示/非表示を切り替える（レベルJSONには保存しない一時状態） @return 名前が見つかればtrue */
    bool SetObjectVisibleByName(const std::string& name, bool visible);
    /** @brief 配置物のローカル位置を即座に書き換える @return 名前が見つかればtrue */
    bool TeleportObjectByName(const std::string& name, const Vector3& position);
    /** @brief 配置物のローカル位置をseconds秒かけて目標へ補間移動させる（0以下なら即座） @return 名前が見つかればtrue */
    bool MoveObjectByName(const std::string& name, const Vector3& target, float seconds);
    /** @brief 配置物の有効/無効を上書きする（activationFlagより優先。レベルJSONには保存しない） @return 名前が見つかればtrue */
    bool SetObjectEnabledByName(const std::string& name, bool enabled);
    /** @brief 配置物のワールド位置を返す @return 名前が見つかればtrue */
    bool FindObjectWorldPosition(const std::string& name, Vector3& outPosition) const;

    /** @brief レベルに紐付いたグラフ実行（常駐グラフ＋フラグ起動グラフ）の状態を返す */
    const LevelGraphRunner& GetLevelGraphs() const { return levelGraphs_; }

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
    bool UsesGameWindow() const { return visible_ && !viewportFocusMode_; }
    void SetGamePreview(uint64_t textureId, float u0, float v0, float u1, float v1)
    {
        previewTextureId_ = textureId;
        previewU0_ = u0; previewV0_ = v0; previewU1_ = u1; previewV1_ = v1;
    }

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
    void RegisterExternalEntity(const std::string& name, Vector3* position,
        std::function<int()> getVisualPreset = { },
        std::function<void(int)> setVisualPreset = { },
        std::function<void(const std::string&, const std::string&)> setStaticVisualModel = { },
        std::function<std::string()> getStaticVisualModel = { },
        std::function<std::string()> getStaticVisualTexture = { });
    /**
     * @brief トリガーのspawnsWaterSplashが成立した瞬間に呼ぶコールバックを登録する
     * @note 水しぶきの実体（パーティクル発生）はシーン側の責務のため、StageEditorはここでは何もしない
     */
    void SetWaterSplashCallback(std::function<void(const Vector3&)> callback) { onWaterSplashRequested_ = std::move(callback); }

private:
    enum class SelKind { None,
        Object,
        Trigger,
        External };

    // 1オブジェクト定義ぶんの編集単位（"row"は複数インスタンスを1エントリにまとめる）
    // kind=="prop"ならinstancesを使い、kindがenemy系ならknight/enemyのどちらかだけが生成される
    /** @brief 配置物1件ぶんの編集データと、生成済みランタイム実体（見た目のみ/ナイト/汎用敵のいずれか）を束ねる */
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
        bool fallFloorWasSolid = true; // "fall"ギミック用: 直前フレームの床の有無（崩落/復帰の瞬間だけ砂ぼこりを出す判定に使う）
        bool healChanneling = false; // healer用: 詠唱（回復発動までのタメ）中かどうか
        int healChannelHpAtStart = 0; // healer用: 詠唱開始時のHP。詠唱中に減ったら被弾＝中断とみなす
        bool pickupCollected = false; // pickup用: 回収済みなら描画・判定から外す
        int breakableHp = 0; // breakable用: 残りHP（RegenerateInstancesでdesc.breakableHpから初期化）
        bool breakableDestroyed = false; // breakable用: 破壊済みなら描画・判定から外す
        bool visibleOverride = true; // グラフのSetObjectVisibleで切り替える一時的な表示状態
        int enabledOverride = -1; // グラフのSetObjectEnabledによる上書き（-1: 無し / 0: 無効 / 1: 有効）
        bool graphMoveActive = false; // グラフのMoveObjectによる補間移動中か
        Vector3 graphMoveFrom = { };
        Vector3 graphMoveTo = { };
        float graphMoveTimer = 0.0f;
        float graphMoveDuration = 0.0f;
    };

    /** @brief ギミックの一時変形量（位置と回転のオフセット。保存対象の編集値には加えない） */
    struct GimmickOffset {
        Vector3 position = { };
        Vector3 rotation = { };
    };
    /**
     * @brief ギミック種別と経過時間から現在フレームの一時変形量を求める（UpdateRuntimeEntryとGetSolidCollidersで共用）
     * @param timerOverride 負値なら entry.runtimeTimer をそのまま使う正値を渡すとその時刻として計算する
     * （GetSolidColliders()が、この後UpdateObjects()で進む今フレーム分のタイマーを先読みし、
     *   当たり判定と見た目の1フレームのズレ＝乗った時に浮いて見える現象を無くすために使う）
     */
    GimmickOffset ComputeGimmickOffset(const ObjectEntry& entry, float timerOverride = -1.0f) const;
    /** @brief pickup配置物の回収判定と演出（プレイヤーが半径内に入ったら回収し、覚醒ゲージを増やす） */
    void UpdatePickupEntry(ObjectEntry& entry, engine::graphics::ParticleManager* pm, const Vector3& playerPos);
    /** @brief グラフのMoveObjectによる補間移動を1フレーム進める */
    void UpdateGraphMove(ObjectEntry& entry, float dt);
    /** @brief 名前から配置物を探す（無ければnullptr） */
    ObjectEntry* FindEntryByName(const std::string& name);

    /**
     * @brief モデル+テクスチャの組み合わせをキャッシュから探し、無ければロードして登録する
     * @param modelPath OBJ ファイルパス
     * @param texPath テクスチャパス
     * @return キャッシュ済み、または新規ロードした Model へのポインタ
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
    void RenderGameViewport();
    /** @brief 編集停止とゲーム動作テストを切り替えて時間倍率を同期する */
    void SetPlayTestMode(bool enabled);

    /** @brief 階層パネルの実際の編集内容を描画する（StageEditorHierarchyPanel::Renderへ委譲） */
    void RenderHierarchy();
    /** @brief 中央シーンビューの上部に編集モードと補助パネルの操作を表示する */
    void RenderEditorToolbar();
    /** @brief ゲーム画面を広く確認するための最小操作バーを表示する */
    void RenderViewportFocusBar();
    /** @brief 詳細パネルの実際の編集内容を描画する（StageEditorInspectorPanel::Renderへ委譲） */
    void RenderInspector();
    /** @brief モデル/テクスチャをプリセットから選んで置ける一覧パネル選択中の配置物があればそれに適用、無ければ新規追加する */
    void RenderAssetPalette();
    /** @brief GameFlagsの現在値一覧とチェックポイントの追加/一覧を表示する */
    void RenderFlagsPanel();
    /** @brief プレハブ、検証、自動保存、編集とテストの切り替えをまとめて表示する */
    void RenderWorkflowPanel();
    /** @brief トリガーと配置対象を選ぶだけでイベント接続を構築する */
    void RenderNoCodeEventPanel();
    /** @brief レベルに紐付ける常駐グラフとフラグ起動グラフの一覧を編集する */
    void RenderGraphPanel();
    /** @brief 敵Wave用のSpawnPoint群を表形式の設定から生成する */
    void RenderWavePanel();
    /** @brief 配置・接続・到達性の問題を解析して一覧表示する */
    void RenderStageAnalysisPanel();
    /** @brief 最後に保存した状態との差分を一覧表示する */
    void RenderDiffPanel();
    /** @brief 制作手順と確認項目をエディタ内に表示する */
    void RenderEditorHelpPanel();
    /** @brief チェックポイント・トリガー・配置物・イベント接続線・外部エンティティの補助表示（十字・AABB・接続線）を描画する */
    void DrawGizmos();

    /** @brief 画面中央(z=0平面)に新規の配置物(prop)を1つ追加して選択状態にする（+ボタン/アセットパレット共通） */
    void AddPropAtScreenCenter(const std::string& model, const std::string& texture);
    /** @brief WASD(+QEで奥/手前)でカメラを移動するImGuiのテキスト入力中は無効化する */
    void UpdateFreeCamera(engine::Input* input, float dt);

    /** @brief 3Dビュー上での左クリック選択とドラッグ移動（ImGuiウィンドウ上のマウスは無視する） */
    void UpdateViewportInteraction();
    // UpdateViewportInteraction()の下請け（責務ごとに分割）
    /**
     * @brief クリック位置に最も近いオブジェクト/トリガー/外部エンティティを探す（画面40px以内、モデル外形にヒットすれば距離0扱い）
     * @param mouseX,mouseY 判定するスクリーン座標
     * @param outKind 見つかった対象の種別（見つからなければSelKind::Noneのまま）
     * @param outIdx  見つかった対象のインデックス（見つからなければ-1のまま）
     * @return 何かヒットしたか
     */
    bool PickViewportTarget(float mouseX, float mouseY, SelKind& outKind, int& outIdx) const;
    /** @brief 左クリック時の選択処理（親子リンク待機中ならその接続、それ以外は選択+ドラッグ開始準備）を行う */
    void HandleViewportClick(float mouseX, float mouseY);
    /** @brief ドラッグ中の選択物を移動させる（Shift中はマウス垂直移動をZ移動、それ以外はXY平面移動） */
    void UpdateViewportDrag(float mouseX, float mouseY);

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

    /** @brief 編集カメラを指定ワールド位置が画面中央に来るよう移動する（奥行きは維持） */
    void FocusCameraOn(const Vector3& worldPosition);
    /** @brief 現在の選択物（配置物/トリガー/外部エンティティ）へカメラを寄せる。未選択ならプレイヤーへ */
    void FocusCameraOnSelection();
    /**
     * @brief プレイヤー実体を指定位置へ移し、テストモードを開始する（配置した場所から即プレイして確かめる用）
     * @return RegisterExternalEntityで"Player"が登録されていなければfalse
     */
    bool StartPlayTestAt(const Vector3& worldPosition);
    /** @brief 選択中の配置物・トリガーを矢印キーで少しずつ動かす（スナップONならその間隔、OFFなら固定の微動量） */
    void NudgeSelection(float dx, float dy, float dz);
    /** @brief スナップONの間、z=0平面にスナップ間隔のグリッド線を描く */
    void DrawGridOverlay();
    /** @brief 画面中央のz=0平面上のワールド座標を返す（テンプレート生成の基準位置） */
    Vector3 ViewCenterOnGround() const;

    // ── ビューポート直接操作（画面上のハンドル、右クリックメニュー、範囲選択、親子付け）──
    /** @brief 変形ツール（W: 移動 / E: 回転 / R: 拡縮）。ハンドルの見た目とドラッグの意味が変わる */
    enum class TransformTool { Move,
        Rotate,
        Scale };
    /**
     * @brief 選択物のワールド位置を返す（配置物/トリガー/外部エンティティ共通）
     * @return 何も選択していなければfalse
     */
    bool SelectionWorldPosition(Vector3& outWorld) const;
    /**
     * @brief クリック位置が選択物のハンドル（軸の矢印・回転リング・中央の四角）に乗っているか判定する
     * @param outAxis 乗っていた軸（1: X / 2: Y / 3: Z / 0: 中央）
     * @param outUniform 拡縮ツールで中央を掴んだ（全軸同時）ならtrue
     * @return ハンドルを掴んだならtrue
     */
    bool PickTransformHandle(float mouseX, float mouseY, int& outAxis, bool& outUniform) const;
    /** @brief 選択物にツールごとのハンドルを画面固定サイズで描く（DrawGizmosから呼ぶ） */
    void DrawTransformHandles();
    /** @brief 回転/拡縮ツールのドラッグを1フレームぶん反映する（マウス移動量のピクセル） */
    void UpdateRotateScaleDrag(float deltaX, float deltaY);
    /** @brief 範囲選択の矩形を確定し、内側の配置物を選択する（小さすぎる矩形はクリック扱いで選択解除） */
    void FinishBoxSelect(float mouseX, float mouseY);
    /**
     * @brief 画面UI（UILayoutの位置項目）の印をクリック位置から探す
     * @param outLayout 見つかった項目のレイアウト
     * @param outIndex 見つかった項目のレイアウト内番号
     * @return 印の上をクリックしていたらtrue
     */
    bool PickUILayoutHandle(float mouseX, float mouseY, UILayout*& outLayout, size_t& outIndex) const;
    /** @brief 画面UIの位置項目を四角い印と名前で描く（DrawGizmosから呼ぶ） */
    void DrawUILayoutHandles();
    /** @brief 右クリックメニューを描く（RenderEditorPanelsから毎フレーム呼ぶ） */
    void RenderViewportContextMenu();
    /**
     * @brief 種類に応じた既定値で配置物を指定位置へ追加し、選択する
     * @param kind "prop" "pickup" "breakable" "enemy_basic" "spawn_point" "gimmick" "camera_point" "patrol_point" "ui_text"
     * @return 追加した配置物の添字
     */
    int AddObjectAt(const std::string& kind, const Vector3& position);
    int AddUITextAt(const Vector3& position, bool screenSpace = true);
    /** @brief トリガーを指定位置へ追加して選択する */
    void AddTriggerAt(const Vector3& position);
    /**
     * @brief 見た目の位置を変えずに親を付け替える
     * @param childIndex 子にする配置物
     * @param parentIndex 親にする配置物（-1で親を外す）
     */
    void SetParentPreservingWorld(int childIndex, int parentIndex);
    /** @brief 全配置物を選択する（Ctrl+A） */
    void SelectAllObjects();
    /** @brief 選択を解除する（Escape） */
    void ClearSelection();

    TransformTool transformTool_ = TransformTool::Move;
    int activeDragAxis_ = 0; // ハンドルを掴んだドラッグ中の軸（0: 自由 / 1: X / 2: Y / 3: Z）。離すと0に戻る
    bool rotateDragging_ = false; // 回転ツールのドラッグ中か
    bool scaleDragging_ = false; // 拡縮ツールのドラッグ中か
    bool scaleUniform_ = false; // 拡縮ドラッグが全軸同時か
    bool boxSelecting_ = false; // 何も無い場所からの左ドラッグで範囲選択中か
    float boxStartX_ = 0.0f;
    float boxStartY_ = 0.0f;
    SelKind hoverKind_ = SelKind::None; // マウス直下の対象（ハイライト表示用）
    int hoverIndex_ = -1;
    bool contextMenuRequested_ = false; // 右クリックの直後、次のRenderでメニューを開く
    SelKind contextKind_ = SelKind::None;
    int contextIndex_ = -1;
    Vector3 contextWorldPos_ = { }; // 右クリックした地面位置（生成メニューの配置先）
    Vector3 contextScreenPos_ = {};
    bool focusTextEditor_ = false;

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
    /** @brief RegisterExternalEntity()で登録された、レベルJSON外のランタイム実体（Player等）への参照1件 */
    struct ExternalEntityRef {
        std::string name;
        Vector3* position = nullptr;
        std::function<int()> getVisualPreset;
        std::function<void(int)> setVisualPreset;
        std::function<void(const std::string&, const std::string&)> setStaticVisualModel; // (モデルパス, テクスチャパス)
        std::function<std::string()> getStaticVisualModel;
        std::function<std::string()> getStaticVisualTexture;
    };
    std::vector<ExternalEntityRef> externalEntities_;

    // トリガーのspawnsWaterSplash成立時にシーン側の演出を呼ぶためのコールバック（SetWaterSplashCallback()で登録）
    std::function<void(const Vector3&)> onWaterSplashRequested_;

    // 実体はModelManagerが所有・共有する（GetOrLoadModel()参照）。ここは非所有の高速参照用
    std::map<std::string, engine::graphics::Model*> modelCache_;

    Vector3 playerSpawn_ = { };
    Vector3 enemySpawn_ = { };

    // レベルに紐付いたノードグラフ（レベルJSONのgraphPath/flagGraphs）。実行はlevelGraphs_が担う
    std::string graphPath_;
    std::vector<FlagGraphBinding> flagGraphs_;
    LevelGraphRunner levelGraphs_;

    static inline StageEditor* activeEditor_ = nullptr; // GetActive()用。Open()で設定、Finalize()で解除

    // F2で表示/非表示（GraphEditorのF1と違い、ゲーム画面を隠さない小窓パネル構成）
    bool visible_ = false;
    bool viewportFocusMode_ = false; // 編集パネルを隠してゲーム画面とギズモの確認領域を広げる
    uint64_t previewTextureId_ = 0;
    float previewU0_ = 0.0f, previewV0_ = 0.0f, previewU1_ = 1.0f, previewV1_ = 1.0f;
    bool playTestMode_ = false; // パネルを表示したままゲームを動かすテスト状態を保持する
    float savedTimeScale_ = 1.0f;
    bool showUIText_ = true; // falseなら編集中だけui_textのマーカーと実表示を隠す（配置物の陰になって邪魔な時用）

    SelKind selKind_ = SelKind::None;
    int selIndex_ = -1;
    std::vector<int> selectedObjectIndices_; // Ctrl選択した配置物を一括削除・複製するために保持する

    int nextSerial_ = 0; // 新規オブジェクト/トリガーの名前生成用

    bool viewportDragging_ = false; // 3Dビュー上で選択物をドラッグ移動中か

    UILayout* uiDragLayout_ = nullptr; // ドラッグ中の画面UI位置項目（nullptrならUIを掴んでいない）
    size_t uiDragIndex_ = 0;
    float uiDragGrabOffsetX_ = 0.0f; // 掴んだ瞬間の(項目の位置 - マウスの画面座標)
    float uiDragGrabOffsetY_ = 0.0f;

    bool objectsDrawnThisFrame_ = false; // DrawObjects()の二重呼び出し防止用（UpdateObjects()で毎フレームリセット）

    std::string statusMessage_;
    float statusTimer_ = 0.0f;

    /** @brief 現在の編集内容を指定パスへ書き出す */
    void SaveToPath(const std::string& path) const;

    /** @brief 読み込み済みレベルの実体を依存関係に沿った順序で破棄する */
    void ReleaseLevelResources(bool releaseExternalEntities);

    /** @brief 配置物を今フレーム描くか（回収済み・破壊済み・点滅で消えている間・落下中の床などは描かない） */
    bool IsEntryDrawable(const ObjectEntry& entry) const;


    /**
     * @brief このレベルが自分で立てるフラグ（トリガーのflag、condition_<名前>、pickup_<名前>、broken_<名前>）をfalseへ戻す
     * @note GameFlagsはシーンをまたいで残るため、同じレベルを再度開いた時に前回の進行状態（区画到達・回収済み等）が
     * 持ち越されないようにする。他レベルやグラフが立てた進行フラグには触れない（Open()から呼ぶ）
     */
    void ResetLevelLocalFlags(const LevelData& data);

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
        std::string graphPath;
        std::vector<FlagGraphBinding> flagGraphs;
    };
    /**
     * @brief 現在の配置物・トリガー・チェックポイント・スポーン位置からUndo用スナップショットを構築する
     * @return Undo/Redoスタックへ積む、descのみのコピー
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
    /** @brief Undoスタックから1つ前の状態へ戻す（Ctrl+Z） */
    void Undo();
    /** @brief Redoスタックから1つ先の状態へ進める（Ctrl+Y） */
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
    char paletteSearch_[96] = {}; // 素材名の検索語

    float dragRawZ_ = 0.0f; // Shift+ドラッグ(奥行き移動)中のスナップ前のZ累積値
    // クリックした瞬間にオブジェクト原点へ位置が飛ばないよう、掴んだ時の
    // (オブジェクト位置 - マウスの接地位置)のオフセットを保持し、ドラッグ中はこれを保ったまま追従させる
    float dragGrabOffsetX_ = 0.0f;
    float dragGrabOffsetY_ = 0.0f;
    int gizmoAxis_ = 0; // 0は自由移動、1から3はX・Y・Z軸へ移動を制限する
    int parentLinkChildIndex_ = -1; // マウス親子リンクで親のクリックを待っている子

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
    bool showGraphPanel_ = false;
    bool showGrid_ = true; // スナップON時にグリッド線を描くか
    int levelFileIndex_ = -1; // レベル切替コンボの選択位置（-1は未選択）
    char newLevelName_[64] = "level03";
    char templateGuideText_[128] = "案内文をここに";
    char templateDoorCondition_[96] = "condition_room_clear";
    int templateWallWeapon_ = 3; // 壊せる壁テンプレの武器（kWeaponTypesの添字、既定はHammer）
    bool showWavePanel_ = false;
    char graphPathBuffer_[160] = { };
    char newFlagGraphFlag_[64] = { };
    char newFlagGraphPath_[160] = "Resources/Graphs/";
    bool helpChecklist_[5] = { false, false, false, false, false };
#endif
};

} // namespace engine::game
