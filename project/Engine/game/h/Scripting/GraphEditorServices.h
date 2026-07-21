/**
 * @file GraphEditorServices.h
 * @brief グラフ編集の入力処理とノード表示を分担するファイル
 */
#pragma once
#ifdef USE_IMGUI
#include <imgui.h>
#include <string>
namespace engine {
class Input;
}
namespace engine::game {
class GraphEditor;
struct GraphNode;
enum class GraphValueType;

/**
 * @brief グラフ描画で共有する寸法、色、当たり判定を提供する
 * @details キャンバス描画とノード描画の見た目および判定規則を一元管理する
 */
class GraphEditorDrawingStyle {
public:
    static constexpr float kBaseNodeWidth = 200.0f;
    static constexpr float kBasePinRadius = 6.0f;
    static constexpr float kBasePinPad = 10.0f;
    static constexpr ImU32 kBackgroundColor = IM_COL32(45, 45, 55, 235);
    static constexpr ImU32 kSelectedBackgroundColor = IM_COL32(70, 70, 95, 235);
    static constexpr ImU32 kBorderColor = IM_COL32(90, 90, 110, 255);
    static constexpr ImU32 kStartColor = IM_COL32(90, 200, 120, 255);
    static constexpr ImU32 kInputPinColor = IM_COL32(230, 230, 230, 255);
    static constexpr ImU32 kOutputPinColor = IM_COL32(230, 200, 90, 255);
    static constexpr ImU32 kTruePinColor = IM_COL32(90, 200, 120, 255);
    static constexpr ImU32 kFalsePinColor = IM_COL32(210, 90, 90, 255);
    static constexpr ImU32 kRunningColor = IM_COL32(255, 210, 60, 255);

    /**
     * @brief 値型に対応するピン色を返す
     * @param type グラフ値の型
     * @return 型に対応するRGBA色
     */
    static ImU32 ColorForType(GraphValueType type);

    /**
     * @brief マウス座標が円形ピンの判定範囲内か調べる
     * @param center 円の中心座標
     * @param radius 判定半径
     * @return 範囲内ならtrue
     */
    static bool IsHoveringCircle(const ImVec2& center, float radius);
};

/**
 * @brief グラフエディタの入力を処理するサービス
 * @details 表示切替、ショートカット、キャンバス操作、ノード選択の更新を担当する
 */
class GraphEditorInteraction {
public:
    /**
     * @brief 1フレーム分の編集入力を処理する
     * @param editor 編集状態
     * @param input 入力管理。未指定の場合はキーボード入力を省略する
     * @return なし
     */
    static void Update(GraphEditor& editor, engine::Input* input);

private:
    static bool PrepareFrame(GraphEditor& editor, engine::Input* input, float& realDeltaTime);
    static void DrawGuidance(GraphEditor& editor);
    static void DrawToolbar(GraphEditor& editor);
    static void FinishFrame(GraphEditor& editor, float realDeltaTime);
};

/**
 * @brief グラフノードを描画するサービス
 * @details ノード本体、入出力ピン、値編集UI、選択枠の描画を担当する
 */
class GraphNodeRenderer {
public:
    /**
     * @brief ノード一件の表示と操作を処理する
     * @param editor 編集状態
     * @param drawList 描画先
     * @param origin キャンバス原点
     * @param id ノード識別子
     * @param node ノードデータ
     * @param state フレーム共有状態
     * @return なし
     */
    static void Draw(GraphEditor& editor, ImDrawList* drawList, const ImVec2& origin,
        const std::string& id, GraphNode& node, void* state);
};
} // namespace engine::game
#endif
