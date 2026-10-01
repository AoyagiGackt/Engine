/**
 * @file SceneTransitionOverlay.h
 * @brief シーン切り替え時のフェードとロード表示をまとめて管理するクラスを定義するファイル
 */
#pragma once
#include "Fade.h"
#include "FontRenderer.h"
#include "SceneTransitionFlow.h"
#include "SpriteCommon.h"
#include <memory>
namespace engine { class DirectXCommon; }
namespace engine::game {
/** @brief 遷移演出の資源と状態をまとめ、ロード画面を送信してから切り替えを許可する。 */
class SceneTransitionOverlay {
public:
    void Initialize(engine::DirectXCommon* graphics);
    void Begin(float fadeOutSeconds, float fadeInSeconds);
    bool Update();
    void Draw();
    void FinishSwitch();
    bool IsChanging() const { return flow_.IsChanging(); }
    bool IsLoading() const { return flow_.IsLoading(); }
private:
    engine::DirectXCommon* graphics_ = nullptr;
    std::unique_ptr<engine::graphics::SpriteCommon> sprites_;
    engine::graphics::Fade fade_;
    FontRenderer font_;
    SceneTransitionFlow flow_;
    bool presented_ = false;
    float fadeInSeconds_ = 0.38f;
};
}
