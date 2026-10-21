/**
 * @file OptionsScene.h
 * @brief BGM/SE音量を調整するオプション画面のシーンを管理するファイル
 */
#pragma once
#include "Audio.h"
#include "BaseScene.h"
#include "CommandListView.h"
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
 * @brief BGM VOLUME / SE VOLUME / COMMAND LIST / BACK を選択するオプション画面
 * @note カーソル移動はW/S、選択中がBGM/SEの行ならA/D（または←→）で音量を10%刻みで増減する
 * 変更は即座にAudioへ反映しGameSettingsManager::Save()で永続化する
 * COMMAND LIST を選ぶとコマンド表のページへ切り替わる（ページは状態パターンで持つ）
 */
class OptionsScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

    /** @brief 設定画面を閉じたかを返す */
    bool IsFinished() const {
        return finished_;
    }

private:
    /** @brief オプション画面の1ページ（設定の一覧 / コマンド表） */
    class IPage {
    public:
        /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
        virtual ~IPage() = default;
        /** @brief 入力を受け付ける（ページの切り替えもここで行う） */
        virtual void Update(OptionsScene& scene) const = 0;
        /** @brief 文字とパネルを積む */
        virtual void Draw(OptionsScene& scene) const = 0;
    };
    class SettingsPage;
    class CommandListPage;
    static const IPage& Settings();
    static const IPage& CommandList();

    /** @brief 設定の一覧ページの入力処理 */
    void UpdateSettings();
    /** @brief 設定の一覧ページの描画 */
    void DrawSettings();

    /** @brief 現在カーソルが合っている行が音量行なら音量を+delta（-1.0〜1.0）だけ増減する */
    void AdjustVolumeAtCursor(float delta);

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite> bgSprite_;
    FontRenderer fontRenderer_;
    UIMenu menu_;
    CommandListView commandList_;
    const IPage* page_ = nullptr; ///< 表示中のページ

    static constexpr int kRowBgmVolume = 0;
    static constexpr int kRowSeVolume = 1;
    static constexpr int kRowCommandList = 2;
    static constexpr int kRowBack = 3;

    bool finished_ = false;
};

} // namespace engine::game
