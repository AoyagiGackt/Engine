/**
 * @file SceneTransitionOverlay.cpp
 * @brief シーン切り替え時のフェードとロード表示（SceneTransitionOverlay）の実装
 */
#include "SceneTransitionOverlay.h"
#include "DirectXCommon.h"
namespace engine::game {
/** @brief フェードと読み込み表示を用意する */
void SceneTransitionOverlay::Initialize(engine::DirectXCommon* graphics) {
    graphics_ = graphics;
    sprites_ = std::make_unique<engine::graphics::SpriteCommon>();
    sprites_->Initialize(graphics_);
    fade_.Initialize(sprites_.get());
    font_.Initialize(sprites_.get());
    flow_.Reset();
    presented_ = false;
}
/** @brief フェードアウトとフェードインの秒数を指定して遷移を始める */
void SceneTransitionOverlay::Begin(float fadeOut, float fadeIn) {
    if (flow_.IsChanging()) {
        return;
    }
    fadeInSeconds_ = fadeIn;
    presented_ = false;
    flow_.Begin();
    fade_.Start(engine::graphics::Fade::Status::kFadeOut, fadeOut);
}
/** @brief フェードを進め、シーンを切り替える時なら true を返す */
bool SceneTransitionOverlay::Update() {
    fade_.Update();
    return flow_.Update(fade_.IsFinished(), presented_) == SceneTransitionFlow::Action::kCommitSwitch;
}
/** @brief シーンの切り替えが済んだのでフェードインを始める */
void SceneTransitionOverlay::FinishSwitch() {
    flow_.FinishSwitch();
    presented_ = false;
    fade_.Start(engine::graphics::Fade::Status::kFadeIn, fadeInSeconds_);
}
/** @brief フェードと、読み込み中なら読み込み表示を描画する */
void SceneTransitionOverlay::Draw() {
    fade_.Draw();
    if (!flow_.IsLoading()) {
        return;
    }
    const auto viewport = graphics_->GetCenteredClientViewport();
    const auto scissor = graphics_->GetCenteredClientScissorRect();
    auto* cmd = graphics_->GetCommandList();
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);
    sprites_->CommonDrawSettings();
    font_.Reset();
    constexpr Vector2 kLoadingTextPosition = { 550.0f, 350.0f };
    constexpr float kLoadingTextScale = 2.0f;
    constexpr Vector4 kLoadingTextColor = { 0.95f, 0.78f, 0.46f, 1.0f };
    font_.DrawStringW(L"ロード中...", kLoadingTextPosition.x, kLoadingTextPosition.y, kLoadingTextScale, kLoadingTextColor);
    font_.Draw();
    presented_ = true;
}
}
