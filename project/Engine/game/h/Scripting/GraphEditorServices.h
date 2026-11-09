/**
 * @file GraphEditorServices.h
 * @brief グラフ編集の入力処理とノード表示を分担するファイル
 */
#pragma once
#ifdef USE_IMGUI
#include <imgui.h>
#include "GraphTypes.h"
#include <string>
namespace engine {
class Input;
}
namespace engine::game {
/** @brief グラフ値入力の結果とUndoの確定に必要な操作状態 */
struct GraphValueEditResult {
    GraphValue value;
    bool changed = false;
    bool activated = false;
    bool deactivated = false;
    bool discrete = false;
};

/** @brief グラフ値の型に対応する入力欄を表示し、編集結果を値として返す */
GraphValueEditResult DrawGraphValueInput(const char* label, const GraphValue& value);

/** @brief 指定ノードへ向かう実行の配線をすべて外す（外した物があれば true） */
bool ClearExecLinksTo(GraphDesc& graph, const std::string& nodeId);
/** @brief 指定ノードを供給元にするデータ配線をすべて外す（外した物があれば true） */
bool ClearDataLinksFrom(GraphDesc& graph, const std::string& nodeId);

/**
 * @brief ノード名とパラメータ名の日本語表示を引く（Resources/Config/UI/node_labels.json）
 * @details 表示だけを置き換え、グラフJSONに保存する type 名とパラメータ名は変えない。表に無いものは元の名前を返す
 */
class GraphNodeLabels {
public:
    /** @brief ノード種別の表示名を返す */
    static std::string TypeName(const std::string& type);
    /** @brief ノードのパラメータの表示名を返す */
    static std::string ParamName(const std::string& type, const std::string& param);
    /** @brief 表を読み直す（JSONを書き換えた後にエディタから呼ぶ） */
    static void Reload();
};

class GraphEditor;
struct GraphNode;
enum class GraphValueType;

/**
 * @brief グラフ描画で共有する寸法、色、当たり判定を提供する
 * @details キャンバス描画とノード描画の見た目および判定規則を一元管理する
 */
class GraphEditorDrawingStyle {
public:
    static constexpr const char* kExecInPinName = "in";
    static constexpr float kBaseNodeWidth = 200.0f;
    static constexpr float kFallbackNodeHeight = 120.0f; // まだ描いていないノードの高さの目安
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

    // ノードとピンの配置（ズーム1倍時の寸法）
    static constexpr float kPinRowOffset = 10.0f; // ノード上端（下端）から実行ピンまでの距離
    static constexpr float kTruePinHeightRatio = 0.33f; // 分岐ノードのtrueピンを置く高さ（ノード高さ比）
    static constexpr float kFalsePinHeightRatio = 0.66f;
    static constexpr float kDataPinRadiusRatio = 0.75f; // 実行ピンに対するデータピンの大きさ
    static constexpr float kPinHoverPadding = 3.0f; // ピンの見た目より広く取る当たり判定の余白
    static constexpr float kMinPinGrabRadius = 11.0f; // ズームしても下回らないピンの掴み半径（画面ピクセル）
    static constexpr float kLinkSnapRadius = 28.0f; // 配線中にピンへ吸着する距離（画面ピクセル）
    static constexpr float kPinRingPadding = 3.0f; // 接続できるピンに付ける輪の余白
    static constexpr float kPinRingThickness = 2.0f;
    static constexpr ImU32 kPinRingColor = IM_COL32(120, 220, 255, 220);
    static constexpr ImU32 kPinSnapColor = IM_COL32(255, 255, 255, 255);
    static constexpr float kWireHoverDistance = 6.0f; // カーソルが配線に乗ったとみなす距離（画面ピクセル）
    static constexpr int kWireHoverSamples = 24; // 配線の当たり判定に使う折れ線の分割数
    static constexpr float kWireHoverThickness = 4.0f;
    static constexpr ImU32 kWireHoverColor = IM_COL32(255, 170, 90, 255);
    static constexpr float kLinkThickness = 2.5f;
    static constexpr float kNodeCornerRadius = 4.0f;
    static constexpr float kNodeBoxPadding = 6.0f;
    static constexpr float kNodeTopPaddingRatio = 0.67f; // 上側の余白（左右下の余白比）
    static constexpr float kRunningBorderThickness = 3.5f;
    static constexpr float kNodeLabelGap = 2.0f;
    static constexpr float kNodeBorderThickness = 2.0f;
    static constexpr float kDataLinkThickness = 2.0f;

    // ノード描画のチャンネル（コメント → ノードの背景 → ノードの中身の順に重なる）
    static constexpr int kChannelCount = 3;
    static constexpr int kCommentChannel = 0;
    static constexpr int kNodeBackChannel = 1;
    static constexpr int kNodeFrontChannel = 2;

    // コメント枠の掴み（ズームしても下回らない画面ピクセルの大きさ）
    static constexpr float kCommentHeaderHeight = 20.0f;
    static constexpr float kCommentHandleSize = 10.0f;
    static constexpr float kMinCommentHeaderPixels = 14.0f;
    static constexpr float kMinCommentHandlePixels = 16.0f;
    static constexpr float kCommentMinWidth = 80.0f;
    static constexpr float kCommentMinHeight = 60.0f;

    // 背景の格子とドラッグ中のスナップ
    static constexpr float kGridStep = 40.0f; // グラフ座標での格子の間隔（Ctrl+ドラッグのスナップにも使う）
    static constexpr float kMinGridPixels = 16.0f; // これより細かくなるズームでは格子を間引く
    static constexpr ImU32 kGridColor = IM_COL32(255, 255, 255, 16);

    // 端へ寄せた時の自動スクロール
    static constexpr float kEdgePanMargin = 28.0f; // 端からこのピクセル以内で流し始める
    static constexpr float kEdgePanSpeed = 700.0f; // 流す速さの最大（画面ピクセル/秒）

    // 範囲選択の枠
    static constexpr ImU32 kBoxSelectFillColor = IM_COL32(120, 180, 255, 40);
    static constexpr ImU32 kBoxSelectBorderColor = IM_COL32(120, 180, 255, 220);

    // 全体表示
    static constexpr float kFrameMargin = 60.0f; // 全体表示の時に周囲へ空ける余白（グラフ座標）
    static constexpr float kMaxFrameZoom = 1.0f; // 全体表示でこれ以上は拡大しない

    // 右側の説明パネル
    static constexpr float kHelpPanelWidth = 330.0f;

    // キャンバスのズーム
    static constexpr float kZoomStep = 0.1f; // ホイール1目盛りあたりの変化量
    static constexpr float kMinZoom = 0.3f;
    static constexpr float kMaxZoom = 2.5f;

    // ツールバー
    static constexpr float kGraphPathFieldWidth = 360.0f;
    static constexpr ImVec4 kRunningTextColor = { 0.5f, 1.0f, 0.6f, 1.0f };

    /**
     * @brief 値型に対応するピン色を返す
     * @param type グラフ値の型
     * @return 型に対応するRGBA色
     */
    static ImU32 ColorForType(GraphValueType type);

    /** @brief 点が矩形の中にあるか調べる */
    static bool RectContains(const ImVec2& min, const ImVec2& max, const ImVec2& point) {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }
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
