/**
 * @file StageEditorGameplayInspector.cpp
 * @brief 配置物のゲーム動作・文章UIの編集
 */
#ifdef USE_IMGUI
#include "StageEditorPanels.h"
#include "Camera.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "KnightEnemy.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "StageEditorUiStyle.h"
#include "WinApp.h"
#include <algorithm>
#include <commdlg.h>
#include <cstring>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")

namespace {
namespace EditorUi = engine::game::StageEditorUiStyle;

constexpr float kMaxConditionSeconds = 300.0f;
constexpr float kMaxPickupRadius = 10.0f;
constexpr float kMaxCameraBlendSeconds = 10.0f;
constexpr float kMaxCameraHoldSeconds = 30.0f;
constexpr float kTextBoxHeight = 140.0f;
constexpr float kMaxTextScale = 10.0f;

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

// screen座標のui_textが編集パネル（ツールバー/左カラム/右インスペクタ）の下に隠れて
// 3Dビュー上でドラッグできない場合に警告し、見える位置へ逃がすボタンを出す
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
    if (desc.kind == "gimmick" || desc.kind == "spawn_point" || desc.kind == "camera_point"
        || desc.kind == "pickup" || desc.kind == "breakable") {
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
            captureItemUndo(ImGui::DragFloat("巡回速度", &desc.patrolSpeed, EditorUi::kDragStepFine, 0.0f, EditorUi::kMaxSpeed));
        }
    }
    if (desc.kind == "enemy_basic" || desc.kind == "spawn_point") {
        // 空="武器を持たない一般敵"。指定すると倒してJキーで奪取できるようになる（GamePlayScene参照）
        // spawn_pointも同じ設定を持ち、出現した敵がそのまま戦闘対象になる
        constexpr const char* kWeaponTypes[] = { "なし", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
        constexpr int kWeaponTypeCount = static_cast<int>(sizeof(kWeaponTypes) / sizeof(kWeaponTypes[0]));
        int weaponTypeIndex = 0;
        for (int i = 1; i < kWeaponTypeCount; ++i) {
            if (desc.weaponType == kWeaponTypes[i]) {
                weaponTypeIndex = i;
                break;
            }
        }
        if (ImGui::Combo("奪取可能な武器", &weaponTypeIndex, kWeaponTypes, kWeaponTypeCount)) {
            editor.RecordUndoSnapshotNow();
            desc.weaponType = weaponTypeIndex == 0 ? "" : kWeaponTypes[weaponTypeIndex];
            structuralDirty = true; // 武器種別はEnemyEntity生成時にしか反映できないため実体を作り直す
        }
        EditorUI::HelpMarker("なしの敵はプレイヤーの攻撃対象になりません（本編の戦闘対象は武器持ちの敵だけです）");
        if (desc.kind == "enemy_basic") {
            // isStageBossはEnemyEntity生成には関わらないメタデータなのでstructuralDirtyは不要
            captureItemUndo(ImGui::Checkbox("ステージボス（倒して奪取するとクリア）", &desc.isStageBoss));
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
            captureItemUndo(ImGui::DragFloat("成立までの秒数", &desc.conditionSeconds, EditorUi::kDragStepSeconds, 0.0f, kMaxConditionSeconds));
        } else if (desc.conditionType == "enemy_group_defeated") {
            char groupBuffer[96] = { };
            strncpy_s(groupBuffer, desc.enemyGroup.c_str(), _TRUNCATE);
            if (captureItemUndo(ImGui::InputText("監視する敵グループ", groupBuffer, sizeof(groupBuffer)))) {
                desc.enemyGroup = groupBuffer;
            }
        }
    }
    if (desc.kind == "gimmick") {
        constexpr const char* kMotions[] = { "none", "move_x", "move_y", "rotate_y", "rotate_z", "fall", "blink", "custom" };
        constexpr int kMotionCount = static_cast<int>(sizeof(kMotions) / sizeof(kMotions[0]));
        int motionIndex = 0;
        for (int i = 1; i < kMotionCount; ++i) {
            if (desc.gimmickMotion == kMotions[i]) {
                motionIndex = i;
                break;
            }
        }
        if (ImGui::Combo("動作プリセット", &motionIndex, kMotions, kMotionCount)) {
            editor.RecordUndoSnapshotNow();
            desc.gimmickMotion = kMotions[motionIndex];
        }
        EditorUI::HelpMarker("customは移動方向・回転量・往復方式を自由に組み合わせる汎用動作です。プリセットに無い動きはここで作れます");
        captureItemUndo(ImGui::DragFloat("動作量", &desc.motionAmount, EditorUi::kDragStepPosition));
        captureItemUndo(ImGui::DragFloat("動作速度", &desc.motionSpeed, EditorUi::kDragStepPosition, 0.0f, EditorUi::kMaxSpeed));
        if (desc.gimmickMotion == "custom") {
            captureItemUndo(ImGui::DragFloat3("移動方向", &desc.motionAxis.x, EditorUi::kDragStepFine));
            EditorUI::HelpMarker("この方向へ動作量ぶん動きます。(0,0,0)なら移動しません");
            captureItemUndo(ImGui::DragFloat3("回転量(rad)", &desc.motionRotation.x, EditorUi::kDragStepFine));
            EditorUI::HelpMarker("進行度1.0に対する各軸の回転量です。(0,0,0)なら回転しません");
            constexpr const char* kModes[] = { "loop", "pingpong", "once" };
            constexpr int kModeCount = static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));
            int modeIndex = desc.motionMode == "pingpong" ? 1 : desc.motionMode == "once" ? 2 : 0;
            if (ImGui::Combo("往復方式", &modeIndex, kModes, kModeCount)) {
                editor.RecordUndoSnapshotNow();
                desc.motionMode = kModes[modeIndex];
            }
            EditorUI::HelpMarker("loop: 波のように往復 / pingpong: 等速で往復 / once: 有効化から一度だけ動いて止まる（扉の開閉など）");
            constexpr const char* kEases[] = { "linear", "smooth" };
            int easeIndex = desc.motionEase == "smooth" ? 1 : 0;
            if (ImGui::Combo("加減速", &easeIndex, kEases, 2)) {
                editor.RecordUndoSnapshotNow();
                desc.motionEase = kEases[easeIndex];
            }
        }
    }
    if (desc.kind == "pickup") {
        captureItemUndo(ImGui::DragFloat("回収半径", &desc.pickupRadius, EditorUi::kDragStepFine, EditorUi::kMinRadius, kMaxPickupRadius));
        captureItemUndo(ImGui::DragFloat("覚醒ゲージ増加量", &desc.pickupGaugeAmount, EditorUi::kDragStepRatio, 0.0f, 1.0f));
        captureItemUndo(ImGui::ColorEdit4("表示色", &desc.pickupColor.x));
        EditorUI::HelpMarker("回収するとGameFlagsに pickup_<名前> が立ちます。ノードグラフのフラグ起動やGetFlagで反応できます");
    }
    if (desc.kind == "breakable") {
        if (ImGui::InputInt("耐久(ヒット回数)", &desc.breakableHp)) {
            editor.RecordUndoSnapshotNow();
            structuralDirty = true; // 残りHPは実体生成時に初期化するため作り直す
        }
        captureItemUndo(ImGui::DragFloat("爆風半径", &desc.breakableRadius, EditorUi::kDragStepPosition, 0.0f, EditorUi::kMaxRadius));
        if (ImGui::InputInt("プレイヤーへのダメージ", &desc.breakablePlayerDamage)) {
            editor.RecordUndoSnapshotNow();
        }
        if (ImGui::InputInt("敵へのダメージ", &desc.breakableEnemyDamage)) {
            editor.RecordUndoSnapshotNow();
        }
        captureItemUndo(ImGui::ColorEdit4("表示色", &desc.breakableColor.x));
        EditorUI::HelpMarker("壊すとGameFlagsに broken_<名前> が立ちます");
        constexpr const char* kBreakWeapons[] = { "何でも", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
        constexpr int kBreakWeaponCount = static_cast<int>(sizeof(kBreakWeapons) / sizeof(kBreakWeapons[0]));
        int breakWeaponIndex = 0;
        for (int i = 1; i < kBreakWeaponCount; ++i) {
            if (desc.breakableWeapon == kBreakWeapons[i]) {
                breakWeaponIndex = i;
                break;
            }
        }
        if (ImGui::Combo("壊せる武器", &breakWeaponIndex, kBreakWeapons, kBreakWeaponCount)) {
            editor.RecordUndoSnapshotNow();
            desc.breakableWeapon = breakWeaponIndex == 0 ? "" : kBreakWeapons[breakWeaponIndex];
        }
        EditorUI::HelpMarker("武器を指定すると、その武器の近接攻撃でしか壊れません。当たり判定ONと爆風半径0にすると壊せる壁になります");
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
        captureItemUndo(ImGui::DragFloat("カメラ補間秒数", &desc.cameraBlendSeconds, EditorUi::kDragStepFine, 0.0f, kMaxCameraBlendSeconds));
        captureItemUndo(ImGui::DragFloat("カメラ維持秒数", &desc.cameraHoldSeconds, EditorUi::kDragStepSeconds, 0.0f, kMaxCameraHoldSeconds));
    }

    // テスト中に今どうなっているかを確認し、必要ならその場で初期状態へ戻す
    if (ImGui::CollapsingHeader("ゲーム中の状態")) {
        auto& entry = editor.objects_[editor.selIndex_];
        ImGui::Text("有効: %s", entry.runtimeActive ? "はい" : "いいえ");
        if (!desc.activationFlag.empty()) {
            ImGui::Text("有効化フラグ %s: %s", desc.activationFlag.c_str(),
                GameFlags::GetInstance()->GetFlag(desc.activationFlag) ? "ON" : "OFF");
        }
        if (entry.enabledOverride >= 0) {
            ImGui::TextColored(EditorUi::kWarningColor, "グラフによる上書き: %s", entry.enabledOverride == 1 ? "有効" : "無効");
        }
        if (desc.kind == "pickup") {
            ImGui::Text("回収: %s", entry.pickupCollected ? "済み" : "未");
        }
        if (desc.kind == "breakable") {
            ImGui::Text("耐久: %d / %d  %s", entry.breakableHp, desc.breakableHp, entry.breakableDestroyed ? "(破壊済み)" : "");
        }
        if (entry.enemy) {
            ImGui::Text("敵HP: %d / %d  %s", entry.enemy->GetHp(), entry.enemy->GetMaxHp(), entry.enemy->IsDefeated() ? "(撃破済み)" : "");
        }
        if (entry.knight) {
            ImGui::Text("ナイトHP: %d / %d  %s", entry.knight->GetHp(), entry.knight->GetMaxHp(), entry.knight->IsAlive() ? "" : "(撃破済み)");
        }
        if (desc.kind == "gimmick") {
            ImGui::Text("動作経過: %.2f 秒", entry.runtimeTimer);
        }
        if (ImGui::Button("この配置物を初期状態に戻す", ImVec2(-1.0f, 0.0f))) {
            editor.RegenerateInstances(entry);
        }
        EditorUI::HelpMarker("回収済み・破壊済み・敵のHP・グラフによる上書きをリセットして実体を作り直します");
    }
}

void StageEditorInspectorPanel::RenderObjectText(StageEditor& editor)
{
    auto& desc = editor.objects_[editor.selIndex_].desc;
    if (desc.kind != "ui_text") {
        return;
    }
    ImGui::PushID(desc.name.c_str());
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

    ImGui::SeparatorText("文章UI");
    ImGui::TextWrapped("文章を入力するとゲーム画面に反映されます。Enterで改行できます。");
    // 長い文章を編集したときも、固定長のバッファで末尾を切り捨てない。
    std::vector<char> textBuf(desc.text.size() + 4096, '\0');
    std::copy(desc.text.begin(), desc.text.end(), textBuf.begin());
    if (editor.focusTextEditor_) {
        ImGui::SetKeyboardFocusHere();
        editor.focusTextEditor_ = false;
    }
    if (captureItemUndo(ImGui::InputTextMultiline("##文章内容", textBuf.data(), textBuf.size(), ImVec2(-1.0f, kTextBoxHeight)))) {
        desc.text = textBuf.data();
    }
    ImGui::TextDisabled("画面の十字をドラッグして配置 / Ctrl+Sで保存");

    captureItemUndo(ImGui::ColorEdit4("色", &desc.textColor.x));
    bool bold = desc.textBold;
    if (ImGui::Checkbox("太字", &bold)) {
        editor.RecordUndoSnapshotNow();
        desc.textBold = bold;
    }
    ImGui::SameLine();
    bool shadow = desc.textShadow;
    if (ImGui::Checkbox("文字の影", &shadow)) {
        editor.RecordUndoSnapshotNow();
        desc.textShadow = shadow;
    }
    captureItemUndo(ImGui::DragFloat("大きさ", &desc.textScale, EditorUi::kDragStepFine, EditorUi::kMinRadius, kMaxTextScale));

    const char* spaces[] = { "screen", "world" };
    const char* spaceLabels[] = { "画面座標(px)", "ワールド座標" };
    int spaceIndex = (desc.textSpace == "world") ? 1 : 0;
    if (ImGui::Combo("座標基準", &spaceIndex, spaceLabels, 2)) {
        const std::string newSpace = spaces[spaceIndex];
        if (newSpace != desc.textSpace) {
            editor.RecordUndoSnapshotNow();
            // 座標基準の切り替え時にposition(px⇔ワールド単位)を素通りさせると、
            // 数値のスケールが噛み合わずカメラ範囲外へ飛んで見えなくなるため、その場でスクリーン上の見た目を保つよう変換する
            if (newSpace == "world") {
                Vector3 worldPos;
                const Vector3 image = editor.viewport_.ScreenToImage(desc.position.x, desc.position.y);
                desc.position = editor.MouseToGround(image.x, image.y, worldPos) ? worldPos : Vector3 { };
            } else {
                float camX = 0.0f, camY = 0.0f;
                if (editor.camera_) {
                    const Vector3& camPos = editor.camera_->GetTranslate();
                    camX = camPos.x;
                    camY = camPos.y;
                }
                const Vector3 world = editor.WorldPositionOf(desc);
                float screenX, screenY;
                SceneShared::WorldToScreen(world.x, world.y, camX, camY, screenX, screenY);
                desc.position = { screenX, screenY, 0.0f };
            }
            desc.textSpace = newSpace;
        }
    }
    EditorUI::HelpMarker("画面座標: 位置のx/yをスクリーンピクセル座標として使います（カメラに影響されず常に同じ位置に表示）\nワールド座標: 位置をワールド座標として扱い、カメラに応じて画面へ投影します");

    if (desc.textSpace == "screen") {
        RenderScreenAnchorOcclusionWarning(editor, desc);
    }
    ImGui::PopID();
}
} // namespace engine::game
#endif
