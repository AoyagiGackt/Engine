/**
 * @file StageEditorPanels.cpp
 * @brief ステージ編集パネルの表示責務を個別に実行する
 */
#ifdef USE_IMGUI
#include "StageEditorPanels.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "StageEditor.h"
#include "WinApp.h"
#include <algorithm>
#include <commdlg.h>
#include <cstring>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")

namespace {
std::string OpenFileDialog(const char* filter, const char* initialDirectory)
{
    char path[MAX_PATH] = { };
    OPENFILENAMEA dialog = { };
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = initialDirectory;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&dialog) ? std::string(path) : std::string { };
}

std::string ToProjectRelativePath(const std::string& absolutePath)
{
    std::string path = absolutePath;
    std::replace(path.begin(), path.end(), '\\', '/');
    const size_t resourcesPosition = path.find("Resources/");
    return resourcesPosition == std::string::npos ? path : path.substr(resourcesPosition);
}
} // namespace

namespace engine::game {
using namespace engine::graphics;
void StageEditorHierarchyPanel::RenderGuideAndFileActions(StageEditor& editor)
{
    ImGui::TextDisabled("F2: 表示/非表示    F4: 画面優先    WASD/QE: カメラ移動");
    if (ImGui::Button("ゲーム画面を広く表示 (F4)", ImVec2(-1.0f, 0.0f))) {
        editor.viewportFocusMode_ = true;
    }
    if (ImGui::CollapsingHeader("制作ガイド", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool hasGround = false;
        bool hasEnemy = false;
        bool hasEventConnection = false;
        for (const auto& entry : editor.objects_) {
            hasGround |= entry.desc.solid || entry.desc.kind == "terrain";
            hasEnemy |= entry.desc.kind == "spawn_point" || entry.desc.kind == "enemy_basic"
                || entry.desc.kind == "enemy_knight";
            hasEventConnection |= !entry.desc.activationFlag.empty();
        }
        const int completed = static_cast<int>(hasGround) + static_cast<int>(hasEnemy)
            + static_cast<int>(!editor.triggers_.empty()) + static_cast<int>(hasEventConnection);
        ImGui::ProgressBar(static_cast<float>(completed) / 4.0f, ImVec2(-1.0f, 0.0f));
        ImGui::TextWrapped("上から順に進めると、配置からゲーム進行までコードを書かずに作成できます。");
        ImGui::BulletText("%s 1 地形を置き、詳細設定で当たり判定を有効にする",
            hasGround ? "[完了]" : "[次]  ");
        ImGui::BulletText("%s 2 敵またはWaveを配置する",
            hasEnemy ? "[完了]" : "[未]  ");
        ImGui::BulletText("%s 3 プレイヤーが入るトリガーを配置する",
            !editor.triggers_.empty() ? "[完了]" : "[未]  ");
        ImGui::BulletText("%s 4 イベントで条件と動作対象を接続する",
            hasEventConnection ? "[完了]" : "[未]  ");
        if (ImGui::SmallButton("Waveを作る")) {
            editor.showWavePanel_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("イベントを作る")) {
            editor.showNoCodeEventPanel_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("水イベントを作る")) {
            editor.RecordUndoSnapshotNow();
            Vector3 center = editor.playerSpawn_;
            editor.MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);
            TriggerDesc desc;
            desc.name = "water_trigger_" + std::to_string(editor.nextSerial_++);
            desc.position = center;
            desc.spawnsWaterSplash = true;
            TriggerVolume trg;
            trg.Init(desc);
            editor.triggers_.push_back(std::move(trg));
            editor.selKind_ = StageEditor::SelKind::Trigger;
            editor.selIndex_ = static_cast<int>(editor.triggers_.size()) - 1;
            editor.statusMessage_ = "水イベントを画面中央に作成しました。ドラッグして位置を調整してください";
            editor.statusTimer_ = 3.0f;
        }
        EditorUI::HelpMarker("動作対象やイベントパネルでの接続を行わず、この場所に来たら水しぶきが出るトリガーを1回で作ります");
        ImGui::SameLine();
        if (ImGui::SmallButton("詳しい説明")) {
            editor.showEditorHelp_ = true;
        }
        ImGui::TextDisabled("最後に上部のテストで確認し、制作パネルから検証して保存する");
    }
    if (ImGui::CollapsingHeader("使い方")) {
        ImGui::BulletText("WASD: カメラ移動    Q/E・マウスホイール: 奥/手前へズーム");
        ImGui::BulletText("画面上のオブジェクトを左クリック: 選択");
        ImGui::BulletText("そのまま左ドラッグ: つかんで移動（Shift+ドラッグ: 奥行き(Z)移動）");
        ImGui::BulletText("Ctrl+Z: 元に戻す  Ctrl+Y: やり直す  Ctrl+S: 保存");
        ImGui::BulletText("Ctrl+D・複製ボタン: 選択中の物を複製    Deleteキー: 削除");
        ImGui::BulletText("Ctrl+C / Ctrl+V: 選択中の配置物をコピー・貼り付け");
        ImGui::BulletText("Ctrlを押しながら選択: 複数選択して一括移動・回転・拡縮");
        ImGui::BulletText("スナップをONにすると、移動・配置・複製の座標が指定間隔の倍数に揃う");
        ImGui::BulletText("[+]ボタン: 配置物/敵(ナイト・汎用エネミー)を選んで画面中央に新規追加");
        ImGui::BulletText("敵は実際にHPを持って湧く本物の敵配置数・種類は自由に増減できる");
        ImGui::BulletText("左下のアセットパレット: モード切替で新規配置/選択物への差し替えを選べる");
        ImGui::BulletText("右の詳細設定で数値・モデル・親子関係を編集");
        ImGui::BulletText("親を設定すると、親を動かしたとき子も一緒に動く");
        ImGui::BulletText("トリガー(水色の球): プレイヤーが入るとフラグON");
        ImGui::BulletText("　フラグはノードエディタ(F1)のGetFlagで参照できる");
        ImGui::BulletText("エンティティ(マゼンタの十字): Player/Enemy等も同様に選択・ドラッグ移動できる");
        ImGui::BulletText("　ただし削除不可・JSONに保存されない（位置はシーン起動時の初期値に戻る）");
        ImGui::BulletText("保存ボタンでJSONへ書き出しゲーム本編に即反映");
    }
}

void StageEditorHierarchyPanel::RenderEntityBrowser(StageEditor& editor)
{
    if (editor.camera_ && ImGui::CollapsingHeader("ゲームカメラ", ImGuiTreeNodeFlags_DefaultOpen)) {
        Vector3 position = editor.camera_->GetTranslate();
        Vector3 rotation = editor.camera_->GetRotate();
        if (ImGui::DragFloat3("カメラ位置", &position.x, 0.1f)) {
            editor.camera_->SetTranslate(position);
        }
        if (ImGui::DragFloat3("カメラ回転", &rotation.x, 0.01f)) {
            editor.camera_->SetRotate(rotation);
        }
        ImGui::TextDisabled("WASD: XY移動 / Q,E: 奥行き移動");
    }

    char pathBuf[256];
    strncpy_s(pathBuf, editor.levelPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) {
        editor.levelPath_ = pathBuf;
    }
    if (ImGui::Button("開く", ImVec2(80, 0))) {
        // 未保存の編集がある時は黙って破棄せず、確認モーダルを挟む
        if (editor.dirty_) {
            ImGui::OpenPopup("開くの確認");
        } else {
            editor.Open(editor.levelPath_, editor.modelCommon_, editor.camera_);
        }
    }
    if (EditorUI::ConfirmModal("開くの確認",
            "未保存の変更があります。\n変更を破棄して読み込み直しますか？",
            "破棄して開く")
        == EditorUI::ConfirmResult::Ok) {
        editor.Open(editor.levelPath_, editor.modelCommon_, editor.camera_);
    }
    ImGui::SameLine();
    if (ImGui::Button("保存", ImVec2(80, 0))) {
        editor.Save();
    }
    if (editor.dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "未保存");
    }

    ImGui::BeginDisabled(!editor.history_.CanUndo());
    if (ImGui::Button("元に戻す", ImVec2(80, 0))) {
        editor.Undo();
    }
    ImGui::EndDisabled();
    if (ImGui::Button("選択条件をテスト発火")) {
        const int sourceIndex = editor.eventConnection_.SourceIndex();
        if (sourceIndex >= 0 && sourceIndex < static_cast<int>(editor.triggers_.size())) {
            GameFlags::GetInstance()->SetFlag(editor.triggers_[sourceIndex].GetDesc().flag, true);
        } else {
            const int conditionIndex = sourceIndex - static_cast<int>(editor.triggers_.size());
            if (conditionIndex >= 0 && conditionIndex < static_cast<int>(editor.objects_.size())) {
                GameFlags::GetInstance()->SetFlag("condition_" + editor.objects_[conditionIndex].desc.name, true);
            }
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.history_.CanRedo());
    if (ImGui::Button("やり直す", ImVec2(80, 0))) {
        editor.Redo();
    }
    ImGui::EndDisabled();

    ImGui::Checkbox("スナップ", &editor.snapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("##snapStep", &editor.snapStep_, 0.1f, 0.1f, 10.0f, "%.1f");
    const bool canLink = editor.selKind_ == StageEditor::SelKind::Object
        && editor.selIndex_ >= 0 && editor.selIndex_ < static_cast<int>(editor.objects_.size());
    ImGui::BeginDisabled(!canLink);
    if (ImGui::Button(editor.parentLinkChildIndex_ >= 0 ? "親子リンクをキャンセル" : "選択物を子にして親をクリック")) {
        editor.parentLinkChildIndex_ = editor.parentLinkChildIndex_ >= 0 ? -1 : editor.selIndex_;
    }
    ImGui::EndDisabled();
    if (editor.parentLinkChildIndex_ >= 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "接続する親ブロックを画面上でクリック");
    }
    EditorUI::HelpMarker("ドラッグ移動・新規配置・複製の座標を、この間隔の倍数に揃えます");

    if (editor.statusTimer_ > 0.0f) {
        ImGui::TextDisabled("%s", editor.statusMessage_.c_str());
    }

    ImGui::Separator();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hierarchySearch", "名前・種類・モデルを検索", editor.hierarchySearch_, sizeof(editor.hierarchySearch_));

    std::string searchText = editor.hierarchySearch_;
    std::transform(searchText.begin(), searchText.end(), searchText.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto matchesSearch = [&](const std::string& text) {
        if (searchText.empty()) {
            return true;
        }
        std::string target = text;
        std::transform(target.begin(), target.end(), target.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return target.find(searchText) != std::string::npos;
    };

    char objHeader[48];
    snprintf(objHeader, sizeof(objHeader), "オブジェクト (%d)", static_cast<int>(editor.objects_.size()));
    bool objOpen = ImGui::TreeNodeEx(objHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addObj")) {
        ImGui::OpenPopup("AddObjectPopup");
    }
    if (ImGui::BeginPopup("AddObjectPopup")) {
        // 見えている画面の中央（z=0平面上）に置くカメラをどこへ動かしていても手元に出る
        Vector3 center = editor.playerSpawn_;
        editor.MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);

        auto addEntry = [&](const std::string& namePrefix, const std::string& kind) {
            editor.RecordUndoSnapshotNow();
            StageEditor::ObjectEntry entry;
            entry.desc.name = namePrefix + "_" + std::to_string(editor.nextSerial_++);
            entry.desc.kind = kind;
            entry.desc.position = center;
            entry.desc.position.x = editor.SnapValue(entry.desc.position.x);
            entry.desc.position.y = editor.SnapValue(entry.desc.position.y);
            editor.objects_.push_back(std::move(entry));
            editor.RegenerateInstances(editor.objects_.back());
            editor.selKind_ = StageEditor::SelKind::Object;
            editor.selIndex_ = static_cast<int>(editor.objects_.size()) - 1;
            editor.selectedObjectIndices_ = { editor.selIndex_ };
        };

        if (ImGui::MenuItem("配置物（ブロック）")) {
            editor.AddPropAtScreenCenter("Resources/block/block.obj", "Resources/block/block.png");
        }
        if (ImGui::MenuItem("背景モデル")) {
            addEntry("background", "background");
            auto& background = editor.objects_.back().desc;
            background.model = "Resources/DowntownCityMegaKit[Standard]/Exports/glTF (Godot)/Building_Small_1.gltf";
            background.texture = "Resources/DowntownCityMegaKit[Standard]/Textures/T_RedBrick_BaseColor.png";
            background.scale = { 0.42f, 0.42f, 0.42f };
            background.position.y = -0.6f;
            background.lighting = true;
            background.solid = false;
            background.position.z = 6.0f;
            editor.RegenerateInstances(editor.objects_.back());
        }
        if (ImGui::MenuItem("敵：ナイト")) {
            addEntry("knight", "enemy_knight");
        }
        if (ImGui::MenuItem("敵：汎用エネミー")) {
            addEntry("enemy", "enemy_basic");
        }
        if (ImGui::MenuItem("SpawnPoint")) {
            addEntry("spawn", "spawn_point");
            editor.objects_.back().desc.activationFlag = editor.objects_.back().desc.name + "_active";
        }
        if (ImGui::MenuItem("ギミック")) {
            addEntry("gimmick", "gimmick");
            editor.objects_.back().desc.activationFlag = editor.objects_.back().desc.name + "_active";
            editor.objects_.back().desc.model = "Resources/block/block.obj";
            editor.objects_.back().desc.texture = "Resources/block/block.png";
            editor.RegenerateInstances(editor.objects_.back());
        }
        if (ImGui::MenuItem("カメラポイント")) {
            addEntry("camera", "camera_point");
            editor.objects_.back().desc.activationFlag = editor.objects_.back().desc.name + "_active";
        }
        if (ImGui::MenuItem("巡回Waypoint")) {
            addEntry("waypoint", "patrol_point");
        }
        if (ImGui::MenuItem("イベント条件")) {
            addEntry("condition", "event_condition");
        }
        if (ImGui::MenuItem("Terrain")) {
            addEntry("terrain", "terrain");
            ObjectDesc& terrain = editor.objects_.back().desc;
            terrain.model = "Resources/block/block.obj";
            terrain.texture = "Resources/block/block.png";
            terrain.solid = true;
            terrain.meshCollider = true;
            editor.RegenerateInstances(editor.objects_.back());
        }
        ImGui::EndPopup();
    }
    if (objOpen) {
        if (!searchText.empty()) {
            for (int i = 0; i < static_cast<int>(editor.objects_.size()); ++i) {
                const ObjectDesc& d = editor.objects_[i].desc;
                if (!matchesSearch(d.name) && !matchesSearch(d.kind) && !matchesSearch(d.model)) {
                    continue;
                }
                bool selected = std::find(editor.selectedObjectIndices_.begin(), editor.selectedObjectIndices_.end(), i)
                    != editor.selectedObjectIndices_.end();
                std::string label = d.name + "  " + d.kind + "##searchObj" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), selected)) {
                    editor.selKind_ = StageEditor::SelKind::Object;
                    editor.selIndex_ = i;
                    if (!ImGui::GetIO().KeyCtrl) {
                        editor.selectedObjectIndices_.clear();
                    }
                    auto selectedIt = std::find(editor.selectedObjectIndices_.begin(), editor.selectedObjectIndices_.end(), i);
                    if (selectedIt == editor.selectedObjectIndices_.end()) {
                        editor.selectedObjectIndices_.push_back(i);
                    } else if (ImGui::GetIO().KeyCtrl) {
                        editor.selectedObjectIndices_.erase(selectedIt);
                    }
                }
            }
        } else {
            auto parentExists = [&](const std::string& parentName) {
                if (parentName.empty()) {
                    return false;
                }
                for (const auto& e : editor.objects_) {
                    if (e.desc.name == parentName) {
                        return true;
                    }
                }
                return false;
            };
            for (int i = 0; i < static_cast<int>(editor.objects_.size()); ++i) {
                if (!parentExists(editor.objects_[i].desc.parent)) {
                    editor.DrawHierarchyEntry(i, 0);
                }
            }
        }
        ImGui::TreePop();
    }

    if (!editor.externalEntities_.empty()) {
        char entHeader[48];
        snprintf(entHeader, sizeof(entHeader), "エンティティ (%d)", static_cast<int>(editor.externalEntities_.size()));
        bool entOpen = ImGui::TreeNodeEx(entHeader, ImGuiTreeNodeFlags_DefaultOpen);
        if (entOpen) {
            for (int i = 0; i < static_cast<int>(editor.externalEntities_.size()); ++i) {
                if (!matchesSearch(editor.externalEntities_[i].name)) {
                    continue;
                }
                bool sel = (editor.selKind_ == StageEditor::SelKind::External && editor.selIndex_ == i);
                char label[96];
                snprintf(label, sizeof(label), "  %s##ent%d", editor.externalEntities_[i].name.c_str(), i);
                if (ImGui::Selectable(label, sel)) {
                    editor.selKind_ = StageEditor::SelKind::External;
                    editor.selIndex_ = i;
                }
            }
            ImGui::TreePop();
        }
    }

    char trgHeader[48];
    snprintf(trgHeader, sizeof(trgHeader), "トリガー (%d)", static_cast<int>(editor.triggers_.size()));
    bool trgOpen = ImGui::TreeNodeEx(trgHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addTrg")) {
        editor.RecordUndoSnapshotNow();
        TriggerDesc desc;
        desc.name = "trigger_" + std::to_string(editor.nextSerial_++);
        desc.position = editor.playerSpawn_;
        desc.flag = desc.name;
        TriggerVolume trg;
        trg.Init(desc);
        editor.triggers_.push_back(std::move(trg));
        editor.selKind_ = StageEditor::SelKind::Trigger;
        editor.selIndex_ = static_cast<int>(editor.triggers_.size()) - 1;
    }
    if (trgOpen) {
        for (int i = 0; i < static_cast<int>(editor.triggers_.size()); ++i) {
            bool sel = (editor.selKind_ == StageEditor::SelKind::Trigger && editor.selIndex_ == i);
            const TriggerDesc& d = editor.triggers_[i].GetDesc();
            if (!matchesSearch(d.name) && !matchesSearch(d.flag)) {
                continue;
            }
            char label[96];
            snprintf(label, sizeof(label), "  %s -> %s=%s", d.name.c_str(), d.flag.c_str(), d.value ? "true" : "false");
            if (ImGui::Selectable(label, sel)) {
                editor.selKind_ = StageEditor::SelKind::Trigger;
                editor.selIndex_ = i;
            }
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
}

void StageEditorHierarchyPanel::RenderSelectionActions(StageEditor& editor)
{
    // エンティティ(Player/Enemy等)はエディタが生成したものではないため複製の対象外
    // ただし onDelete が設定されているエンティティ（シーン所有の背景オブジェクト等）だけは削除できる
    bool canDuplicate = (editor.selKind_ == StageEditor::SelKind::Object || editor.selKind_ == StageEditor::SelKind::Trigger);
    bool canDelete = canDuplicate
        || (editor.selKind_ == StageEditor::SelKind::External && editor.selIndex_ >= 0
            && editor.selIndex_ < static_cast<int>(editor.externalEntities_.size())
            && editor.externalEntities_[editor.selIndex_].onDelete);
    float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!canDuplicate);
    if (ImGui::Button("複製 (Ctrl+D)", ImVec2(halfWidth, 0))) {
        editor.DuplicateSelected();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("選択を削除 (Del)", ImVec2(halfWidth, 0))) {
        editor.DeleteSelected();
    }
    ImGui::EndDisabled();
}

void StageEditorHierarchyPanel::Render(StageEditor& editor)
{

    constexpr float kToolbarHeight = 42.0f;
    constexpr float kPanelWidth = 280.0f;
    const float hierarchyHeight = (static_cast<float>(WinApp::kClientHeight) - kToolbarHeight) * 0.62f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, hierarchyHeight), ImGuiCond_Always);
    ImGui::Begin("ステージエディタ", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    RenderGuideAndFileActions(editor);
    RenderEntityBrowser(editor);
    RenderSelectionActions(editor);
    ImGui::End();
}

void StageEditorInspectorPanel::RenderObjectIdentity(StageEditor& editor, bool& structuralDirty)
{
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
        char nameBuf[96];
        strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
        bool changed = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
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
                        Vector3 world = editor.WorldPositionOf(desc);
                        desc.parent = name;
                        Vector3 parentW = editor.ParentWorldPositionOf(desc);
                        desc.position = Subtract(world, parentW);
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
}

void StageEditorInspectorPanel::RenderObjectVisual(StageEditor& editor, bool& structuralDirty)
{
    auto& entry = editor.objects_[editor.selIndex_];
    auto& desc = entry.desc;
    const bool visualKind = desc.kind == "prop" || desc.kind == "background" || desc.kind == "gimmick" || desc.kind == "terrain";
    if (visualKind) {
        if (desc.kind == "background") {
            ImGui::TextDisabled("背景モデル（レベルJSONに保存）");
            ImGui::SameLine();
            EditorUI::HelpMarker("モデル・テクスチャ・位置・回転・スケールを通常のオブジェクトと同様に調整できます。当たり判定は初期状態で無効です。");
        }
        char modelBuf[256];
        strncpy_s(modelBuf, desc.model.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(180.0f);
        bool modelChanged = ImGui::InputText("モデル", modelBuf, sizeof(modelBuf));
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (modelChanged) {
            editor.MarkUndoDirty();
            desc.model = modelBuf;
        }
        structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("参照##model")) {
            std::string p = OpenFileDialog("3Dモデル\0*.obj;*.gltf;*.glb\0OBJ\0*.obj\0glTF\0*.gltf;*.glb\0すべてのファイル\0*.*\0\0", "Resources");
            if (!p.empty()) {
                editor.RecordUndoSnapshotNow();
                desc.model = ToProjectRelativePath(p);
                structuralDirty = true;
            }
        }

        char texBuf[256];
        strncpy_s(texBuf, desc.texture.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(180.0f);
        bool texChanged = ImGui::InputText("テクスチャ", texBuf, sizeof(texBuf));
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (texChanged) {
            editor.MarkUndoDirty();
            desc.texture = texBuf;
        }
        structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("参照##tex")) {
            std::string p = OpenFileDialog("画像ファイル\0*.png;*.jpg;*.jpeg\0すべてのファイル\0*.*\0\0", "Resources");
            if (!p.empty()) {
                editor.RecordUndoSnapshotNow();
                desc.texture = ToProjectRelativePath(p);
                structuralDirty = true;
            }
        }

        const char* kTypes[] = { "static", "row" };
        const char* kTypeLabels[] = { "単体配置(static)", "並べて配置(row)" };
        int typeIdx = (desc.type == "row") ? 1 : 0;
        if (ImGui::Combo("種類", &typeIdx, kTypeLabels, 2)) {
            editor.RecordUndoSnapshotNow();
            desc.type = kTypes[typeIdx];
            structuralDirty = true;
        }
        EditorUI::HelpMarker("単体配置: 1つだけ置く\n並べて配置: 同じモデルを一定間隔で複数並べる（階段や壁に便利）");
    } else {
        const char* kindLabel = desc.kind.c_str();
        ImGui::TextDisabled("%s", kindLabel);
        if (entry.knight) {
            ImGui::Text("HP: %d / %d", entry.knight->GetHp(), entry.knight->GetMaxHp());
        } else if (entry.enemy) {
            ImGui::Text("HP: %d / %d", entry.enemy->GetHp(), entry.enemy->GetMaxHp());
        }
    }

    if (!desc.parent.empty()) {
        ImGui::TextDisabled("※位置は親からの相対値");
    }
}

void StageEditorInspectorPanel::RenderObjectTransform(
    StageEditor& editor, bool& structuralDirty, bool& transformDirty)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
    const bool visualKind = desc.kind == "prop" || desc.kind == "background" || desc.kind == "gimmick" || desc.kind == "terrain";
    auto captureItemUndo = [&](bool changed) {
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            editor.MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        return changed;
    };
    // ドラッグ系ウィジェットは操作開始で変更前を控え、離した時に1回分のUndoとして確定する

    const Vector3 previousPosition = desc.position;
    const bool positionChanged = captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, 0.1f));
    transformDirty |= positionChanged;
    if (positionChanged && editor.selectedObjectIndices_.size() > 1) {
        const Vector3 delta = {
            desc.position.x - previousPosition.x,
            desc.position.y - previousPosition.y,
            desc.position.z - previousPosition.z
        };
        for (int index : editor.selectedObjectIndices_) {
            if (index >= 0 && index < static_cast<int>(editor.objects_.size()) && index != editor.selIndex_) {
                editor.objects_[index].desc.position = editor.objects_[index].desc.position + delta;
                editor.RefreshTransforms(editor.objects_[index]);
            }
        }
    }

    if (visualKind || desc.kind == "camera_point") {
        const Vector3 previousRotation = desc.rotation;
        const bool rotationChanged = captureItemUndo(ImGui::DragFloat3("回転", &desc.rotation.x, 0.01f));
        transformDirty |= rotationChanged;
        const Vector3 previousScale = desc.scale;
        const bool scaleChanged = captureItemUndo(ImGui::DragFloat3("スケール", &desc.scale.x, 0.05f));
        transformDirty |= scaleChanged;
        if ((rotationChanged || scaleChanged) && editor.selectedObjectIndices_.size() > 1) {
            const Vector3 rotationDelta = {
                desc.rotation.x - previousRotation.x,
                desc.rotation.y - previousRotation.y,
                desc.rotation.z - previousRotation.z
            };
            const Vector3 scaleDelta = {
                desc.scale.x - previousScale.x,
                desc.scale.y - previousScale.y,
                desc.scale.z - previousScale.z
            };
            for (int index : editor.selectedObjectIndices_) {
                if (index < 0 || index >= static_cast<int>(editor.objects_.size()) || index == editor.selIndex_
                    || (editor.objects_[index].desc.kind != "prop" && editor.objects_[index].desc.kind != "background" && editor.objects_[index].desc.kind != "gimmick"
                        && editor.objects_[index].desc.kind != "terrain" && editor.objects_[index].desc.kind != "camera_point")) {
                    continue;
                }
                if (rotationChanged) {
                    editor.objects_[index].desc.rotation = editor.objects_[index].desc.rotation + rotationDelta;
                }
                if (scaleChanged) {
                    editor.objects_[index].desc.scale = editor.objects_[index].desc.scale + scaleDelta;
                }
                editor.RefreshTransforms(editor.objects_[index]);
            }
        }
        {
            bool lighting = desc.lighting;
            if (ImGui::Checkbox("ライティング", &lighting)) {
                editor.RecordUndoSnapshotNow();
                desc.lighting = lighting;
                transformDirty = true;
            }
            bool solid = desc.solid;
            if (ImGui::Checkbox("当たり判定あり(solid)", &solid)) {
                editor.RecordUndoSnapshotNow();
                desc.solid = solid;
            }
            EditorUI::HelpMarker("ONにするとプレイヤーや敵が乗れる・ぶつかる足場になります（オレンジの枠で表示）");
        }

        if (desc.kind == "terrain") {
            if (ImGui::Checkbox("メッシュ同期コライダー", &desc.meshCollider)) {
                editor.RecordUndoSnapshotNow();
                desc.solid = desc.meshCollider || desc.solid;
            }
        }

        if (desc.type == "row") {
            const char* axes[] = { "x", "y", "z" };
            int axisIndex = desc.axis == 'y' ? 1 : desc.axis == 'z' ? 2
                                                                    : 0;
            if (ImGui::Combo("並べる軸", &axisIndex, axes, 3)) {
                editor.RecordUndoSnapshotNow();
                desc.axis = axes[axisIndex][0];
                structuralDirty = true;
            }
            int count = desc.count;
            if (ImGui::InputInt("個数", &count)) {
                editor.RecordUndoSnapshotNow();
                desc.count = (std::max)(1, count);
                structuralDirty = true;
            }
            transformDirty |= captureItemUndo(ImGui::DragFloat("間隔", &desc.step, 0.05f));
        }
    }
}

void StageEditorInspectorPanel::RenderObjectGameplay(StageEditor& editor, bool& structuralDirty)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
    auto captureItemUndo = [&](bool changed) {
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            editor.MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        return changed;
    };
    if (desc.kind == "gimmick" || desc.kind == "spawn_point" || desc.kind == "camera_point") {
        char flagBuffer[96] = { };
        strncpy_s(flagBuffer, desc.activationFlag.c_str(), _TRUNCATE);
        if (captureItemUndo(ImGui::InputText("有効化フラグ", flagBuffer, sizeof(flagBuffer)))) {
            desc.activationFlag = flagBuffer;
        }
        EditorUI::HelpMarker("空なら常時有効です。イベントトリガーが同名のフラグを立てると有効になります");
    }
    if (desc.kind == "spawn_point") {
        const char* spawnTypes[] = { "basic", "knight" };
        int spawnTypeIndex = desc.spawnType == "knight" ? 1 : 0;
        if (ImGui::Combo("発生する敵", &spawnTypeIndex, spawnTypes, 2)) {
            editor.RecordUndoSnapshotNow();
            desc.spawnType = spawnTypes[spawnTypeIndex];
            structuralDirty = true;
        }
    }
    if (desc.kind == "spawn_point" || desc.kind == "enemy_basic" || desc.kind == "enemy_knight") {
        char groupBuffer[96] = { };
        strncpy_s(groupBuffer, desc.enemyGroup.c_str(), _TRUNCATE);
        if (captureItemUndo(ImGui::InputText("敵グループ", groupBuffer, sizeof(groupBuffer)))) {
            desc.enemyGroup = groupBuffer;
        }
    }
    if (desc.kind == "enemy_basic" || desc.kind == "enemy_knight" || desc.kind == "patrol_point") {
        char routeBuffer[96] = { };
        strncpy_s(routeBuffer, desc.patrolRoute.c_str(), _TRUNCATE);
        if (captureItemUndo(ImGui::InputText("巡回ルート名", routeBuffer, sizeof(routeBuffer)))) {
            desc.patrolRoute = routeBuffer;
        }
        if (desc.kind == "patrol_point") {
            if (ImGui::InputInt("巡回順", &desc.routeOrder)) {
                editor.RecordUndoSnapshotNow();
            }
        } else {
            captureItemUndo(ImGui::DragFloat("巡回速度", &desc.patrolSpeed, 0.05f, 0.0f, 20.0f));
        }
    }
    if (desc.kind == "event_condition") {
        const char* conditionTypes[] = { "manual", "timer", "enemy_group_defeated" };
        int conditionIndex = desc.conditionType == "timer" ? 1
            : desc.conditionType == "enemy_group_defeated" ? 2
                                                           : 0;
        if (ImGui::Combo("条件", &conditionIndex, conditionTypes, 3)) {
            editor.RecordUndoSnapshotNow();
            desc.conditionType = conditionTypes[conditionIndex];
        }
        if (desc.conditionType == "timer") {
            captureItemUndo(ImGui::DragFloat("成立までの秒数", &desc.conditionSeconds, 0.1f, 0.0f, 300.0f));
        } else if (desc.conditionType == "enemy_group_defeated") {
            char groupBuffer[96] = { };
            strncpy_s(groupBuffer, desc.enemyGroup.c_str(), _TRUNCATE);
            if (captureItemUndo(ImGui::InputText("監視する敵グループ", groupBuffer, sizeof(groupBuffer)))) {
                desc.enemyGroup = groupBuffer;
            }
        }
    }
    if (desc.kind == "gimmick") {
        const char* motions[] = { "none", "move_y", "rotate_y", "rotate_z", "fall", "blink" };
        int motionIndex = desc.gimmickMotion == "move_y" ? 1
            : desc.gimmickMotion == "rotate_y"           ? 2
            : desc.gimmickMotion == "rotate_z"           ? 3
            : desc.gimmickMotion == "fall"               ? 4
            : desc.gimmickMotion == "blink"              ? 5
                                                         : 0;
        if (ImGui::Combo("動作プリセット", &motionIndex, motions, 5)) {
            editor.RecordUndoSnapshotNow();
            desc.gimmickMotion = motions[motionIndex];
        }
        captureItemUndo(ImGui::DragFloat("動作量", &desc.motionAmount, 0.1f));
        captureItemUndo(ImGui::DragFloat("動作速度", &desc.motionSpeed, 0.1f, 0.0f, 20.0f));
    }
    if (desc.kind == "camera_point") {
        if (editor.camera_ && ImGui::Button("現在のビューをカメラポイントへ保存")) {
            editor.RecordUndoSnapshotNow();
            desc.position = editor.camera_->GetTranslate();
            desc.rotation = editor.camera_->GetRotate();
            editor.RefreshTransforms(editor.objects_[editor.selIndex_]);
        }
        if (editor.camera_ && ImGui::Button("カメラポイントをプレビュー")) {
            editor.camera_->SetTranslate(editor.WorldPositionOf(desc));
            editor.camera_->SetRotate(desc.rotation);
        }
        captureItemUndo(ImGui::DragFloat("カメラ補間秒数", &desc.cameraBlendSeconds, 0.05f, 0.0f, 10.0f));
        captureItemUndo(ImGui::DragFloat("カメラ維持秒数", &desc.cameraHoldSeconds, 0.1f, 0.0f, 30.0f));
    }
}

bool StageEditorInspectorPanel::RenderObjectInspector(StageEditor& editor)
{
    if (editor.selKind_ != StageEditor::SelKind::Object || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.objects_.size())) {
        return false;
    }
    bool structuralDirty = false;
    bool transformDirty = false;
    RenderObjectIdentity(editor, structuralDirty);
    RenderObjectVisual(editor, structuralDirty);
    RenderObjectTransform(editor, structuralDirty, transformDirty);
    RenderObjectGameplay(editor, structuralDirty);

    auto& entry = editor.objects_[editor.selIndex_];
    const bool visualKind = entry.desc.kind == "prop" || entry.desc.kind == "background" || entry.desc.kind == "gimmick" || entry.desc.kind == "terrain";
    if (structuralDirty) {
        editor.RegenerateInstances(entry);
    } else if (transformDirty && visualKind) {
        editor.RefreshTransforms(entry);
    }
    return true;
}

bool StageEditorInspectorPanel::RenderTriggerInspector(StageEditor& editor)
{
    if (editor.selKind_ != StageEditor::SelKind::Trigger || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.triggers_.size())) {
        return false;
    }
    TriggerDesc& desc = editor.triggers_[editor.selIndex_].GetDesc();

    auto captureItemUndo = [&](bool changed) {
        if (ImGui::IsItemActivated()) {
            editor.BeginUndoCapture();
        }
        if (changed) {
            editor.MarkUndoDirty();
        }
        if (ImGui::IsItemDeactivated()) {
            editor.CommitUndoCapture();
        }
        return changed;
    };

    char nameBuf[96];
    strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
    bool nameChanged = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
    if (captureItemUndo(nameChanged)) {
        desc.name = nameBuf;
    }

    char flagBuf[96];
    strncpy_s(flagBuf, desc.flag.c_str(), _TRUNCATE);
    bool flagChanged = ImGui::InputText("フラグ名", flagBuf, sizeof(flagBuf));
    if (captureItemUndo(flagChanged)) {
        desc.flag = flagBuf;
    }
    EditorUI::HelpMarker("プレイヤーが球に入ると、この名前のフラグが立ちます。\nノードエディタ(F1)のGetFlagノードで参照できます");

    captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, 0.1f));
    captureItemUndo(ImGui::DragFloat("半径", &desc.radius, 0.05f, 0.1f, 50.0f));
    {
        bool value = desc.value;
        if (ImGui::Checkbox("進入時に設定する値", &value)) {
            editor.RecordUndoSnapshotNow();
            desc.value = value;
        }
        bool once = desc.once;
        if (ImGui::Checkbox("一度だけ成立させる", &once)) {
            editor.RecordUndoSnapshotNow();
            desc.once = once;
        }
        bool spawnsWaterSplash = desc.spawnsWaterSplash;
        if (ImGui::Checkbox("この地点に来たら水しぶきを出す", &spawnsWaterSplash)) {
            editor.RecordUndoSnapshotNow();
            desc.spawnsWaterSplash = spawnsWaterSplash;
        }
        EditorUI::HelpMarker("動作対象やイベントパネルでの接続は不要です。プレイヤーがこのトリガーに進入した瞬間、この位置で水しぶきが発生します");
    }
    ImGui::TextDisabled(editor.triggers_[editor.selIndex_].IsInside() ? "プレイヤーは範囲内にいます" : "プレイヤーは範囲外です");
    return true;
}

bool StageEditorInspectorPanel::RenderExternalInspector(StageEditor& editor)
{
    if (editor.selKind_ != StageEditor::SelKind::External || editor.selIndex_ < 0
        || editor.selIndex_ >= static_cast<int>(editor.externalEntities_.size())) {
        return false;
    }
    StageEditor::ExternalEntityRef& ref = editor.externalEntities_[editor.selIndex_];
    ImGui::Text("%s", ref.name.c_str());
    ImGui::TextDisabled("ランタイム実体（JSONには保存されません）");
    if (ref.position) {
        ImGui::DragFloat3("位置", &ref.position->x, 0.1f);
    }
    if (ref.object) {
        Transform& transform = ref.object->GetTransform();
        ImGui::DragFloat3("回転", &transform.rotate.x, 0.01f);
        ImGui::DragFloat3("スケール", &transform.scale.x, 0.05f);
        ImGui::TextDisabled("シーン背景（実行中のみ編集）");
    }
    if (ref.getVisualPreset && ref.setVisualPreset) {
        const int preset = ref.getVisualPreset();
        const bool isCustomStatic = preset < 0 && ref.getStaticVisualModel && !ref.getStaticVisualModel().empty();
        const std::string currentModel = preset == 1
            ? "Resources/AnimatedMechPack/Textured/glTF/Mike.gltf"
            : preset == 0 ? "Resources/AlienAnimated/glTF/Alien.gltf"
            : isCustomStatic ? ref.getStaticVisualModel()
                              : "（未設定）";
        ImGui::TextWrapped("モデル: %s", currentModel.c_str());
        if (isCustomStatic) {
            const std::string currentTex = ref.getStaticVisualTexture ? ref.getStaticVisualTexture() : "";
            ImGui::TextWrapped("テクスチャ: %s", currentTex.empty() ? "（白テクスチャ）" : currentTex.c_str());
        }
        if (ImGui::Button("モデルファイルを選択...")) {
            const std::string selected = OpenFileDialog(
                "対応モデル(gltf/glb/obj)\0*.gltf;*.glb;*.obj\0すべてのファイル\0*.*\0\0", "Resources");
            if (!selected.empty()) {
                std::string normalized = selected;
                std::replace(normalized.begin(), normalized.end(), '\\', '/');
                if (normalized.find("Alien.gltf") != std::string::npos) {
                    if (ref.setStaticVisualModel)
                        ref.setStaticVisualModel("", "");
                    ref.setVisualPreset(0);
                } else if (normalized.find("Mike.gltf") != std::string::npos) {
                    if (ref.setStaticVisualModel)
                        ref.setStaticVisualModel("", "");
                    ref.setVisualPreset(1);
                } else {
                    // Model::Initializeは拡張子で分岐する（.obj以外はAssimp経由のLoadGltfFileへ）ため、
                    // gltf/glb/obj問わずここへそのまま渡してよい。テクスチャは既存の指定があれば引き継ぐ
                    if (ref.setStaticVisualModel) {
                        const std::string keepTex = ref.getStaticVisualTexture ? ref.getStaticVisualTexture() : "";
                        ref.setStaticVisualModel(ToProjectRelativePath(selected), keepTex);
                        editor.statusMessage_ = "アニメーションなしの静的モデルとして読み込みました";
                        editor.statusTimer_ = 4.0f;
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
                    editor.statusTimer_ = 4.0f;
                }
            }
        }
        if (ImGui::Button("モデルを自動選択へ戻す")) {
            if (ref.setStaticVisualModel)
                ref.setStaticVisualModel("", "");
            ref.setVisualPreset(-1);
        }
    }
    return true;
}

void StageEditorInspectorPanel::Render(StageEditor& editor)
{

    constexpr float kToolbarHeight = 42.0f;
    constexpr float kPanelWidth = 300.0f;
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(WinApp::kClientWidth) - kPanelWidth, kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, static_cast<float>(WinApp::kClientHeight) - kToolbarHeight), ImGuiCond_Always);
    ImGui::Begin("詳細設定", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (!RenderObjectInspector(editor)
        && !RenderTriggerInspector(editor)
        && !RenderExternalInspector(editor)) {
        ImGui::TextDisabled("左のステージエディタでオブジェクト/トリガーを選択してください");
    }
    ImGui::End();
}
} // namespace engine::game
#endif // USE_IMGUI
