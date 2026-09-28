/**
 * @file OptionsScene.h
 * @brief BGM/SE音量を調整するオプション画面のシーンを管理するファイル
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
 * @brief BGM VOLUME / SE VOLUME / BACK を選択するオプション画面
 * @note カーソル移動はW/S、選択中がBGM/SEの行ならA/D（または←→）で音量を10%刻みで増減する
 * 変更は即座にAudioへ反映しGameSettingsManager::Save()で永続化する
 */
class OptionsScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

    bool IsFinished() const { return finished_; }
    void SetImGuiManager(ImGuiManager* imgui) { imguiManager_ = imgui; }

private:
    /** @brief 現在カーソルが合っている行が音量行なら音量を+delta（-1.0〜1.0）だけ増減する */
    void AdjustVolumeAtCursor(float delta);

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> bgSprite_;
    FontRenderer fontRenderer_;
    UIMenu menu_;

    static constexpr int kRowBgmVolume = 0;
    static constexpr int kRowSeVolume = 1;
    static constexpr int kRowBack = 2;

    bool finished_ = false;
};

} // namespace engine::game
