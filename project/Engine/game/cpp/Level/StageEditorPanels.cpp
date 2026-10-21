/**
 * @file StageEditorPanels.cpp
 * @brief ステージ編集パネルの表示責務を個別に実行する
 */
#ifdef USE_IMGUI
#include "StageEditorPanels.h"
#include "StageEditorObjectPresets.h"
#include "TextBufferCapacity.h"
#include "Camera.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "StageEditor.h"
#include "StageEditorUiStyle.h"
#include "WinApp.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <vector>

namespace engine::game {
using namespace engine::graphics;
namespace {
namespace EditorUi = engine::game::StageEditorUiStyle;

constexpr int kGuideStepCount = 4; // 制作手順の項目数（地面・敵・トリガー・イベント接続）
constexpr float kNewLevelNameWidth = 150.0f;
constexpr float kSnapStepWidth = 70.0f;
constexpr float kFileButtonWidth = 80.0f; // 開く・保存・元に戻す・やり直すのボタンの幅
constexpr Vector3 kNewLevelPlayerSpawn = { 4.0f, 0.4f, 0.0f };
constexpr int kNewLevelLeftColumn = 2; // 新規レベルの床・天井・左壁を並べ始める列

/** @brief 検索語(小文字化済み)がtext(小文字化して比較)に部分一致するか。空検索語は常にtrue */
bool MatchesSearch(const std::string& searchTextLower, const std::string& text)
{
    if (searchTextLower.empty())
    {
        return true;
    }
    std::string target = text;
    std::transform(target.begin(), target.end(), target.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return target.find(searchTextLower) != std::string::npos;
}
} // namespace
/** @brief 制作ガイドの進捗チェックリストと、開く/保存等のファイル操作行を描画する */
void StageEditorHierarchyPanel::RenderGuideAndFileActions(StageEditor& editor)
{
    if (ImGui::CollapsingHeader("制作ガイド"))
    {
        // 地形・敵・トリガー・イベント接続の4手順がそれぞれ済んでいるかを配置物から数える
        bool hasGround = false;
        bool hasEnemy = false;
        bool hasEventConnection = false;
        for (const auto& entry : editor.objects_)
        {
            // 当たり判定のある物か地面扱いの種類があれば地形は済み、敵を置く種類があれば敵は済み
            const ObjectKind& kind = ObjectKind::Of(entry.desc.kind);
            hasGround |= entry.desc.solid || kind.CountsAsGround();
            hasEnemy |= kind.PlacesEnemy();
            // きっかけのフラグが入っていればイベントで繋がっている
            hasEventConnection |= !entry.desc.activationFlag.empty();
        }
        // トリガーは別の一覧なので、1つ以上あるかで判定する
        const int completed = static_cast<int>(hasGround) + static_cast<int>(hasEnemy)
            + static_cast<int>(!editor.triggers_.empty()) + static_cast<int>(hasEventConnection);
        // 済んだ手順の割合と、手順ごとの完了表示
        ImGui::ProgressBar(static_cast<float>(completed) / static_cast<float>(kGuideStepCount), ImVec2(-1.0f, 0.0f));
        ImGui::TextWrapped("上から順に進めると、配置からゲーム進行までコードを書かずに作成できます。");
        // 最初の手順だけは、未完了なら次にやることとして目立たせる
        ImGui::BulletText("%s 1 地形を置き、詳細設定で当たり判定を有効にする",
            hasGround ? "[完了]" : "[次]  ");
        ImGui::BulletText("%s 2 敵またはWaveを配置する",
            hasEnemy ? "[完了]" : "[未]  ");
        ImGui::BulletText("%s 3 プレイヤーが入るトリガーを配置する",
            !editor.triggers_.empty() ? "[完了]" : "[未]  ");
        ImGui::BulletText("%s 4 イベントで条件と動作対象を接続する",
            hasEventConnection ? "[完了]" : "[未]  ");
        // 次の手順に使うパネルを開くボタン
        if (ImGui::SmallButton("Waveを作る"))
        {
            editor.showWavePanel_ = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("イベントを作る"))
        {
            editor.showNoCodeEventPanel_ = true;
        }
        ImGui::SameLine();
        // 画面中央に水しぶきトリガーを1つ作り、そのまま選択する
        if (ImGui::SmallButton("水イベントを作る"))
        {
            editor.RecordUndoSnapshotNow();
            const Vector3 center = editor.ViewCenterOnGround();
            // 名前は通し番号で重ならないようにし、水しぶきを出す設定だけを入れる
            TriggerDesc desc;
            desc.name = "water_trigger_" + std::to_string(editor.nextSerial_++);
            desc.position = center;
            desc.spawnsWaterSplash = true;
            editor.AppendTriggerAndSelect(desc);
            editor.statusMessage_ = "水イベントを画面中央に作成しました。ドラッグして位置を調整してください";
            editor.statusTimer_ = StageEditor::kStatusNormalSeconds;
        }
        EditorUI::HelpMarker("動作対象やイベントパネルでの接続を行わず、この場所に来たら水しぶきが出るトリガーを1回で作ります");
        ImGui::SameLine();
        // 手順の詳しい説明は使い方パネルに任せる
        if (ImGui::SmallButton("詳しい説明"))
        {
            editor.showEditorHelp_ = true;
        }
        ImGui::TextDisabled("最後に上部のテストで確認し、制作パネルから検証して保存する");
    }
    // 操作方法の一覧
    if (ImGui::CollapsingHeader("使い方"))
    {
        // カメラと変形ツールの操作
        ImGui::BulletText("右ボタンを押しながらWASD/QE: カメラ移動    中ドラッグ: 平行移動    ホイール: ズーム");
        ImGui::BulletText("W/E/R: 移動・回転・拡縮ツール。選択物の矢印・リング・四角をつかんで操作");
        // 選択とマウスでの配置操作
        ImGui::BulletText("画面上のオブジェクトを左クリック: 選択    何も無い所から左ドラッグ: 範囲選択");
        ImGui::BulletText("本体をそのまま左ドラッグ: つかんで移動（Shift+ドラッグ: 奥行き(Z)移動）");
        ImGui::BulletText("右クリック: その場に置く / 複製・削除・親子付け・ここからテスト のメニュー");
        ImGui::BulletText("階層でドラッグ＆ドロップ: 落とした先の子になる（見出しへ落とすと親を外す）");
        ImGui::BulletText("Ctrlを押しながらドラッグ: スナップOFFでも一時的に揃う    Esc: 選択解除    Ctrl+A: 全選択");
        // キーボードショートカット
        ImGui::BulletText("Ctrl+Z: 元に戻す  Ctrl+Y: やり直す  Ctrl+S: 保存");
        ImGui::BulletText("Ctrl+D・複製ボタン: 選択中の物を複製    Deleteキー: 削除");
        ImGui::BulletText("Ctrl+C / Ctrl+V: 選択中の配置物をコピー・貼り付け");
        ImGui::BulletText("Ctrlを押しながら選択: 複数選択して一括移動・回転・拡縮");
        ImGui::BulletText("矢印キー: 選択物を少しずつ移動（Shift+上下で奥行き）  F: 選択物へカメラ");
        ImGui::BulletText("階層のダブルクリック: その配置物へカメラを寄せる");
        // 各パネルの役割
        ImGui::BulletText("制作パネルのテスト: 画面中央や選択位置にプレイヤーを置いてすぐ遊べる");
        ImGui::BulletText("スナップをONにすると、移動・配置・複製の座標が指定間隔の倍数に揃う");
        ImGui::BulletText("[+]ボタン: 配置物/敵(ナイト・汎用エネミー)を選んで画面中央に新規追加");
        ImGui::BulletText("敵は実際にHPを持って湧く本物の敵配置数・種類は自由に増減できる");
        ImGui::BulletText("左下のアセットパレット: モード切替で新規配置/選択物への差し替えを選べる");
        ImGui::BulletText("右の詳細設定で数値・モデル・親子関係を編集");
        ImGui::BulletText("親を設定すると、親を動かしたとき子も一緒に動く");
        // 画面上の印の意味と保存
        ImGui::BulletText("トリガー(水色の球): プレイヤーが入るとフラグON");
        ImGui::BulletText("　フラグはノードエディタ(F1)のGetFlagで参照できる");
        ImGui::BulletText("エンティティ(マゼンタの十字): Player/Enemy等も同様に選択・ドラッグ移動できる");
        ImGui::BulletText("　ただし削除不可・JSONに保存されない（位置はシーン起動時の初期値に戻る）");
        ImGui::BulletText("保存ボタンでJSONへ書き出しゲーム本編に即反映");
    }
}
/** @brief オブジェクトエンティティトリガーの各ツリーをまとめて描画する（RenderObjectTree等の呼び出し元） */
void StageEditorHierarchyPanel::RenderEntityBrowser(StageEditor& editor)
{
    const std::string searchTextLower = RenderSearchBar(editor);
    RenderObjectTree(editor, searchTextLower);
    RenderExternalEntityList(editor, searchTextLower);
    RenderTriggerList(editor, searchTextLower);
    ImGui::Separator();
}
/** @brief ゲームカメラの位置・回転を編集するセクションを描画する */
void StageEditorHierarchyPanel::RenderCameraSection(StageEditor& editor)
{
    if (editor.camera_ && ImGui::CollapsingHeader("ゲームカメラ", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Vector3 position = editor.camera_->GetTranslate();
        Vector3 rotation = editor.camera_->GetRotate();
        if (ImGui::DragFloat3("カメラ位置", &position.x, EditorUi::kDragStepPosition))
        {
            editor.camera_->SetTranslate(position);
        }
        if (ImGui::DragFloat3("カメラ回転", &rotation.x, EditorUi::kDragStepRotation))
        {
            editor.camera_->SetRotate(rotation);
        }
        ImGui::TextDisabled("WASD: XY移動 / Q,E: 奥行き移動");
    }
}
/** @brief レベルファイルの開く/保存、元に戻す/やり直す、スナップ、親子リンク等の操作行を描画する */
void StageEditorHierarchyPanel::RenderFileAndHistoryActions(StageEditor& editor)
{
    // レベル切替: Resources/Levels 直下のJSONを列挙して選ぶ（自動保存ファイルは除外）
    // 選んだ瞬間に読み込み直す。未保存の編集があれば下の開く確認モーダルに回す
    constexpr const char* kLevelsDirectory = "Resources/Levels";
    constexpr const char* kAutoSaveSuffix = ".autosave.json";
    bool requestOpen = false;
    const std::string currentFile = std::filesystem::path(editor.levelPath_).filename().string();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##levelFile", currentFile.empty() ? "レベルを選択" : currentFile.c_str()))
    {
        // フォルダが無くても例外にせず、空の一覧として扱う
        std::error_code fileError;
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(kLevelsDirectory, fileError))
        {
            // 通常の .json ファイルだけを候補にする
            const std::string name = entry.path().filename().string();
            if (!entry.is_regular_file() || entry.path().extension() != ".json" || name.ends_with(kAutoSaveSuffix))
            {
                continue;
            }
            files.push_back(name);
        }
        // 列挙の順番は環境で変わるので名前順に並べる
        std::sort(files.begin(), files.end());
        for (const std::string& name : files)
        {
            // 今開いているファイルを選び直した時は読み込み直さない
            const bool selected = name == currentFile;
            if (ImGui::Selectable(name.c_str(), selected) && !selected)
            {
                editor.levelPath_ = std::string(kLevelsDirectory) + "/" + name;
                requestOpen = true;
            }
        }
        ImGui::EndCombo();
    }
    EditorUI::HelpMarker("Resources/Levels のレベルを切り替えます。本編で使うレベルは game_rules.json の levelPaths で指定します");
    // 新規レベル名を入れると作成ボタンが押せるようになる
    ImGui::SetNextItemWidth(kNewLevelNameWidth);
    ImGui::InputTextWithHint("##newLevel", "新規レベル名", editor.newLevelName_, sizeof(editor.newLevelName_));
    ImGui::SameLine();
    ImGui::BeginDisabled(editor.newLevelName_[0] == '\0');
    if (ImGui::Button("新規作成"))
    {
        // 床・左右の壁・天井だけの土台を書き出してから開く（真っ白なレベルだと足場が無く落ちて始まらない）
        constexpr int kNewLevelWidth = 40;
        constexpr int kNewLevelHeight = 13;
        constexpr float kFloorY = -0.98f;
        constexpr float kCeilingY = 13.0f;
        LevelData data;
        data.playerSpawn = kNewLevelPlayerSpawn;
        // 当たり判定つきのブロックを指定の軸に並べた列配置を1つ作る
        auto makeRow = [](const char* name, char axis, int count, const Vector3& position) {
            ObjectDesc row;
            row.name = name;
            row.type = PlacementType::Row;
            row.kind = "prop";
            row.axis = axis;
            row.count = count;
            row.model = "Resources/block/block.obj";
            row.texture = "Resources/block/block.png";
            row.position = position;
            row.lighting = false;
            row.solid = true;
            return row;
        };
        // 床・天井は横一列、左右の壁は縦一列に並べる（右の壁は床の高さから1段多く積む）
        data.objects.push_back(makeRow("floor", 'x', kNewLevelWidth, { static_cast<float>(kNewLevelLeftColumn), kFloorY, 0.0f }));
        data.objects.push_back(makeRow("ceiling", 'x', kNewLevelWidth, { static_cast<float>(kNewLevelLeftColumn), kCeilingY, 0.0f }));
        data.objects.push_back(makeRow("wall_left", 'y', kNewLevelHeight, { static_cast<float>(kNewLevelLeftColumn), 0.0f, 0.0f }));
        data.objects.push_back(makeRow("wall_right", 'y', kNewLevelHeight + 1, { static_cast<float>(kNewLevelWidth + kNewLevelLeftColumn), kFloorY, 0.0f }));
        // 土台をファイルに書き出し、そのファイルを開く
        const std::string newPath = std::string(kLevelsDirectory) + "/" + editor.newLevelName_ + ".json";
        LevelLoader::Save(newPath, data);
        editor.levelPath_ = newPath;
        requestOpen = true;
    }
    ImGui::EndDisabled();
    // レベルのパスは直接入力でも指定できる
    char pathBuf[TextBufferCapacity::kPath];
    strncpy_s(pathBuf, editor.levelPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf)))
    {
        editor.levelPath_ = pathBuf;
    }
    // 未保存の編集がある時は黙って破棄せず、確認モーダルを挟んでから開く
    auto openOrConfirm = [&editor]() {
        if (editor.dirty_)
        {
            ImGui::OpenPopup("開くの確認");
        }
        else
        {
            editor.Open(editor.levelPath_, editor.modelCommon_, editor.camera_);
        }
    };
    // 一覧で選んだ・新規作成した時と、開くボタンを押した時に開く
    // ボタンは毎フレーム描くので、requestOpen より先に評価する
    const bool openPressed = ImGui::Button("開く", ImVec2(kFileButtonWidth, 0.0f));
    if (requestOpen || openPressed)
    {
        openOrConfirm();
    }
    // 確認で破棄を選んだ時だけ開く
    if (EditorUI::ConfirmModal("開くの確認",
            "未保存の変更があります。\n変更を破棄して読み込み直しますか？",
            "破棄して開く")
        == EditorUI::ConfirmResult::Ok)
    {
        editor.Open(editor.levelPath_, editor.modelCommon_, editor.camera_);
    }
    ImGui::SameLine();
    // 保存と、未保存の表示
    if (ImGui::Button("保存", ImVec2(kFileButtonWidth, 0.0f)))
    {
        editor.Save();
    }
    if (editor.dirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(EditorUi::kWarningColor, "未保存");
    }
    // 元に戻す・選んだ発生条件のフラグを立てて試す・やり直す
    ImGui::BeginDisabled(!editor.history_.CanUndo());
    if (ImGui::Button("元に戻す", ImVec2(kFileButtonWidth, 0.0f)))
    {
        editor.Undo();
    }
    ImGui::EndDisabled();
    if (ImGui::Button("選択条件をテスト発火"))
    {
        // 発生条件の番号はトリガーが先、その後ろに配置物の番号が続く
        const int sourceIndex = editor.eventConnection_.SourceIndex();
        if (sourceIndex >= 0 && sourceIndex < static_cast<int>(editor.triggers_.size()))
        {
            // トリガーなら、そのトリガーのフラグを立てる
            GameFlags::GetInstance()->SetFlag(editor.triggers_[sourceIndex].GetDesc().flag, true);
        }
        else
        {
            // イベント条件なら、条件名から作ったフラグを立てる
            const int conditionIndex = sourceIndex - static_cast<int>(editor.triggers_.size());
            if (conditionIndex >= 0 && conditionIndex < static_cast<int>(editor.objects_.size()))
            {
                GameFlags::GetInstance()->SetFlag(ConditionFlagName(editor.objects_[conditionIndex].desc.name), true);
            }
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.history_.CanRedo());
    if (ImGui::Button("やり直す", ImVec2(kFileButtonWidth, 0.0f)))
    {
        editor.Redo();
    }
    ImGui::EndDisabled();
    // 配置の座標をそろえる間隔
    ImGui::Checkbox("スナップ", &editor.snapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kSnapStepWidth);
    ImGui::DragFloat("##snapStep", &editor.snapStep_, EditorUi::kDragStepPosition, EditorUi::kMinSnapStep, EditorUi::kMaxSnapStep, "%.1f");
    // 選択物を子にして、次にクリックした物を親にする親子付け
    const bool canLink = editor.selKind_ == StageEditor::SelKind::Object
        && editor.selIndex_ >= 0 && editor.selIndex_ < static_cast<int>(editor.objects_.size());
    ImGui::BeginDisabled(!canLink);
    if (ImGui::Button(editor.parentLinkChildIndex_ >= 0 ? "親子リンクをキャンセル" : "選択物を子にして親をクリック"))
    {
        editor.parentLinkChildIndex_ = editor.parentLinkChildIndex_ >= 0 ? -1 : editor.selIndex_;
    }
    ImGui::EndDisabled();
    // 親を選ぶ待ちの間は、次に何をすればよいかを出す
    if (editor.parentLinkChildIndex_ >= 0)
    {
        ImGui::TextColored(EditorUi::kNoticeColor, "接続する親ブロックを画面上でクリック");
    }
    EditorUI::HelpMarker("ドラッグ移動・新規配置・複製の座標を、この間隔の倍数に揃えます");
    // 直前の操作結果
    if (editor.statusTimer_ > 0.0f)
    {
        ImGui::TextDisabled("%s", editor.statusMessage_.c_str());
    }
    ImGui::Separator();
}
/** @brief 階層検索ボックスを描画し、小文字化した検索語を返す（空なら全件一致扱い） */
std::string StageEditorHierarchyPanel::RenderSearchBar(StageEditor& editor)
{
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hierarchySearch", "名前・種類・モデルを検索", editor.hierarchySearch_, sizeof(editor.hierarchySearch_));
    std::string searchText = editor.hierarchySearch_;
    std::transform(searchText.begin(), searchText.end(), searchText.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return searchText;
}
/** @brief オブジェクトツリー（新規追加ポップアップ・検索一致時のフラット表示・通常時の親子階層表示）を描画する */
void StageEditorHierarchyPanel::RenderObjectTree(StageEditor& editor, const std::string& searchTextLower)
{
    auto matchesSearch = [&](const std::string& text) {
        return MatchesSearch(searchTextLower, text);
    };
    char objHeader[TextBufferCapacity::kSectionTitle];
    // 見出しに配置物の数を出す
    snprintf(objHeader, sizeof(objHeader), "オブジェクト (%d)", static_cast<int>(editor.objects_.size()));
    bool objOpen = ImGui::TreeNodeEx(objHeader, ImGuiTreeNodeFlags_DefaultOpen);
    // 見出しへ落とすと親を外す（一番上の階層へ戻す）
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("STAGE_OBJECT"))
        {
            editor.SetParentPreservingWorld(*static_cast<const int*>(payload->Data), -1);
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    // 追加ボタンで種類を選ぶメニューを開く
    if (ImGui::SmallButton("追加##addObj"))
    {
        ImGui::OpenPopup("AddObjectPopup");
    }
    if (ImGui::BeginPopup("AddObjectPopup"))
    {
        // 見えている画面の中央（z=0平面上）に置くカメラをどこへ動かしていても手元に出る
        const Vector3 center = editor.ViewCenterOnGround();
        /** @brief 完成した配置設定をUndo記録後に追加し、実体生成と選択更新を一度だけ行う */
        auto addEntry = [&](const std::string& namePrefix, const std::string& kind,
                            const char* preset = nullptr, bool activateOnFlag = false) {
            StageEditor::ObjectEntry entry;
            // 画面中央をスナップした位置に置き、プリセットがあれば見た目や設定を当てる
            entry.desc.kind = kind;
            entry.desc.position = center;
            entry.desc.position.x = editor.SnapValue(entry.desc.position.x);
            entry.desc.position.y = editor.SnapValue(entry.desc.position.y);
            if (preset && !StageEditorObjectPresets::TryApply(preset, entry.desc))
            {
                editor.statusMessage_ = "配置プリセットを読み込めませんでした。設定ファイルを確認してください";
                editor.statusTimer_ = StageEditor::kStatusNormalSeconds;
                return;
            }
            editor.RecordUndoSnapshotNow();
            entry.desc.name = namePrefix + "_" + std::to_string(editor.nextSerial_++);
            if (activateOnFlag)
            {
                entry.desc.activationFlag = entry.desc.name + "_active";
            }
            editor.objects_.push_back(std::move(entry));
            editor.RegenerateInstances(editor.objects_.back());
            // 追加した物だけを選択した状態にする
            editor.SelectLastObject();
        };
        /** @brief 追加メニューの1項目（表示名・名前の頭・種類・プリセット・フラグで動かすか） */
        struct AddMenuItem {
            const char* label;
            const char* namePrefix; ///< nullptr なら文章UIとして画面中央に追加する
            const char* kind;
            const char* preset;
            bool activateOnFlag;
        };
        static constexpr AddMenuItem kAddMenuItems[] = {
            { "配置物（ブロック）", "obj", "prop", "prop", false },
            { "背景モデル", "background", "background", "background", false },
            { "敵：ナイト", "knight", "enemy_knight", nullptr, false },
            { "敵：汎用エネミー", "enemy", "enemy_basic", nullptr, false },
            { "SpawnPoint", "spawn", "spawn_point", nullptr, true },
            { "ギミック", "gimmick", "gimmick", "gimmick", true },
            { "カメラポイント", "camera", "camera_point", nullptr, true },
            { "収集物（エネルギーコア）", "pickup", "pickup", "pickup", false },
            { "壊せる物（爆発バレル）", "breakable", "breakable", "breakable", false },
            { "巡回Waypoint", "waypoint", "patrol_point", nullptr, false },
            { "イベント条件", "condition", "event_condition", nullptr, false },
            { "文章UI（画面に固定）", nullptr, nullptr, nullptr, false },
            { "Terrain", "terrain", "terrain", "terrain", false },
        };
        // 表の順に並べ、選ばれた項目だけを追加する
        for (const AddMenuItem& item : kAddMenuItems)
        {
            if (!ImGui::MenuItem(item.label))
            {
                continue;
            }
            if (item.namePrefix == nullptr)
            {
                editor.AddUITextAt(EditorUi::kScreenCenterPosition);
            }
            else
            {
                addEntry(item.namePrefix, item.kind, item.preset, item.activateOnFlag);
            }
        }
        ImGui::EndPopup();
    }
    if (objOpen)
    {
        // 検索中は親子をたどらず、名前・種類・モデルが一致するものを平らに並べる（Ctrlで複数選択の切り替え）
        if (!searchTextLower.empty())
        {
            for (int i = 0; i < static_cast<int>(editor.objects_.size()); ++i)
            {
                const ObjectDesc& d = editor.objects_[i].desc;
                if (!matchesSearch(d.name) && !matchesSearch(d.kind) && !matchesSearch(d.model))
                {
                    continue;
                }
                bool selected = std::find(editor.selectedObjectIndices_.begin(), editor.selectedObjectIndices_.end(), i)
                    != editor.selectedObjectIndices_.end();
                std::string label = d.name + "  " + d.kind + "##searchObj" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    editor.selKind_ = StageEditor::SelKind::Object;
                    editor.selIndex_ = i;
                    if (!ImGui::GetIO().KeyCtrl)
                    {
                        editor.selectedObjectIndices_.clear();
                    }
                    auto selectedIt = std::find(editor.selectedObjectIndices_.begin(), editor.selectedObjectIndices_.end(), i);
                    if (selectedIt == editor.selectedObjectIndices_.end())
                    {
                        editor.selectedObjectIndices_.push_back(i);
                    }
                    else if (ImGui::GetIO().KeyCtrl)
                    {
                        editor.selectedObjectIndices_.erase(selectedIt);
                    }
                }
            }
        }
        else
        {
            // 検索していない時は、親のいない物から子をたどって木構造で並べる
            auto parentExists = [&](const std::string& parentName) {
                return !parentName.empty() && editor.FindDescByName(parentName) != nullptr;
            };
            for (int i = 0; i < static_cast<int>(editor.objects_.size()); ++i)
            {
                if (!parentExists(editor.objects_[i].desc.parent))
                {
                    editor.DrawHierarchyEntry(i, 0);
                }
            }
        }
        ImGui::TreePop();
    }
}
/** @brief エンティティ（Player/Enemy等の外部登録実体）の一覧を描画する */
void StageEditorHierarchyPanel::RenderExternalEntityList(StageEditor& editor, const std::string& searchTextLower)
{
    if (editor.externalEntities_.empty())
    {
        return;
    }
    char entHeader[TextBufferCapacity::kSectionTitle];
    snprintf(entHeader, sizeof(entHeader), "エンティティ (%d)", static_cast<int>(editor.externalEntities_.size()));
    bool entOpen = ImGui::TreeNodeEx(entHeader, ImGuiTreeNodeFlags_DefaultOpen);
    if (entOpen)
    {
        for (int i = 0; i < static_cast<int>(editor.externalEntities_.size()); ++i)
        {
            if (!MatchesSearch(searchTextLower, editor.externalEntities_[i].name))
            {
                continue;
            }
            bool sel = (editor.selKind_ == StageEditor::SelKind::External && editor.selIndex_ == i);
            char label[TextBufferCapacity::kName];
            snprintf(label, sizeof(label), "  %s##ent%d", editor.externalEntities_[i].name.c_str(), i);
            if (ImGui::Selectable(label, sel))
            {
                editor.selKind_ = StageEditor::SelKind::External;
                editor.selIndex_ = i;
            }
        }
        ImGui::TreePop();
    }
}
/** @brief トリガーの一覧と新規追加ボタンを描画する */
void StageEditorHierarchyPanel::RenderTriggerList(StageEditor& editor, const std::string& searchTextLower)
{
    char trgHeader[TextBufferCapacity::kSectionTitle];
    // 見出しにトリガーの数を出し、追加ボタンで画面中央に新しいトリガーを置く
    snprintf(trgHeader, sizeof(trgHeader), "トリガー (%d)", static_cast<int>(editor.triggers_.size()));
    bool trgOpen = ImGui::TreeNodeEx(trgHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("追加##addTrg"))
    {
        editor.RecordUndoSnapshotNow();
        TriggerDesc desc;
        desc.name = "trigger_" + std::to_string(editor.nextSerial_++);
        desc.position = editor.ViewCenterOnGround();
        desc.flag = desc.name;
        editor.AppendTriggerAndSelect(desc);
    }
    // 検索語に一致するトリガーを、フラグと値つきで並べる
    if (trgOpen)
    {
        for (int i = 0; i < static_cast<int>(editor.triggers_.size()); ++i)
        {
            bool sel = (editor.selKind_ == StageEditor::SelKind::Trigger && editor.selIndex_ == i);
            const TriggerDesc& d = editor.triggers_[i].GetDesc();
            if (!MatchesSearch(searchTextLower, d.name) && !MatchesSearch(searchTextLower, d.flag))
            {
                continue;
            }
            char label[TextBufferCapacity::kName];
            snprintf(label, sizeof(label), "  %s -> %s=%s", d.name.c_str(), d.flag.c_str(), d.value ? "true" : "false");
            if (ImGui::Selectable(label, sel))
            {
                editor.selKind_ = StageEditor::SelKind::Trigger;
                editor.selIndex_ = i;
            }
        }
        ImGui::TreePop();
    }
}
/** @brief 選択中の配置物/トリガーに対する削除・複製・コピー&ペーストのボタン行を描画する */
void StageEditorHierarchyPanel::RenderSelectionActions(StageEditor& editor)
{
    // エンティティ(Player/Enemy等)はエディタが生成したものではないため複製・削除の対象外
    bool canDuplicate = (editor.selKind_ == StageEditor::SelKind::Object || editor.selKind_ == StageEditor::SelKind::Trigger);
    bool canDelete = canDuplicate;
    float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!canDuplicate);
    if (ImGui::Button("複製 (Ctrl+D)", ImVec2(halfWidth, 0)))
    {
        editor.DuplicateSelected();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("選択を削除 (Del)", ImVec2(halfWidth, 0)))
    {
        editor.DeleteSelected();
    }
    ImGui::EndDisabled();
}
/** @brief 現在の編集内容から階層パネルを描画する */
void StageEditorHierarchyPanel::Render(StageEditor& editor)
{
    // ツールバーの下、左端の上半分に固定する
    const auto* display = ImGui::GetMainViewport();
    const float hierarchyHeight = (std::max)(1.0f, display->Size.y - StageEditor::kToolbarHeight) * EditorUi::kHierarchyHeightRatio;
    ImGui::SetNextWindowPos(ImVec2(display->Pos.x, display->Pos.y + StageEditor::kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(StageEditor::kLeftPanelWidth, hierarchyHeight), ImGuiCond_Always);
    ImGui::Begin("配置物一覧", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    // 一覧だけをスクロールさせ、選択中の操作を下端に固定する。
    ImGui::BeginChild("配置物スクロール", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false);
    // ファイル操作・カメラ・制作ガイドは普段は畳んでおく
    if (ImGui::CollapsingHeader("ファイル・制作ガイド"))
    {
        RenderFileAndHistoryActions(editor);
        RenderCameraSection(editor);
        RenderGuideAndFileActions(editor);
    }
    // 文章UIだけを抜き出した一覧（マウスを乗せると文章を出す）
    if (ImGui::CollapsingHeader("文章UI", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(editor.playTestMode_);
        if (ImGui::Button("文章を追加", ImVec2(-1.0f, 0.0f)))
        {
            editor.AddUITextAt(EditorUi::kScreenCenterPosition);
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("選択すると右側で文章を編集できます。改行も使えます。");
        // 1件も無い時の案内に使うので数えておく
        int textCount = 0;
        for (int i = 0; i < static_cast<int>(editor.objects_.size()); ++i)
        {
            // 文章UI以外の配置物は下の一覧に出るのでここでは飛ばす
            const auto& desc = editor.objects_[i].desc;
            if (desc.kind != ObjectKindName::kUIText)
            {
                continue;
            }
            ++textCount;
            ImGui::PushID(i);
            // 画面に固定の文章か、ステージに置いた文章かを名前の前に付ける
            const std::string label = std::string(desc.textSpace == TextSpace::Screen ? "[画面] " : "[ステージ] ") + desc.name;
            if (ImGui::Selectable(label.c_str(), editor.selKind_ == StageEditor::SelKind::Object && editor.selIndex_ == i))
            {
                // クリックしたものだけを選んだ状態にする
                editor.selKind_ = StageEditor::SelKind::Object;
                editor.selIndex_ = i;
                editor.selectedObjectIndices_ = { i };
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", desc.text.c_str());
            }
            ImGui::PopID();
        }
        if (textCount == 0)
        {
            ImGui::TextDisabled("まだ文章UIがありません");
        }
    }
    // 配置物・トリガーの一覧と、下端に固定する選択中の操作
    RenderEntityBrowser(editor);
    ImGui::EndChild();
    RenderSelectionActions(editor);
    ImGui::End();
}
} // namespace engine::game
#endif // USE_IMGUI
