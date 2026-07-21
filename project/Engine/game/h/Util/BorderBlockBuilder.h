/**
 * @file BorderBlockBuilder.h
 * @brief BorderBlockBuilderが公開する型とAPIを定義するファイル
 */
#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <memory>
#include <vector>
namespace engine::game {

// 標準の壁沿いに境界ブロックを並べて生成する（TrainingScene/BattleTestSceneで共用）
inline void BuildBorderBlocks(engine::graphics::ModelCommon* modelCommon, engine::graphics::Model* modelBlock,
    std::vector<std::unique_ptr<engine::graphics::Object3d>>& borderBlocks)
{
    auto addBlock = [&](float x, float y, float z) {
        auto b = std::make_unique<engine::graphics::Object3d>();
        b->Initialize(modelCommon);
        b->SetModel(modelBlock);
        b->SetEnableLighting(false);
        b->SetPosition({ x, y, z });
        b->Update();
        borderBlocks.push_back(std::move(b));
    };
    for (int x = 0; x <= 28; ++x) {
        addBlock(static_cast<float>(x), -0.6f, 0.0f);
    }
    for (int x = 0; x <= 28; ++x) {
        addBlock(static_cast<float>(x), 13.0f, 0.0f);
    }
    for (int y = 0; y <= 12; ++y) {
        addBlock(2.0f, static_cast<float>(y), 0.0f);
    }
    for (int y = 0; y <= 12; ++y) {
        addBlock(28.0f, static_cast<float>(y), 0.0f);
    }
}

} // namespace engine::game
