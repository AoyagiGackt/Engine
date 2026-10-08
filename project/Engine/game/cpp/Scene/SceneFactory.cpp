/**
 * @file SceneFactory.cpp
 * @brief シーン名から対応するシーンクラスを生成する具体的な工場（SceneFactory）の実装
 */
#include "SceneFactory.h"
#include "BattleTestScene.h"
#include "ClearScene.h"
#include "GameOverScene.h"
#include "GamePlayScene.h"
#include "MapScene.h"
#include "OptionsScene.h"
#include "ShopScene.h"
#include "TitleScene.h"
#include "TrainingScene.h"
#include <unordered_map>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
using SceneCreator = std::unique_ptr<BaseScene> (*)();

template <class T>
std::unique_ptr<BaseScene> Create()
{
    return std::make_unique<T>();
}

/** @brief シーン名と生成関数の対応表 */
const std::unordered_map<std::string, SceneCreator>& SceneCreators()
{
    static const std::unordered_map<std::string, SceneCreator> kCreators = {
        { "TITLE", &Create<TitleScene> },
        { "GAMEPLAY", &Create<GamePlayScene> },
        { "TRAINING", &Create<TrainingScene> },
        { "BATTLETEST", &Create<BattleTestScene> },
        { "CLEAR", &Create<ClearScene> },
        { "GAMEOVER", &Create<GameOverScene> },
        { "MAP", &Create<MapScene> },
        { "SHOP", &Create<ShopScene> },
        { "OPTIONS", &Create<OptionsScene> },
    };
    return kCreators;
}
} // namespace

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName)
{
    const auto& creators = SceneCreators();
    const auto it = creators.find(sceneName);
    return it != creators.end() ? it->second() : nullptr;
}