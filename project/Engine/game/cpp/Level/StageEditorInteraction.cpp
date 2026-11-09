/**
 * @file StageEditorInteraction.cpp
 * @brief ステージエディタの3D操作（ギズモ描画・自由カメラ・マウスによる選択とドラッグ）を実装するファイル
 * @note StageEditor の3D操作だけを StageEditor.cpp から分けて置いている（クラスは StageEditor のまま）
 * （中央ビューの座標変換自体を担うStageEditorViewportクラスとは別物）
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "Camera.h"
#include "DiagnosticsDraw.h"
#include "EnemyEntity.h"
#include "Input.h"
#include "KnightEnemy.h"
#include "Matrix4x4.h"
#include "Object3d.h"
#include "UILayout.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#endif
#include "StageEditorUiStyle.h"
using namespace engine::graphics;

namespace engine::game {
#ifdef USE_IMGUI
/** @brief 複数の接続元が一致した場合も、トリガー優先・登録順で警告表示と接続線の判定を揃える */
bool StageEditor::FindEventSourcePosition(const std::string& flag, Vector3& position) const {
    for (const auto& trigger : triggers_) {
        if (trigger.GetDesc().flag == flag) {
            position = trigger.GetDesc().position;
            return true;
        }
    }
    if (flag.starts_with(kConditionFlagPrefix)) {
        const std::string conditionName = flag.substr(kConditionFlagPrefix.size());
        for (const auto& condition : objects_) {
            if (condition.desc.kind == objectKindName::kEventCondition && condition.desc.name == conditionName) {
                position = WorldPositionOf(condition.desc);
                return true;
            }
        }
    }
    return false;
}

namespace {
using stageEditorUiStyle::kZDragPerPixel;
using stageEditorUiStyle::kPickRadiusPx;
using stageEditorUiStyle::kSpawnMarkerHalf;
using stageEditorUiStyle::kMotionEndMarkerRadius;
using stageEditorUiStyle::kGizmoAxisCount;

// DrawGizmos()の十字マーカー半径（ワールド単位）非選択時と選択時でサイズを変え、選択状態を見た目で分かりやすくする
constexpr float kCheckpointCrossRadius = 0.45f;
constexpr float kTriggerCrossRadius = 0.3f;
constexpr float kObjectCrossRadiusSelected = 0.6f;
constexpr float kObjectCrossRadiusEnemy = 0.35f;
constexpr float kObjectCrossRadiusProp = 0.25f;
constexpr float kExternalCrossRadiusSelected = 0.6f;
constexpr float kExternalCrossRadiusNormal = 0.3f;
constexpr ImU32 kBoxSelectFill = IM_COL32(80, 160, 255, 40); // 範囲選択矩形の塗り
constexpr ImU32 kBoxSelectBorder = IM_COL32(80, 160, 255, 200);
constexpr ImU32 kHoverColor = IM_COL32(120, 255, 255, 255); // マウス直下の対象の十字
constexpr float kHoverCrossRadius = 0.55f;
// 親子関係の接続線の色（白、半透明）
constexpr ImU32 kParentLinkLineColor = IM_COL32(255, 255, 255, 90);

// "screen"座標ui_textのマーカー（十字＋枠＋文言）関連のスクリーンpx単位のサイズ
constexpr float kScreenTextMarkerSize = 10.0f;
constexpr float kScreenTextMarkerLineThickness = 2.0f;
constexpr float kScreenTextMarkerRectPadding = 3.0f;
constexpr float kScreenTextLabelGapX = 6.0f;
constexpr float kScreenTextLabelOffsetY = 8.0f;

} // namespace

/** @brief チェックポイント・トリガー・配置物・イベント接続線・外部エンティティの補助表示（十字・AABB・接続線）を描画する */
void StageEditor::DrawGizmos() {
    // チェックポイントは緑の範囲と十字で表示する
    for (const CheckpointDesc& checkpoint : checkpoints_) {
        diagnosticsDraw::DrawSphere({ checkpoint.position, checkpoint.activationRadius }, diagnosticsDraw::kColorGreen);
        diagnosticsDraw::DrawCross(checkpoint.position, kCheckpointCrossRadius, diagnosticsDraw::kColorGreen);
    }

    for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
        const TriggerDesc& d = triggers_[i].GetDesc();
        ImU32 color = triggers_[i].IsInside()                  ? diagnosticsDraw::kColorGreen
            : (selKind_ == SelKind::kTrigger && selIndex_ == i) ? diagnosticsDraw::kColorYellow
                                                               : diagnosticsDraw::kColorCyan;
        diagnosticsDraw::DrawSphere({ d.position, d.radius }, color);
        diagnosticsDraw::DrawCross(d.position, kTriggerCrossRadius, color);
    }
    // 全オブジェクトに小さな十字マーカーを出し、どこをクリックすれば掴めるか分かるようにする
    // solid=trueのものは実際の当たり判定AABBをオレンジのワイヤーフレームで重ねて表示する
    // 敵配置（enemy_knight/enemy_basic）は赤い十字にして配置物(白)と一目で区別できるようにする
    VisitShownObjects([&](int i, const ObjectDesc& d) {
        bool sel = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), i)
            != selectedObjectIndices_.end();

        // "screen"座標のui_textはスクリーンpx座標を3Dワールド座標として扱うと、
        // カメラ投影で画面外/後方に飛んでしまい見えなくなるため、ここだけ2Dで直接描く
        if (IsScreenAnchorObject(d)) {
            ImDrawList* dl = diagnosticsDraw::GetDrawList();
            const ImU32 color = sel ? diagnosticsDraw::kColorYellow : diagnosticsDraw::kColorCyan;
            const Vector3 image = viewport_.ScreenToImage(d.position.x, d.position.y);
            const ImVec2 p(image.x, image.y);
            dl->AddLine({ p.x - kScreenTextMarkerSize, p.y }, { p.x + kScreenTextMarkerSize, p.y }, color, kScreenTextMarkerLineThickness);
            dl->AddLine({ p.x, p.y - kScreenTextMarkerSize }, { p.x, p.y + kScreenTextMarkerSize }, color, kScreenTextMarkerLineThickness);
            dl->AddRect({ p.x - kScreenTextMarkerSize - kScreenTextMarkerRectPadding, p.y - kScreenTextMarkerSize - kScreenTextMarkerRectPadding },
                { p.x + kScreenTextMarkerSize + kScreenTextMarkerRectPadding, p.y + kScreenTextMarkerSize + kScreenTextMarkerRectPadding }, color);
            dl->AddText({ p.x + kScreenTextMarkerSize + kScreenTextLabelGapX, p.y - kScreenTextLabelOffsetY }, color,
                d.text.empty() ? "(空文字列)" : d.text.c_str());
            return;
        }

        bool isEnemy = !ObjectKind::Of(d.kind).IsDecoration();
        Vector3 world = WorldPositionOf(d);
        ImU32 baseColor = isEnemy ? diagnosticsDraw::kColorRed : diagnosticsDraw::kColorWhite;
        diagnosticsDraw::DrawCross(world, sel ? kObjectCrossRadiusSelected : (isEnemy ? kObjectCrossRadiusEnemy : kObjectCrossRadiusProp), sel ? diagnosticsDraw::kColorYellow : baseColor);
        if (sel) {
            // 選択位置から3軸を表示し、ワークフロー上の軸制限と対応させる
            constexpr float kAxisLength = 2.0f;
            diagnosticsDraw::DrawLine(world, world + Vector3 { kAxisLength, 0.0f, 0.0f }, diagnosticsDraw::kColorRed);
            diagnosticsDraw::DrawLine(world, world + Vector3 { 0.0f, kAxisLength, 0.0f }, diagnosticsDraw::kColorGreen);
            diagnosticsDraw::DrawLine(world, world + Vector3 { 0.0f, 0.0f, kAxisLength }, diagnosticsDraw::kColorCyan);
        }

        if (d.solid) {
            Vector3 half = { 0.5f * d.scale.x, 0.5f * d.scale.y, 0.5f * d.scale.z };
            diagnosticsDraw::DrawAABB({ { world.x - half.x, world.y - half.y, world.z - half.z },
                                          { world.x + half.x, world.y + half.y, world.z + half.z } },
                diagnosticsDraw::kColorOrange);
        }

        // 種類ごとの判定範囲と動作範囲を見せる（数値だけでは掴みにくい半径・可動域を画面上で確認できるように）
        ObjectKind::Of(d.kind).DrawDebugRange(d, world);

        // 親子関係を白線で可視化する（親→子）
        const std::string& parentName = d.parent;
        if (const ObjectDesc* parent = parentName.empty() ? nullptr : FindDescByName(parentName)) {
            diagnosticsDraw::DrawLine(WorldPositionOf(*parent), world, kParentLinkLineColor);
        }
    });

    // イベントの条件から対象へ接続線を引き、複数アクションの流れを可視化する
    for (const auto& targetEntry : objects_) {
        const ObjectDesc& target = targetEntry.desc;
        if (target.activationFlag.empty()) {
            continue;
        }
        Vector3 sourcePosition = { };
        if (FindEventSourcePosition(target.activationFlag, sourcePosition)) {
            const ImU32 color = target.activeWhenFlag ? diagnosticsDraw::kColorGreen : diagnosticsDraw::kColorRed;
            diagnosticsDraw::DrawLine(sourcePosition, WorldPositionOf(target), color);
        }
    }

    // Player/Enemy等のランタイム実体（マゼンタの十字。オブジェクトの白・トリガーのシアンと区別する）
    for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
        const ExternalEntityRef& ref = externalEntities_[i];
        if (!ref.positionTarget) {
            continue;
        }
        bool sel = (selKind_ == SelKind::kExternal && selIndex_ == i);
        diagnosticsDraw::DrawCross(ref.positionTarget->GetPosition(), sel ? kExternalCrossRadiusSelected : kExternalCrossRadiusNormal, sel ? diagnosticsDraw::kColorYellow : diagnosticsDraw::kColorMagenta);
    }

    // マウス直下の対象を明るい十字で示す（クリックしたら何が選ばれるかを先に見せる）
    if (!viewportDragging_ && hoverKind_ != SelKind::kNone) {
        const ISelectionKind& hovered = SelectionKindOf(hoverKind_);
        Vector3 hoverWorld;
        if (!hovered.IsScreenAnchor(*this, hoverIndex_) && hovered.WorldPosition(*this, hoverIndex_, hoverWorld)) {
            diagnosticsDraw::DrawCross(hoverWorld, kHoverCrossRadius, kHoverColor);
        }
    }

    // 選択物の変形ハンドル（ツールごとに矢印/リング/四角）
    DrawTransformHandles();

    // 画面UIの位置（3Dの配置物より手前に出す）
    DrawUILayoutHandles();

    // 範囲選択中の矩形
    if (boxSelecting_) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImDrawList* dl = diagnosticsDraw::GetDrawList();
        const ImVec2 a = { (std::min)(boxStartX_, mouse.x), (std::min)(boxStartY_, mouse.y) };
        const ImVec2 b = { (std::max)(boxStartX_, mouse.x), (std::max)(boxStartY_, mouse.y) };
        dl->AddRectFilled(a, b, kBoxSelectFill);
        dl->AddRect(a, b, kBoxSelectBorder);
    }

    // ドラッグ中の数値（カーソルの近く）
    DrawDragReadout();
}

/** @brief 3Dビュー上での左クリック選択とドラッグ移動（ImGuiウィンドウ上のマウスは無視する） */
void StageEditor::UpdateViewportInteraction() {
    // テストプレイ中はビュー上の編集操作を受け付けない
    if (playTestMode_) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();

    // 掴んだ後はビューの外やパネルの上でも追従を続け、どこで離しても確定する
    if (ImGui::IsMousePosValid()) {
        dragLastMouseX_ = io.MousePos.x;
        dragLastMouseY_ = io.MousePos.y;
    }
    if (UpdateActiveViewportDrag(dragLastMouseX_, dragLastMouseY_)) {
        return;
    }

    // 編集パネル上の操作をシーンビューの選択やカメラ移動として扱わない
    const ImVec2 m = io.MousePos;
    if ((io.WantCaptureMouse && !viewport_.IsImageHovered()) || !viewport_.Contains(m.x, m.y, viewportFocusMode_)) {
        ClearViewportHover();
        return;
    }

    UpdateViewportHover(m.x, m.y);

    // マウスホイール  カーソル位置へ寄る/離れる（手前に回すと近づく）
    viewport_.ZoomAtCursor(io.MouseWheel, m.x, m.y);

    // 左クリック  ハンドル → 画面UIの印 → オブジェクト → 何も無ければ範囲選択、の順に判定する
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        HandleViewportClick(m.x, m.y);
    }

    // 右クリック（ドラッグせずに離した時だけ）で、その場の対象に応じたメニューを開く
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        constexpr float kContextClickTolerancePx = 4.0f;
        const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        if (std::abs(dragDelta.x) < kContextClickTolerancePx && std::abs(dragDelta.y) < kContextClickTolerancePx) {
            contextKind_ = SelKind::kNone;
            contextIndex_ = -1;
            PickViewportTarget(m.x, m.y, contextKind_, contextIndex_);
            contextScreenPos_ = viewport_.ImageToScreen(m.x, m.y);
            if (!MouseToGround(m.x, m.y, contextWorldPos_)) {
                contextWorldPos_ = ViewCenterOnGround();
            }
            SelectionKindOf(contextKind_).SelectFromContext(*this, contextIndex_);
            contextMenuRequested_ = true;
        }
    }
}

/** @brief マウス直下のハンドル・印・配置物を調べてカーソルを決める */
void StageEditor::UpdateViewportHover(float mouseX, float mouseY) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool linkingParent = parentLinkChildIndex_ >= 0;

    // ハンドルと印の判定は軽いので毎フレーム行う（カーソルの形は毎フレーム指定しないと矢印へ戻る）
    bool handleUniform = false;
    hoverHandleActive_ = !linkingParent && PickTransformHandle(mouseX, mouseY, hoverAxis_, handleUniform);
    uiHoverLayout_ = nullptr;
    if (!hoverHandleActive_ && !linkingParent) {
        PickUILayoutHandle(mouseX, mouseY, uiHoverLayout_, uiHoverIndex_);
    }
    if (hoverHandleActive_ || uiHoverLayout_) {
        hoverKind_ = SelKind::kNone;
        hoverIndex_ = -1;
        hoverNeedsRefresh_ = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        return;
    }

    // 配置物の判定は全頂点を投影して重いので、マウスかカメラが動いた時だけやり直す
    const bool mouseMoved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    const bool cameraMoving = io.MouseWheel != 0.0f || ImGui::IsMouseDown(ImGuiMouseButton_Right)
        || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    if (mouseMoved || cameraMoving || hoverNeedsRefresh_) {
        hoverKind_ = SelKind::kNone;
        hoverIndex_ = -1;
        PickViewportTarget(mouseX, mouseY, hoverKind_, hoverIndex_);
        hoverNeedsRefresh_ = false;
    }
    if (hoverKind_ != SelKind::kNone) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

/** @brief マウス直下の情報を消す */
void StageEditor::ClearViewportHover() {
    hoverKind_ = SelKind::kNone;
    hoverIndex_ = -1;
    hoverHandleActive_ = false;
    hoverAxis_ = 0;
    uiHoverLayout_ = nullptr;
    hoverNeedsRefresh_ = true;
}

namespace {
// ハンドルの見た目と判定（スクリーンpx固定。カメラの遠近に関係なく同じ大きさで掴める）
constexpr float kHandleLengthPx = 70.0f;
constexpr float kHandlePickPx = 13.0f; // 軸の線からこの距離までなら掴める
constexpr float kHandleTipPickPx = 20.0f; // 軸の先端からこの距離までなら優先して掴める
constexpr float kHandleCenterPx = 10.0f;
constexpr float kHandleCenterPickPx = 15.0f; // 中央の四角を掴める距離
constexpr float kHandleArrowPx = 10.0f;
constexpr float kRotateRingPx = 60.0f;
constexpr float kRotateRingPickPx = 15.0f;
constexpr float kScaleTipPx = 6.0f;
constexpr float kHandleThicknessPx = 2.5f;
constexpr float kHandleHoverThicknessPx = 5.0f; // マウスが乗っている軸や掴んでいる軸の太さ
constexpr float kHandleHoverTipScale = 1.4f; // マウスが乗っている軸の先端の拡大率
constexpr float kAxisLabelGapPx = 8.0f; // 軸の先端から軸の名前までの距離
constexpr float kAxisLabelHalfHeight = 8.0f; // 軸の名前を先端の高さへ揃える量
constexpr float kMinProjectedPx = 8.0f; // 単位長さの投影がこれより短い軸は奥行きで潰れているとみなす
constexpr float kParallelAxisDot = 0.92f; // 向きの内積がこれより大きい軸は重なっているとみなす
constexpr int kRotateRingSegments = 48;
constexpr ImU32 kHandleColorX = IM_COL32(255, 70, 70, 230);
constexpr ImU32 kHandleColorY = IM_COL32(90, 255, 90, 230);
constexpr ImU32 kHandleColorZ = IM_COL32(90, 150, 255, 230);
constexpr ImU32 kHandleColorCenter = IM_COL32(255, 255, 255, 200);
constexpr ImU32 kHandleColorRing = IM_COL32(90, 150, 255, 230);
constexpr ImU32 kHandleColorHover = IM_COL32(255, 240, 90, 255); // マウスが乗っている軸
constexpr ImU32 kHandleColorDimmed = IM_COL32(150, 150, 150, 110); // ドラッグ中の、掴んでいない軸
constexpr ImU32 kHandleCenterFillColor = IM_COL32(255, 255, 255, 60); // 中央の半透明の白
constexpr ImU32 kHandleCenterHoverFillColor = IM_COL32(255, 240, 90, 130); // マウスが乗っている時の中央
constexpr const char* kAxisLabels[kGizmoAxisCount] = { "X", "Y", "Z 奥行き" };
constexpr float kBoxSelectMinPx = 4.0f;

/** @brief 各軸のスクリーン上の向きを求める（原点と原点+単位ベクトルを投影した差）。奥行き軸が潰れたりX/Y軸と重なる時は斜めへ逃がす */
bool AxisScreenDirections(const Vector3& world, ImVec2& outOrigin, ImVec2 outDirs[3]) {
    if (!diagnosticsDraw::WorldToScreen(world, outOrigin)) {
        return false;
    }
    const Vector3 units[kGizmoAxisCount] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    constexpr float kInvSqrt2 = 0.70710678f; // 斜め45度の単位ベクトル成分
    const ImVec2 fallback[kGizmoAxisCount] = { { 1.0f, 0.0f }, { 0.0f, -1.0f }, { -kInvSqrt2, kInvSqrt2 } };
    for (int i = 0; i < kGizmoAxisCount; ++i) {
        ImVec2 tip;
        ImVec2 dir = fallback[i];
        if (diagnosticsDraw::WorldToScreen(world + units[i], tip)) {
            const float dx = tip.x - outOrigin.x;
            const float dy = tip.y - outOrigin.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > kMinProjectedPx) {
                dir = { dx / len, dy / len };
            }
        }
        outDirs[i] = dir;
    }
    // 奥行き軸がX/Y軸と重なると掴み分けられないので、斜めの向きへ逃がす
    const int depthAxis = kGizmoAxisCount - 1;
    for (int i = 0; i < depthAxis; ++i) {
        const float dot = outDirs[depthAxis].x * outDirs[i].x + outDirs[depthAxis].y * outDirs[i].y;
        if (std::abs(dot) > kParallelAxisDot) {
            outDirs[depthAxis] = fallback[depthAxis];
            break;
        }
    }
    return true;
}

/** @brief 点から線分までの最短距離を返す */
float DistancePointToSegment(float px, float py, const ImVec2& a, const ImVec2& b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float lenSq = abx * abx + aby * aby;
    float t = lenSq > 0.0f ? ((px - a.x) * abx + (py - a.y) * aby) / lenSq : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float cx = a.x + abx * t - px;
    const float cy = a.y + aby * t - py;
    return std::sqrt(cx * cx + cy * cy);
}
} // namespace

/** @brief クリック位置が選択物のハンドル（軸の矢印・回転リング・中央の四角）に乗っているか判定する */
bool StageEditor::PickTransformHandle(float mouseX, float mouseY, int& outAxis, bool& outUniform) const {
    // 選択物の位置を画面へ投影し、各軸の画面上の向きを求める
    outAxis = 0;
    outUniform = false;
    Vector3 world;
    if (!SelectionWorldPosition(world)) {
        return false;
    }
    ImVec2 origin;
    ImVec2 dirs[3];
    if (!AxisScreenDirections(world, origin, dirs)) {
        return false;
    }
    // ハンドル中心からマウスまでの画面上の距離
    const float centerDx = mouseX - origin.x;
    const float centerDy = mouseY - origin.y;
    const float centerDist = std::sqrt(centerDx * centerDx + centerDy * centerDy);

    if (transformTool_ == TransformTool::kRotate) {
        // 回転はトリガーや外部エンティティには無いので配置物だけ
        if (selKind_ != SelKind::kObject) {
            return false;
        }
        return std::abs(centerDist - kRotateRingPx) <= kRotateRingPickPx;
    }

    // 中央の四角は全軸（拡縮なら等倍拡縮）
    if (centerDist <= kHandleCenterPickPx) {
        outAxis = 0;
        outUniform = transformTool_ == TransformTool::kScale;
        return true;
    }
    if (transformTool_ == TransformTool::kScale && selKind_ != SelKind::kObject) {
        return false;
    }
    // 線分に十分近い軸のうち一番近いもの（1=X, 2=Y, 3=Z）を掴む。先端の近くはその軸を優先する
    float bestDistance = kHandlePickPx;
    bool found = false;
    for (int i = 0; i < kGizmoAxisCount; ++i) {
        const ImVec2 tip = { origin.x + dirs[i].x * kHandleLengthPx, origin.y + dirs[i].y * kHandleLengthPx };
        const float tipDx = mouseX - tip.x;
        const float tipDy = mouseY - tip.y;
        const bool nearTip = tipDx * tipDx + tipDy * tipDy <= kHandleTipPickPx * kHandleTipPickPx;
        const float distance = nearTip ? 0.0f : DistancePointToSegment(mouseX, mouseY, origin, tip);
        if (distance <= bestDistance) {
            bestDistance = distance;
            outAxis = i + 1;
            found = true;
        }
    }
    return found;
}

/** @brief ハンドルの軸の画面上の向きに沿ったマウス移動量を返す */
float StageEditor::AxisDragPixels(int axis, float deltaX, float deltaY) const {
    // 向きが分からない時は、右か上へ動かすと正になる量を返す
    const int index = axis - stageEditorUiStyle::kGizmoAxisX;
    Vector3 world;
    ImVec2 origin;
    ImVec2 dirs[kGizmoAxisCount];
    if (index < 0 || index >= kGizmoAxisCount || !SelectionWorldPosition(world) || !AxisScreenDirections(world, origin, dirs)) {
        return deltaX - deltaY;
    }
    return deltaX * dirs[index].x + deltaY * dirs[index].y;
}

/** @brief 選択物にツールごとのハンドルを画面固定サイズで描く（DrawGizmosから呼ぶ） */
void StageEditor::DrawTransformHandles() {
    // 選択物が画面に映っていなければハンドルを描かない
    Vector3 world;
    if (!SelectionWorldPosition(world)) {
        return;
    }
    ImVec2 origin;
    ImVec2 dirs[3];
    if (!AxisScreenDirections(world, origin, dirs)) {
        return;
    }
    ImDrawList* dl = diagnosticsDraw::GetDrawList();
    const ImU32 axisColors[3] = { kHandleColorX, kHandleColorY, kHandleColorZ };

    // 回転ツールは配置物にだけ輪を描く（マウスが乗っている時と回している時は太くする）
    if (transformTool_ == TransformTool::kRotate) {
        if (selKind_ == SelKind::kObject) {
            const bool ringActive = hoverHandleActive_ || rotateDragging_;
            dl->AddCircle(origin, kRotateRingPx, ringActive ? kHandleColorHover : kHandleColorRing, kRotateRingSegments,
                ringActive ? kHandleHoverThicknessPx : kHandleThicknessPx);
            dl->AddCircleFilled(origin, kHandleCenterPx * 0.5f, kHandleColorCenter);
        }
        return;
    }

    // 各軸の線を描き、先端は拡縮なら四角、移動なら矢印にする
    // 掴んでいる軸は白、マウスが乗っている軸は黄色で太くし、ドラッグ中の他の軸は薄くする
    const bool axisDragging = viewportDragging_ && activeDragAxis_ != 0;
    const bool centerHovered = hoverHandleActive_ && hoverAxis_ == 0;
    for (int i = 0; i < kGizmoAxisCount; ++i) {
        const bool active = activeDragAxis_ == i + 1;
        const bool hovered = hoverHandleActive_ && hoverAxis_ == i + 1;
        const bool emphasized = active || hovered;
        const ImU32 color = active ? kHandleColorCenter
            : hovered              ? kHandleColorHover
            : axisDragging         ? kHandleColorDimmed
                                   : axisColors[i];
        const float tipScale = emphasized ? kHandleHoverTipScale : 1.0f;
        const ImVec2 tip = { origin.x + dirs[i].x * kHandleLengthPx, origin.y + dirs[i].y * kHandleLengthPx };
        dl->AddLine(origin, tip, color, emphasized ? kHandleHoverThicknessPx : kHandleThicknessPx);
        if (transformTool_ == TransformTool::kScale) {
            const float half = kScaleTipPx * tipScale;
            dl->AddRectFilled({ tip.x - half, tip.y - half }, { tip.x + half, tip.y + half }, color);
        } else {
            // 矢印の先端（軸方向に対して左右へ開いた三角形）
            const float arrow = kHandleArrowPx * tipScale;
            const ImVec2 back = { tip.x - dirs[i].x * arrow, tip.y - dirs[i].y * arrow };
            const ImVec2 side = { -dirs[i].y * arrow * 0.5f, dirs[i].x * arrow * 0.5f };
            dl->AddTriangleFilled(tip, { back.x + side.x, back.y + side.y }, { back.x - side.x, back.y - side.y }, color);
        }
        // どの軸か分かるよう、先端の外側に名前を添える
        dl->AddText({ tip.x + dirs[i].x * kAxisLabelGapPx, tip.y + dirs[i].y * kAxisLabelGapPx - kAxisLabelHalfHeight }, color, kAxisLabels[i]);
    }
    // 中央の全軸ハンドル
    dl->AddRectFilled({ origin.x - kHandleCenterPx, origin.y - kHandleCenterPx },
        { origin.x + kHandleCenterPx, origin.y + kHandleCenterPx }, centerHovered ? kHandleCenterHoverFillColor : kHandleCenterFillColor);
    dl->AddRect({ origin.x - kHandleCenterPx, origin.y - kHandleCenterPx },
        { origin.x + kHandleCenterPx, origin.y + kHandleCenterPx }, kHandleColorCenter);
}

/** @brief 範囲選択の矩形を確定し、内側の配置物を選択する（小さすぎる矩形はクリック扱いで選択解除） */
void StageEditor::FinishBoxSelect(float mouseX, float mouseY) {
    // ドラッグの始点と終点から範囲の矩形を作る（Ctrl中は今の選択に足す）
    boxSelecting_ = false;
    const float minX = (std::min)(boxStartX_, mouseX);
    const float maxX = (std::max)(boxStartX_, mouseX);
    const float minY = (std::min)(boxStartY_, mouseY);
    const float maxY = (std::max)(boxStartY_, mouseY);
    const bool additive = ImGui::GetIO().KeyCtrl;
    if (maxX - minX < kBoxSelectMinPx && maxY - minY < kBoxSelectMinPx) {
        // 何も無い場所のクリック: Unityと同じく選択解除（Ctrl中は維持）
        if (!additive) {
            ClearSelection();
        }
        return;
    }
    if (!additive) {
        selectedObjectIndices_.clear();
    }
    // 画面上の位置が範囲に入っている配置物を選択に加える（非表示の文章UIは除く）
    VisitShownObjects([&](int i, const ObjectDesc& desc) {
        ImVec2 screen;
        if (IsScreenAnchorObject(desc)) {
            const Vector3 image = viewport_.ScreenToImage(desc.position.x, desc.position.y);
            screen = { image.x, image.y };
        } else if (!diagnosticsDraw::WorldToScreen(WorldPositionOf(desc), screen)) {
            return;
        }
        if (screen.x < minX || screen.x > maxX || screen.y < minY || screen.y > maxY) {
            return;
        }
        if (std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), i) == selectedObjectIndices_.end()) {
            selectedObjectIndices_.push_back(i);
        }
    });
    // 最後に加えたものを主選択にし、何個選んだかを知らせる
    if (selectedObjectIndices_.empty()) {
        selKind_ = SelKind::kNone;
        selIndex_ = -1;
        return;
    }
    selKind_ = SelKind::kObject;
    selIndex_ = selectedObjectIndices_.back();
    statusMessage_ = std::to_string(selectedObjectIndices_.size()) + " 個を選択しました";
    statusTimer_ = StageEditor::kStatusShortSeconds;
}
#else
/** @brief 見た目の位置を変えずに親を付け替える */
void StageEditor::SetParentPreservingWorld(int, int) { }
/** @brief 種類に応じた既定値で配置物を指定位置へ追加し、選択する */
int StageEditor::AddObjectAt(const std::string&, const Vector3&) {
    return -1;
}
/** @brief トリガーを指定位置へ追加して選択する */
void StageEditor::AddTriggerAt(const Vector3&) { }
/** @brief 全配置物を選択する（Ctrl+A） */
void StageEditor::SelectAllObjects() { }
/** @brief 選択を解除する（Escape） */
void StageEditor::ClearSelection() { }
/** @brief チェックポイント・トリガー・配置物・イベント接続線・外部エンティティの補助表示（十字・AABB・接続線）を描画する */
void StageEditor::DrawGizmos() { }
/** @brief WASD(+QEで奥/手前)でカメラを移動するImGuiのテキスト入力中は無効化する */
void StageEditor::UpdateFreeCamera(engine::Input*, float) { }
/** @brief 3Dビュー上での左クリック選択とドラッグ移動（ImGuiウィンドウ上のマウスは無視する） */
void StageEditor::UpdateViewportInteraction() { }
/** @brief マウススクリーン座標をゲーム平面(z=0)上のワールド座標へ変換する */
bool StageEditor::MouseToGround(float, float, Vector3&) const {
    return false;
}
/** @brief 選択物のワールド位置を返す（配置物/トリガー/外部エンティティ共通） */
bool StageEditor::SelectionWorldPosition(Vector3&) const {
    return false;
}
/** @brief クリック位置が選択物のハンドル（軸の矢印・回転リング・中央の四角）に乗っているか判定する */
bool StageEditor::PickTransformHandle(float, float, int&, bool&) const {
    return false;
}
/** @brief 選択物にツールごとのハンドルを画面固定サイズで描く（DrawGizmosから呼ぶ） */
void StageEditor::DrawTransformHandles() { }
/** @brief 回転/拡縮ツールのドラッグを反映する（ドラッグ開始時の値へ、開始からの累積量を掛ける） */
void StageEditor::UpdateRotateScaleDrag(float, float) { }
/** @brief 範囲選択の矩形を確定し、内側の配置物を選択する（小さすぎる矩形はクリック扱いで選択解除） */
void StageEditor::FinishBoxSelect(float, float) { }
/** @brief 画面UI（UILayoutの位置項目）の印をクリック位置から探す */
bool StageEditor::PickUILayoutHandle(float, float, UILayout*&, size_t&) const {
    return false;
}
/** @brief 画面UIの位置項目を四角い印で描く（名前はマウスが乗った印と選択中の印にだけ出す。DrawGizmosから呼ぶ） */
void StageEditor::DrawUILayoutHandles() { }
#endif
} // namespace engine::game
