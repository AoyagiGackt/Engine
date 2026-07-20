/**
 * @file LoadingScene.h
 * @brief ロード画面シーン
 */
#pragma once
#include "BaseScene.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
namespace engine::game {
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

class LoadingScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Update() override;
    void Draw() override;
    void Finalize() override;

private:
    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> bgSprite_;
    std::unique_ptr<Sprite> dotSprites_[3];
    std::unique_ptr<Sprite> progressBg_;
    std::unique_ptr<Sprite> progressFg_;

    float timer_ = 0.0f;
    float dotTimer_ = 0.0f;
    int activeDot_ = 0;
    bool sceneChangeRequested_ = false;
    bool failureHandled_ = false;

    static constexpr float kMinDisplayTime = 0.25f;
    static constexpr float kDotInterval = 0.3f;
};

} // namespace engine::game
