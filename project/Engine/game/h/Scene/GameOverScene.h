/**
 * @file GameOverScene.h
 * @brief ゲームオーバー画面のシーンを管理するファイル
 */
#pragma once
#include "Audio.h"
#include "BaseScene.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "UIMenu.h"
#include <memory>
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::Input;
using engine::graphics::ImGuiManager;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/**
 * @brief ゲームオーバー画面のシーンクラス
 * @note W/S・↑/↓で選び、Space/Enterで確定する（リスタート=MAP、タイトルに戻る=TITLE）
 */
class GameOverScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    /** @brief 半透明オーバーレイと選択肢（リスタート/タイトルに戻る）を描画する */
    void Draw() override;

    void SetImGuiManager(ImGuiManager* imgui) { imguiManager_ = imgui; }

private:
    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;

    /** @brief 半透明の黒背景 */
    std::unique_ptr<Sprite> overlay_;

    /** @brief GAME OVER表記・選択肢ラベル・操作説明の文字描画 */
    FontRenderer fontRenderer_;

    /** @brief リスタート / タイトルに戻る の選択肢 */
    UIMenu menu_;
};

} // namespace engine::game
