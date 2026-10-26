/**
 * @file SceneFlow.cpp
 * @brief SceneFlowのシーン遷移設定の読み込みと解決を実装するファイル
 */
#include "SceneFlow.h"
#include "CrashBreadcrumbs.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "SceneManager.h"

namespace engine::game {
namespace {
constexpr const char* kSceneFlowPath = "Resources/Config/scene_flow.json";
constexpr float kDefaultFadeSeconds = SceneManager::kDefaultFadeSeconds;
}

/** @brief 唯一のSceneFlowインスタンスを取得する（初回はJSONを読み込む） */
SceneFlow* SceneFlow::GetInstance() {
    static SceneFlow sInstance;
    return &sInstance;
}

/** @brief シーンの流れの定義を読み込んで初期化する */
SceneFlow::SceneFlow() {
    Reload();
}

/** @brief JSONを読み直す（エディタでの調整後に使う想定） */
void SceneFlow::Reload() {
    transitions_.clear();
    const nlohmann::json root = JsonHelper::Load(kSceneFlowPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("SceneFlow: ") + kSceneFlowPath + " が読めません。遷移はコード側の既定値で動きます");
        return;
    }
    for (const auto& [sceneName, outcomes] : root.items()) {
        if (!outcomes.is_object()) {
            continue;
        }
        for (const auto& [outcome, entry] : outcomes.items()) {
            SceneTransition transition;
            if (entry.is_string()) {
                transition.scene = entry.get<std::string>();
            } else if (entry.is_object()) {
                transition.scene = entry.value("scene", "");
                transition.fadeOut = entry.value("fadeOut", kDefaultFadeSeconds);
                transition.fadeIn = entry.value("fadeIn", kDefaultFadeSeconds);
            }
            if (!transition.scene.empty()) {
                transitions_[sceneName][outcome] = transition;
            }
        }
    }
}

/** @brief 遷移先を解決する */
SceneTransition SceneFlow::Resolve(const std::string& sceneName, const std::string& outcome,
    const std::string& fallbackScene) const {
    auto sceneIt = transitions_.find(sceneName);
    if (sceneIt != transitions_.end()) {
        auto outcomeIt = sceneIt->second.find(outcome);
        if (outcomeIt != sceneIt->second.end()) {
            return outcomeIt->second;
        }
    }
    Logger::LogWarning("SceneFlow: " + sceneName + "." + outcome + " が未定義のため既定の " + fallbackScene + " へ遷移します");
    SceneTransition fallback;
    fallback.scene = fallbackScene;
    return fallback;
}

/** @brief 解決した遷移先へSceneManager経由で切り替える（Resolve+ChangeSceneの一括版） */
void SceneFlow::Transition(const std::string& sceneName, const std::string& outcome,
    const std::string& fallbackScene) const {
    const SceneTransition transition = Resolve(sceneName, outcome, fallbackScene);
    CrashBreadcrumbs::Add("flow", sceneName + " --" + outcome + "--> " + transition.scene);
    SceneManager::GetInstance()->ChangeScene(transition.scene, transition.fadeOut, transition.fadeIn);
}

} // namespace engine::game
