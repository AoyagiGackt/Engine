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

/** @brief 非同期シーンロード中に背景・進捗バー・点滅ドットを表示し、完了したら遷移先シーンへ切り替える */
class LoadingScene : public BaseScene {
public:
    /**
     * @brief ロード画面の背景・進捗バー・ドットアニメーション用スプライトを初期化する
     * @param dxCommon DirectXの共通処理
     * @param input 入力管理（本シーンでは未使用）
     * @param audio 音声管理（本シーンでは未使用）
     */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /** @brief 進捗バーとドットアニメーションを更新し、最低表示時間経過後にロード完了なら遷移先シーンへ切り替える */
    void Update() override;
    /** @brief 背景・進捗バー・ドットアニメーションを描画する */
    void Draw() override;
    /** @brief 特に解放処理は行わない（何もしない） */
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
