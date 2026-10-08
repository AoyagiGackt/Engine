/**
 * @file StageEditorInteraction.cpp
 * @brief ステージエディタの3D操作（ギズモ描画・自由カメラ・マウスによる選択とドラッグ）を実装するファイル
 * @note StageEditor.cppからの分割ファイルクラス自体はStageEditorのまま、定義の置き場所だけを分けている
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
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

#ifdef USE_IMGUI
namespace {
// Shift+ドラッグのZ移動  マウス垂直1pxあたりの移動量（カメラ距離1.0基準720p想定の見かけ等速係数）
constexpr float kZDragPerPixel = 0.0015f;

// DrawGizmos()の十字マーカー半径（ワールド単位）非選択時と選択時でサイズを変え、選択状態を見た目で分かりやすくする
constexpr float kCheckpointCrossRadius = 0.45f;
constexpr float kTriggerCrossRadius = 0.3f;
constexpr float kObjectCrossRadiusSelected = 0.6f;
constexpr float kObjectCrossRadiusEnemy = 0.35f;
constexpr float kObjectCrossRadiusProp = 0.25f;
constexpr float kExternalCrossRadiusSelected = 0.6f;
constexpr float kExternalCrossRadiusNormal = 0.3f;
constexpr float kSpawnMarkerHalf = 0.4f; // spawn_pointを示す赤い枠の半径
constexpr float kMotionEndMarkerRadius = 0.2f; // ギミック可動域の端に出す十字の大きさ
constexpr ImU32 kBoxSelectFill = IM_COL32(80, 160, 255, 40); // 範囲選択矩形の塗り
constexpr ImU32 kBoxSelectBorder = IM_COL32(80, 160, 255, 200);
constexpr ImU32 kHoverColor = IM_COL32(120, 255, 255, 255); // マウス直下の対象の十字
constexpr float kHoverCrossRadius = 0.55f;
// 親子関係の接続線の色（白、半透明）
constexpr ImU32 kParentLinkLineColor = IM_COL32(255, 255, 255, 90);

// PickViewportTarget()の最大ピック距離（画面px）これより遠いものはクリック対象にしない
constexpr float kPickRadiusPx = 40.0f;

// 画面UI（UILayoutの位置項目）の印
constexpr float kUIHandleHalfSize = 6.0f; // 四角い印の半分の大きさ（表示上のpx）
constexpr float kUIHandlePickRadiusPx = 12.0f; // これより近ければ印を掴める
constexpr float kUIHandleLabelGapX = 4.0f;
constexpr float kUIHandleLineThickness = 2.0f;
constexpr ImU32 kUIHandleColor = IM_COL32(255, 150, 40, 230);
constexpr ImU32 kUIHandleSelectedColor = IM_COL32(255, 230, 60, 255);
constexpr ImU32 kUIHandleFill = IM_COL32(255, 150, 40, 70);

// "screen"座標ui_textのマーカー（十字＋枠＋文言）関連のスクリーンpx単位のサイズ
constexpr float kScreenTextMarkerSize = 10.0f;
constexpr float kScreenTextMarkerLineThickness = 2.0f;
constexpr float kScreenTextMarkerRectPadding = 3.0f;
constexpr float kScreenTextLabelGapX = 6.0f;
constexpr float kScreenTextLabelOffsetY = 8.0f;

// 3D投影せず2Dスクリーン座標のまま扱うべき配置物か（"screen"座標のui_text）
bool IsScreenAnchorObject(const ObjectDesc& d)
{
    return ObjectKind::Of(d.kind).IsScreenSpace(d);
}
} // namespace

void StageEditor::DrawGizmos()
{
    // チェックポイントは緑の範囲と十字で表示する
    for (const CheckpointDesc& checkpoint : checkpoints_) {
        DiagnosticsDraw::DrawSphere({ checkpoint.position, checkpoint.activationRadius }, DiagnosticsDraw::kColorGreen);
        DiagnosticsDraw::DrawCross(checkpoint.position, kCheckpointCrossRadius, DiagnosticsDraw::kColorGreen);
    }

    for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
        const TriggerDesc& d = triggers_[i].GetDesc();
        ImU32 color = triggers_[i].IsInside()                  ? DiagnosticsDraw::kColorGreen
            : (selKind_ == SelKind::Trigger && selIndex_ == i) ? DiagnosticsDraw::kColorYellow
                                                               : DiagnosticsDraw::kColorCyan;
        DiagnosticsDraw::DrawSphere({ d.position, d.radius }, color);
        DiagnosticsDraw::DrawCross(d.position, kTriggerCrossRadius, color);
    }
    // 全オブジェクトに小さな十字マーカーを出し、どこをクリックすれば掴めるか分かるようにする
    // solid=trueのものは実際の当たり判定AABBをオレンジのワイヤーフレームで重ねて表示する
    // 敵配置（enemy_knight/enemy_basic）は赤い十字にして配置物(白)と一目で区別できるようにする
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        bool sel = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), i)
            != selectedObjectIndices_.end();
        const ObjectDesc& d = objects_[i].desc;

        // 配置物の陰になって邪魔な時は、テキスト表示チェックボックスでui_textだけ一時的に隠せる
        if (d.kind == ObjectKindName::kUIText && !showUIText_) {
            continue;
        }

        // "screen"座標のui_textはスクリーンpx座標を3Dワールド座標として扱うと、
        // カメラ投影で画面外/後方に飛んでしまい見えなくなるため、ここだけ2Dで直接描く
        if (IsScreenAnchorObject(d)) {
            ImDrawList* dl = DiagnosticsDraw::GetDrawList();
            const ImU32 color = sel ? DiagnosticsDraw::kColorYellow : DiagnosticsDraw::kColorCyan;
            const Vector3 image = viewport_.ScreenToImage(d.position.x, d.position.y);
            const ImVec2 p(image.x, image.y);
            dl->AddLine({ p.x - kScreenTextMarkerSize, p.y }, { p.x + kScreenTextMarkerSize, p.y }, color, kScreenTextMarkerLineThickness);
            dl->AddLine({ p.x, p.y - kScreenTextMarkerSize }, { p.x, p.y + kScreenTextMarkerSize }, color, kScreenTextMarkerLineThickness);
            dl->AddRect({ p.x - kScreenTextMarkerSize - kScreenTextMarkerRectPadding, p.y - kScreenTextMarkerSize - kScreenTextMarkerRectPadding },
                { p.x + kScreenTextMarkerSize + kScreenTextMarkerRectPadding, p.y + kScreenTextMarkerSize + kScreenTextMarkerRectPadding }, color);
            dl->AddText({ p.x + kScreenTextMarkerSize + kScreenTextLabelGapX, p.y - kScreenTextLabelOffsetY }, color,
                d.text.empty() ? "(空文字列)" : d.text.c_str());
            continue;
        }

        bool isEnemy = !ObjectKind::Of(d.kind).IsDecoration();
        Vector3 world = WorldPositionOf(d);
        ImU32 baseColor = isEnemy ? DiagnosticsDraw::kColorRed : DiagnosticsDraw::kColorWhite;
        DiagnosticsDraw::DrawCross(world, sel ? kObjectCrossRadiusSelected : (isEnemy ? kObjectCrossRadiusEnemy : kObjectCrossRadiusProp), sel ? DiagnosticsDraw::kColorYellow : baseColor);
        if (sel) {
            // 選択位置から3軸を表示し、ワークフロー上の軸制限と対応させる
            constexpr float kAxisLength = 2.0f;
            DiagnosticsDraw::DrawLine(world, world + Vector3 { kAxisLength, 0.0f, 0.0f }, DiagnosticsDraw::kColorRed);
            DiagnosticsDraw::DrawLine(world, world + Vector3 { 0.0f, kAxisLength, 0.0f }, DiagnosticsDraw::kColorGreen);
            DiagnosticsDraw::DrawLine(world, world + Vector3 { 0.0f, 0.0f, kAxisLength }, DiagnosticsDraw::kColorCyan);
        }

        if (d.solid) {
            Vector3 half = { 0.5f * d.scale.x, 0.5f * d.scale.y, 0.5f * d.scale.z };
            DiagnosticsDraw::DrawAABB({ { world.x - half.x, world.y - half.y, world.z - half.z },
                                          { world.x + half.x, world.y + half.y, world.z + half.z } },
                DiagnosticsDraw::kColorOrange);
        }

        // 種類ごとの判定範囲と動作範囲を見せる（数値だけでは掴みにくい半径・可動域を画面上で確認できるように）
        ObjectKind::Of(d.kind).DrawDebugRange(d, world);

        // 親子関係を白線で可視化する（親→子）
        const std::string& parentName = objects_[i].desc.parent;
        if (!parentName.empty()) {
            for (const auto& other : objects_) {
                if (other.desc.name == parentName) {
                    DiagnosticsDraw::DrawLine(WorldPositionOf(other.desc), world, kParentLinkLineColor);
                    break;
                }
            }
        }
    }

    // イベントの条件から対象へ接続線を引き、複数アクションの流れを可視化する
    for (const auto& targetEntry : objects_) {
        const ObjectDesc& target = targetEntry.desc;
        if (target.activationFlag.empty()) {
            continue;
        }
        bool sourceFound = false;
        Vector3 sourcePosition = { };
        for (const auto& trigger : triggers_) {
            if (trigger.GetDesc().flag == target.activationFlag) {
                sourcePosition = trigger.GetDesc().position;
                sourceFound = true;
                break;
            }
        }
        if (!sourceFound && target.activationFlag.starts_with("condition_")) {
            const std::string conditionName = target.activationFlag.substr(10);
            for (const auto& condition : objects_) {
                if (condition.desc.kind == ObjectKindName::kEventCondition && condition.desc.name == conditionName) {
                    sourcePosition = WorldPositionOf(condition.desc);
                    sourceFound = true;
                    break;
                }
            }
        }
        if (sourceFound) {
            const ImU32 color = target.activeWhenFlag ? DiagnosticsDraw::kColorGreen : DiagnosticsDraw::kColorRed;
            DiagnosticsDraw::DrawLine(sourcePosition, WorldPositionOf(target), color);
        }
    }

    // Player/Enemy等のランタイム実体（マゼンタの十字。オブジェクトの白・トリガーのシアンと区別する）
    for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
        const ExternalEntityRef& ref = externalEntities_[i];
        if (!ref.position) {
            continue;
        }
        bool sel = (selKind_ == SelKind::External && selIndex_ == i);
        DiagnosticsDraw::DrawCross(*ref.position, sel ? kExternalCrossRadiusSelected : kExternalCrossRadiusNormal, sel ? DiagnosticsDraw::kColorYellow : DiagnosticsDraw::kColorMagenta);
    }

    // マウス直下の対象を明るい十字で示す（クリックしたら何が選ばれるかを先に見せる）
    if (!viewportDragging_ && hoverKind_ != SelKind::None) {
        const ISelectionKind& hovered = SelectionKindOf(hoverKind_);
        Vector3 hoverWorld;
        if (!hovered.IsScreenAnchor(*this, hoverIndex_) && hovered.WorldPosition(*this, hoverIndex_, hoverWorld)) {
            DiagnosticsDraw::DrawCross(hoverWorld, kHoverCrossRadius, kHoverColor);
        }
    }

    // 選択物の変形ハンドル（ツールごとに矢印/リング/四角）
    DrawTransformHandles();

    // 画面UIの位置（3Dの配置物より手前に出す）
    DrawUILayoutHandles();

    // 範囲選択中の矩形
    if (boxSelecting_) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImDrawList* dl = DiagnosticsDraw::GetDrawList();
        const ImVec2 a = { (std::min)(boxStartX_, mouse.x), (std::min)(boxStartY_, mouse.y) };
        const ImVec2 b = { (std::max)(boxStartX_, mouse.x), (std::max)(boxStartY_, mouse.y) };
        dl->AddRectFilled(a, b, kBoxSelectFill);
        dl->AddRect(a, b, kBoxSelectBorder);
    }
}

void StageEditor::UpdateFreeCamera(Input* input, float dt)
{
    if (playTestMode_) { return; }
    viewport_.UpdateCamera(input, dt, viewportFocusMode_);
}

bool StageEditor::MouseToGround(float mouseX, float mouseY, Vector3& outWorld) const
{
    return viewport_.ScreenToGround(mouseX, mouseY, outWorld);
}

void StageEditor::UpdateViewportInteraction()
{
    if (playTestMode_) { return; }
    ImGuiIO& io = ImGui::GetIO();
    const bool insideSceneView = viewport_.Contains(io.MousePos.x, io.MousePos.y, viewportFocusMode_);

    // 編集パネル上の操作をシーンビューの選択やカメラ移動として扱わない
    if ((io.WantCaptureMouse && !viewport_.IsImageHovered()) || !insideSceneView) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (viewportDragging_) {
                CommitUndoCapture(); // パネル上で離した場合もドラッグ分をここで確定する
            }
            viewportDragging_ = false;
            uiDragLayout_ = nullptr;
        }
        return;
    }

    const ImVec2 m = io.MousePos;

    // 画面UIの位置項目は3Dの配置物より優先して掴む（UIは常に手前に表示されているため）
    if (uiDragLayout_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const Vector3 logicalMouse = viewport_.ImageToScreen(m.x, m.y);
            uiDragLayout_->SetPosition(uiDragIndex_,
                { std::round(logicalMouse.x + uiDragGrabOffsetX_), std::round(logicalMouse.y + uiDragGrabOffsetY_) });
        } else {
            uiDragLayout_ = nullptr;
        }
        return;
    }
    {
        UILayout* layout = nullptr;
        size_t index = 0;
        if (PickUILayoutHandle(m.x, m.y, layout, index)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                for (const UILayout::PositionHandle& handle : UILayout::CollectPositionHandles()) {
                    if (handle.layout == layout && handle.index == index) {
                        const Vector3 logicalMouse = viewport_.ImageToScreen(m.x, m.y);
                        uiDragGrabOffsetX_ = handle.position.x - logicalMouse.x;
                        uiDragGrabOffsetY_ = handle.position.y - logicalMouse.y;
                        break;
                    }
                }
                uiDragLayout_ = layout;
                uiDragIndex_ = index;
                UILayout::SetSelectedHandle(layout, index);
                return;
            }
        }
    }

    // マウスホイール  カメラを奥/手前へ移動（Q/Eと同じ軸、手前に回すと近づく）
    if (camera_ && io.MouseWheel != 0.0f) {
        constexpr float kWheelSpeed = 2.0f;
        camera_->GetTranslate().z += io.MouseWheel * kWheelSpeed;
    }

    // マウス直下の対象をハイライトする（ドラッグ中以外、マウスが動いた時だけ判定する）
    if (!viewportDragging_ && !boxSelecting_ && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        hoverKind_ = SelKind::None;
        hoverIndex_ = -1;
        int handleAxis = 0;
        bool handleUniform = false;
        if (PickTransformHandle(m.x, m.y, handleAxis, handleUniform)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (PickViewportTarget(m.x, m.y, hoverKind_, hoverIndex_)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }

    // 左クリック  ハンドル → オブジェクト → 何も無ければ範囲選択、の順に判定する
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        HandleViewportClick(m.x, m.y);
    }

    // ドラッグ中  移動ツールはz=0平面上へ追従（Shiftまたは軸ハンドルで軸制限）、回転/拡縮はマウス移動量で反映する
    if (viewportDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (rotateDragging_ || scaleDragging_) {
            UpdateRotateScaleDrag(io.MouseDelta.x, io.MouseDelta.y);
        } else {
            UpdateViewportDrag(m.x, m.y);
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (viewportDragging_) {
            CommitUndoCapture(); // 実際に動かしていた場合だけ1回分のUndoとして確定する
        }
        if (boxSelecting_) {
            FinishBoxSelect(m.x, m.y);
        }
        viewportDragging_ = false;
        rotateDragging_ = false;
        scaleDragging_ = false;
        activeDragAxis_ = 0;
    }

    // 右クリック（ドラッグせずに離した時だけ）で、その場の対象に応じたメニューを開く
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        constexpr float kContextClickTolerancePx = 4.0f;
        const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
        if (std::abs(dragDelta.x) < kContextClickTolerancePx && std::abs(dragDelta.y) < kContextClickTolerancePx) {
            contextKind_ = SelKind::None;
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

bool StageEditor::SelectionWorldPosition(Vector3& outWorld) const
{
    const ISelectionKind& selected = SelectionKindOf(selKind_);
    return !selected.IsScreenAnchor(*this, selIndex_) && selected.WorldPosition(*this, selIndex_, outWorld);
}

namespace {
// ハンドルの見た目と判定（スクリーンpx固定。カメラの遠近に関係なく同じ大きさで掴める）
constexpr float kHandleLengthPx = 70.0f;
constexpr float kHandlePickPx = 10.0f;
constexpr float kHandleCenterPx = 9.0f;
constexpr float kHandleArrowPx = 10.0f;
constexpr float kRotateRingPx = 60.0f;
constexpr float kRotateRingPickPx = 9.0f;
constexpr float kScaleTipPx = 6.0f;
constexpr float kHandleThicknessPx = 2.5f;
constexpr int kRotateRingSegments = 48;
constexpr ImU32 kHandleColorX = IM_COL32(255, 70, 70, 230);
constexpr ImU32 kHandleColorY = IM_COL32(90, 255, 90, 230);
constexpr ImU32 kHandleColorZ = IM_COL32(90, 150, 255, 230);
constexpr ImU32 kHandleColorCenter = IM_COL32(255, 255, 255, 200);
constexpr ImU32 kHandleColorRing = IM_COL32(90, 150, 255, 230);
constexpr float kBoxSelectMinPx = 4.0f;
constexpr float kRotatePerPixel = 0.01f; // 回転ドラッグ1pxあたりのラジアン
constexpr float kScalePerPixel = 0.01f; // 拡縮ドラッグ1pxあたりの倍率変化
constexpr float kMinScale = 0.05f;

// 各軸のスクリーン上の向き（原点と原点+単位ベクトルを投影した差）。奥行き軸は投影がつぶれるので斜めへ逃がす
bool AxisScreenDirections(const Vector3& world, ImVec2& outOrigin, ImVec2 outDirs[3])
{
    if (!DiagnosticsDraw::WorldToScreen(world, outOrigin)) {
        return false;
    }
    const Vector3 units[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    constexpr float kInvSqrt2 = 0.70710678f; // 斜め45度の単位ベクトル成分
    const ImVec2 fallback[3] = { { 1.0f, 0.0f }, { 0.0f, -1.0f }, { -kInvSqrt2, kInvSqrt2 } };
    constexpr float kMinProjectedPx = 2.0f;
    for (int i = 0; i < 3; ++i) {
        ImVec2 tip;
        ImVec2 dir = fallback[i];
        if (DiagnosticsDraw::WorldToScreen(world + units[i], tip)) {
            const float dx = tip.x - outOrigin.x;
            const float dy = tip.y - outOrigin.y;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > kMinProjectedPx) {
                dir = { dx / len, dy / len };
            }
        }
        outDirs[i] = dir;
    }
    return true;
}

float DistancePointToSegment(float px, float py, const ImVec2& a, const ImVec2& b)
{
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

bool StageEditor::PickTransformHandle(float mouseX, float mouseY, int& outAxis, bool& outUniform) const
{
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
    const float centerDx = mouseX - origin.x;
    const float centerDy = mouseY - origin.y;
    const float centerDist = std::sqrt(centerDx * centerDx + centerDy * centerDy);

    if (transformTool_ == TransformTool::Rotate) {
        // 回転はトリガーや外部エンティティには無いので配置物だけ
        if (selKind_ != SelKind::Object) {
            return false;
        }
        return std::abs(centerDist - kRotateRingPx) <= kRotateRingPickPx;
    }

    if (centerDist <= kHandleCenterPx) {
        outAxis = 0;
        outUniform = transformTool_ == TransformTool::Scale;
        return true;
    }
    if (transformTool_ == TransformTool::Scale && selKind_ != SelKind::Object) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        const ImVec2 tip = { origin.x + dirs[i].x * kHandleLengthPx, origin.y + dirs[i].y * kHandleLengthPx };
        if (DistancePointToSegment(mouseX, mouseY, origin, tip) <= kHandlePickPx) {
            outAxis = i + 1;
            return true;
        }
    }
    return false;
}

void StageEditor::DrawTransformHandles()
{
    Vector3 world;
    if (!SelectionWorldPosition(world)) {
        return;
    }
    ImVec2 origin;
    ImVec2 dirs[3];
    if (!AxisScreenDirections(world, origin, dirs)) {
        return;
    }
    ImDrawList* dl = DiagnosticsDraw::GetDrawList();
    const ImU32 axisColors[3] = { kHandleColorX, kHandleColorY, kHandleColorZ };

    if (transformTool_ == TransformTool::Rotate) {
        if (selKind_ == SelKind::Object) {
            dl->AddCircle(origin, kRotateRingPx, kHandleColorRing, kRotateRingSegments, kHandleThicknessPx);
            dl->AddCircleFilled(origin, kHandleCenterPx * 0.5f, kHandleColorCenter);
        }
        return;
    }

    for (int i = 0; i < 3; ++i) {
        const bool active = activeDragAxis_ == i + 1;
        const ImU32 color = active ? kHandleColorCenter : axisColors[i];
        const ImVec2 tip = { origin.x + dirs[i].x * kHandleLengthPx, origin.y + dirs[i].y * kHandleLengthPx };
        dl->AddLine(origin, tip, color, kHandleThicknessPx);
        if (transformTool_ == TransformTool::Scale) {
            dl->AddRectFilled({ tip.x - kScaleTipPx, tip.y - kScaleTipPx }, { tip.x + kScaleTipPx, tip.y + kScaleTipPx }, color);
        } else {
            // 矢印の先端（軸方向に対して左右へ開いた三角形）
            const ImVec2 back = { tip.x - dirs[i].x * kHandleArrowPx, tip.y - dirs[i].y * kHandleArrowPx };
            const ImVec2 side = { -dirs[i].y * kHandleArrowPx * 0.5f, dirs[i].x * kHandleArrowPx * 0.5f };
            dl->AddTriangleFilled(tip, { back.x + side.x, back.y + side.y }, { back.x - side.x, back.y - side.y }, color);
        }
    }
    dl->AddRectFilled({ origin.x - kHandleCenterPx, origin.y - kHandleCenterPx },
        { origin.x + kHandleCenterPx, origin.y + kHandleCenterPx }, IM_COL32(255, 255, 255, 60));
    dl->AddRect({ origin.x - kHandleCenterPx, origin.y - kHandleCenterPx },
        { origin.x + kHandleCenterPx, origin.y + kHandleCenterPx }, kHandleColorCenter);
}

void StageEditor::UpdateRotateScaleDrag(float deltaX, float deltaY)
{
    if (selKind_ != SelKind::Object || (deltaX == 0.0f && deltaY == 0.0f)) {
        return;
    }
    for (int index : selectedObjectIndices_) {
        if (index < 0 || index >= static_cast<int>(objects_.size())) {
            continue;
        }
        ObjectEntry& entry = objects_[index];
        ObjectDesc& desc = entry.desc;
        if (rotateDragging_) {
            // 右へ動かすと反時計回り。Ctrlを押していれば15度刻みに揃える
            constexpr float kRotateSnapRadians = 0.261799f;
            desc.rotation.z += -deltaX * kRotatePerPixel;
            if (ImGui::GetIO().KeyCtrl) {
                desc.rotation.z = std::round(desc.rotation.z / kRotateSnapRadians) * kRotateSnapRadians;
            }
        } else if (scaleDragging_) {
            const float factor = 1.0f + (deltaX - deltaY) * kScalePerPixel;
            if (scaleUniform_ || activeDragAxis_ == 0) {
                desc.scale = desc.scale * factor;
            } else if (activeDragAxis_ == 1) {
                desc.scale.x *= factor;
            } else if (activeDragAxis_ == 2) {
                desc.scale.y *= factor;
            } else {
                desc.scale.z *= factor;
            }
            desc.scale.x = (std::max)(desc.scale.x, kMinScale);
            desc.scale.y = (std::max)(desc.scale.y, kMinScale);
            desc.scale.z = (std::max)(desc.scale.z, kMinScale);
        }
        if (ObjectKind::Of(desc.kind).IsVisual()) {
            RefreshTransforms(entry);
        }
    }
    MarkUndoDirty();
}

void StageEditor::FinishBoxSelect(float mouseX, float mouseY)
{
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
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const ObjectDesc& desc = objects_[i].desc;
        if (desc.kind == ObjectKindName::kUIText && !showUIText_) {
            continue;
        }
        ImVec2 screen;
        if (IsScreenAnchorObject(desc)) {
            const Vector3 image = viewport_.ScreenToImage(desc.position.x, desc.position.y);
            screen = { image.x, image.y };
        } else if (!DiagnosticsDraw::WorldToScreen(WorldPositionOf(desc), screen)) {
            continue;
        }
        if (screen.x < minX || screen.x > maxX || screen.y < minY || screen.y > maxY) {
            continue;
        }
        if (std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), i) == selectedObjectIndices_.end()) {
            selectedObjectIndices_.push_back(i);
        }
    }
    if (selectedObjectIndices_.empty()) {
        selKind_ = SelKind::None;
        selIndex_ = -1;
        return;
    }
    selKind_ = SelKind::Object;
    selIndex_ = selectedObjectIndices_.back();
    statusMessage_ = std::to_string(selectedObjectIndices_.size()) + " 個を選択しました";
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

bool StageEditor::PickUILayoutHandle(float mouseX, float mouseY, UILayout*& outLayout, size_t& outIndex) const
{
    float bestDistanceSq = kUIHandlePickRadiusPx * kUIHandlePickRadiusPx;
    bool found = false;
    for (const UILayout::PositionHandle& handle : UILayout::CollectPositionHandles()) {
        const Vector3 image = viewport_.ScreenToImage(handle.position.x, handle.position.y);
        const float dx = image.x - mouseX;
        const float dy = image.y - mouseY;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq <= bestDistanceSq) {
            bestDistanceSq = distanceSq;
            outLayout = handle.layout;
            outIndex = handle.index;
            found = true;
        }
    }
    return found;
}

void StageEditor::DrawUILayoutHandles()
{
    ImDrawList* dl = DiagnosticsDraw::GetDrawList();
    for (const UILayout::PositionHandle& handle : UILayout::CollectPositionHandles()) {
        const bool selected = UILayout::IsSelectedHandle(handle.layout, handle.index);
        const ImU32 color = selected ? kUIHandleSelectedColor : kUIHandleColor;
        const Vector3 image = viewport_.ScreenToImage(handle.position.x, handle.position.y);
        const ImVec2 minCorner = { image.x - kUIHandleHalfSize, image.y - kUIHandleHalfSize };
        const ImVec2 maxCorner = { image.x + kUIHandleHalfSize, image.y + kUIHandleHalfSize };
        dl->AddRectFilled(minCorner, maxCorner, kUIHandleFill);
        dl->AddRect(minCorner, maxCorner, color, 0.0f, 0, kUIHandleLineThickness);
        dl->AddText({ maxCorner.x + kUIHandleLabelGapX, minCorner.y }, color, handle.label.c_str());
    }
}

#else
void StageEditor::SetParentPreservingWorld(int, int) { }
int StageEditor::AddObjectAt(const std::string&, const Vector3&) { return -1; }
void StageEditor::AddTriggerAt(const Vector3&) { }
void StageEditor::SelectAllObjects() { }
void StageEditor::ClearSelection() { }
void StageEditor::DrawGizmos() { }
void StageEditor::UpdateFreeCamera(engine::Input*, float) { }
void StageEditor::UpdateViewportInteraction() { }
bool StageEditor::MouseToGround(float, float, Vector3&) const { return false; }
bool StageEditor::SelectionWorldPosition(Vector3&) const { return false; }
bool StageEditor::PickTransformHandle(float, float, int&, bool&) const { return false; }
void StageEditor::DrawTransformHandles() { }
void StageEditor::UpdateRotateScaleDrag(float, float) { }
void StageEditor::FinishBoxSelect(float, float) { }
bool StageEditor::PickUILayoutHandle(float, float, UILayout*&, size_t&) const { return false; }
void StageEditor::DrawUILayoutHandles() { }

#endif
