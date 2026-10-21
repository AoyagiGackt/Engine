/**
 * @file StageEditor.cpp
 * @brief ステージ配置の実体管理と実行時編集ワークフローを実装するファイル
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
constexpr float kNudgeStep = 0.25f; // スナップOFF時に矢印キーで動かす量（ワールド単位）
}

/** @brief 配置物と編集状態をメンバの既定値で初期化する */
StageEditor::StageEditor() = default;

/** @brief レベルJSONを読み込み、配置物とトリガーを生成する */
void StageEditor::Open(const std::string& levelPath, ModelCommon* modelCommon, Camera* camera)
{
    // 再読み込み前に旧レベルのGPU参照を完了させ、実体からモデルの順で破棄する
    ReleaseLevelResources(false);

    levelPath_ = levelPath;
    modelCommon_ = modelCommon;
    camera_ = camera;
    viewport_.SetCamera(camera);

    LevelData data = LevelLoader::Load(levelPath);
    playerSpawn_ = data.playerSpawn;
    enemySpawn_ = data.enemySpawn;
    graphPath_ = data.graphPath;
    flagGraphs_ = data.flagGraphs;
    activeEditor_ = this;
    ResetLevelLocalFlags(data);

    for (auto& desc : data.objects)
    {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        entry.authoredPosition = entry.desc.position;
        entry.runtimeActive = IsRuntimeActive(entry.desc) && entry.desc.activationDelay <= 0.0f;
        objects_.push_back(std::move(entry));
    }

    // 新規作成時の連番(obj_N等)が、読み込んだレベルに既にある名前と衝突しないよう、
    // "_数字"で終わる名前を走査してnextSerial_をその最大値+1から再開させる
    // （これを怠ると保存済みのobj_0等と同じ名前が新規オブジェクトに振られ、
    //   保存前検証の名前重複エラーで保存できなくなる）
    auto bumpSerialPastName = [this](const std::string& name) {
        const size_t underscorePos = name.find_last_of('_');
        if (underscorePos == std::string::npos || underscorePos + 1 >= name.size())
        {
            return;
        }
        const std::string suffix = name.substr(underscorePos + 1);
        if (!std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) {
            return std::isdigit(c);
        }))
        {
            return;
        }
        const int value = std::atoi(suffix.c_str());
        if (value >= nextSerial_)
        {
            nextSerial_ = value + 1;
        }
    };
    for (const auto& entry : objects_)
    {
        bumpSerialPastName(entry.desc.name);
    }
    for (const auto& trigger : data.triggers)
    {
        bumpSerialPastName(trigger.name);
    }

    // EnemyRegistryへの登録キーになるため、実体を生成する前に名前を確定させる
    EnsureUniqueNames(); // 手書きJSON等で名前が無い/重複しているエントリに自動命名する（親子参照に必要）

    for (auto& entry : objects_)
    {
        RegenerateInstances(entry);
    }

    triggers_.clear();
    for (const auto& desc : data.triggers)
    {
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
    }
    checkpoints_ = data.checkpoints;

    // 配置物とトリガーが揃ってから常駐グラフを起動する（グラフが配置物を名前で操作できるように）
    levelGraphs_.Start(data);

    selKind_ = SelKind::None;
    selIndex_ = -1;
    selectedObjectIndices_.clear();
#ifdef USE_IMGUI
    strncpy_s(graphPathBuffer_, graphPath_.c_str(), _TRUNCATE);
    eventConnection_.Reset();
    // 別ファイルを開いたら、直前のレベルに対するUndo/Redo履歴は無関係になるため破棄する
    history_.Clear();
    dirty_ = false;
    lastSavedSnapshot_ = MakeSnapshot();
    recoveryPath_ = levelPath_ + ".autosave.json";
    std::error_code fileError;
    recoveryAvailable_ = std::filesystem::exists(recoveryPath_, fileError)
        && std::filesystem::exists(levelPath_, fileError)
        && std::filesystem::last_write_time(recoveryPath_, fileError)
            > std::filesystem::last_write_time(levelPath_, fileError);
#endif
    statusMessage_ = "読み込みました: " + levelPath_;
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

/** @brief 敵の登録を解除してから所有する資源を解放する */
StageEditor::~StageEditor()
{
    Finalize();
}

/** @brief GPUの使用完了を待ってからレベル実体と外部参照を破棄する */
void StageEditor::Finalize()
{
    if (visible_)
    {
        TimeManager::GetInstance()->SetTimeScale(savedTimeScale_);
    }
    visible_ = false;
    playTestMode_ = false;
    ReleaseLevelResources(true);
    viewport_.Reset();
    camera_ = nullptr;
    modelCommon_ = nullptr;
    if (activeEditor_ == this)
    {
        activeEditor_ = nullptr;
    }
}

/** @brief 描画に使った実体を破棄する前に、GPU がそれを使い終わるのを待つ（描画の準備ができていなければ何もしない） */
void StageEditor::WaitForGpuIdle() const
{
    if (modelCommon_ && modelCommon_->GetDxCommon())
    {
        modelCommon_->GetDxCommon()->WaitForGpu();
    }
}

/** @brief 読み込み済みレベルの実体を依存関係に沿った順序で破棄する */
void StageEditor::ReleaseLevelResources(bool releaseExternalEntities)
{
    // 描画に使用した実体を解放する前にGPUからの参照完了を保証する
    if (!objects_.empty())
    {
        WaitForGpuIdle();
    }

    // グラフは配置物を名前で参照するため、実体より先に止める
    levelGraphs_.Stop();

    // レジストリ参照、描画実体の順に破棄する
    // （モデルの実体はModelManagerが所有・共有するのでここでは破棄しない。GetOrLoadModel()参照）
    for (auto& entry : objects_)
    {
        DestroyObjectRuntime(entry, false);
    }
    objects_.clear();
    modelCache_.clear();
    triggers_.clear();
    checkpoints_.clear();
    if (releaseExternalEntities)
    {
        externalEntities_.clear();
    }
}

/** @brief 削除・Open()の再読み込み・破棄の前に、enemy_basic配置分をEnemyRegistryから解除する（ダングリングポインタ防止） */
void StageEditor::UnregisterEnemyEntity(const ObjectEntry& entry)
{
    if (entry.enemy)
    {
        EnemyRegistry::GetInstance()->Unregister(entry.desc.name);
    }
}

/** @brief 配置物が所有する描画実体と敵実体を安全な順序で破棄する */
void StageEditor::DestroyObjectRuntime(ObjectEntry& entry, bool waitForGpu)
{
    const bool ownsRuntime = !entry.instances.empty() || entry.knight || entry.enemy;
    if (waitForGpu && ownsRuntime)
    {
        WaitForGpuIdle();
    }

    // レジストリは実体を参照するため、所有ポインタを破棄する前に登録を解除する。
    UnregisterEnemyEntity(entry);
    entry.instances.clear();
    entry.knight.reset();
    entry.enemy.reset();
}

/** @brief このレベルが自分で立てるフラグ（トリガーのflag、condition_<名前>、pickup_<名前>、broken_<名前>）をfalseへ戻す */
void StageEditor::ResetLevelLocalFlags(const LevelData& data)
{
    GameFlags* flags = GameFlags::GetInstance();
    for (const TriggerDesc& trigger : data.triggers)
    {
        if (!trigger.flag.empty() && flags->HasFlag(trigger.flag))
        {
            flags->SetFlag(trigger.flag, false);
        }
    }
    for (const ObjectDesc& desc : data.objects)
    {
        if (desc.name.empty())
        {
            continue;
        }
        const char* prefix = ObjectKind::Of(desc.kind).RuntimeFlagPrefix();
        if (!prefix)
        {
            continue;
        }
        const std::string flag = std::string(prefix) + desc.name;
        if (flags->HasFlag(flag))
        {
            flags->SetFlag(flag, false);
        }
    }
}

/** @brief 編集カメラを指定ワールド位置が画面中央に来るよう移動する（奥行きは維持） */
void StageEditor::FocusCameraOn(const Vector3& worldPosition)
{
    if (!camera_)
    {
        return;
    }
    constexpr float kFocusOffsetY = 3.0f; // ゲームカメラと同じく少し上から見下ろす
    Vector3 cameraPosition = camera_->GetTranslate();
    cameraPosition.x = worldPosition.x;
    cameraPosition.y = worldPosition.y + kFocusOffsetY;
    camera_->SetTranslate(cameraPosition);
}

#ifdef USE_IMGUI
/** @brief 現在の選択物（配置物/トリガー/外部エンティティ）へカメラを寄せる。未選択ならプレイヤーへ */
void StageEditor::FocusCameraOnSelection()
{
    Vector3 selectionPosition;
    if (SelectionKindOf(selKind_).WorldPosition(*this, selIndex_, selectionPosition))
    {
        FocusCameraOn(selectionPosition);
        return;
    }
    for (const ExternalEntityRef& ref : externalEntities_)
    {
        if (ref.name == "Player" && ref.positionTarget)
        {
            FocusCameraOn(ref.positionTarget->GetPosition());
            return;
        }
    }
}
#endif

/** @brief プレイヤー実体を指定位置へ移し、テストモードを開始する（配置した場所から即プレイして確かめる用） */
bool StageEditor::StartPlayTestAt(const Vector3& worldPosition)
{
    for (const ExternalEntityRef& ref : externalEntities_)
    {
        if (ref.name != "Player" || !ref.positionTarget)
        {
            continue;
        }
        ref.positionTarget->SetPosition(worldPosition);
        FocusCameraOn(worldPosition);
        SetPlayTestMode(true);
        return true;
    }
    return false;
}

/** @brief 編集停止とゲーム動作テストを切り替えて時間倍率を同期する */
void StageEditor::SetPlayTestMode(bool enabled)
{
    playTestMode_ = enabled;
    TimeManager::GetInstance()->SetTimeScale(enabled ? savedTimeScale_ : 0.0f);
}

/** @brief 画面中央のz=0平面上のワールド座標を返す（テンプレート生成の基準位置） */
Vector3 StageEditor::ViewCenterOnGround() const
{
    Vector3 center = playerSpawn_;
    const Vector3 screen = viewport_.ViewCenter();
    MouseToGround(screen.x, screen.y, center);
    return center;
}

/** @brief 現在の内容をOpenで指定したパスへ書き戻す */
void StageEditor::Save()
{
#ifdef USE_IMGUI
    validationIssues_ = ValidateLevel();
    if (!validationIssues_.empty())
    {
        statusMessage_ = "保存前検証で問題が見つかりました";
        statusTimer_ = StageEditor::kStatusLongSeconds;
        return;
    }
#endif
    SaveToPath(levelPath_);
#ifdef USE_IMGUI
    dirty_ = false;
    autoSaveElapsed_ = 0.0f;
    lastSavedSnapshot_ = MakeSnapshot();
#endif
    statusMessage_ = "保存しました: " + levelPath_;
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

/** @brief 現在の編集内容を指定パスへ書き出す */
void StageEditor::SaveToPath(const std::string& path) const
{
    LevelData data;
    data.playerSpawn = playerSpawn_;
    data.enemySpawn = enemySpawn_;
    for (const auto& entry : objects_)
    {
        data.objects.push_back(entry.desc);
    }
    for (const auto& trigger : triggers_)
    {
        data.triggers.push_back(trigger.GetDesc());
    }
    data.checkpoints = checkpoints_;
    data.graphPath = graphPath_;
    data.flagGraphs = flagGraphs_;
    LevelLoader::Save(path, data);
}

/** @brief 毎フレーム呼ぶF2でパネルの表示/非表示を切り替える */
void StageEditor::Update(Input* input, const Vector3& playerPos)
{
    // トリガー判定はエディタの表示状態に関係なく常に行う（普段のプレイ中でも成立させるため）
    for (auto& trg : triggers_)
    {
        const bool justFired = trg.Update(playerPos);
        if (justFired && trg.GetDesc().spawnsWaterSplash && onWaterSplashRequested_)
        {
            onWaterSplashRequested_(trg.GetDesc().position);
        }
    }

#ifdef USE_IMGUI
    // ここから下は F2 でエディタを開いている時だけ（閉じている間はビューの切り抜きを解除して戻る）
    UpdateEditorVisibility(input);
    if (!visible_)
    {
        viewport_.ClearImageRect();
        DiagnosticsDraw::SetImageViewport();
        return;
    }

    // F4で編集状態を維持したままパネル表示だけを切り替える
    // （F3はゲーム側の当たり判定オーバーレイ表示と統一するためF4にしている）
    if (ImGui::IsKeyPressed(ImGuiKey_F4, false))
    {
        viewportFocusMode_ = !viewportFocusMode_;
    }
    RenderGameViewport();

    // ゲーム側のヒットストップやスローに左右されないよう、実時間で自動保存とメッセージ表示を進める
    const float realDt = ImGui::GetIO().DeltaTime;
    UpdateAutoSave(realDt);
    if (statusTimer_ > 0.0f)
    {
        statusTimer_ -= realDt;
    }

    // エディタカメラを動かし、デバッグ描画にも同じ行列を渡す
    UpdateFreeCamera(input, realDt);

    if (camera_)
    {
        DiagnosticsDraw::SetCamera(camera_->GetViewProjectionMatrix(),
            static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight));
    }

    // ビュー上のクリック・ドラッグ → ショートカット → パネル描画の順に処理する
    UpdateViewportInteraction();

    HandleEditorShortcuts();
    RenderEditorPanels();
    // グリッドとギズモはゲームビューの枠の中だけに描く（テストプレイ中は出さない）
    ImDrawList* overlay = DiagnosticsDraw::GetDrawList();
    const Vector3 topLeft = viewport_.ScreenToImage(0.0f, 0.0f);
    const Vector3 bottomRight = viewport_.ScreenToImage(WinApp::kClientWidth, WinApp::kClientHeight);
    overlay->PushClipRect({ topLeft.x, topLeft.y }, { bottomRight.x, bottomRight.y }, true);
    if (!playTestMode_)
    {
        DrawGridOverlay();
        DrawGizmos();
    }
    overlay->PopClipRect();
#else
    (void)input;
#endif
}

#ifdef USE_IMGUI
/** @brief F2による編集セッションの開始と終了を処理する */
void StageEditor::UpdateEditorVisibility(Input* input)
{
    const bool wasVisible = visible_;
    if (input && input->TriggerKey(DIK_F2))
    {
        visible_ = !visible_;
    }
    if (visible_ == wasVisible)
    {
        return;
    }
    if (visible_)
    {
        savedTimeScale_ = TimeManager::GetInstance()->GetTimeScale();
        TimeManager::GetInstance()->SetTimeScale(0.0f);
        return;
    }
    playTestMode_ = false;
    TimeManager::GetInstance()->SetTimeScale(savedTimeScale_);
}

/** @brief 編集操作のキーボードショートカットを処理する */
void StageEditor::HandleEditorShortcuts()
{
    // テストプレイ中と、文字入力欄に入力している間はショートカットを受け付けない
    if (playTestMode_)
    {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return;
    }
    // 編集操作のキー割り当て（上から順に見て、最初に押されたもの1つだけを実行する）
    struct EditShortcut {
        ImGuiKey key;
        bool needsCtrl;
        void (StageEditor::*action)();
    };
    static constexpr EditShortcut kEditShortcuts[] = {
        { ImGuiKey_Z, true, &StageEditor::Undo },
        { ImGuiKey_Y, true, &StageEditor::Redo },
        { ImGuiKey_S, true, &StageEditor::Save },
        { ImGuiKey_D, true, &StageEditor::DuplicateSelected },
        { ImGuiKey_C, true, &StageEditor::CopySelected },
        { ImGuiKey_V, true, &StageEditor::PasteClipboard },
        { ImGuiKey_Delete, false, &StageEditor::DeleteSelected },
        { ImGuiKey_F, false, &StageEditor::FocusCameraOnSelection },
        { ImGuiKey_Escape, false, &StageEditor::ClearSelection },
        { ImGuiKey_A, true, &StageEditor::SelectAllObjects },
    };
    for (const EditShortcut& shortcut : kEditShortcuts)
    {
        // Ctrl が要るものは Ctrl を押している時だけ反応する
        if ((!shortcut.needsCtrl || io.KeyCtrl) && ImGui::IsKeyPressed(shortcut.key))
        {
            (this->*shortcut.action)();
            break;
        }
    }

    // 変形ツールの切替（右ボタンでカメラを飛ばしている間はWASDなので切り替えない）
    struct ToolKey {
        ImGuiKey key;
        TransformTool tool;
    };
    // W で移動、E で回転、R で拡大縮小
    static constexpr ToolKey kToolKeys[] = {
        { ImGuiKey_W, TransformTool::Move },
        { ImGuiKey_E, TransformTool::Rotate },
        { ImGuiKey_R, TransformTool::Scale },
    };
    // Ctrl 中は Ctrl+R 等の別操作と重なるので切り替えない
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !io.KeyCtrl)
    {
        for (const ToolKey& toolKey : kToolKeys)
        {
            if (ImGui::IsKeyPressed(toolKey.key))
            {
                transformTool_ = toolKey.tool;
                break;
            }
        }
    }

    // 矢印キーで選択物を微調整（Shift+上下は奥行き）。押しっぱなしはImGuiのリピートに任せる
    struct NudgeKey {
        ImGuiKey key;
        float dir[3]; ///< 1回で動かす向き（スナップ間隔を掛けて使う）
        float shiftDir[3]; ///< Shiftを押している時の向き
    };
    static constexpr NudgeKey kNudgeKeys[] = {
        { ImGuiKey_LeftArrow, { -1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } },
        { ImGuiKey_RightArrow, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { ImGuiKey_UpArrow, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { ImGuiKey_DownArrow, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
    };
    const float step = snapEnabled_ && snapStep_ > 0.0f ? snapStep_ : kNudgeStep;
    for (const NudgeKey& nudge : kNudgeKeys)
    {
        if (ImGui::IsKeyPressed(nudge.key))
        {
            const float* dir = io.KeyShift ? nudge.shiftDir : nudge.dir;
            NudgeSelection(dir[0] * step, dir[1] * step, dir[2] * step);
            break;
        }
    }
}

/** @brief 選択中の配置物・トリガーを矢印キーで少しずつ動かす（スナップONならその間隔、OFFなら固定の微動量） */
void StageEditor::NudgeSelection(float dx, float dy, float dz)
{
    SelectionKindOf(selKind_).Nudge(*this, selIndex_, { dx, dy, dz });
}

/** @brief スナップONの間、z=0平面にスナップ間隔のグリッド線を描く */
void StageEditor::DrawGridOverlay()
{
    if (!snapEnabled_ || !showGrid_ || snapStep_ <= 0.0f || !camera_)
    {
        return;
    }
    constexpr float kGridHalfWidth = 14.0f; // カメラ中心から左右に描く範囲
    constexpr float kGridMinY = -2.0f;
    constexpr float kGridMaxY = 14.0f;
    constexpr int kMaxLinesPerAxis = 80; // スナップ間隔が小さすぎる時に線で埋まらないよう上限を設ける
    constexpr ImU32 kGridColor = IM_COL32(255, 255, 255, 40);
    constexpr ImU32 kGridAxisColor = IM_COL32(255, 255, 255, 110);

    const Vector3& cameraPosition = camera_->GetTranslate();
    const float step = snapStep_;
    if ((kGridHalfWidth * 2.0f) / step > static_cast<float>(kMaxLinesPerAxis))
    {
        return;
    }
    const float left = std::floor((cameraPosition.x - kGridHalfWidth) / step) * step;
    const float right = cameraPosition.x + kGridHalfWidth;
    for (float x = left; x <= right; x += step)
    {
        DiagnosticsDraw::DrawLine({ x, kGridMinY, 0.0f }, { x, kGridMaxY, 0.0f }, std::abs(x) < step * 0.5f ? kGridAxisColor : kGridColor);
    }
    const float bottom = std::floor(kGridMinY / step) * step;
    for (float y = bottom; y <= kGridMaxY; y += step)
    {
        DiagnosticsDraw::DrawLine({ left, y, 0.0f }, { right, y, 0.0f }, std::abs(y) < step * 0.5f ? kGridAxisColor : kGridColor);
    }
}

/** @brief 現在のレイアウト状態に応じて編集パネルを表示する */
void StageEditor::RenderEditorPanels()
{
    if (viewportFocusMode_)
    {
        RenderViewportFocusBar();
        return;
    }
    RenderEditorToolbar();
    RenderHierarchy();
    RenderInspector();
    RenderAssetPalette();
    RenderViewportContextMenu();
    if (showFlagsPanel_)
    {
        RenderFlagsPanel();
    }
    if (showWorkflowPanel_)
    {
        RenderWorkflowPanel();
    }
    if (showNoCodeEventPanel_)
    {
        RenderNoCodeEventPanel();
    }
    if (showGraphPanel_)
    {
        RenderGraphPanel();
    }
    if (showWavePanel_)
    {
        RenderWavePanel();
    }
    RenderStageAnalysisPanel();
    RenderDiffPanel();
    RenderEditorHelpPanel();
}

// ══════════════════════════════════════════════════════
// 編集履歴と選択操作
// ══════════════════════════════════════════════════════

/** @brief 現在の配置物・トリガー・チェックポイント・スポーン位置から Undo 用のスナップショットを作る */
StageEditor::LevelSnapshot StageEditor::MakeSnapshot() const
{
    LevelSnapshot snap;
    snap.objects.reserve(objects_.size());
    for (const auto& entry : objects_)
    {
        snap.objects.push_back(entry.desc);
    }
    snap.triggers.reserve(triggers_.size());
    for (const auto& trg : triggers_)
    {
        snap.triggers.push_back(trg.GetDesc());
    }
    snap.checkpoints = checkpoints_;
    snap.playerSpawn = playerSpawn_;
    snap.enemySpawn = enemySpawn_;
    snap.graphPath = graphPath_;
    snap.flagGraphs = flagGraphs_;
    return snap;
}

#endif

} // namespace engine::game
