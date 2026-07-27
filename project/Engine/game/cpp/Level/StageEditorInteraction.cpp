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
} // namespace

void StageEditor::DrawGizmos()
{
    // チェックポイントは緑の範囲と十字で表示する
    for (const CheckpointDesc& checkpoint : checkpoints_) {
        DiagnosticsDraw::DrawSphere({ checkpoint.position, checkpoint.activationRadius }, DiagnosticsDraw::kColorGreen);
        DiagnosticsDraw::DrawCross(checkpoint.position, 0.45f, DiagnosticsDraw::kColorGreen);
    }

    for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
        const TriggerDesc& d = triggers_[i].GetDesc();
        ImU32 color = triggers_[i].IsInside()                  ? DiagnosticsDraw::kColorGreen
            : (selKind_ == SelKind::Trigger && selIndex_ == i) ? DiagnosticsDraw::kColorYellow
                                                               : DiagnosticsDraw::kColorCyan;
        DiagnosticsDraw::DrawSphere({ d.position, d.radius }, color);
        DiagnosticsDraw::DrawCross(d.position, 0.3f, color);
    }
    // 全オブジェクトに小さな十字マーカーを出し、どこをクリックすれば掴めるか分かるようにする
    // solid=trueのものは実際の当たり判定AABBをオレンジのワイヤーフレームで重ねて表示する
    // 敵配置（enemy_knight/enemy_basic）は赤い十字にして配置物(白)と一目で区別できるようにする
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        bool sel = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), i)
            != selectedObjectIndices_.end();
        const ObjectDesc& d = objects_[i].desc;
        bool isEnemy = (d.kind != "prop");
        Vector3 world = WorldPositionOf(d);
        ImU32 baseColor = isEnemy ? DiagnosticsDraw::kColorRed : DiagnosticsDraw::kColorWhite;
        DiagnosticsDraw::DrawCross(world, sel ? 0.6f : (isEnemy ? 0.35f : 0.25f), sel ? DiagnosticsDraw::kColorYellow : baseColor);
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

        // 親子関係を白線で可視化する（親→子）
        const std::string& parentName = objects_[i].desc.parent;
        if (!parentName.empty()) {
            for (const auto& other : objects_) {
                if (other.desc.name == parentName) {
                    DiagnosticsDraw::DrawLine(WorldPositionOf(other.desc), world, IM_COL32(255, 255, 255, 90));
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
                if (condition.desc.kind == "event_condition" && condition.desc.name == conditionName) {
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
        DiagnosticsDraw::DrawCross(*ref.position, sel ? 0.6f : 0.3f, sel ? DiagnosticsDraw::kColorYellow : DiagnosticsDraw::kColorMagenta);
    }
}

void StageEditor::UpdateFreeCamera(Input* input, float dt)
{
    viewport_.UpdateCamera(input, dt, viewportFocusMode_);
}

bool StageEditor::MouseToGround(float mouseX, float mouseY, Vector3& outWorld) const
{
    return viewport_.ScreenToGround(mouseX, mouseY, outWorld);
}

void StageEditor::UpdateViewportInteraction()
{
    ImGuiIO& io = ImGui::GetIO();
    const bool insideSceneView = viewport_.Contains(io.MousePos.x, io.MousePos.y, viewportFocusMode_);

    // 編集パネル上の操作をシーンビューの選択やカメラ移動として扱わない
    if (io.WantCaptureMouse || !insideSceneView) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (viewportDragging_) {
                CommitUndoCapture(); // パネル上で離した場合もドラッグ分をここで確定する
            }
            viewportDragging_ = false;
        }
        return;
    }

    const ImVec2 m = io.MousePos;

    // マウスホイール  カメラを奥/手前へ移動（Q/Eと同じ軸、手前に回すと近づく）
    if (camera_ && io.MouseWheel != 0.0f) {
        constexpr float kWheelSpeed = 2.0f;
        camera_->GetTranslate().z += io.MouseWheel * kWheelSpeed;
    }

    // 左クリック  画面上で一番近いオブジェクト/トリガーを選択（40px以内）
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float bestDist = 40.0f;
        SelKind bestKind = SelKind::None;
        int bestIdx = -1;

        auto consider = [&](const Vector3& worldPos, SelKind kind, int index) {
            ImVec2 s;
            if (!DiagnosticsDraw::WorldToScreen(worldPos, s)) {
                return;
            }
            float dx = s.x - m.x;
            float dy = s.y - m.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestKind = kind;
                bestIdx = index;
            }
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
            float minX = FLT_MAX;
            float minY = FLT_MAX;
            float maxX = -FLT_MAX;
            float maxY = -FLT_MAX;
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
                minX = (std::min)(minX, screen.x);
                minY = (std::min)(minY, screen.y);
                maxX = (std::max)(maxX, screen.x);
                maxY = (std::max)(maxY, screen.y);
            }
            constexpr float kPickPadding = 4.0f;
            if (projected && m.x >= minX - kPickPadding && m.x <= maxX + kPickPadding
                && m.y >= minY - kPickPadding && m.y <= maxY + kPickPadding && bestDist > 0.0f) {
                bestDist = 0.0f;
                bestKind = SelKind::Object;
                bestIdx = index;
            }
        };

        auto considerExternalObjectBounds = [&](const ExternalEntityRef& ref, int index) {
            if (!ref.object || !ref.object->GetModel()) {
                return;
            }
            const auto& vertices = ref.object->GetModel()->GetVertices();
            const Transform& transform = ref.object->GetTransform();
            const Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
            float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
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
                minX = (std::min)(minX, screen.x);
                minY = (std::min)(minY, screen.y);
                maxX = (std::max)(maxX, screen.x);
                maxY = (std::max)(maxY, screen.y);
            }
            constexpr float kPickPadding = 4.0f;
            if (projected && m.x >= minX - kPickPadding && m.x <= maxX + kPickPadding
                && m.y >= minY - kPickPadding && m.y <= maxY + kPickPadding && bestDist > 0.0f) {
                bestDist = 0.0f;
                bestKind = SelKind::External;
                bestIdx = index;
            }
        };

        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            considerModelBounds(objects_[i], i);
            consider(WorldPositionOf(objects_[i].desc), SelKind::Object, i);
        }
        for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
            consider(triggers_[i].GetDesc().position, SelKind::Trigger, i);
        }
        for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
            considerExternalObjectBounds(externalEntities_[i], i);
            if (externalEntities_[i].position) {
                consider(*externalEntities_[i].position, SelKind::External, i);
            }
        }

        if (bestIdx >= 0) {
            if (parentLinkChildIndex_ >= 0 && bestKind == SelKind::Object) {
                const int childIndex = parentLinkChildIndex_;
                parentLinkChildIndex_ = -1;
                if (childIndex != bestIdx && childIndex >= 0 && childIndex < static_cast<int>(objects_.size())
                    && !IsDescendantOf(objects_[bestIdx].desc.name, objects_[childIndex].desc.name)) {
                    RecordUndoSnapshotNow();
                    ObjectDesc& child = objects_[childIndex].desc;
                    const Vector3 world = WorldPositionOf(child);
                    const ObjectDesc& parent = objects_[bestIdx].desc;
                    const Vector3 parentWorld = WorldPositionOf(parent);
                    const float c = std::cos(-parent.rotation.z);
                    const float s = std::sin(-parent.rotation.z);
                    const float dx = world.x - parentWorld.x;
                    const float dy = world.y - parentWorld.y;
                    child.parent = parent.name;
                    child.position = { dx * c - dy * s, dx * s + dy * c, world.z - parentWorld.z };
                    dirty_ = true;
                    statusMessage_ = child.name + " を " + parent.name + " に接続しました";
                    statusTimer_ = 3.0f;
                    selKind_ = SelKind::Object;
                    selIndex_ = childIndex;
                    selectedObjectIndices_ = { childIndex };
                }
                return;
            }
            selKind_ = bestKind;
            selIndex_ = bestIdx;
            if (bestKind == SelKind::Object) {
                if (!io.KeyCtrl) {
                    selectedObjectIndices_.clear();
                }
                auto selected = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), bestIdx);
                if (selected == selectedObjectIndices_.end()) {
                    selectedObjectIndices_.push_back(bestIdx);
                }
            } else {
                selectedObjectIndices_.clear();
            }
            viewportDragging_ = true;

            // ドラッグ1回ぶんを1つのUndoにまとめるため、変更前をここで控える
            // （エンティティはスナップショット対象外なので、動かしても確定時に捨てられる）
            BeginUndoCapture();

            // Shift+ドラッグ(Z移動)用に、選択物の現在のZをスナップ前の生値として持つ
            Vector3 grabWorldPos { };
            bool hasGrabWorldPos = true;
            if (bestKind == SelKind::Object) {
                dragRawZ_ = objects_[bestIdx].desc.position.z;
                grabWorldPos = WorldPositionOf(objects_[bestIdx].desc);
            } else if (bestKind == SelKind::Trigger) {
                dragRawZ_ = triggers_[bestIdx].GetDesc().position.z;
                grabWorldPos = triggers_[bestIdx].GetDesc().position;
            } else if (externalEntities_[bestIdx].position) {
                dragRawZ_ = externalEntities_[bestIdx].position->z;
                grabWorldPos = *externalEntities_[bestIdx].position;
            } else {
                hasGrabWorldPos = false;
            }

            // クリックした位置がオブジェクト原点とずれていても、その場で原点まで飛ばないよう、
            // 掴んだ瞬間の(オブジェクト位置 - マウス接地位置)のオフセットを控えておく
            dragGrabOffsetX_ = 0.0f;
            dragGrabOffsetY_ = 0.0f;
            Vector3 grabGround { };
            if (hasGrabWorldPos && MouseToGround(m.x, m.y, grabGround)) {
                dragGrabOffsetX_ = grabWorldPos.x - grabGround.x;
                dragGrabOffsetY_ = grabWorldPos.y - grabGround.y;
            }
        }
    }

    // ドラッグ中  通常はマウス位置（z=0平面上）へXY移動（Zは保持）、Shift中は垂直マウス移動をZ移動にする
    if (viewportDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const bool mouseMoved = (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);
        if (io.KeyShift || gizmoAxis_ == 3) {
            // カメラが遠いほど1pxあたりの移動量を増やし、近くでも遠くでも同じ操作感にする
            float camDist = 10.0f;
            if (camera_) {
                camDist = (std::max)(1.0f, std::abs(camera_->GetTranslate().z));
            }
            dragRawZ_ += -io.MouseDelta.y * camDist * kZDragPerPixel;
            const float snappedZ = SnapValue(dragRawZ_);
            if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
                objects_[selIndex_].desc.position.z = snappedZ;
                if (mouseMoved) {
                    MarkUndoDirty();
                }
            } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
                triggers_[selIndex_].GetDesc().position.z = snappedZ;
                if (mouseMoved) {
                    MarkUndoDirty();
                }
            } else if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())) {
                Vector3* pos = externalEntities_[selIndex_].position;
                if (pos) {
                    pos->z = snappedZ;
                }
            }
        } else {
            Vector3 ground;
            if (MouseToGround(m.x, m.y, ground)) {
                // 掴んだ時のオフセットを保ったまま追従させる（クリック位置がオブジェクト原点からずれていても飛ばない）
                // スナップはワールド座標側で丸めてから、親がいる場合はローカル座標へ逆算する
                const float wx = SnapValue(ground.x + dragGrabOffsetX_);
                const float wy = SnapValue(ground.y + dragGrabOffsetY_);
                if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
                    ObjectDesc& desc = objects_[selIndex_].desc;
                    Vector3 parentW = ParentWorldPositionOf(desc);
                    if (gizmoAxis_ == 0 || gizmoAxis_ == 1) {
                        desc.position.x = wx - parentW.x;
                    }
                    if (gizmoAxis_ == 0 || gizmoAxis_ == 2) {
                        desc.position.y = wy - parentW.y;
                    }
                    if (mouseMoved) {
                        MarkUndoDirty();
                    }
                } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
                    TriggerDesc& desc = triggers_[selIndex_].GetDesc();
                    if (gizmoAxis_ == 0 || gizmoAxis_ == 1) {
                        desc.position.x = wx;
                    }
                    if (gizmoAxis_ == 0 || gizmoAxis_ == 2) {
                        desc.position.y = wy;
                    }
                    if (mouseMoved) {
                        MarkUndoDirty();
                    }
                } else if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())) {
                    Vector3* pos = externalEntities_[selIndex_].position;
                    if (pos) {
                        pos->x = wx;
                        pos->y = wy;
                    }
                }
            }
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (viewportDragging_) {
            CommitUndoCapture(); // 実際に動かしていた場合だけ1回分のUndoとして確定する
        }
        viewportDragging_ = false;
    }
}
#else
void StageEditor::DrawGizmos() { }
void StageEditor::UpdateFreeCamera(engine::Input*, float) { }
void StageEditor::UpdateViewportInteraction() { }
bool StageEditor::MouseToGround(float, float, Vector3&) const { return false; }
#endif
