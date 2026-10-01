/**
 * @file SceneFlow.cpp
 * @brief SceneFlowのシーン遷移設定の読み込みと解決を実装するファイル
 */
#include "SceneFlow.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "SceneManager.h"
using namespace engine::game;
using namespace engine;

namespace {
constexpr const char* kSceneFlowPath = "Resources/Config/scene_flow.json";
constexpr float kDefaultFadeSeconds = SceneManager::kDefaultFadeSeconds;
}

SceneFlow* SceneFlow::GetInstance()
{
    static SceneFlow instance;
    return &instance;
}

SceneFlow::SceneFlow()
{
    Reload();
}

void SceneFlow::Reload()
{
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

SceneTransition SceneFlow::Resolve(const std::string& sceneName, const std::string& outcome,
    const std::string& fallbackScene) const
{
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

void SceneFlow::Transition(const std::string& sceneName, const std::string& outcome,
    const std::string& fallbackScene) const
{
    const SceneTransition transition = Resolve(sceneName, outcome, fallbackScene);
    SceneManager::GetInstance()->ChangeScene(transition.scene, transition.fadeOut, transition.fadeIn);
}
