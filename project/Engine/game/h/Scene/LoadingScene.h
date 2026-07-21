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

/**
 * @brief LoadingScene に関する型を提供する
 * @details LoadingScene が扱うデータと操作の責務をまとめる
 */
class LoadingScene : public BaseScene {
public:
    /**
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @param input 処理に使用する値
     * @param audio 処理に使用する値
     * @return なし
     */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /**
     * @brief Update に対応する状態を更新する
     * @return なし
     */
    void Update() override;
    /**
     * @brief Draw に対応する内容を描画する
     * @return なし
     */
    void Draw() override;
    /**
     * @brief Finalize に対応する終了処理を行う
     * @return なし
     */
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
