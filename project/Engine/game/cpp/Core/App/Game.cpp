/**
 * @file Game.cpp
 * @brief Frameworkを継承したゲーム固有の初期化・更新・描画・シーン遷移制御（MyGame）の実装
 */
#include "Game.h"
#include "AssetPack.h"
#include "Audio.h"
#include "CombatTuning.h"
#include "EnemyTuning.h"
#include "EnemyTypeLibrary.h"
#include "GameRules.h"
#include "GameSounds.h"
#include "HitEffectLibrary.h"
#include "JsonConfigEditor.h"
#include "PlayerDefeat.h"
#include "PlayerTuning.h"
#include "ResultBackdrop.h"
#include "StageLiberation.h"
#include "StyleMeter.h"
#include "Logger.h"
#include "FrameProfiler.h"
#include "GameConstants.h"
#include "GamePlayScene.h"
#include "GameSettings.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ImageFilter.h"
#include "InputBuffer.h"
#include "RenderPassGraph.h"
#include "PostEffectRenderTarget.h"
#include "SaveData.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "ScreenFlash.h"
#include "TextureManager.h"
#include "TimeManager.h"
#include "TitleScene.h"
#include "VignetteEffect.h"
#include <SrvManager.h>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine::graphics;

namespace engine {
namespace {
constexpr double kFileTimeTicksPerMillisecond = 10000.0; // FILETIMEは100ナノ秒単位

/** @brief exeのプロセスが作られてからの経過ミリ秒（main()より前の起動処理も含めて測るため） */
double MillisecondsSinceProcessStart() {
    FILETIME creation {}, exitTime {}, kernel {}, user {};
    GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user);
    FILETIME now {};
    GetSystemTimePreciseAsFileTime(&now);
    const ULARGE_INTEGER start { { creation.dwLowDateTime, creation.dwHighDateTime } };
    const ULARGE_INTEGER current { { now.dwLowDateTime, now.dwHighDateTime } };
    return static_cast<double>(current.QuadPart - start.QuadPart) / kFileTimeTicksPerMillisecond;
}

/** @brief ロード時間の比較用に、目印付きでログへ出す（素材の読み方のモードも添える） */
void LogLoadProfile(const std::string& label) {
    const char* mode = AssetPack::GetInstance()->GetMode() == AssetPack::Mode::kUse ? "use"
        : AssetPack::GetInstance()->IsCooking() ? "cook" : "off";
    Logger::Log("[LoadProfile] " + label + ": " + std::to_string(static_cast<int>(MillisecondsSinceProcessStart()))
        + " ms（exe起動から, pak=" + mode + "）");
}

#ifdef USE_IMGUI
/** @brief 調整値エディタの対象フォルダと、保存したらすぐ読み直す設定ファイルを登録する */
void SetupConfigEditor() {
    auto* editor = JsonConfigEditor::GetInstance();
    editor->Initialize("Resources/Config", "Resources/Config/UI/tuning_labels.json", "Resources");
    // ここに無いファイルは保存しても次回起動時の反映になる（起動時に部品の数を決める演出などは、途中で読み直すと数が合わなくなるため登録しない）
    editor->SetReloadCallback("player_params.json", [] { game::PlayerTuning::Reload(); });
    editor->SetReloadCallback("combat_tuning.json", [] { game::CombatTuning::GetInstance()->Reload(); });
    editor->SetReloadCallback("enemy_params.json", [] { game::EnemyTuning::GetInstance()->Reload(); });
    editor->SetReloadCallback("enemy_types.json", [] { game::EnemyTypeLibrary::GetInstance()->Reload(); });
    editor->SetReloadCallback("game_rules.json", [] { game::GameRules::GetInstance()->Reload(); });
    editor->SetReloadCallback("hit_effects.json", [] { game::HitEffectLibrary::GetInstance()->Reload(); });
    editor->SetReloadCallback("style_meter.json", [] { game::StyleMeter::ReloadTuning(); });
    editor->SetReloadCallback("player_defeat.json", [] { game::PlayerDefeat::ReloadTuning(); });
    editor->SetReloadCallback("stage_liberation.json", [] { game::StageLiberation::ReloadTuning(); });
    editor->SetReloadCallback("result_backdrop.json", [] { game::ResultBackdrop::ReloadTuning(); });
    editor->SetReloadCallback("sounds.json", [] { game::gameSounds::Reload(); });
    editor->SetReloadCallback("bgm.json", [] { Audio::ReloadBGMConfig(); });
}
#endif
} // namespace
using namespace engine::game;

/** @brief ゲーム固有の初期化処理 */
void MyGame::Initialize() {
    // 基盤の初期化
    Framework::Initialize();

    // 工場を作る
    sceneFactory_ = std::make_unique<SceneFactory>();

    // SceneManagerに工場を教える
    SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());

    // 最初のBGMが鳴る前に、保存済みの音量設定を反映する。
    GameSettingsManager::GetInstance()->Load();
    const GameSettings& s = GameSettingsManager::GetInstance()->Get();
    audio_->SetBGMVolume(s.bgmVolume);
    audio_->SetSEVolume(s.seVolume);

    // 最初のシーンを工場経由でセットする
    LogLoadProfile("エンジン初期化完了");
    SceneManager::GetInstance()->Initialize(
        dxCommon_.get(),
        input_.get(),
        audio_.get(),
        imguiManager_.get());
    LogLoadProfile("タイトル準備完了");

    // コンティニューデータ・通算記録・スコアランキングを読み込む
    SaveDataManager::GetInstance()->Load();
    ScoreManager::GetInstance()->LoadScores();

    // スクリーンフラッシュ初期化
    ScreenFlash::GetInstance()->Initialize(dxCommon_.get());

    // --smoke-test で起動したら自動テストプレイを始める（画面の更新を待たずに速く回すため VSync は切る）
    if (smokeTest_.StartIfRequested(input_.get())) {
        dxCommon_->SetVSyncEnabled(false);
    }

#ifdef USE_IMGUI
    SetupConfigEditor();
#endif
}

/** @brief ゲーム固有の更新処理 */
void MyGame::Update() {
    // 基盤の更新
    Framework::Update();

    // 入力バッファ更新（Input::Update() の直後）
    InputBuffer::GetInstance()->Update(input_.get());

    // 時間管理・オーディオフェードを毎フレーム更新
    TimeManager::GetInstance()->Update();
    audio_->Update(gameConstants::kFrameDeltaTime);

    // 遅延コールバック・スクリーンフラッシュを毎フレーム更新
    ScreenFlash::GetInstance()->Update(gameConstants::kFrameDeltaTime);

#ifdef _DEBUG
    // テクスチャのホットリロード（保存したら即座に反映、開発時のみ）
    TextureManager::GetInstance()->CheckHotReload();
#endif

    // シーンマネージャー更新
    SceneManager::GetInstance()->Update();

    // 自動テストプレイの手順を進める（押したキーは次のフレームの入力から効く）。終えたらゲームを閉じる
    if (smokeTest_.Update()) {
        endRequest_ = true;
    }

#ifdef USE_IMGUI
    // F8でエンジン共通の実行時診断を開閉する
    if (input_->TriggerKey(DIK_F8)) {
        showEngineDiagnostics_ = !showEngineDiagnostics_;
    }
    if (showEngineDiagnostics_) {
        const bool panelOpen = ImGui::Begin("Engine Diagnostics", &showEngineDiagnostics_);
        if (panelOpen) {
            auto* profiler = FrameProfiler::GetInstance();
            ImGui::Text("CPU %.2f ms  %.1f FPS", profiler->GetMs(), profiler->GetFPS());
            ImGui::Text("Gamepad %s", input_->IsGamepadConnected() ? "Connected" : "Disconnected");
            bool hotReloadEnabled = TextureManager::GetInstance()->IsHotReloadEnabled();
            if (ImGui::Checkbox("ゲーム中のテクスチャホットリロード", &hotReloadEnabled)) {
                TextureManager::GetInstance()->SetHotReloadEnabled(hotReloadEnabled);
            }

            bool vsync = dxCommon_->IsVSyncEnabled();
            if (ImGui::Checkbox("VSync", &vsync)) {
                dxCommon_->SetVSyncEnabled(vsync);
            }

            GameSettings settings = GameSettingsManager::GetInstance()->Get();
            bool settingsChanged = false;
            settingsChanged |= ImGui::SliderFloat("BGM", &settings.bgmVolume, 0.0f, 1.0f);
            settingsChanged |= ImGui::SliderFloat("SE", &settings.seVolume, 0.0f, 1.0f);
            if (settingsChanged) {
                audio_->SetBGMVolume(settings.bgmVolume);
                audio_->SetSEVolume(settings.seVolume);
                GameSettingsManager::GetInstance()->Apply(settings);
                GameSettingsManager::GetInstance()->Save();
            }

            // JSONを手で書かずに直すための編集画面
            ImGui::Separator();
            if (ImGui::Button("調整値エディタ")) {
                JsonConfigEditor::GetInstance()->Toggle();
            }
            ImGui::SameLine();
            if (ImGui::Button("音の割り当て")) {
                showSoundAssignEditor_ = !showSoundAssignEditor_;
            }

            ImGui::Separator();
            if (ImGui::Button("Title")) {
                SceneManager::GetInstance()->ChangeScene("TITLE");
            }
            ImGui::SameLine();
            if (ImGui::Button("Stage Select")) {
                SceneManager::GetInstance()->ChangeScene("MAP");
            }
            if (ImGui::Button("Training")) {
                SceneManager::GetInstance()->ChangeScene("TRAINING");
            }
            ImGui::SameLine();
            if (ImGui::Button("Battle Test")) {
                SceneManager::GetInstance()->ChangeScene("BATTLETEST");
            }
        }
        ImGui::End();
    }
    JsonConfigEditor::GetInstance()->Draw();
    soundAssignEditor_.Draw(&showSoundAssignEditor_);
#endif

    ImGuiControlPanel::ShowControls();

    // ImGui終了処理
    imguiManager_->End();
}

/** @brief ゲーム固有の描画処理 */
void MyGame::Draw() {
    // バックバッファとSRVヒープを描画できる状態にする
    dxCommon_->PreDraw();
    SrvManager::GetInstance()->PreDraw();

    // ポストエフェクトの候補（FindActivePostEffect に渡す優先順）
    auto* gs = GrayscaleEffect::GetInstance();
    auto* imgFilter = ImageFilter::GetInstance();
    auto* hsv = HsvFilter::GetInstance();

    // オフスクリーンRTVへの描画に対応していないシーンでは
    // BeginScene/EndScene/Apply を呼ばない。呼んでしまうと、シーン側が何も描き込まない
    // オフスクリーンテクスチャでバックバッファが上書きされ、画面から絵が消えてしまう
    const bool postEffectsSupported = SceneManager::GetInstance()->CurrentScenePostEffectsSupported();

    RenderPassGraph graph(dxCommon_.get());

    // 同じ優先順で選んだエフェクトに開始と合成を委譲する
    IPostEffectSource* activeEffect = postEffectsSupported ? FindActivePostEffect({ imgFilter, gs, hsv }) : nullptr;
    graph.AddPass("Scene Capture Begin", [activeEffect] {
        activeEffect->BeginScene();
    }, activeEffect != nullptr);

    graph.AddPass("Scene", [] {
        SceneManager::GetInstance()->Draw();
    });

    graph.AddPass("Post Effects", [activeEffect] {
        activeEffect->EndScene();
        activeEffect->Apply(SrvManager::GetInstance());
    }, activeEffect != nullptr);

    // ポストエフェクトの後に、画面全体へかける演出・遷移・エディタ用の取り込み・デバッグUIの順で重ねる
    graph.AddPass("Vignette", [] {
        VignetteEffect::GetInstance()->Apply();
    });
    graph.AddPass("Screen Flash", [] {
        ScreenFlash::GetInstance()->Draw();
    });
    graph.AddPass("Scene Transition", [] {
        SceneManager::GetInstance()->DrawTransition();
    });
    graph.AddPass("Editor Preview", [] {
        SceneManager::GetInstance()->CaptureEditorPreview();
    });
    graph.AddPass("Diagnostics UI", [this] {
        imguiManager_->Draw(dxCommon_.get());
    });
    graph.Execute();

    dxCommon_->PostDraw();
}

/** @brief ゲーム固有の終了処理 */
void MyGame::Finalize() {
    SceneManager::GetInstance()->Finalize();

    Framework::Finalize();
}

} // namespace engine
