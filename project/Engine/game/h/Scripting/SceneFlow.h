/**
 * @file SceneFlow.h
 * @brief シーン遷移先をResources/Config/scene_flow.jsonから解決するグローバル設定
 * @note 各シーンは「どの結果になったか」（"gameover" "clear" 等の結果名）だけを伝え、
 * 実際に次へ進むシーン名とフェード時間はこのJSONが決める。遷移の組み替えにコード変更が要らないようにする
 */
#pragma once
#include <map>
#include <string>
namespace engine::game {

/** @brief 1件ぶんの遷移先（シーン名とフェード秒数） */
struct SceneTransition {
    std::string scene;
    float fadeOut = 0.38f;
    float fadeIn = 0.38f;
};

/**
 * @brief シーン名と結果名の組から遷移先を引くレジストリ
 * @details JSONに無い組はfallbackの遷移先を返し、ログに警告を出す
 */
class SceneFlow {
public:
    /**
     * @brief 唯一のSceneFlowインスタンスを取得する（初回はJSONを読み込む）
     * @return SceneFlowのインスタンス
     */
    static SceneFlow* GetInstance();

    /** @brief JSONを読み直す（エディタでの調整後に使う想定） */
    void Reload();

    /**
     * @brief 遷移先を解決する
     * @param sceneName 現在のシーン名（SceneFactoryの登録名）
     * @param outcome 結果名（例: "gameover" "clear" "next"）
     * @param fallbackScene JSONに定義が無い場合に使うシーン名
     * @return 遷移先の情報
     */
    SceneTransition Resolve(const std::string& sceneName, const std::string& outcome,
        const std::string& fallbackScene) const;

    /**
     * @brief 解決した遷移先へSceneManager経由で切り替える（Resolve+ChangeSceneの一括版）
     * @param sceneName 現在のシーン名
     * @param outcome 結果名
     * @param fallbackScene JSONに定義が無い場合に使うシーン名
     */
    void Transition(const std::string& sceneName, const std::string& outcome,
        const std::string& fallbackScene) const;

private:
    SceneFlow();
    SceneFlow(const SceneFlow&) = delete;
    SceneFlow& operator=(const SceneFlow&) = delete;

    std::map<std::string, std::map<std::string, SceneTransition>> transitions_;
};

} // namespace engine::game
