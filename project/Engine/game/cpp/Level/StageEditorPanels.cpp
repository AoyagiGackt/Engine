/**
 * @file StageEditorPanels.cpp
 * @brief ステージ編集パネルの表示責務を個別に実行する
 */
#ifdef USE_IMGUI
#include "StageEditorPanels.h"
#include "Camera.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "StageEditor.h"
#include "WinApp.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace engine::game {
using namespace engine::graphics;
namespace {
/** @brief 検索語(小文字化済み)がtext(小文字化して比較)に部分一致するか。空検索語は常にtrue */
bool MatchesSearch(const std::string& searchTextLower, const std::string& text)
{
    if (searchTextLower.empty()) {
        return true;
    }
    std::string target = text;
    std::transform(target.begin(), target.end(), target.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return target.find(searchTextLower) != std::string::npos;
}
} // namespace

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
    RenderCameraSection(editor);
    RenderFileAndHistoryActions(editor);

    const std::string searchTextLower = RenderSearchBar(editor);
    RenderObjectTree(editor, searchTextLower);
    RenderExternalEntityList(editor, searchTextLower);
    RenderTriggerList(editor, searchTextLower);

    ImGui::Separator();
}

void StageEditorHierarchyPanel::RenderCameraSection(StageEditor& editor)
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
}

void StageEditorHierarchyPanel::RenderFileAndHistoryActions(StageEditor& editor)
{
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
}

std::string StageEditorHierarchyPanel::RenderSearchBar(StageEditor& editor)
{
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hierarchySearch", "名前・種類・モデルを検索", editor.hierarchySearch_, sizeof(editor.hierarchySearch_));

    std::string searchText = editor.hierarchySearch_;
    std::transform(searchText.begin(), searchText.end(), searchText.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return searchText;
}

void StageEditorHierarchyPanel::RenderObjectTree(StageEditor& editor, const std::string& searchTextLower)
{
    auto matchesSearch = [&](const std::string& text) { return MatchesSearch(searchTextLower, text); };

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
        if (ImGui::MenuItem("テキスト")) {
            addEntry("text", "ui_text");
            ObjectDesc& text = editor.objects_.back().desc;
            text.text = "テキスト";
            text.textSpace = "screen";
            text.position = { 100.0f, 100.0f, 0.0f }; // スクリーン座標(px)として使う
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
        if (!searchTextLower.empty()) {
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
}

void StageEditorHierarchyPanel::RenderExternalEntityList(StageEditor& editor, const std::string& searchTextLower)
{
    if (editor.externalEntities_.empty()) {
        return;
    }
    char entHeader[48];
    snprintf(entHeader, sizeof(entHeader), "エンティティ (%d)", static_cast<int>(editor.externalEntities_.size()));
    bool entOpen = ImGui::TreeNodeEx(entHeader, ImGuiTreeNodeFlags_DefaultOpen);
    if (entOpen) {
        for (int i = 0; i < static_cast<int>(editor.externalEntities_.size()); ++i) {
            if (!MatchesSearch(searchTextLower, editor.externalEntities_[i].name)) {
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

void StageEditorHierarchyPanel::RenderTriggerList(StageEditor& editor, const std::string& searchTextLower)
{
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
            if (!MatchesSearch(searchTextLower, d.name) && !MatchesSearch(searchTextLower, d.flag)) {
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

    const float hierarchyHeight = (static_cast<float>(WinApp::kClientHeight) - StageEditor::kToolbarHeight) * 0.62f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, StageEditor::kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(StageEditor::kLeftPanelWidth, hierarchyHeight), ImGuiCond_Always);
    ImGui::Begin("ステージエディタ", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    RenderGuideAndFileActions(editor);
    RenderEntityBrowser(editor);
    RenderSelectionActions(editor);
    ImGui::End();
}

} // namespace engine::game
#endif // USE_IMGUI
