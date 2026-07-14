/**
 * @file StageEditor.h
 * @brief レベルJSON（配置オブジェクト＋トリガー）の読み込み・描画・実行時編集を1つにまとめたステージエディタ
 * @note オブジェクトの生成・毎フレームUpdate/Drawは通常ビルドでも動く「レベルの実体」そのものであり、
 * F2で開くImGuiパネル（Hierarchy/Inspector）だけがUSE_IMGUIビルド限定のデバッグ機能
 * ロジックはノードグラフ（GraphEditor）側の役目なので、ここではトリガーの「フラグを立てる」までしかやらない
 */
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "CollisionConfig.h"
#include "LevelLoader.h"
#include "TriggerVolume.h"

namespace engine {
class Input;
}
namespace engine::graphics {
class Model;
class ModelCommon;
class Object3d;
class Camera;
}

namespace engine::game {

class StageEditor {
public:
    /// @brief レベルJSONを読み込み、オブジェクト/トリガーを生成する
    void Open(const std::string& levelPath, engine::graphics::ModelCommon* modelCommon, engine::graphics::Camera* camera);

    /// @brief 現在の内容を Open() したパスへ書き戻す
    void Save();

    /**
     * @brief 毎フレーム呼ぶF2でパネルの表示/非表示を切り替える
     * @note トリガー判定（フラグを立てる処理）はパネルの表示状態に関係なく常に行う
     */
    void Update(engine::Input* input, const Vector3& playerPos);

    /// @brief 生成済みオブジェクトのトランスフォームを反映する（Draw前に毎フレーム呼ぶ）
    void UpdateObjects();

    /// @brief 生成済みオブジェクトを描画する（3Dパス中に毎フレーム呼ぶ）
    void DrawObjects();

    /**
     * @brief solid=trueのオブジェクトのワールドAABB一覧を返す（毎フレーム呼ぶ想定）
     * @note ブロックの追加・移動・削除がそのまま次フレームの当たり判定に反映される
     */
    std::vector<engine::AABB> GetSolidColliders() const;

    bool IsVisible() const { return visible_; }

private:
    // 1オブジェクト定義ぶんの編集単位（"row"は複数インスタンスを1エントリにまとめる）
    struct ObjectEntry {
        ObjectDesc desc;
        std::vector<std::unique_ptr<engine::graphics::Object3d>> instances;
    };

    engine::graphics::Model* GetOrLoadModel(const std::string& modelPath, const std::string& texPath);

    /// @brief モデル/軸/個数など構造が変わったときの再構築（instancesを作り直す）
    void RegenerateInstances(ObjectEntry& entry);
    /// @brief 位置/回転/スケールだけを既存instancesへ反映する軽量パス
    void RefreshTransforms(ObjectEntry& entry);

    void RenderHierarchy();
    void RenderInspector();
    void RenderFlagsPanel();
    void DrawGizmos();
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
    engine::graphics::Camera*      camera_      = nullptr;

    std::vector<ObjectEntry>   objects_;
    std::vector<TriggerVolume> triggers_;

    std::vector<std::unique_ptr<engine::graphics::Model>>  modelStorage_;
    std::map<std::string, engine::graphics::Model*>        modelCache_;

    Vector3 playerSpawn_ = {};
    Vector3 enemySpawn_  = {};

    // F2で表示/非表示（GraphEditorのF1と違い、ゲーム画面を隠さない小窓パネル構成）
    bool  visible_        = false;
    float savedTimeScale_ = 1.0f;

    enum class SelKind { None, Object, Trigger };
    SelKind selKind_  = SelKind::None;
    int     selIndex_ = -1;

    int nextSerial_ = 0; // 新規オブジェクト/トリガーの名前生成用

    bool viewportDragging_ = false; // 3Dビュー上で選択物をドラッグ移動中か

    std::string statusMessage_;
    float       statusTimer_ = 0.0f;
};

} // namespace engine::game
