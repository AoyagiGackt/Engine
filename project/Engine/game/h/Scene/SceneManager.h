/**
 * @file SceneManager.h
 * @brief シーンの切り替え、更新、描画を一括管理するクラスを定義するファイル
 */
#pragma once
#include "AbstractSceneFactory.h"
#include "Audio.h"
#include "BaseScene.h"
#include "DirectXCommon.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "SpriteCommon.h"
#include <Fade.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::Input;
using engine::graphics::Fade;
using engine::graphics::ImGuiManager;
using engine::graphics::SpriteCommon;

/**
 * @brief シーン運用を統括するマネージャークラス
 * @note シングルトンパターンを採用しており、どこからでも ChangeScene() を呼び出して
 * シーン遷移を予約できます実際の切り替えは Update() の冒頭で安全に行われます
 */
class SceneManager {
public:
    /**
     * @brief SceneManagerの唯一のインスタンスを取得する
     * @return SceneManager* シングルトンインスタンスへのポインタ
     */
    static SceneManager* GetInstance();

    /**
     * @brief シーンマネージャーの初期化
     * @param dxCommon DirectX基盤のポインタ
     * @param input 入力管理のポインタ
     * @param audio 音響管理のポインタ
     * @param imgui ImGui管理のポインタ
     * @note 各シーンで共有する基盤システムのポインタを保持します
     */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio, ImGuiManager* imgui);

    /**
     * @brief 終了処理
     * @note 実行中のシーンを破棄し、後片付けを行います
     */
    void Finalize();

    /**
     * @brief シーンの更新処理
     * @note 次のシーンが予約されている場合は切り替え処理を行い、
     * その後現在アクティブなシーンの Update() を呼び出します
     */
    void Update();

    /**
     * @brief シーンの描画処理
     * @note 現在アクティブなシーンの Draw() を呼び出します
     */
    void Draw();

    /**
     * @brief 次のシーンを予約する
     * @param sceneName 生成したいシーンの名前（例: "TITLE", "GAMEPLAY"）
     * @note この関数を呼ぶと、次フレームの Update() の冒頭でシーンが切り替わります
     */
    void ChangeScene(const std::string& sceneName, float fadeOut = 0.15f, float fadeIn = 0.15f);

    /**
     * @brief ロード画面を経由してシーンを切り替える
     * @param targetScene 最終的に遷移したいシーン名
     */
    void ChangeSceneWithLoading(const std::string& targetScene);

    /**
     * @brief ロード画面が遷移すべき先のシーン名を返す
     */
    const std::string& GetLoadingTarget() const { return loadingTargetScene_; }

    bool IsAsyncLoadReady() const { return asyncLoadReady_.load(); }
    /** @brief 非同期ロードの進捗率を0.0から1.0で返す */
    float GetAsyncLoadProgress() const { return asyncLoadProgress_.load(); }
    /** @brief 非同期ロードが失敗したかを返す */
    bool HasAsyncLoadFailed() const { return asyncLoadFailed_.load(); }
    /** @brief 非同期ロードの失敗理由を返す */
    std::string GetAsyncLoadError() const;

    /**
     * @brief シーン生成用工場をセットする
     * @param factory AbstractSceneFactoryを継承した具体的な工場のポインタ
     * @note シーン名から実際のクラスを生成するために必要です
     */
    void SetSceneFactory(AbstractSceneFactory* factory) { sceneFactory_ = factory; }

    /**
     * @brief 現在のシーンがポストエフェクト（グレースケール等）のオフスクリーンRTVリダイレクトに対応しているか
     * @note 未対応シーン中は Game.cpp 側で BeginScene/EndScene/Apply を呼ばないようにするためのガード
     */
    bool CurrentScenePostEffectsSupported() const
    {
        return currentScene_ && currentScene_->SupportsPostEffects();
    }

private:
    SceneManager() = default;
    /** @brief バックグラウンドロードスレッドが残っていれば合流させてから破棄する */
    ~SceneManager();
    SceneManager(const SceneManager&) = delete;
    const SceneManager& operator=(const SceneManager&) = delete;

    /** @brief フェードアウト完了後のシーン切替本体（Update()の冒頭から呼ばれる） */
    void PerformSceneSwitch();
    /** @brief LOADINGシーンへの切替時、遷移先シーンをバックグラウンドスレッドで事前生成する */
    void StartBackgroundLoad();

    // 外部から提供される基盤システム
    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;

    // シーン管理メンバ

    /** @brief 現在アクティブなシーン */
    std::unique_ptr<BaseScene> currentScene_;

    /** @brief 次のフレームで切り替える予定のシーン */
    std::unique_ptr<BaseScene> nextScene_;

    /** @brief バックグラウンドで事前初期化済みのシーン */
    std::unique_ptr<BaseScene> preloadedScene_;

    /** @brief 非同期ロードスレッド */
    std::thread loadingThread_;

    /** @brief バックグラウンドロード完了フラグ */
    std::atomic<bool> asyncLoadReady_ { false };
    std::atomic<float> asyncLoadProgress_ { 0.0f };
    std::atomic<bool> asyncLoadFailed_ { false };
    mutable std::mutex asyncLoadErrorMutex_;
    std::string asyncLoadError_;

    /** @brief シーンを生成するための工場ポインタ（外部からセットされる） */
    AbstractSceneFactory* sceneFactory_ = nullptr;

    /** @brief フェード管理 **/
    std::unique_ptr<SpriteCommon> spriteCommon_;
    Fade fade_;

    /** @brief 次に読み込むシーン名 **/
    std::string nextSceneName_;

    /** @brief ChangeSceneWithLoading で指定した最終遷移先シーン名 **/
    std::string loadingTargetScene_;

    /** @brief 現在遷移中かどうかのフラグ **/
    bool isChanging_ = false;

    /** @brief フェードイン時間（ChangeScene で設定） **/
    float fadeInDuration_ = 0.15f;
};

} // namespace engine::game
