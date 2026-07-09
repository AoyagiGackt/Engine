#include "SceneFactory.h"
#include "AnimationEditorScene.h"
#include "BattleTestScene.h"
#include "ClearScene.h"
#include "GameOverScene.h"
#include "GamePlayScene.h"
#include "LoadingScene.h"
#include "MapScene.h"
#include "ShopScene.h"
#include "TitleScene.h"
#include "TrainingScene.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

std::unique_ptr<BaseScene> SceneFactory::CreateScene(const std::string& sceneName)
{
    // 次のシーンを生成
    std::unique_ptr<BaseScene> newScene = nullptr;

    if (sceneName == "TITLE") {
        newScene = std::make_unique<TitleScene>();
    } else if (sceneName == "GAMEPLAY") {
        newScene = std::make_unique<GamePlayScene>();
    } else if (sceneName == "TRAINING") {
        newScene = std::make_unique<TrainingScene>();
    } else if (sceneName == "BATTLETEST") {
        newScene = std::make_unique<BattleTestScene>();
    } else if (sceneName == "ANIMEDIT") {
        newScene = std::make_unique<AnimationEditorScene>();
    } else if (sceneName == "CLEAR") {
        newScene = std::make_unique<ClearScene>();
    } else if (sceneName == "GAMEOVER") {
        newScene = std::make_unique<GameOverScene>();
    } else if (sceneName == "LOADING") {
        newScene = std::make_unique<LoadingScene>();
    } else if (sceneName == "MAP") {
        newScene = std::make_unique<MapScene>();
    } else if (sceneName == "SHOP") {
        newScene = std::make_unique<ShopScene>();
    }

    return newScene;
}