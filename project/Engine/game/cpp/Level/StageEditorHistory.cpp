/**
 * @file StageEditorHistory.cpp
 * @brief 編集履歴・検証・自動保存の管理
 */
#include "StageEditor.h"
#include "Camera.h"
#include "DiagnosticsDraw.h"
#include "DirectXCommon.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "GameFlags.h"
#include "Input.h"
#include "KnightEnemy.h"
#include "Matrix4x4.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "ParticleManager.h"
#include "StageEditorPanels.h"
#include "StageEditorPrefabService.h"
#include "StageEditorSelectionService.h"
#include "TimeManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <unordered_set>
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <commdlg.h>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")
#endif
using namespace engine::graphics;

// ══════════════════════════════════════════════════════
// ファイル操作とデータ管理
// ══════════════════════════════════════════════════════

namespace engine::game {
namespace {
}

#ifdef USE_IMGUI
/** @brief スナップショットの内容へ丸ごと戻す（敵のHPやトリガーの成立済み状態はリセットされる） */
void StageEditor::ApplySnapshot(const LevelSnapshot& snap) {
    // UndoとRedoで配置実体を破棄する前に、GPUからの参照完了を保証する
    if (!objects_.empty()) {
        WaitForGpuIdle();
    }
    // Open()のファイル読み込み抜き版。実体はdescから作り直す
    for (auto& entry : objects_) {
        UnregisterEnemyEntity(entry);
    }
    objects_.clear();
    for (const auto& desc : snap.objects) {
        ObjectEntry entry;
        entry.desc = desc;
        entry.authoredPosition = entry.desc.position;
        entry.runtimeActive = IsRuntimeActive(entry.desc) && entry.desc.activationDelay <= 0.0f;
        objects_.push_back(std::move(entry));
    }
    for (auto& entry : objects_) {
        RegenerateInstances(entry);
    }

    // トリガー・チェックポイント・開始位置・グラフ設定も丸ごと戻す
    triggers_.clear();
    for (const auto& desc : snap.triggers) {
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
    }
    checkpoints_ = snap.checkpoints;

    playerSpawn_ = snap.playerSpawn;
    enemySpawn_ = snap.enemySpawn;
    graphPath_ = snap.graphPath;
    flagGraphs_ = snap.flagGraphs;
    strncpy_s(graphPathBuffer_, graphPath_.c_str(), _TRUNCATE);

    // 戻した後は消えた配置物を指さないよう選択とドラッグを解除する
    selKind_ = SelKind::kNone;
    selIndex_ = -1;
    viewportDragging_ = false;
}

/** @brief 現在の状態を即座にUndoスタックへ積む（追加/削除など単発で完結する変更の直前に呼ぶ） */
void StageEditor::RecordUndoSnapshotNow() {
    history_.Record(MakeSnapshot());
    dirty_ = true;
}

/** @brief ドラッグ/テキスト編集の開始時に変更前を仮記録する。IsItemActivated() の直後に呼ぶ */
void StageEditor::BeginUndoCapture() {
    if (!history_.IsCapturing()) {
        history_.Begin(MakeSnapshot());
    }
}

/** @brief BeginUndoCapture()後、実際に値が変わったことを記録する（変更が無ければCommit時に捨てられる） */
void StageEditor::MarkUndoDirty() {
    history_.MarkChanged();
    dirty_ = true;
}

/** @brief ドラッグ/テキスト編集の終了時に呼ぶ。実際に変化していた場合だけ Undo スタックへ確定する */
void StageEditor::CommitUndoCapture() {
    history_.Commit();
}

/** @brief 直前の ImGui の項目の操作に合わせて Undo の記録を始める・変更を記録する・確定する */
bool StageEditor::CaptureItemUndo(bool changed) {
    // 操作を始めた時に変更前を記録し、値が変わったら印を付け、操作を終えた時に確定する
    if (ImGui::IsItemActivated()) {
        BeginUndoCapture();
    }
    if (changed) {
        MarkUndoDirty();
    }
    if (ImGui::IsItemDeactivated()) {
        CommitUndoCapture();
    }
    return changed;
}

/** @brief Undoスタックから1つ前の状態へ戻す（Ctrl+Z） */
void StageEditor::Undo() {
    std::optional<LevelSnapshot> snapshot = history_.Undo(MakeSnapshot());
    if (!snapshot) {
        return;
    }
    ApplySnapshot(*snapshot);
    dirty_ = true;
    statusMessage_ = "元に戻しました";
    statusTimer_ = StageEditor::kStatusBriefSeconds;
}

/** @brief Redoスタックから1つ先の状態へ進める（Ctrl+Y） */
void StageEditor::Redo() {
    std::optional<LevelSnapshot> snapshot = history_.Redo(MakeSnapshot());
    if (!snapshot) {
        return;
    }
    ApplySnapshot(*snapshot);
    dirty_ = true;
    statusMessage_ = "やり直しました";
    statusTimer_ = StageEditor::kStatusBriefSeconds;
}

/** @brief スナップ有効時、値をsnapStep_の倍数へ丸める（無効時はそのまま返す） */
float StageEditor::SnapValue(float v) const {
    // Ctrlを押している間はスナップOFFでも一時的に揃える
    const bool snapActive = snapEnabled_ || ImGui::GetIO().KeyCtrl;
    if (!snapActive || snapStep_ <= 0.0f) {
        return v;
    }
    return std::round(v / snapStep_) * snapStep_;
}

/** @brief 保存前検証を実行し、修正が必要な項目を返す */
std::vector<std::string> StageEditor::ValidateLevel() const {
    // 配置物: 名前の空・重複、モデル未設定、種類ごとの検査、拡縮、列配置、親子、イベント接続元を確かめる
    std::vector<std::string> issues;
    std::unordered_set<std::string> names;
    for (const auto& entry : objects_) {
        const ObjectDesc& desc = entry.desc;
        // 名前は親子やイベントの参照に使うので、空と重複を許さない（重複は登録に失敗したかで見る）
        if (desc.name.empty()) {
            issues.push_back("名前が空のオブジェクトがあります");
        } else if (!names.insert(desc.name).second) {
            issues.push_back("オブジェクト名が重複しています: " + desc.name);
        }
        // 見た目が要る種類なのにモデルが無いもの
        const ObjectKind& kind = ObjectKind::Of(desc.kind);
        if (kind.RequiresModel() && desc.model.empty()) {
            issues.push_back("モデル未設定: " + desc.name);
        }
        // 種類ごとの固有の検査は各種類に任せる
        kind.Validate(desc, issues);
        // 大きさが0以下だと見えず、当たり判定も壊れる
        if (desc.scale.x <= 0.0f || desc.scale.y <= 0.0f || desc.scale.z <= 0.0f) {
            issues.push_back("スケールが0以下です: " + desc.name);
        }
        // 列配置は個数が1以上で、間隔が0でないこと
        if (desc.type == PlacementType::kRow && (desc.count <= 0 || desc.step == 0.0f)) {
            issues.push_back("列配置の個数または間隔が無効です: " + desc.name);
        }
        // 親が存在するか、親子が輪になっていないか
        if (!desc.parent.empty()) {
            const bool parentExists = FindDescByName(desc.parent) != nullptr;
            if (!parentExists) {
                issues.push_back("親が見つかりません: " + desc.name + " -> " + desc.parent);
            } else if (desc.parent == desc.name || IsDescendantOf(desc.parent, desc.name)) {
                issues.push_back("親子関係が循環しています: " + desc.name);
            }
        }
        // 動作のきっかけになるフラグを立てるトリガーかイベント条件があるか
        if (!desc.activationFlag.empty()) {
            // まずトリガーのフラグから探す
            bool sourceExists = std::any_of(triggers_.begin(), triggers_.end(), [&](const TriggerVolume& trigger) {
                return trigger.GetDesc().flag == desc.activationFlag;
            });
            // 条件用の接頭辞が付いたフラグなら、同じ名前のイベント条件の配置物を探す
            if (!sourceExists && desc.activationFlag.starts_with(kConditionFlagPrefix)) {
                const std::string conditionName = desc.activationFlag.substr(kConditionFlagPrefix.size());
                sourceExists = std::any_of(objects_.begin(), objects_.end(), [&](const ObjectEntry& condition) {
                    return condition.desc.kind == objectKindName::kEventCondition && condition.desc.name == conditionName;
                });
            }
            if (!sourceExists) {
                issues.push_back("イベント接続元が見つかりません: " + desc.name);
            }
        }
    }

    // トリガー: 名前の空・重複と、フラグ名・半径の設定
    std::unordered_set<std::string> triggerNames;
    for (const auto& trigger : triggers_) {
        const TriggerDesc& desc = trigger.GetDesc();
        if (desc.name.empty() || !triggerNames.insert(desc.name).second) {
            issues.push_back("トリガー名が空または重複しています: " + desc.name);
        }
        if (desc.flag.empty() || desc.radius <= 0.0f) {
            issues.push_back("トリガー設定が不正です: " + desc.name);
        }
    }

    // グラフ: 紐付けの入力漏れと、ファイルが実在するか
    for (const auto& binding : flagGraphs_) {
        if (binding.flag.empty() || binding.graphPath.empty()) {
            issues.push_back("フラグ起動グラフのフラグ名またはパスが空です");
            continue;
        }
        std::error_code fileError;
        if (!std::filesystem::exists(binding.graphPath, fileError)) {
            issues.push_back("フラグ起動グラフのファイルが見つかりません: " + binding.graphPath);
        }
    }
    if (!graphPath_.empty()) {
        std::error_code fileError;
        if (!std::filesystem::exists(graphPath_, fileError)) {
            issues.push_back("常駐グラフのファイルが見つかりません: " + graphPath_);
        }
    }
    return issues;
}

/** @brief 未保存内容を一定間隔で復旧用ファイルへ退避する */
void StageEditor::UpdateAutoSave(float realDt) {
    if (!autoSaveEnabled_ || !dirty_ || levelPath_.empty()) {
        autoSaveElapsed_ = 0.0f;
        return;
    }
    autoSaveElapsed_ += realDt;
    if (autoSaveElapsed_ < kAutoSaveIntervalSeconds) {
        return;
    }
    recoveryPath_ = levelPath_ + ".autosave.json";
    SaveToPath(recoveryPath_);
    autoSaveElapsed_ = 0.0f;
    statusMessage_ = "自動保存しました: " + recoveryPath_;
    statusTimer_ = StageEditor::kStatusShortSeconds;
}
#endif

} // namespace engine::game
