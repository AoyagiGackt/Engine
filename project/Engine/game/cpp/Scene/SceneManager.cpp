/**
 * @file SceneManager.cpp
 * @brief シーンの切替・更新・描画を統括するマネージャー（SceneManager）の実装
 */
#include "SceneManager.h"
#include "AssetPack.h"
#include "GameRules.h"
#include "WeaponManager.h"
#include "CrashHandler.h"
#include "Logger.h"
#include "StageEditor.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "SrvManager.h"
#include "UILayout.h"
#include "EngineAssert.h"
#include "JsonHelper.h"
#include "RunData.h"
#include <fstream>
#include <array>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#ifdef USE_IMGUI
#include "EditorUI.h"
#include "DiagnosticsDraw.h"
#include "GraphEditor.h"
#endif
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kDebugStartupPath = "Resources/Config/debug_startup.json";
// cook時に素材を記録するため一度ずつ開く画面（ランを始めずに開けるもの。MAP/GAMEPLAYはフロアごとに別途開く）
constexpr const char* kCookScenesWithoutRun[] = { "TITLE", "OPTIONS", "TRAINING", "BATTLETEST", "GAMEOVER", "CLEAR" };
// Debug でエディターから選べる開始シーン（startsRun はラン中でないと開けない画面）
/** @brief Debug の開始シーンの1行（シーン名・エディタの表示名・ランを始める画面か） */
struct DebugStartScene {
    const char* name;
    const char* label;
    bool startsRun;
};
constexpr DebugStartScene kDebugStartScenes[] = {
    { "TITLE", "タイトル", false },
    { "MAP", "マップ選択", true },
    { "GAMEPLAY", "本編の最初のステージ", true },
    { "TRAINING", "トレーニング", false },
    { "BATTLETEST", "戦闘テスト", false },
    { "OPTIONS", "オプション", false },
};
constexpr int kDebugStartSceneCount = static_cast<int>(std::size(kDebugStartScenes));
/** @brief 名前から Debug の開始シーンの定義を探す（無ければ nullptr） */
const DebugStartScene* FindDebugStartScene(const std::string& scene) {
    const auto found = std::find_if(std::begin(kDebugStartScenes), std::end(kDebugStartScenes),
        [&scene](const DebugStartScene& entry) {
            return scene == entry.name;
        });
    return found != std::end(kDebugStartScenes) ? &*found : nullptr;
}
/** @brief Debug の開始シーンとして使える名前かを返す */
bool IsDebugStartScene(const std::string& scene) {
    return FindDebugStartScene(scene) != nullptr;
}
}

/** @brief SceneManagerの唯一のインスタンスを取得する */
SceneManager* SceneManager::GetInstance() {
    static SceneManager sInstance;
    return &sInstance;
}

/** @brief シーンマネージャーの初期化 */
void SceneManager::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio, ImGuiManager* imgui) {
    // 共通の機能を受け取り、クラッシュ時の記録用に今の場面名を伝えておく
    dxCommon_ = dxCommon;
    input_ = input;
    audio_ = audio;
    imguiManager_ = imgui;
    dxCommon_->SetDiagnosticContext("TitleScene");
    CrashHandler::SetContext("TitleScene");

    // cook中は、遊ばなかった画面の素材もpakへ入るよう、起動シーンを作る前に全シーンを一度ずつ読み込む
    // （起動シーンより後に行うと、破棄したシーンのカメラ等が共通設定に残ってしまうため先に済ませる）
    if (AssetPack::GetInstance()->IsCooking()) {
        CookAllScenes();
    }

    // 通常はタイトルから開始し、Debugだけエディターで保存した開始シーンを使う。
    std::string startupScene = "TITLE";
#ifdef _DEBUG
    const auto settings = JsonHelper::Load(kDebugStartupPath);
    if (settings.is_object() && settings.contains("scene") && settings["scene"].is_string()) {
        const auto candidate = settings["scene"].get<std::string>();
        if (IsDebugStartScene(candidate)) {
            startupScene = candidate;
        }
    }
    debugStartScene_ = startupScene;
    if (const DebugStartScene* start = FindDebugStartScene(startupScene); start != nullptr && start->startsRun) {
        RunData::GetInstance()->StartNewRun();
    }
#endif
    // 起動シーンを作る（作れない名前ならタイトルで代用する）
    currentScene_ = sceneFactory_ ? sceneFactory_->CreateScene(startupScene) : nullptr;
    if (!currentScene_) {
        startupScene = "TITLE";
        currentScene_ = std::make_unique<TitleScene>();
    }
    nextSceneName_ = startupScene;
    currentSceneName_ = startupScene;
    dxCommon_->SetDiagnosticContext(startupScene);
    CrashHandler::SetContext(startupScene);
    transition_.Initialize(dxCommon_);
    TextureManager::GetInstance()->FlushUploads();

    // 起動シーンを読み込み、かかった時間を記録する（テクスチャの転送も済ませてから測る）
    const auto loadingStarted = std::chrono::steady_clock::now();
    currentScene_->Init(dxCommon_, input_, audio_);
    TextureManager::GetInstance()->FlushUploads();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - loadingStarted).count();
    Logger::Log("Startup scene load " + startupScene + ": " + std::to_string(elapsed) + " ms");
    currentScene_->SetImGuiManager(imguiManager_);
}

/** @brief シーンの更新処理 */
void SceneManager::Update() {
#ifdef USE_IMGUI
    diagnosticsDraw::SetImageViewport();
#endif
    if (transition_.Update()) {
        PerformSceneSwitch();
    }

    // フェード中であっても、今のシーンの更新は続ける
    if (currentScene_ && !transition_.IsLoading()) {
        PrepareEditorPreview();
        // F2トグル・パネル・トリガー判定は先に処理してから、Tick()内のIsVisible()分岐に反映させる
        currentScene_->GetStageEditor().Update(input_, currentScene_->GetEditorPlayerPos());
        // Tick()がUpdate()呼び出し・エディタ表示中の一時停止・UpdateObjects()を一括して面倒を見る
        // （各シーン側はDrawObjects()の呼び出し位置だけ自分のDraw()内で気にすればよい）
        currentScene_->Tick();
    }

#ifdef USE_IMGUI
    // ImGuiのウィジェット構築はUpdateフェーズ（imguiManager_->End()より前）で行う必要があるため、
    // Draw()ではなくここで呼ぶシーンに関係なく常に開けるようにする
    GraphEditor::GetInstance()->Update(input_);

    // 画面UIの位置・サイズ・色の調整パネル（各シーンがUILayoutで参照した項目が自動で並ぶ）
    if (currentScene_ && !transition_.IsLoading() && currentScene_->GetStageEditor().IsVisible()) {
        UILayout::DrawEditorPanel();
    }

    // エディタ起動キーの一覧を常に画面左下へ出す（開き方が画面のどこにも出ていないと気づけないため）
    if (!currentScene_ || currentScene_->ShouldShowHotkeyOverlay()) {
        editorUI::ShowHotkeyOverlay(
            GraphEditor::GetInstance()->IsVisible(),
            currentScene_ && currentScene_->GetStageEditor().IsVisible(),
            currentScene_ ? currentScene_->GetHotkeyOverlayExtra() : nullptr);
    }
#endif
}

/** @brief cookモードで、全シーンを一度ずつ初期化・破棄して使う素材をpakへ記録させる（起動シーンを作る前に呼ぶ） */
void SceneManager::CookAllScenes() {
    if (!sceneFactory_) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    auto* runData = RunData::GetInstance();
    auto* weapons = WeaponManager::GetInstance();

    // 作って初期化し、読み込みが終わったらすぐ破棄する（素材の記録はAssetPack側で自動的に行われる）
    auto loadOnce = [&](const char* sceneName) {
        std::unique_ptr<BaseScene> scene = sceneFactory_->CreateScene(sceneName);
        if (!scene) {
            return;
        }
        scene->Init(dxCommon_, input_, audio_);
        TextureManager::GetInstance()->FlushUploads();
        dxCommon_->WaitForGpu();
        scene->Shutdown();
    };

    // ランを始めていない状態で開く画面（ゲームオーバーはランを始めていなければ記録を書き換えない）
    for (const char* sceneName : kCookScenesWithoutRun) {
        loadOnce(sceneName);
    }
    // ランが必要な画面。本編はフロアごとにレベルが違うため、全フロアぶん開く
    const int floorCount = (std::max)(1, static_cast<int>(GameRules::GetInstance()->Get().levelPaths.size()));
    for (int floor = 0; floor < floorCount; ++floor) {
        runData->StartNewRun();
        for (int i = 0; i < floor; ++i) {
            runData->AdvanceFloor();
        }
        weapons->Reset();
        loadOnce("MAP");
        loadOnce("GAMEPLAY");
    }

    // タイトルを開く前の状態（ラン無し・武器無し・無音）へ戻す
    runData->StartNewRun();
    runData->EndRun();
    weapons->Reset();
    audio_->StopWave();
    UILayout::ResetUsedLayouts();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    Logger::Log("[AssetPack] 全シーンの素材を読み込んで記録しました: " + std::to_string(elapsed) + " ms");
}

/** @brief フェードアウト完了後のシーン切替本体（Update()の冒頭から呼ばれる） */
void SceneManager::PerformSceneSwitch() {
    // 生成できることを確かめてから、現在のシーンを終了する。
    auto nextScene = sceneFactory_ ? sceneFactory_->CreateScene(nextSceneName_) : nullptr;
    if (!nextScene) {
        Logger::LogError("シーンを生成できません: " + nextSceneName_);
        nextSceneName_ = currentSceneName_;
        transition_.FinishSwitch();
        return;
    }
    const auto loadingStarted = std::chrono::steady_clock::now();
    audio_->StopWave(); // 前のシーンの音を止める

    // PostDraw は複数フレームを並行実行するため、直前まで描画していた
    // シーンの GPU リソースを Finalize する前に使用完了を保証する。
    // ここで待たないと、解放済みのリソース／ディスクリプタ参照が次回の
    // ExecuteCommandLists で検出されることがある。
    dxCommon_->WaitForGpu();

    // 前のシーンを終了処理してからリソースを解放
    if (currentScene_) {
        currentScene_->Shutdown();
    }

    // UIレイアウトの編集パネルには、新しいシーンが参照した項目だけを並べる
    UILayout::ResetUsedLayouts();

    // 工場を使って新しいシーンを作成・初期化
    currentScene_ = std::move(nextScene);
    currentScene_->Init(dxCommon_, input_, audio_);
    // シーン切り替え時にロードされたテクスチャを一括転送・同期する
    TextureManager::GetInstance()->FlushUploads();

    // ImGuiのセット
    currentScene_->SetImGuiManager(imguiManager_);
    dxCommon_->SetDiagnosticContext(nextSceneName_);
    CrashHandler::SetContext(nextSceneName_);
    currentSceneName_ = nextSceneName_;
    ++sceneSwitchCount_;

    // シーンが切り替わったので、画面を明るくし始める
    transition_.FinishSwitch();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - loadingStarted).count();
    Logger::Log("Scene load " + nextSceneName_ + ": " + std::to_string(elapsed) + " ms");
}

/** @brief シーンの描画処理 */
void SceneManager::Draw() {
    if (currentScene_) {
        // D3D12デバッグメッセージへ現在のシーンを添えて原因箇所を絞り込む
        dxCommon_->SetDiagnosticContext(nextSceneName_.empty() ? "TitleScene" : nextSceneName_);
        CrashHandler::SetContext(nextSceneName_.empty() ? "TitleScene" : nextSceneName_);
        currentScene_->Render();
    }

}

/** @brief ポストエフェクトの後に画面遷移を重ねる */
void SceneManager::DrawTransition() {
    transition_.Draw();
}

/** @brief エディタのゲーム画面プレビュー用テクスチャを用意する */
void SceneManager::PrepareEditorPreview() {
    editorPreview_.Prepare(*dxCommon_, currentScene_->GetStageEditor());
}

/** @brief 編集中のゲーム映像を小窓用テクスチャに転送する */
void SceneManager::CaptureEditorPreview() {
    if (currentScene_ && !transition_.IsLoading()) {
        editorPreview_.Capture(*dxCommon_, currentScene_->GetStageEditor());
    }
}

/** @brief 終了処理 */
void SceneManager::Finalize() {
    // 最終フレームで使用したシーンのGPUリソースを安全に破棄できるまで待機する
    // ステージエディタ表示中は配置モデルとギズモも描画するため、シーン破棄より前に同期する
    if (dxCommon_) {
        dxCommon_->WaitForGpu();
    }

    editorPreview_.Finalize();

    if (currentScene_) {
        currentScene_->Shutdown();
    }

    currentScene_.reset();
    nextScene_.reset();

    transition_ = SceneTransitionOverlay {};

}

// シーン切り替え予約
void SceneManager::ChangeScene(const std::string& sceneName, float fadeOut, float fadeIn) {
    if (transition_.IsChanging()) {
        return;
    }
    nextSceneName_ = sceneName;
    transition_.Begin(fadeOut, fadeIn);
}

/** @brief Debug の開始シーンとして選べる数を返す */
int SceneManager::GetDebugStartSceneCount() {
    return kDebugStartSceneCount;
}

/** @brief 指定した番号の開始シーンの名前を返す */
const char* SceneManager::GetDebugStartSceneName(int index) {
    return kDebugStartScenes[index].name;
}

/** @brief 指定した番号の開始シーンのエディタでの表示名を返す */
const char* SceneManager::GetDebugStartSceneLabel(int index) {
    return kDebugStartScenes[index].label;
}

/** @brief Debug開始シーンを設定ファイルへ保存する。失敗時はfalse */
bool SceneManager::SetDebugStartScene(const std::string& sceneName) {
#ifdef _DEBUG
    if (!IsDebugStartScene(sceneName)) {
        return false;
    }
    try {
        const nlohmann::json settings = { { "scene", sceneName } };
        std::ofstream file(kDebugStartupPath, std::ios::trunc);
        if (!file) {
            return false;
        }
        file << settings.dump(2) << '\n';
        file.flush();
        if (!file) {
            return false;
        }
        debugStartScene_ = sceneName;
        return true;
    } catch (const std::exception& error) {
        Logger::LogError("Debug startup settings: " + std::string(error.what()));
        return false;
    }
#else
    (void)sceneName;
    return false;
#endif
}

} // namespace engine::game
