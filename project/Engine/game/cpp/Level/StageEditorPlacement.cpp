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
#include "UILayout.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#endif
using namespace engine::graphics;

#ifdef USE_IMGUI
namespace engine::game {
namespace {
using stageEditorUiStyle::kZDragPerPixel;
using stageEditorUiStyle::kPickRadiusPx;
using stageEditorUiStyle::kPickMarkerPriorityPx;
using stageEditorUiStyle::kDragStartThresholdPx;
} // namespace

/** @brief クリック位置の対象を探す（印のすぐ近くの物を最優先し、次にモデル外形に重なる物のうち画面上で小さい物、最後に kPickRadiusPx 以内で最も近い物） */
bool StageEditor::PickViewportTarget(float mouseX, float mouseY, SelKind& outKind, int& outIdx) const {
    float bestDist = kPickRadiusPx;
    float bestArea = FLT_MAX; // 外形や印で直接当たった候補の大きさ（小さい物ほど優先する）
    SelKind bestKind = SelKind::kNone;
    int bestIdx = -1;

    // 大きな床や壁の外形に隠れて小さな物が選べなくならないよう、直接当たった候補は小さい物を優先する
    auto considerDirectHit = [&](float area, SelKind kind, int index) {
        if (area < bestArea) {
            bestArea = area;
            bestDist = 0.0f;
            bestKind = kind;
            bestIdx = index;
        }
    };
    // 画面上の点とマウスの距離を比べる（印のすぐ近くなら直接当たった扱い。それ以外は今までで一番近ければ候補を入れ替える）
    auto considerScreen = [&](float screenX, float screenY, SelKind kind, int index) {
        const float dx = screenX - mouseX;
        const float dy = screenY - mouseY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= kPickMarkerPriorityPx) {
            considerDirectHit(dist * dist, kind, index);
            return;
        }
        if (dist < bestDist) {
            bestDist = dist;
            bestKind = kind;
            bestIdx = index;
        }
    };
    // ワールド座標の点を画面へ投影してから比べる（画面外なら候補にしない）
    auto consider = [&](const Vector3& worldPos, SelKind kind, int index) {
        ImVec2 s;
        if (!diagnosticsDraw::WorldToScreen(worldPos, s)) {
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
        for (const auto& vertex : vertices) {
            const Vector4& p = vertex.position;
            const Vector3 worldVertex = TransformPoint({ p.x, p.y, p.z }, worldMatrix);
            ImVec2 screen;
            if (!diagnosticsDraw::WorldToScreen(worldVertex, screen)) {
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
        if (entry.instances.empty() || !entry.instances.front()->GetModel()) {
            return;
        }
        const auto& vertices = entry.instances.front()->GetModel()->GetVertices();
        if (vertices.empty()) {
            return;
        }
        const ObjectDesc& desc = entry.desc;
        // 外接矩形に少し余白を足した中にマウスがあれば直接当たった扱いにする
        const Matrix4x4 worldMatrix = MakeAffineMatrix(desc.scale, desc.rotation, WorldPositionOf(desc));
        float minX, minY, maxX, maxY;
        constexpr float kPickPadding = 4.0f;
        if (projectedBounds(vertices, worldMatrix, minX, minY, maxX, maxY)
            && mouseX >= minX - kPickPadding && mouseX <= maxX + kPickPadding
            && mouseY >= minY - kPickPadding && mouseY <= maxY + kPickPadding) {
            considerDirectHit((maxX - minX) * (maxY - minY), SelKind::kObject, index);
        }
    };

    // テキスト表示を隠している間は、見えていないui_textを誤って選択できないようにする
    VisitShownObjects([&](int i, const ObjectDesc& d) {
        // "screen"座標のui_textはpositionが既にスクリーンpx座標なので、3D投影せずマウスと直接比較する
        if (IsScreenAnchorObject(d)) {
            const Vector3 image = viewport_.ScreenToImage(d.position.x, d.position.y);
            considerScreen(image.x, image.y, SelKind::kObject, i);
            return;
        }
        considerModelBounds(objects_[i], i);
        consider(WorldPositionOf(d), SelKind::kObject, i);
    });
    // トリガーと、位置を持つ外部の実体は中心点の近さで選ぶ
    for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
        consider(triggers_[i].GetDesc().position, SelKind::kTrigger, i);
    }
    for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
        if (externalEntities_[i].positionTarget) {
            consider(externalEntities_[i].positionTarget->GetPosition(), SelKind::kExternal, i);
        }
    }

    // 一番近かったもの（何も無ければ None と -1）を返す
    outKind = bestKind;
    outIdx = bestIdx;
    return bestIdx >= 0;
}

/** @brief 左クリック時の選択処理（親子リンク待機中ならその接続、それ以外は選択+ドラッグ開始準備）を行う */
void StageEditor::HandleViewportClick(float mouseX, float mouseY) {
    const bool linkingParent = parentLinkChildIndex_ >= 0;
    ClearViewportHover();

    // 1. 選択物のハンドル（軸矢印・回転リング・中央）を掴んだら、選択を変えずにその操作を始める
    int handleAxis = 0;
    bool handleUniform = false;
    if (!linkingParent && PickTransformHandle(mouseX, mouseY, handleAxis, handleUniform)) {
        viewportDragging_ = true;
        BeginUndoCapture();
        CaptureDragStart(mouseX, mouseY);
        activeDragAxis_ = handleAxis;
        scaleUniform_ = handleUniform;
        rotateDragging_ = transformTool_ == TransformTool::kRotate;
        scaleDragging_ = transformTool_ == TransformTool::kScale;
        return;
    }

    // 2. 画面UIの印（3Dの配置物より手前に表示されているので先に見る。Altで無視できる）
    UILayout* uiLayout = nullptr;
    size_t uiIndex = 0;
    if (!linkingParent && PickUILayoutHandle(mouseX, mouseY, uiLayout, uiIndex)) {
        BeginUILayoutDrag(uiLayout, uiIndex, mouseX, mouseY);
        return;
    }

    SelKind bestKind = SelKind::kNone;
    int bestIdx = -1;
    if (!PickViewportTarget(mouseX, mouseY, bestKind, bestIdx)) {
        // 3. 何も無い場所: 範囲選択を始める（離した時に矩形が小さければ選択解除）
        if (!linkingParent) {
            boxSelecting_ = true;
            boxStartX_ = mouseX;
            boxStartY_ = mouseY;
        }
        return;
    }

    if (linkingParent && bestKind == SelKind::kObject) {
        const int childIndex = parentLinkChildIndex_;
        parentLinkChildIndex_ = -1;
        SetParentPreservingWorld(childIndex, bestIdx);
        return;
    }

    // 4. 配置物など: 選択してドラッグの準備をする
    // 選択中の物を押した時は、まとめて動かせるよう今の選択を保つ（動かさず離した時に FinishViewportDrag で確定する）
    const bool additive = ImGui::GetIO().KeyCtrl;
    const bool alreadySelected = selKind_ == SelKind::kObject && bestKind == SelKind::kObject
        && std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), bestIdx) != selectedObjectIndices_.end();
    if (alreadySelected) {
        selIndex_ = bestIdx;
        clickPendingIndex_ = bestIdx;
        clickPendingAdditive_ = additive;
    } else {
        SelectionKindOf(bestKind).SelectFromClick(*this, bestIdx, additive);
    }
    UILayout::SetSelectedHandle(nullptr, 0);
    viewportDragging_ = true;

    // ドラッグ1回ぶんを1つのUndoにまとめるため、変更前をここで控える
    // （エンティティはスナップショット対象外なので、動かしても確定時に捨てられる）
    BeginUndoCapture();
    CaptureDragStart(mouseX, mouseY);
}

/** @brief ドラッグ開始時のマウス位置と選択物の値を控える */
void StageEditor::CaptureDragStart(float mouseX, float mouseY) {
    dragStartMouseX_ = mouseX;
    dragStartMouseY_ = mouseY;
    dragThresholdPassed_ = false;
    rotateAccumRadians_ = 0.0f;
    rotateAngleValid_ = false;
    dragGrabOffsetX_ = 0.0f;
    dragGrabOffsetY_ = 0.0f;
    dragStarts_.clear();
    if (selKind_ == SelKind::kNone) {
        return;
    }

    const ISelectionKind& selected = SelectionKindOf(selKind_);
    const int objectCount = static_cast<int>(objects_.size());
    // 1件ぶんの開始時の値を控える
    const auto addStart = [&](int index) {
        DragStartTransform start;
        start.index = index;
        selected.WorldPosition(*this, index, start.world);
        start.localZ = selected.LocalZ(*this, index);
        if (selKind_ == SelKind::kObject && index >= 0 && index < objectCount) {
            start.rotation = objects_[index].desc.rotation;
            start.scale = objects_[index].desc.scale;
        }
        dragStarts_.push_back(start);
    };
    // 主選択を先頭に、複数選択の残りを続けて控える
    addStart(selIndex_);
    if (selKind_ == SelKind::kObject) {
        for (int index : selectedObjectIndices_) {
            if (index == selIndex_ || index < 0 || index >= objectCount) {
                continue;
            }
            addStart(index);
            // 画面座標の文章UIはワールドの移動量で動かさない
            // 親も選択中の子は親と一緒に動くので、自分でも動かすと2倍動いてしまう
            bool followsMove = !selected.IsScreenAnchor(*this, index);
            const std::string& name = objects_[index].desc.name;
            for (int other : selectedObjectIndices_) {
                if (other != index && other >= 0 && other < objectCount && IsDescendantOf(name, objects_[other].desc.name)) {
                    followsMove = false;
                    break;
                }
            }
            dragStarts_.back().followsMove = followsMove;
        }
    }

    // Shift+ドラッグ(Z移動)用に、主選択の現在のZをスナップ前の生値として持つ
    dragRawZ_ = dragStarts_.front().localZ;

    // クリックした位置がオブジェクト原点とずれていても、その場で原点まで飛ばないよう、
    // 掴んだ瞬間の(オブジェクト位置 - マウス位置)のオフセットを控えておく
    // （"screen"座標のui_textはワールド平面と無関係なので、ゲーム画面のpx位置を基準にする）
    if (selected.IsScreenAnchor(*this, selIndex_)) {
        const Vector3 logicalMouse = viewport_.ImageToScreen(mouseX, mouseY);
        dragGrabOffsetX_ = objects_[selIndex_].desc.position.x - logicalMouse.x;
        dragGrabOffsetY_ = objects_[selIndex_].desc.position.y - logicalMouse.y;
        return;
    }
    Vector3 grabWorld { };
    Vector3 grabGround { };
    if (selected.WorldPosition(*this, selIndex_, grabWorld) && MouseToGround(mouseX, mouseY, grabGround)) {
        dragGrabOffsetX_ = grabWorld.x - grabGround.x;
        dragGrabOffsetY_ = grabWorld.y - grabGround.y;
    }
}

/** @brief ドラッグ中の処理を1フレーム進める（ボタンが離れていれば確定する） */
bool StageEditor::UpdateActiveViewportDrag(float mouseX, float mouseY) {
    if (!IsViewportDragActive()) {
        return false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        FinishViewportDrag(mouseX, mouseY);
        return true;
    }
    // 範囲選択は矩形を描くだけなので、離すまで何もしない
    if (boxSelecting_) {
        return true;
    }
    // 押してから少し動くまでは何も変えない（クリックだけで物がずれるのを防ぐ）
    if (!dragThresholdPassed_) {
        const float dx = mouseX - dragStartMouseX_;
        const float dy = mouseY - dragStartMouseY_;
        if (dx * dx + dy * dy < kDragStartThresholdPx * kDragStartThresholdPx) {
            return true;
        }
        dragThresholdPassed_ = true;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (uiDragLayout_) {
        UpdateUILayoutDrag(mouseX, mouseY);
    } else if (rotateDragging_ || scaleDragging_) {
        UpdateRotateScaleDrag(mouseX, mouseY);
    } else {
        UpdateViewportDrag(mouseX, mouseY);
    }
    return true;
}

/** @brief ドラッグ・範囲選択を確定して状態を戻す */
void StageEditor::FinishViewportDrag(float mouseX, float mouseY) {
    // 実際に動かしていた場合だけ1回分のUndoとして確定する
    if (viewportDragging_ || uiDragLayout_) {
        CommitUndoCapture();
    }
    if (boxSelecting_) {
        FinishBoxSelect(mouseX, mouseY);
    }
    // 選択中の物を動かさずに離した: Ctrl付きなら選択から外し、Ctrl無しならその1つだけを選択する
    if (clickPendingIndex_ >= 0 && !dragThresholdPassed_) {
        if (!clickPendingAdditive_) {
            selectedObjectIndices_ = { clickPendingIndex_ };
        } else {
            selectedObjectIndices_.erase(std::remove(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), clickPendingIndex_),
                selectedObjectIndices_.end());
            selIndex_ = selectedObjectIndices_.empty() ? -1 : selectedObjectIndices_.back();
            selKind_ = selectedObjectIndices_.empty() ? SelKind::kNone : SelKind::kObject;
        }
    }
    ResetViewportDragState();
}

/** @brief ドラッグに関わる状態をすべて戻す */
void StageEditor::ResetViewportDragState() {
    viewportDragging_ = false;
    rotateDragging_ = false;
    scaleDragging_ = false;
    boxSelecting_ = false;
    activeDragAxis_ = 0;
    uiDragLayout_ = nullptr;
    clickPendingIndex_ = -1;
    dragThresholdPassed_ = false;
    dragStarts_.clear();
    ClearViewportHover();
}

/** @brief ドラッグ中の選択物を移動させる（Shift中はマウス垂直移動をZ移動、それ以外はXY平面移動。複数選択は同じ量だけ動かす） */
void StageEditor::UpdateViewportDrag(float mouseX, float mouseY) {
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
    if (dragStarts_.empty()) {
        return;
    }
    const DragStartTransform& primary = dragStarts_.front();

    // ハンドルの軸矢印を掴んだ場合はその軸、そうでなければワークフローパネルの軸制限に従う
    const int axis = activeDragAxis_ != 0 ? activeDragAxis_ : gizmoAxis_;
    if (io.KeyShift || axis == stageEditorUiStyle::kGizmoAxisZ) {
        // カメラが遠いほど1pxあたりの移動量を増やし、近くでも遠くでも同じ操作感にする
        constexpr float kDefaultCameraDistance = 10.0f; // カメラ未設定時に操作量の基準にする距離
        float camDist = kDefaultCameraDistance;
        if (camera_) {
            camDist = (std::max)(1.0f, std::abs(camera_->GetTranslate().z));
        }
        // Zの矢印を掴んでいる時は矢印の向きに沿って動かす。それ以外はマウスの上下で動かす
        const float pixels = activeDragAxis_ == stageEditorUiStyle::kGizmoAxisZ
            ? AxisDragPixels(activeDragAxis_, io.MouseDelta.x, io.MouseDelta.y)
            : -io.MouseDelta.y;
        dragRawZ_ += pixels * camDist * kZDragPerPixel * viewport_.ImagePixelScale();
        const float deltaZ = SnapValue(dragRawZ_) - primary.localZ;
        for (const DragStartTransform& start : dragStarts_) {
            if (start.followsMove) {
                selected.SetLocalZ(*this, start.index, start.localZ + deltaZ, mouseMoved);
            }
        }
        return;
    }

    Vector3 ground;
    if (!MouseToGround(mouseX, mouseY, ground)) {
        return;
    }
    // 掴んだ時のオフセットを保ったまま追従させる（クリック位置がオブジェクト原点からずれていても飛ばない）
    // スナップは主選択のワールド座標側で丸めてから、他の選択物へ同じ移動量を渡す（親がいる場合はローカル座標へ逆算される）
    const float deltaX = SnapValue(ground.x + dragGrabOffsetX_) - primary.world.x;
    const float deltaY = SnapValue(ground.y + dragGrabOffsetY_) - primary.world.y;
    for (const DragStartTransform& start : dragStarts_) {
        if (start.followsMove) {
            selected.DragTo(*this, start.index, start.world.x + deltaX, start.world.y + deltaY, axis, mouseMoved);
        }
    }
}

/** @brief 見た目の位置を変えずに親を付け替える */
void StageEditor::SetParentPreservingWorld(int childIndex, int parentIndex) {
    if (childIndex < 0 || childIndex >= static_cast<int>(objects_.size()) || childIndex == parentIndex) {
        return;
    }
    // 親を変えても見た目の位置が変わらないよう、今のワールド座標を控えておく
    ObjectDesc& child = objects_[childIndex].desc;
    const Vector3 world = WorldPositionOf(child);
    // 親を外す: ワールド座標をそのままローカル座標にする
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
    // 自分の子孫を親にすると親子が輪になるので断る
    if (parentIndex >= static_cast<int>(objects_.size())
        || IsDescendantOf(objects_[parentIndex].desc.name, child.name)) {
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
    selKind_ = SelKind::kObject;
    selIndex_ = childIndex;
    selectedObjectIndices_ = { childIndex };
}

/** @brief 種類に応じた既定値で配置物を指定位置へ追加し、選択する */
int StageEditor::AddObjectAt(const std::string& kind, const Vector3& position) {
    RecordUndoSnapshotNow();
    ObjectEntry entry;
    ObjectDesc& desc = entry.desc;
    desc.kind = kind;
    desc.type = PlacementType::kStatic;
    desc.position = { SnapValue(position.x), SnapValue(position.y), position.z };
    ObjectKind::Of(kind).InitializeNewDesc(desc, nextSerial_++);
    entry.authoredPosition = desc.position;
    objects_.push_back(std::move(entry));
    const int index = static_cast<int>(objects_.size()) - 1;
    RegenerateInstances(objects_.back());
    selKind_ = SelKind::kObject;
    selIndex_ = index;
    selectedObjectIndices_ = { index };
    return index;
}

/** @brief 指定したワールド座標にUI文字を追加し、その番号を返す */
int StageEditor::AddUITextAt(const Vector3& position, bool screenSpace) {
    const int index = AddObjectAt("ui_text", position);
    auto& entry = objects_[index];
    entry.desc.textSpace = screenSpace ? TextSpace::kScreen : TextSpace::kWorld;
    entry.desc.position = position;
    entry.authoredPosition = position;
    entry.desc.text = "ここに文章を入力";
    constexpr float kScreenTextScale = 1.5f;
    constexpr float kWorldTextScale = 1.1f;
    entry.desc.textScale = screenSpace ? kScreenTextScale : kWorldTextScale;
    showUIText_ = true;
    focusTextEditor_ = true;
    transformTool_ = TransformTool::kMove;
    statusMessage_ = "右の「文章UI」で編集できます。画面の十字をドラッグして配置し、Ctrl+Sで保存します";
    statusTimer_ = StageEditor::kStatusVeryLongSeconds;
    return index;
}

/** @brief トリガーを指定位置へ追加して選択する */
void StageEditor::AddTriggerAt(const Vector3& position) {
    RecordUndoSnapshotNow();
    TriggerDesc desc;
    desc.name = "trigger_" + std::to_string(nextSerial_++);
    desc.position = { SnapValue(position.x), SnapValue(position.y), position.z };
    desc.flag = "event_" + desc.name;
    AppendTriggerAndSelect(desc);
}

/** @brief 設定済みのトリガーを追加して、それだけを選択した状態にする（元に戻す履歴は呼び出し側で取る） */
void StageEditor::AppendTriggerAndSelect(const TriggerDesc& desc) {
    TriggerVolume trigger;
    trigger.Init(desc);
    triggers_.push_back(std::move(trigger));
    selKind_ = SelKind::kTrigger;
    selIndex_ = static_cast<int>(triggers_.size()) - 1;
    selectedObjectIndices_.clear();
}

/** @brief 文章UIを隠している間で、ビュー上で扱わない配置物か */
bool StageEditor::IsHiddenUIText(const ObjectDesc& desc) const {
    return desc.kind == objectKindName::kUIText && !showUIText_;
}

/** @brief 隠している文章UIを除いた配置物すべてに visit(番号, 配置情報) を呼ぶ（中央ビューで扱う物だけを回す） */
void StageEditor::VisitShownObjects(const std::function<void(int, const ObjectDesc&)>& visit) const {
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        // 配置物の陰になって邪魔な時は、テキスト表示チェックボックスで文章UIだけ一時的に隠せる
        if (!IsHiddenUIText(objects_[i].desc)) {
            visit(i, objects_[i].desc);
        }
    }
}

/** @brief 全配置物を選択する（Ctrl+A） */
void StageEditor::SelectAllObjects() {
    selectedObjectIndices_.clear();
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        selectedObjectIndices_.push_back(i);
    }
    if (selectedObjectIndices_.empty()) {
        return;
    }
    selKind_ = SelKind::kObject;
    selIndex_ = selectedObjectIndices_.back();
}

/** @brief 選択を解除する（Escape） */
void StageEditor::ClearSelection() {
    selKind_ = SelKind::kNone;
    selIndex_ = -1;
    selectedObjectIndices_.clear();
    parentLinkChildIndex_ = -1;
    UILayout::SetSelectedHandle(nullptr, 0);
}

} // namespace engine::game

#endif
