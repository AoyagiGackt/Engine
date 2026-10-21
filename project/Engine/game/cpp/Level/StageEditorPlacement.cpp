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
#include "StageEditorUiStyle.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#endif
using namespace engine::graphics;

#ifdef USE_IMGUI
namespace engine::game {
namespace {
using StageEditorUiStyle::kZDragPerPixel;
using StageEditorUiStyle::kPickRadiusPx;
} // namespace

/** @brief クリック位置に最も近いオブジェクト/トリガー/外部エンティティを探す（画面 kPickRadiusPx 以内、モデル外形にヒットすれば距離0扱い） */
bool StageEditor::PickViewportTarget(float mouseX, float mouseY, SelKind& outKind, int& outIdx) const
{
    float bestDist = kPickRadiusPx;
    SelKind bestKind = SelKind::None;
    int bestIdx = -1;

    // 画面上の点とマウスの距離を比べ、今までで一番近ければ候補を入れ替える
    auto considerScreen = [&](float screenX, float screenY, SelKind kind, int index) {
        const float dx = screenX - mouseX;
        const float dy = screenY - mouseY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < bestDist)
        {
            bestDist = dist;
            bestKind = kind;
            bestIdx = index;
        }
    };
    // ワールド座標の点を画面へ投影してから比べる（画面外なら候補にしない）
    auto consider = [&](const Vector3& worldPos, SelKind kind, int index) {
        ImVec2 s;
        if (!DiagnosticsDraw::WorldToScreen(worldPos, s))
        {
            return;
        }
        considerScreen(s.x, s.y, kind, index);
    };

    // モデルを持つ配置物/外部オブジェクトは、原点との距離ではなく画面上の外形バウンディングボックスに
    // マウスが重なっているかで判定する（大きいモデルほど原点から離れた場所もクリックできるようにするため）
    auto projectedBounds = [&](const std::vector<Model::VertexData>& vertices, const Matrix4x4& worldMatrix,
                                float& outMinX, float& outMinY, float& outMaxX, float& outMaxY) {
        // 全頂点を画面へ投影し、画面上の外接矩形を求める（1つも映らなければ false）
        outMinX = FLT_MAX;
        outMinY = FLT_MAX;
        outMaxX = -FLT_MAX;
        outMaxY = -FLT_MAX;
        bool projected = false;
        for (const auto& vertex : vertices)
        {
            const Vector4& p = vertex.position;
            const Vector3 worldVertex = TransformPoint({ p.x, p.y, p.z }, worldMatrix);
            ImVec2 screen;
            if (!DiagnosticsDraw::WorldToScreen(worldVertex, screen))
            {
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
        // 最初の実体のモデルの頂点を使う（列配置でも先頭の1つで判定する）
        if (entry.instances.empty() || !entry.instances.front()->GetModel())
        {
            return;
        }
        const auto& vertices = entry.instances.front()->GetModel()->GetVertices();
        if (vertices.empty())
        {
            return;
        }
        const ObjectDesc& desc = entry.desc;
        // 外接矩形に少し余白を足した中にマウスがあれば、距離0として最優先で選ぶ
        const Matrix4x4 worldMatrix = MakeAffineMatrix(desc.scale, desc.rotation, WorldPositionOf(desc));
        float minX, minY, maxX, maxY;
        constexpr float kPickPadding = 4.0f;
        if (projectedBounds(vertices, worldMatrix, minX, minY, maxX, maxY)
            && mouseX >= minX - kPickPadding && mouseX <= maxX + kPickPadding
            && mouseY >= minY - kPickPadding && mouseY <= maxY + kPickPadding && bestDist > 0.0f)
        {
            bestDist = 0.0f;
            bestKind = SelKind::Object;
            bestIdx = index;
        }
    };

    // テキスト表示を隠している間は、見えていないui_textを誤って選択できないようにする
    VisitShownObjects([&](int i, const ObjectDesc& d) {
        // "screen"座標のui_textはpositionが既にスクリーンpx座標なので、3D投影せずマウスと直接比較する
        if (IsScreenAnchorObject(d))
        {
            const Vector3 image = viewport_.ScreenToImage(d.position.x, d.position.y);
            considerScreen(image.x, image.y, SelKind::Object, i);
            return;
        }
        considerModelBounds(objects_[i], i);
        consider(WorldPositionOf(d), SelKind::Object, i);
    });
    // トリガーと、位置を持つ外部の実体は中心点の近さで選ぶ
    for (int i = 0; i < static_cast<int>(triggers_.size()); ++i)
    {
        consider(triggers_[i].GetDesc().position, SelKind::Trigger, i);
    }
    for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i)
    {
        if (externalEntities_[i].positionTarget)
        {
            consider(externalEntities_[i].positionTarget->GetPosition(), SelKind::External, i);
        }
    }

    // 一番近かったもの（何も無ければ None と -1）を返す
    outKind = bestKind;
    outIdx = bestIdx;
    return bestIdx >= 0;
}

/** @brief 左クリック時の選択処理（親子リンク待機中ならその接続、それ以外は選択+ドラッグ開始準備）を行う */
void StageEditor::HandleViewportClick(float mouseX, float mouseY)
{
    // 1. 選択物のハンドル（軸矢印・回転リング・中央）を掴んだら、選択を変えずにその操作を始める
    int handleAxis = 0;
    bool handleUniform = false;
    if (parentLinkChildIndex_ < 0 && PickTransformHandle(mouseX, mouseY, handleAxis, handleUniform))
    {
        viewportDragging_ = true;
        BeginUndoCapture();
        if (transformTool_ == TransformTool::Rotate)
        {
            rotateDragging_ = true;
            return;
        }
        if (transformTool_ == TransformTool::Scale)
        {
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
        if (SelectionWorldPosition(grabWorldPos))
        {
            dragRawZ_ = grabWorldPos.z;
            if (MouseToGround(mouseX, mouseY, grabGround))
            {
                dragGrabOffsetX_ = grabWorldPos.x - grabGround.x;
                dragGrabOffsetY_ = grabWorldPos.y - grabGround.y;
            }
        }
        return;
    }

    SelKind bestKind = SelKind::None;
    int bestIdx = -1;
    if (!PickViewportTarget(mouseX, mouseY, bestKind, bestIdx))
    {
        // 2. 何も無い場所: 範囲選択を始める（離した時に矩形が小さければ選択解除）
        if (parentLinkChildIndex_ < 0)
        {
            boxSelecting_ = true;
            boxStartX_ = mouseX;
            boxStartY_ = mouseY;
        }
        return;
    }

    if (parentLinkChildIndex_ >= 0 && bestKind == SelKind::Object)
    {
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
    if (grabbed.IsScreenAnchor(*this, bestIdx))
    {
        const Vector3 logicalMouse = viewport_.ImageToScreen(mouseX, mouseY);
        dragGrabOffsetX_ = objects_[bestIdx].desc.position.x - logicalMouse.x;
        dragGrabOffsetY_ = objects_[bestIdx].desc.position.y - logicalMouse.y;
        return;
    }

    // Shift+ドラッグ(Z移動)用に、選択物の現在のZをスナップ前の生値として持つ
    Vector3 grabWorldPos { };
    const bool hasGrabWorldPos = grabbed.WorldPosition(*this, bestIdx, grabWorldPos);
    if (hasGrabWorldPos)
    {
        dragRawZ_ = grabbed.LocalZ(*this, bestIdx);
    }

    // クリックした位置がオブジェクト原点とずれていても、その場で原点まで飛ばないよう、
    // 掴んだ瞬間の(オブジェクト位置 - マウス接地位置)のオフセットを控えておく
    dragGrabOffsetX_ = 0.0f;
    dragGrabOffsetY_ = 0.0f;
    Vector3 grabGround { };
    if (hasGrabWorldPos && MouseToGround(mouseX, mouseY, grabGround))
    {
        dragGrabOffsetX_ = grabWorldPos.x - grabGround.x;
        dragGrabOffsetY_ = grabWorldPos.y - grabGround.y;
    }
}

/** @brief ドラッグ中の選択物を移動させる（Shift中はマウス垂直移動をZ移動、それ以外はXY平面移動） */
void StageEditor::UpdateViewportDrag(float mouseX, float mouseY)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool mouseMoved = (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);

    const ISelectionKind& selected = SelectionKindOf(selKind_);
    if (selected.IsScreenAnchor(*this, selIndex_))
    {
        // スクリーン座標のテキストはワールド平面と無関係なので、マウスのピクセル位置へそのまま追従させる（Z移動もない）
        ObjectDesc& desc = objects_[selIndex_].desc;
        const Vector3 logicalMouse = viewport_.ImageToScreen(mouseX, mouseY);
        desc.position.x = logicalMouse.x + dragGrabOffsetX_;
        desc.position.y = logicalMouse.y + dragGrabOffsetY_;
        if (mouseMoved)
        {
            MarkUndoDirty();
        }
        return;
    }

    // ハンドルの軸矢印を掴んだ場合はその軸、そうでなければワークフローパネルの軸制限に従う
    const int axis = activeDragAxis_ != 0 ? activeDragAxis_ : gizmoAxis_;
    if (io.KeyShift || axis == StageEditorUiStyle::kGizmoAxisZ)
    {
        // カメラが遠いほど1pxあたりの移動量を増やし、近くでも遠くでも同じ操作感にする
        constexpr float kDefaultCameraDistance = 10.0f; // カメラ未設定時に操作量の基準にする距離
        float camDist = kDefaultCameraDistance;
        if (camera_)
        {
            camDist = (std::max)(1.0f, std::abs(camera_->GetTranslate().z));
        }
        dragRawZ_ += -io.MouseDelta.y * camDist * kZDragPerPixel;
        selected.SetLocalZ(*this, selIndex_, SnapValue(dragRawZ_), mouseMoved);
        return;
    }

    Vector3 ground;
    if (!MouseToGround(mouseX, mouseY, ground))
    {
        return;
    }
    // 掴んだ時のオフセットを保ったまま追従させる（クリック位置がオブジェクト原点からずれていても飛ばない）
    // スナップはワールド座標側で丸めてから、親がいる場合はローカル座標へ逆算する
    const float wx = SnapValue(ground.x + dragGrabOffsetX_);
    const float wy = SnapValue(ground.y + dragGrabOffsetY_);
    selected.DragTo(*this, selIndex_, wx, wy, axis, mouseMoved);
}

/** @brief 見た目の位置を変えずに親を付け替える */
void StageEditor::SetParentPreservingWorld(int childIndex, int parentIndex)
{
    if (childIndex < 0 || childIndex >= static_cast<int>(objects_.size()) || childIndex == parentIndex)
    {
        return;
    }
    // 親を変えても見た目の位置が変わらないよう、今のワールド座標を控えておく
    ObjectDesc& child = objects_[childIndex].desc;
    const Vector3 world = WorldPositionOf(child);
    // 親を外す: ワールド座標をそのままローカル座標にする
    if (parentIndex < 0)
    {
        if (child.parent.empty())
        {
            return;
        }
        RecordUndoSnapshotNow();
        child.parent.clear();
        child.position = world;
        statusMessage_ = child.name + " の親を外しました";
        statusTimer_ = StageEditor::kStatusNormalSeconds;
        return;
    }
    // 自分の子孫を親にすると親子が輪になるので断る
    if (parentIndex >= static_cast<int>(objects_.size())
        || IsDescendantOf(objects_[parentIndex].desc.name, child.name))
    {
        statusMessage_ = "循環する親子関係になるため接続できません";
        statusTimer_ = StageEditor::kStatusNormalSeconds;
        return;
    }
    RecordUndoSnapshotNow();
    const ObjectDesc& parent = objects_[parentIndex].desc;
    // 新しい親から見た位置にする（親と先祖のZ回転ぶん逆に回す）
    child.parent = parent.name;
    child.position = LocalPositionOf(child, world);
    statusMessage_ = child.name + " を " + parent.name + " に接続しました";
    statusTimer_ = StageEditor::kStatusNormalSeconds;
    selKind_ = SelKind::Object;
    selIndex_ = childIndex;
    selectedObjectIndices_ = { childIndex };
}

/** @brief 種類に応じた既定値で配置物を指定位置へ追加し、選択する */
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

/** @brief 指定したワールド座標にUI文字を追加し、その番号を返す */
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

/** @brief トリガーを指定位置へ追加して選択する */
void StageEditor::AddTriggerAt(const Vector3& position)
{
    RecordUndoSnapshotNow();
    TriggerDesc desc;
    desc.name = "trigger_" + std::to_string(nextSerial_++);
    desc.position = { SnapValue(position.x), SnapValue(position.y), position.z };
    desc.flag = "event_" + desc.name;
    AppendTriggerAndSelect(desc);
}

/** @brief 設定済みのトリガーを追加して、それだけを選択した状態にする（元に戻す履歴は呼び出し側で取る） */
void StageEditor::AppendTriggerAndSelect(const TriggerDesc& desc)
{
    TriggerVolume trigger;
    trigger.Init(desc);
    triggers_.push_back(std::move(trigger));
    selKind_ = SelKind::Trigger;
    selIndex_ = static_cast<int>(triggers_.size()) - 1;
    selectedObjectIndices_.clear();
}

/** @brief 文章UIを隠している間で、ビュー上で扱わない配置物か */
bool StageEditor::IsHiddenUIText(const ObjectDesc& desc) const
{
    return desc.kind == ObjectKindName::kUIText && !showUIText_;
}

/** @brief 隠している文章UIを除いた配置物すべてに visit(番号, 配置情報) を呼ぶ（中央ビューで扱う物だけを回す） */
void StageEditor::VisitShownObjects(const std::function<void(int, const ObjectDesc&)>& visit) const
{
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i)
    {
        // 配置物の陰になって邪魔な時は、テキスト表示チェックボックスで文章UIだけ一時的に隠せる
        if (!IsHiddenUIText(objects_[i].desc))
        {
            visit(i, objects_[i].desc);
        }
    }
}

/** @brief 全配置物を選択する（Ctrl+A） */
void StageEditor::SelectAllObjects()
{
    selectedObjectIndices_.clear();
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i)
    {
        selectedObjectIndices_.push_back(i);
    }
    if (selectedObjectIndices_.empty())
    {
        return;
    }
    selKind_ = SelKind::Object;
    selIndex_ = selectedObjectIndices_.back();
}

/** @brief 選択を解除する（Escape） */
void StageEditor::ClearSelection()
{
    selKind_ = SelKind::None;
    selIndex_ = -1;
    selectedObjectIndices_.clear();
    parentLinkChildIndex_ = -1;
}

} // namespace engine::game

#endif
