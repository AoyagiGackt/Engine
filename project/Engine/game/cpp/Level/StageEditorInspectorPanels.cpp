/**
 * @file StageEditorInspectorPanels.cpp
 * @brief ステージエディタの詳細設定パネル（StageEditorInspectorPanel）の表示責務を実行する
 * @note StageEditorPanels.cppからの分割ファイル（StageEditorHierarchyPanelはStageEditorPanels.cppに残る）
 */
#ifdef USE_IMGUI
#include "StageEditorPanels.h"
#include "TextBufferCapacity.h"
#include "Camera.h"
#include "CharacterVisuals.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "StageEditorUiStyle.h"
#include "WinApp.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <imgui.h>

namespace engine::game {
namespace {
namespace editorUi = engine::game::stageEditorUiStyle;

constexpr float kAssetPathFieldWidth = 180.0f;
constexpr float kMaxTriggerRadius = 50.0f;

using engine::graphics::editorUI::OpenFileDialog;
using engine::graphics::editorUI::ToProjectRelativePath;

} // namespace


using namespace engine::graphics;

/** @brief 入力中は編集開始時の状態を記録し、確定時またはファイル選択時に再生成を要求する */
void StageEditorInspectorPanel::RenderAssetPath(StageEditor& editor, const char* label, const char* browseLabel,
    const char* filter, std::string& path, bool& structuralDirty) {
    char buffer[textBufferCapacity::kPath];
    strncpy_s(buffer, path.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(kAssetPathFieldWidth);
    const bool changed = ImGui::InputText(label, buffer, sizeof(buffer));
    if (editor.CaptureItemUndo(changed)) {
        path = buffer;
    }
    structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton(browseLabel)) {
        const std::string selected = OpenFileDialog(filter, "Resources");
        if (!selected.empty()) {
            editor.RecordUndoSnapshotNow();
            path = ToProjectRelativePath(selected);
            structuralDirty = true;
        }
    }
}

// screen座標のui_textが編集パネル（ツールバー/左カラム/右インスペクタ）の下に隠れて
// 3Dビュー上でドラッグできない場合に警告し、見える位置へ逃がすボタンを出す
void StageEditorInspectorPanel::RenderScreenAnchorOcclusionWarning(StageEditor& editor, ObjectDesc& desc) {
    constexpr float kVisibleAreaTopMargin = 40.0f; // ツールバーのすぐ下は掴みにくいので少し余白を空ける
    const bool inWindow = editor.viewport_.IsImageMode();
    const float visibleLeft = inWindow ? 0.0f : StageEditor::kLeftPanelWidth;
    const float visibleRight = static_cast<float>(WinApp::kClientWidth) - (inWindow ? 0.0f : StageEditor::kRightPanelWidth);
    const float visibleTop = inWindow ? 0.0f : StageEditor::kToolbarHeight;
    const bool hiddenByPanel = desc.position.x < visibleLeft || desc.position.x > visibleRight || desc.position.y < visibleTop;
    if (!hiddenByPanel) {
        return;
    }
    ImGui::TextColored(editorUi::kCautionColor, "この位置は編集パネルの下に隠れており、3Dビュー上ではドラッグできません");
    ImGui::TextDisabled("上の「位置」欄で直接数値を入力するか、下のボタンで一旦ドラッグできる位置へ移動してください");
    if (ImGui::Button("ドラッグできる位置へ移動")) {
        editor.RecordUndoSnapshotNow();
        desc.position.x = (visibleLeft + visibleRight) * 0.5f;
        desc.position.y = visibleTop + kVisibleAreaTopMargin;
    }
}

/** @brief 有効/無効・名前・親子関係を編集するセクションを描画する */
void StageEditorInspectorPanel::RenderObjectIdentity(StageEditor& editor, bool& structuralDirty) {
    // ゲーム側で使うかどうかの切り替え（実体の作り直しが要るので structuralDirty を立てる）
    auto& entry = editor.objects_[editor.selIndex_];
    auto& desc = entry.desc;
    bool enabled = desc.enabled;
    if (ImGui::Checkbox("ゲーム側で有効", &enabled)) {
        editor.RecordUndoSnapshotNow();
        desc.enabled = enabled;
        structuralDirty = true;
    }

    // 名前（親子参照のキーなので、変更時は子の親参照も追従させる）
    {
        char nameBuf[textBufferCapacity::kName];
        strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
        bool changed = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            // 空の名前は受け付けない。入力中の連続した変更は1回の履歴にまとめる
            std::string newName = nameBuf;
            if (newName != desc.name && !newName.empty()) {
                editor.MarkUndoDirty();
                for (auto& other : editor.objects_) {
                    if (other.desc.parent == desc.name) {
                        other.desc.parent = newName;
                    }
                }
                desc.name = newName;
            }
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
    }

    // 親の選択（自分自身と自分の子孫は循環になるため選択肢から除外する）
    {
        std::string currentParent = desc.parent.empty() ? "(なし)" : desc.parent;
        if (ImGui::BeginCombo("親", currentParent.c_str())) {
            if (ImGui::Selectable("(なし)", desc.parent.empty())) {
                if (!desc.parent.empty()) {
                    editor.RecordUndoSnapshotNow();
                    // 親を外しても見た目の位置が変わらないよう、ワールド座標をローカルへ引き継ぐ
                    desc.position = editor.WorldPositionOf(desc);
                    desc.parent.clear();
                }
            }
            for (const auto& other : editor.objects_) {
                const std::string& name = other.desc.name;
                if (name == desc.name || editor.IsDescendantOf(name, desc.name)) {
                    continue;
                }
                if (ImGui::Selectable(name.c_str(), desc.parent == name)) {
                    if (desc.parent != name) {
                        editor.RecordUndoSnapshotNow();
                        // 付け替えても見た目の位置が変わらないよう、新しい親基準のローカル座標へ変換する
                        const Vector3 world = editor.WorldPositionOf(desc);
                        desc.parent = name;
                        desc.position = editor.LocalPositionOf(desc, world);
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
}

/** @brief モデル/テクスチャ/種類（static・row）等、見た目に関するセクションを描画する */
void StageEditorInspectorPanel::RenderObjectVisual(StageEditor& editor, bool& structuralDirty) {
    auto& entry = editor.objects_[editor.selIndex_];
    auto& desc = entry.desc;
    // 見た目を持つ種類はモデル・テクスチャ・配置の種類を、持たない種類は種類名と敵のHPを出す
    const bool visualKind = ObjectKind::Of(desc.kind).IsVisual();
    if (visualKind) {
        ObjectKind::Of(desc.kind).DrawVisualNote();
        RenderAssetPath(editor, "モデル", "参照##model",
            "3Dモデル\0*.obj;*.gltf;*.glb\0OBJ\0*.obj\0glTF\0*.gltf;*.glb\0すべてのファイル\0*.*\0\0", desc.model, structuralDirty);
        RenderAssetPath(editor, "テクスチャ", "参照##tex",
            "画像ファイル\0*.png;*.jpg;*.jpeg\0すべてのファイル\0*.*\0\0", desc.texture, structuralDirty);

        // PlacementType の並び順と一致させる
        const char* kTypeLabels[] = { "単体配置(static)", "並べて配置(row)" };
        int typeIdx = static_cast<int>(desc.type);
        if (ImGui::Combo("種類", &typeIdx, kTypeLabels, static_cast<int>(std::size(kTypeLabels)))) {
            editor.RecordUndoSnapshotNow();
            desc.type = static_cast<PlacementType>(typeIdx);
            structuralDirty = true;
        }
        editorUI::HelpMarker("単体配置: 1つだけ置く\n並べて配置: 同じモデルを一定間隔で複数並べる（階段や壁に便利）");
    } else {
        // 敵なら今のHPも出す
        const char* kindLabel = desc.kind.c_str();
        ImGui::TextDisabled("%s", kindLabel);
        if (const IEnemyEntity* enemy = entry.EnemyBody()) {
            ImGui::Text("HP: %d / %d", enemy->GetHp(), enemy->GetMaxHp());
        }
    }

    // 親を持つ物は、位置欄の値が親からの相対値であることを知らせる
    if (!desc.parent.empty()) {
        ImGui::TextDisabled("※位置は親からの相対値");
    }
}

/** @brief 位置・回転・スケール・並べ方（軸/個数/間隔）を編集するセクションを描画する */
void StageEditorInspectorPanel::RenderObjectTransform(
    StageEditor& editor, bool& structuralDirty, bool& transformDirty) {
    // ドラッグ系ウィジェットは操作開始で変更前を控え、離した時に1回分のUndoとして確定する
    auto& desc = editor.objects_[editor.selIndex_].desc;
    auto captureItemUndo = [&](bool changed) {
        return editor.CaptureItemUndo(changed);
    };

    // 複数選択中は、動かした差分をほかの選択物にも同じだけ加える
    const Vector3 previousPosition = desc.position;
    const bool positionChanged = captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, editorUi::kDragStepPosition));
    transformDirty |= positionChanged;
    if (positionChanged && editor.selectedObjectIndices_.size() > 1) {
        const Vector3 delta = Subtract(desc.position, previousPosition);
        for (int index : editor.selectedObjectIndices_) {
            // 操作中の1つ（すでに動いている）と、範囲外の番号は飛ばす
            if (index >= 0 && index < static_cast<int>(editor.objects_.size()) && index != editor.selIndex_) {
                // 同じ差分だけ動かし、表示用の行列も作り直す
                editor.objects_[index].desc.position = editor.objects_[index].desc.position + delta;
                editor.RefreshTransforms(editor.objects_[index]);
            }
        }
    }

    // 回転・拡縮を持つ種類だけ。複数選択中は差分をほかの選択物にも加える
    if (ObjectKind::Of(desc.kind).HasRotationAndScale()) {
        const Vector3 previousRotation = desc.rotation;
        const bool rotationChanged = captureItemUndo(ImGui::DragFloat3("回転", &desc.rotation.x, editorUi::kDragStepRotation));
        transformDirty |= rotationChanged;
        const Vector3 previousScale = desc.scale;
        const bool scaleChanged = captureItemUndo(ImGui::DragFloat3("スケール", &desc.scale.x, editorUi::kDragStepScale));
        transformDirty |= scaleChanged;
        if ((rotationChanged || scaleChanged) && editor.selectedObjectIndices_.size() > 1) {
            const Vector3 rotationDelta = Subtract(desc.rotation, previousRotation);
            const Vector3 scaleDelta = Subtract(desc.scale, previousScale);
            for (int index : editor.selectedObjectIndices_) {
                // 操作中の1つ・範囲外・回転と拡縮を持たない種類は飛ばす
                if (index < 0 || index >= static_cast<int>(editor.objects_.size()) || index == editor.selIndex_
                    || !ObjectKind::Of(editor.objects_[index].desc.kind).HasRotationAndScale()) {
                    continue;
                }
                // 変わった方だけ差分を足す
                if (rotationChanged) {
                    editor.objects_[index].desc.rotation = editor.objects_[index].desc.rotation + rotationDelta;
                }
                if (scaleChanged) {
                    editor.objects_[index].desc.scale = editor.objects_[index].desc.scale + scaleDelta;
                }
                editor.RefreshTransforms(editor.objects_[index]);
            }
        }
        // ライティングと当たり判定の切り替え（チェックは1回押すごとに戻せる）
        {
            bool lighting = desc.lighting;
            if (ImGui::Checkbox("ライティング", &lighting)) {
                editor.RecordUndoSnapshotNow();
                desc.lighting = lighting;
                // 描画側の設定を作り直すため、見た目の更新を予約する
                transformDirty = true;
            }
            // 当たり判定は描画に関係しないので値を変えるだけ
            bool solid = desc.solid;
            if (ImGui::Checkbox("当たり判定あり(solid)", &solid)) {
                editor.RecordUndoSnapshotNow();
                desc.solid = solid;
            }
            editorUI::HelpMarker("ONにするとプレイヤーや敵が乗れる・ぶつかる足場になります（オレンジの枠で表示）");
        }

        // 種類ごとに足す設定欄（ギミックの動きなど）
        ObjectKind::Of(desc.kind).DrawVisualExtras(editor, desc);

        // 並べて配置: 並べる軸・個数・間隔
        if (desc.type == PlacementType::kRow) {
            // 軸は文字1つで保存しているので、選択肢の先頭の文字を入れる
            const char* axes[] = { "x", "y", "z" };
            int axisIndex = static_cast<int>(PlacementAxisIndex(desc.axis));
            if (ImGui::Combo("並べる軸", &axisIndex, axes, static_cast<int>(std::size(kPlacementAxes)))) {
                editor.RecordUndoSnapshotNow();
                desc.axis = axes[axisIndex][0];
                // 並びが変わるので、並べた分の実体を作り直す
                structuralDirty = true;
            }
            // 個数は1個以上にする。数が変わると実体の作り直しが要る
            int count = desc.count;
            if (ImGui::InputInt("個数", &count)) {
                editor.RecordUndoSnapshotNow();
                desc.count = (std::max)(1, count);
                structuralDirty = true;
            }
            // 間隔は位置が変わるだけなので、行列の更新で済む
            transformDirty |= captureItemUndo(ImGui::DragFloat("間隔", &desc.step, editorUi::kDragStepFine));
        }
    }
}

/** @brief 選択中が配置物(Object)の場合の詳細パネルをまとめて描画する（下記Render*の呼び出し元） */
bool StageEditorInspectorPanel::RenderObjectInspector(StageEditor& editor) {
    if (editor.selKind_ != StageEditor::SelKind::kObject || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.objects_.size())) {
        return false;
    }
    bool structuralDirty = false;
    bool transformDirty = false;
    RenderObjectIdentity(editor, structuralDirty);
    // 文章UIは入力欄を先に出し、3Dモデル用の設定へ埋もれないようにする。
    RenderObjectText(editor);
    RenderObjectVisual(editor, structuralDirty);
    RenderObjectTransform(editor, structuralDirty, transformDirty);
    RenderObjectGameplay(editor, structuralDirty);

    auto& entry = editor.objects_[editor.selIndex_];
    const bool visualKind = ObjectKind::Of(entry.desc.kind).IsVisual();
    if (structuralDirty) {
        editor.RegenerateInstances(entry);
    } else if (transformDirty && visualKind) {
        editor.RefreshTransforms(entry);
    }
    return true;
}

/** @brief 選択中がトリガーの場合の詳細パネル（位置・半径・フラグ・成立条件）を描画する */
bool StageEditorInspectorPanel::RenderTriggerInspector(StageEditor& editor) {
    if (editor.selKind_ != StageEditor::SelKind::kTrigger || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.triggers_.size())) {
        return false;
    }
    // コピーした設定を編集し、変わるたびにトリガー本体へ書き戻す
    TriggerVolume& trigger = editor.triggers_[editor.selIndex_];
    TriggerDesc desc = trigger.GetDesc();

    auto captureItemUndo = [&](bool changed) {
        // 変わった値はトリガーの実体にもすぐ反映する
        if (changed) {
            trigger.SetDesc(desc);
        }
        return editor.CaptureItemUndo(changed);
    };

    // 名前とフラグ名
    char nameBuf[textBufferCapacity::kName];
    strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
    bool nameChanged = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
    if (nameChanged) {
        desc.name = nameBuf;
    }
    captureItemUndo(nameChanged);

    // フラグ名も名前と同じく固定長バッファで編集する
    char flagBuf[textBufferCapacity::kName];
    strncpy_s(flagBuf, desc.flag.c_str(), _TRUNCATE);
    bool flagChanged = ImGui::InputText("フラグ名", flagBuf, sizeof(flagBuf));
    if (flagChanged) {
        desc.flag = flagBuf;
    }
    captureItemUndo(flagChanged);
    editorUI::HelpMarker("プレイヤーが球に入ると、この名前のフラグが立ちます。\nノードエディタ(F1)のGetFlagノードで参照できます");

    // 範囲と、進入した時の振る舞い
    captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, editorUi::kDragStepPosition));
    captureItemUndo(ImGui::DragFloat("半径", &desc.radius, editorUi::kDragStepFine, editorUi::kMinRadius, kMaxTriggerRadius));
    {
        // チェックは押した回ごとに戻せるよう、変える前に履歴を取ってから本体へ書き戻す
        bool value = desc.value;
        if (ImGui::Checkbox("進入時に設定する値", &value)) {
            editor.RecordUndoSnapshotNow();
            desc.value = value;
            trigger.SetDesc(desc);
        }
        // 一度成立したら、出入りしても再成立しないようにする
        bool once = desc.once;
        if (ImGui::Checkbox("一度だけ成立させる", &once)) {
            editor.RecordUndoSnapshotNow();
            desc.once = once;
            trigger.SetDesc(desc);
        }
        bool spawnsWaterSplash = desc.spawnsWaterSplash;
        if (ImGui::Checkbox("この地点に来たら水しぶきを出す", &spawnsWaterSplash)) {
            editor.RecordUndoSnapshotNow();
            desc.spawnsWaterSplash = spawnsWaterSplash;
            trigger.SetDesc(desc);
        }
        editorUI::HelpMarker("動作対象やイベントパネルでの接続は不要です。プレイヤーがこのトリガーに進入した瞬間、この位置で水しぶきが発生します");
    }
    // 今プレイヤーが範囲内にいるかを確認用に出す
    ImGui::TextDisabled(editor.triggers_[editor.selIndex_].IsInside() ? "プレイヤーは範囲内にいます" : "プレイヤーは範囲外です");
    return true;
}

/** @brief 選択中が外部登録エンティティ(Player/Enemy等)の場合の詳細パネル（位置のみ）を描画する */
bool StageEditorInspectorPanel::RenderExternalInspector(StageEditor& editor) {
    if (editor.selKind_ != StageEditor::SelKind::kExternal || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.externalEntities_.size())) {
        return false;
    }
    // レベル外の実体（プレイヤーなど）。位置だけは直接動かせる
    StageEditor::ExternalEntityRef& ref = editor.externalEntities_[editor.selIndex_];
    ImGui::Text("%s", ref.name.c_str());
    ImGui::TextDisabled("ランタイム実体（JSONには保存されません）");
    // 位置は実体から読み、動かしたら実体へ直接書き戻す（レベルの履歴には残さない）
    if (ref.positionTarget) {
        Vector3 position = ref.positionTarget->GetPosition();
        if (ImGui::DragFloat3("位置", &position.x, editorUi::kDragStepPosition)) {
            ref.positionTarget->SetPosition(position);
        }
    }
    // 見た目を切り替えられる実体なら、今のモデルとテクスチャを出してファイルから差し替えられるようにする
    if (ref.getVisualPreset && ref.setVisualPreset) {
        // プリセット番号が負で、固定モデルの指定があれば独自の静的モデルを使っている
        const int preset = ref.getVisualPreset();
        const bool isCustomStatic = preset < 0 && ref.getStaticVisualModel && !ref.getStaticVisualModel().empty();
        // 表示するモデル名はプリセット、独自モデル、未設定の順に決める
        const std::string presetModel = PlayerVisualPresetModelPath(preset);
        const std::string currentModel = !presetModel.empty() ? presetModel
            : isCustomStatic ? ref.getStaticVisualModel() : "（未設定）";
        ImGui::TextWrapped("モデル: %s", currentModel.c_str());
        if (isCustomStatic) {
            const std::string currentTex = ref.getStaticVisualTexture ? ref.getStaticVisualTexture() : "";
            ImGui::TextWrapped("テクスチャ: %s", currentTex.empty() ? "（白テクスチャ）" : currentTex.c_str());
        }
        if (ImGui::Button("モデルファイルを選択...")) {
            const std::string selected = OpenFileDialog(
                "対応モデル(gltf/glb/obj)\0*.gltf;*.glb;*.obj\0すべてのファイル\0*.*\0\0", "Resources");
            // キャンセルされたら何もしない
            if (!selected.empty()) {
                // プリセットの表と比べられるよう、区切り文字を / に揃える
                std::string normalized = selected;
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                // プリセットのモデルならプリセットとして、そうでなければ静的モデルとして読み込む
                const int selectedPreset = FindPlayerVisualPreset(normalized);
                if (selectedPreset >= 0) {
                    // 独自モデルの指定を消してからプリセットに切り替える
                    if (ref.setStaticVisualModel) {
                        ref.setStaticVisualModel("", "");
                    }
                    ref.setVisualPreset(selectedPreset);
                } else {
                    // Model::Initializeは拡張子で分岐する（.obj以外はAssimp経由のLoadGltfFileへ）ため、
                    // gltf/glb/obj問わずここへそのまま渡してよい。テクスチャは既存の指定があれば引き継ぐ
                    if (ref.setStaticVisualModel) {
                        const std::string keepTex = ref.getStaticVisualTexture ? ref.getStaticVisualTexture() : "";
                        ref.setStaticVisualModel(ToProjectRelativePath(selected), keepTex);
                        editor.statusMessage_ = "アニメーションなしの静的モデルとして読み込みました";
                        editor.statusTimer_ = StageEditor::kStatusLongSeconds;
                    }
                }
            }
        }
        if (isCustomStatic) {
            ImGui::SameLine();
            if (ImGui::Button("テクスチャファイルを選択...")) {
                const std::string selectedTex = OpenFileDialog(
                    "画像ファイル\0*.png;*.jpg;*.jpeg\0すべてのファイル\0*.*\0\0", "Resources");
                if (!selectedTex.empty() && ref.setStaticVisualModel && ref.getStaticVisualModel) {
                    ref.setStaticVisualModel(ref.getStaticVisualModel(), ToProjectRelativePath(selectedTex));
                    editor.statusMessage_ = "テクスチャを差し替えました";
                    editor.statusTimer_ = StageEditor::kStatusLongSeconds;
                }
            }
        }
        if (ImGui::Button("モデルを自動選択へ戻す")) {
            if (ref.setStaticVisualModel) {
                ref.setStaticVisualModel("", "");
            }
            ref.setVisualPreset(-1);
        }
    }
    return true;
}

/** @brief 現在の選択対象に対応する詳細パネルを描画する */
void StageEditorInspectorPanel::Render(StageEditor& editor) {
    const auto* display = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(display->Pos.x + display->Size.x - StageEditor::kRightPanelWidth,
        display->Pos.y + StageEditor::kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(StageEditor::kRightPanelWidth,
        (std::max)(1.0f, display->Size.y - StageEditor::kToolbarHeight)), ImGuiCond_Always);
    ImGui::Begin("選択した物の設定", nullptr, editorUi::kFixedPanelFlags);
    const bool hasSelection = editor.selKind_ != StageEditor::SelKind::kNone;
    if (hasSelection) {
        if (ImGui::Button("選択した物へカメラを寄せる (F)", ImVec2(-1.0f, 0.0f))) {
            editor.FocusCameraOnSelection();
        }
        ImGui::TextDisabled("数値はドラッグ、ダブルクリックで直接入力");
        ImGui::Separator();
    }
    if (!StageEditor::SelectionKindOf(editor.selKind_).DrawInspector(editor)) {
        ImGui::TextWrapped("編集したい物を、画面上か左の一覧でクリックしてください。");
        ImGui::Spacing();
        ImGui::TextWrapped("1. 左下の素材をクリックして配置\n2. 画面上でつかんで移動\n3. ここで大きさや動作を調整\n4. 上部のテストで確認して保存");
        if (ImGui::Button("詳しい使い方を見る", ImVec2(-1.0f, 0.0f))) {
            editor.showEditorHelp_ = true;
        }
    }
    ImGui::End();
}
 // namespace engine::game

} // namespace engine::game
#endif // USE_IMGUI
