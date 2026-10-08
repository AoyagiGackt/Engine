/**
 * @file StageEditorPlacement.cpp
 * @brief 画面内の選択・ドラッグ・配置・親子関係の操作
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

bool StageEditor::PickViewportTarget(float mouseX, float mouseY, SelKind& outKind, int& outIdx) const
{
    float bestDist = kPickRadiusPx;
    SelKind bestKind = SelKind::None;
    int bestIdx = -1;

    auto consider = [&](const Vector3& worldPos, SelKind kind, int index) {
        ImVec2 s;
        if (!DiagnosticsDraw::WorldToScreen(worldPos, s)) {
            return;
        }
        float dx = s.x - mouseX;
        float dy = s.y - mouseY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < bestDist) {
            bestDist = dist;
            bestKind = kind;
            bestIdx = index;
        }
    };

    // モデルを持つ配置物/外部オブジェクトは、原点との距離ではなく画面上の外形バウンディングボックスに
    // マウスが重なっているかで判定する（大きいモデルほど原点から離れた場所もクリックできるようにするため）
    auto projectedBounds = [&](const std::vector<Model::VertexData>& vertices, const Matrix4x4& worldMatrix,
                                float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) {
        outMinX = FLT_MAX;
        outMinY = FLT_MAX;
        outMaxX = -FLT_MAX;
        outMaxY = -FLT_MAX;
        bool projected = false;
        for (const auto& vertex : vertices) {
            const Vector4& p = vertex.position;
            Vector3 worldVertex = {
                p.x * worldMatrix.m[0][0] + p.y * worldMatrix.m[1][0] + p.z * worldMatrix.m[2][0] + worldMatrix.m[3][0],
                p.x * worldMatrix.m[0][1] + p.y * worldMatrix.m[1][1] + p.z * worldMatrix.m[2][1] + worldMatrix.m[3][1],
                p.x * worldMatrix.m[0][2] + p.y * worldMatrix.m[1][2] + p.z * worldMatrix.m[2][2] + worldMatrix.m[3][2]
            };
            ImVec2 screen;
            if (!DiagnosticsDraw::WorldToScreen(worldVertex, screen)) {
                continue;
            }
            projected = true;
            outMinX = (std::min)(outMinX, screen.x);
            outMinY = (std::min)(outMinY, screen.y);
            outMaxX = (std::max)(outMaxX, screen.x);
            outMaxY = (std::max)(outMaxY, screen.y);
        }
        return projected;
    };

    auto considerModelBounds = [&](const ObjectEntry& entry, int index) {
        if (entry.instances.empty() || !entry.instances.front()->GetModel()) {
            return;
        }
        const auto& vertices = entry.instances.front()->GetModel()->GetVertices();
        if (vertices.empty()) {
            return;
        }
        const ObjectDesc& desc = entry.desc;
        const Matrix4x4 worldMatrix = MakeAffineMatrix(desc.scale, desc.rotation, WorldPositionOf(desc));
        float minX, minY, maxX, maxY;
        constexpr float kPickPadding = 4.0f;
        if (projectedBounds(vertices, worldMatrix, minX, minY, maxX, maxY)
            && mouseX >= minX - kPickPadding && mouseX <= maxX + kPickPadding
            && mouseY >= minY - kPickPadding && mouseY <= maxY + kPickPadding && bestDist > 0.0f) {
            bestDist = 0.0f;
            bestKind = SelKind::Object;
            bestIdx = index;
        }
    };

    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const ObjectDesc& d = objects_[i].desc;
        // テキスト表示を隠している間は、見えていないui_textを誤って選択できないようにする
        if (d.kind == ObjectKindName::kUIText && !showUIText_) {
            continue;
        }
        // "screen"座標のui_textはpositionが既にスクリーンpx座標なので、3D投影せずマウスと直接比較する
        if (IsScreenAnchorObject(d)) {
            const Vector3 image = viewport_.ScreenToImage(d.position.x, d.position.y);
            float dx = image.x - mouseX;
            float dy = image.y - mouseY;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestKind = SelKind::Object;
                bestIdx = i;
            }
            continue;
        }
        considerModelBounds(objects_[i], i);
        consider(WorldPositionOf(d), SelKind::Object, i);
    }
    for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
        consider(triggers_[i].GetDesc().position, SelKind::Trigger, i);
    }
    for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
        if (externalEntities_[i].position) {
            consider(*externalEntities_[i].position, SelKind::External, i);
        }
    }

    outKind = bestKind;
    outIdx = bestIdx;
    return bestIdx >= 0;
}

void StageEditor::HandleViewportClick(float mouseX, float mouseY)
{
    // 1. 選択物のハンドル（軸矢印・回転リング・中央）を掴んだら、選択を変えずにその操作を始める
    int handleAxis = 0;
    bool handleUniform = false;
    if (parentLinkChildIndex_ < 0 && PickTransformHandle(mouseX, mouseY, handleAxis, handleUniform)) {
        viewportDragging_ = true;
        BeginUndoCapture();
        if (transformTool_ == TransformTool::Rotate) {
            rotateDragging_ = true;
            return;
        }
        if (transformTool_ == TransformTool::Scale) {
            scaleDragging_ = true;
            scaleUniform_ = handleUniform;
            activeDragAxis_ = handleAxis;
            return;
        }
        // 移動: 軸矢印なら軸制限、中央なら自由移動。掴んだ位置のオフセットを控えて飛びを防ぐ
        activeDragAxis_ = handleAxis;
        Vector3 grabWorldPos;
        Vector3 grabGround;
        dragGrabOffsetX_ = 0.0f;
        dragGrabOffsetY_ = 0.0f;
        if (SelectionWorldPosition(grabWorldPos)) {
            dragRawZ_ = grabWorldPos.z;
            if (MouseToGround(mouseX, mouseY, grabGround)) {
                dragGrabOffsetX_ = grabWorldPos.x - grabGround.x;
                dragGrabOffsetY_ = grabWorldPos.y - grabGround.y;
            }
        }
        return;
    }

    SelKind bestKind = SelKind::None;
    int bestIdx = -1;
    if (!PickViewportTarget(mouseX, mouseY, bestKind, bestIdx)) {
        // 2. 何も無い場所: 範囲選択を始める（離した時に矩形が小さければ選択解除）
        if (parentLinkChildIndex_ < 0) {
            boxSelecting_ = true;
            boxStartX_ = mouseX;
            boxStartY_ = mouseY;
        }
        return;
    }

    if (parentLinkChildIndex_ >= 0 && bestKind == SelKind::Object) {
        const int childIndex = parentLinkChildIndex_;
        parentLinkChildIndex_ = -1;
        SetParentPreservingWorld(childIndex, bestIdx);
        return;
    }

    const ISelectionKind& grabbed = SelectionKindOf(bestKind);
    grabbed.SelectFromClick(*this, bestIdx, ImGui::GetIO().KeyCtrl);
    viewportDragging_ = true;

    // ドラッグ1回ぶんを1つのUndoにまとめるため、変更前をここで控える
    // （エンティティはスナップショット対象外なので、動かしても確定時に捨てられる）
    BeginUndoCapture();

    // "screen"座標のui_textはワールド平面と無関係なので、マウスのスクリーンpx位置基準でオフセットを控える
    if (grabbed.IsScreenAnchor(*this, bestIdx)) {
        const Vector3 logicalMouse = viewport_.ImageToScreen(mouseX, mouseY);
        dragGrabOffsetX_ = objects_[bestIdx].desc.position.x - logicalMouse.x;
        dragGrabOffsetY_ = objects_[bestIdx].desc.position.y - logicalMouse.y;
        return;
    }

    // Shift+ドラッグ(Z移動)用に、選択物の現在のZをスナップ前の生値として持つ
    Vector3 grabWorldPos { };
    const bool hasGrabWorldPos = grabbed.WorldPosition(*this, bestIdx, grabWorldPos);
    if (hasGrabWorldPos) {
        dragRawZ_ = grabbed.LocalZ(*this, bestIdx);
    }

    // クリックした位置がオブジェクト原点とずれていても、その場で原点まで飛ばないよう、
    // 掴んだ瞬間の(オブジェクト位置 - マウス接地位置)のオフセットを控えておく
    dragGrabOffsetX_ = 0.0f;
    dragGrabOffsetY_ = 0.0f;
    Vector3 grabGround { };
    if (hasGrabWorldPos && MouseToGround(mouseX, mouseY, grabGround)) {
        dragGrabOffsetX_ = grabWorldPos.x - grabGround.x;
        dragGrabOffsetY_ = grabWorldPos.y - grabGround.y;
    }
}

void StageEditor::UpdateViewportDrag(float mouseX, float mouseY)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool mouseMoved = (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);

    const ISelectionKind& selected = SelectionKindOf(selKind_);
    if (selected.IsScreenAnchor(*this, selIndex_)) {
        // スクリーン座標のテキストはワールド平面と無関係なので、マウスのピクセル位置へそのまま追従させる（Z移動もない）
        ObjectDesc& desc = objects_[selIndex_].desc;
        const Vector3 logicalMouse = viewport_.ImageToScreen(mouseX, mouseY);
        desc.position.x = logicalMouse.x + dragGrabOffsetX_;
        desc.position.y = logicalMouse.y + dragGrabOffsetY_;
        if (mouseMoved) {
            MarkUndoDirty();
        }
        return;
    }

    // ハンドルの軸矢印を掴んだ場合はその軸、そうでなければワークフローパネルの軸制限に従う
    const int axis = activeDragAxis_ != 0 ? activeDragAxis_ : gizmoAxis_;
    if (io.KeyShift || axis == 3) {
        // カメラが遠いほど1pxあたりの移動量を増やし、近くでも遠くでも同じ操作感にする
        constexpr float kDefaultCameraDistance = 10.0f; // カメラ未設定時に操作量の基準にする距離
        float camDist = kDefaultCameraDistance;
        if (camera_) {
            camDist = (std::max)(1.0f, std::abs(camera_->GetTranslate().z));
        }
        dragRawZ_ += -io.MouseDelta.y * camDist * kZDragPerPixel;
        selected.SetLocalZ(*this, selIndex_, SnapValue(dragRawZ_), mouseMoved);
        return;
    }

    Vector3 ground;
    if (!MouseToGround(mouseX, mouseY, ground)) {
        return;
    }
    // 掴んだ時のオフセットを保ったまま追従させる（クリック位置がオブジェクト原点からずれていても飛ばない）
    // スナップはワールド座標側で丸めてから、親がいる場合はローカル座標へ逆算する
    const float wx = SnapValue(ground.x + dragGrabOffsetX_);
    const float wy = SnapValue(ground.y + dragGrabOffsetY_);
    selected.DragTo(*this, selIndex_, wx, wy, axis, mouseMoved);
}

void StageEditor::SetParentPreservingWorld(int childIndex, int parentIndex)
{
    if (childIndex < 0 || childIndex >= static_cast<int>(objects_.size()) || childIndex == parentIndex) {
        return;
    }
    ObjectDesc& child = objects_[childIndex].desc;
    const Vector3 world = WorldPositionOf(child);
    if (parentIndex < 0) {
        if (child.parent.empty()) {
            return;
        }
        RecordUndoSnapshotNow();
        child.parent.clear();
        child.position = world;
        statusMessage_ = child.name + " の親を外しました";
        statusTimer_ = StageEditor::kStatusNormalSeconds;
        return;
    }
    if (parentIndex >= static_cast<int>(objects_.size())
        || IsDescendantOf(objects_[parentIndex].desc.name, child.name)) {
        statusMessage_ = "循環する親子関係になるため接続できません";
        statusTimer_ = StageEditor::kStatusNormalSeconds;
        return;
    }
    RecordUndoSnapshotNow();
    const ObjectDesc& parent = objects_[parentIndex].desc;
    const Vector3 parentWorld = WorldPositionOf(parent);
    const float c = std::cos(-parent.rotation.z);
    const float s = std::sin(-parent.rotation.z);
    const float dx = world.x - parentWorld.x;
    const float dy = world.y - parentWorld.y;
    child.parent = parent.name;
    child.position = { dx * c - dy * s, dx * s + dy * c, world.z - parentWorld.z };
    statusMessage_ = child.name + " を " + parent.name + " に接続しました";
    statusTimer_ = StageEditor::kStatusNormalSeconds;
    selKind_ = SelKind::Object;
    selIndex_ = childIndex;
    selectedObjectIndices_ = { childIndex };
}

int StageEditor::AddObjectAt(const std::string& kind, const Vector3& position)
{
    RecordUndoSnapshotNow();
    ObjectEntry entry;
    ObjectDesc& desc = entry.desc;
    desc.kind = kind;
    desc.type = PlacementType::Static;
    desc.position = { SnapValue(position.x), SnapValue(position.y), position.z };
    ObjectKind::Of(kind).InitializeNewDesc(desc, nextSerial_++);
    entry.authoredPosition = desc.position;
    objects_.push_back(std::move(entry));
    const int index = static_cast<int>(objects_.size()) - 1;
    RegenerateInstances(objects_.back());
    selKind_ = SelKind::Object;
    selIndex_ = index;
    selectedObjectIndices_ = { index };
    return index;
}

int StageEditor::AddUITextAt(const Vector3& position, bool screenSpace)
{
    const int index = AddObjectAt("ui_text", position);
    auto& entry = objects_[index];
    entry.desc.textSpace = screenSpace ? TextSpace::Screen : TextSpace::World;
    entry.desc.position = position;
    entry.authoredPosition = position;
    entry.desc.text = "ここに文章を入力";
    constexpr float kScreenTextScale = 1.5f;
    constexpr float kWorldTextScale = 1.1f;
    entry.desc.textScale = screenSpace ? kScreenTextScale : kWorldTextScale;
    showUIText_ = true;
    focusTextEditor_ = true;
    transformTool_ = TransformTool::Move;
    statusMessage_ = "右の「文章UI」で編集できます。画面の十字をドラッグして配置し、Ctrl+Sで保存します";
    statusTimer_ = StageEditor::kStatusVeryLongSeconds;
    return index;
}

void StageEditor::AddTriggerAt(const Vector3& position)
{
    RecordUndoSnapshotNow();
    TriggerDesc desc;
    desc.name = "trigger_" + std::to_string(nextSerial_++);
    desc.position = { SnapValue(position.x), SnapValue(position.y), position.z };
    desc.flag = "event_" + desc.name;
    TriggerVolume trigger;
    trigger.Init(desc);
    triggers_.push_back(std::move(trigger));
    selKind_ = SelKind::Trigger;
    selIndex_ = static_cast<int>(triggers_.size()) - 1;
    selectedObjectIndices_.clear();
}

void StageEditor::SelectAllObjects()
{
    selectedObjectIndices_.clear();
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        selectedObjectIndices_.push_back(i);
    }
    if (selectedObjectIndices_.empty()) {
        return;
    }
    selKind_ = SelKind::Object;
    selIndex_ = selectedObjectIndices_.back();
}

void StageEditor::ClearSelection()
{
    selKind_ = SelKind::None;
    selIndex_ = -1;
    selectedObjectIndices_.clear();
    parentLinkChildIndex_ = -1;
}

#endif
