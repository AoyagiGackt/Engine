/**
 * @file PauseMenu.cpp
 * @brief プレイ中のシーンで共通に使う一時停止画面（PauseMenu）の実装
 */
#include "PauseMenu.h"
#include "Audio.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "GameSettings.h"
#include "Input.h"
#include "SceneShared.h"
#include "UILayout.h"
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kWhiteTexture = "Resources/white.png";
constexpr Vector4 kOverlayColor = { 0.0f, 0.0f, 0.0f, 0.75f };
constexpr Vector2 kMenuPosition = { 440.0f, 220.0f };
constexpr Vector2 kMenuItemSize = { 400.0f, 60.0f };
constexpr Vector2 kBgmVolumeTextPosition = { 900.0f, 291.0f };
constexpr Vector2 kSeVolumeTextPosition = { 900.0f, 351.0f };
constexpr Vector2 kAutoComboTextPosition = { 900.0f, 411.0f };
constexpr float kValueTextScale = 1.5f;
constexpr Vector4 kValueTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector2 kTitlePosition = { 560.0f, 130.0f };
constexpr float kTitleScale = 2.0f;
constexpr Vector4 kTitleColor = { 1.0f, 1.0f, 0.6f, 1.0f };

// メニューの行番号
constexpr int kRowResume = 0;
constexpr int kRowBgmVolume = 1;
constexpr int kRowSeVolume = 2;
constexpr int kRowAutoCombo = 3;
constexpr int kRowCommandList = 4;
constexpr int kRowExit = 5;
}

// ══════════════════════════════════════════════════════
// ページ（メニュー / コマンド表）
// ══════════════════════════════════════════════════════

/** @brief 一時停止中の画面の1ページ */
class PauseMenu::IPage {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IPage() = default;
    /** @brief 入力を受け付ける（ページの切り替えもここで行う） */
    virtual void Update(PauseMenu& pause, Input* input) const = 0;
    /** @brief 文字とパネルを積む */
    virtual void Draw(PauseMenu& pause, const Input* input) const = 0;
    /** @brief 戻るボタンをこのページで使うか（使うなら一時停止の解除には回さない） */
    virtual bool HandlesBack() const = 0;
};

/** @brief ポーズメニューの入力と描画を処理するページ */
class PauseMenu::MenuPage : public PauseMenu::IPage {
public:
    /** @brief ポーズメニューの入力を処理する */
    void Update(PauseMenu& pause, Input* input) const override {
        pause.UpdateMenu(input);
    }
    /** @brief ポーズメニューを描画する */
    void Draw(PauseMenu& pause, const Input*) const override {
        pause.DrawMenu();
    }
    /** @brief 戻る入力をこのページで扱うかを返す */
    bool HandlesBack() const override {
        return false;
    }
};

/** @brief ポーズ中のコマンド表と戻る操作を処理するページ */
class PauseMenu::CommandListPage : public PauseMenu::IPage {
public:
    /** @brief コマンド表のページの入力を処理する（戻るでメニューへ戻る） */
    void Update(PauseMenu& pause, Input* input) const override
    {
        if (pause.commandList_.Update(input))
        {
            pause.page_ = &Menu();
        }
    }
    /** @brief コマンド表のページを描画する */
    void Draw(PauseMenu& pause, const Input* input) const override {
        pause.commandList_.Draw(input);
    }
    // 戻るボタンはコマンド表を閉じるのに使う（一時停止は解除しない）
    bool HandlesBack() const override {
        return true;
    }
};

/** @brief メニューのページの共有インスタンスを返す */
const PauseMenu::IPage& PauseMenu::Menu()
{
    static const MenuPage page;
    return page;
}

/** @brief コマンド表のページの共有インスタンスを返す */
const PauseMenu::IPage& PauseMenu::CommandList()
{
    static const CommandListPage page;
    return page;
}

// ══════════════════════════════════════════════════════
// 初期化・更新・描画
// ══════════════════════════════════════════════════════

/** @brief 一時停止メニューの描画に使う部品と、退出項目の文言と動作を受け取って準備する */
void PauseMenu::Initialize(SpriteCommon* spriteCommon, FontRenderer* fontRenderer, Audio* audio,
    const std::string& layoutName, ExitItem exitItem)
{
    fontRenderer_ = fontRenderer;
    audio_ = audio;
    layoutName_ = layoutName;
    exitItem_ = std::move(exitItem);

    overlay_ = std::make_unique<Sprite>();
    overlay_->Initialize(spriteCommon, kWhiteTexture);
    overlay_->SetPosition({ 0.0f, 0.0f });
    overlay_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });

    menu_.Initialize(spriteCommon, fontRenderer, audio);
    menu_.BindLayout(layoutName_, "pause_menu", kMenuPosition, kMenuItemSize);
    menu_.SetItems({
        { "RESUME" },
        { "BGM VOLUME" },
        { "SE VOLUME" },
        { "AUTO COMBO" },
        { "COMMAND LIST" },
        { exitItem_.label },
    });
    commandList_.Initialize(fontRenderer, audio);
    page_ = &Menu();
}

/** @brief 開閉と一時停止中の入力を処理する */
bool PauseMenu::Update(Input* input, bool canOpen)
{
    // コマンド表を開いている間の戻るボタンはコマンド表を閉じるのに使い、一時停止の解除には回さない
    const bool toggle = input->TriggerAction(Input::Action::Pause)
        || (controller_.IsPaused() && !page_->HandlesBack() && input->TriggerMenuCancel());
    const bool wasPaused = controller_.IsPaused();
    const GamePauseController::UpdateMode mode = controller_.Advance(toggle && canOpen);
    if (mode == GamePauseController::UpdateMode::Menu)
    {
        page_->Update(*this, input);
    }
    if (!controller_.IsPaused())
    {
        page_ = &Menu(); // 次に一時停止した時はメニューから始める
    }
    // 閉じたフレームも止める（閉じるのに使った戻るボタンがシーン側の操作にまで届かないようにする）
    return wasPaused || mode != GamePauseController::UpdateMode::Play;
}

/** @brief メニューページの入力（音量・オートコンボ・決定） */
void PauseMenu::UpdateMenu(Input* input)
{
    menu_.Update(input);

    const int row = menu_.GetSelectedIndex();
    if (row == kRowBgmVolume || row == kRowSeVolume)
    {
        float delta = 0.0f;
        if (input->TriggerMenuLeft())
        {
            delta = -SceneShared::kVolumeStep;
        }
        else if (input->TriggerMenuRight())
        {
            delta = SceneShared::kVolumeStep;
        }
        if (delta != 0.0f)
        {
            SceneShared::AdjustVolume(audio_, row == kRowBgmVolume, delta);
        }
    }

    const bool confirmed = menu_.ConsumeConfirm(input);
    if (row == kRowAutoCombo && (confirmed || input->TriggerMenuLeft() || input->TriggerMenuRight()))
    {
        SceneShared::ToggleAutoCombo(audio_);
    }
    if (!confirmed)
    {
        return;
    }
    if (row == kRowResume)
    {
        controller_.Resume();
    }
    else if (row == kRowCommandList)
    {
        commandList_.Open();
        page_ = &CommandList();
    }
    else if (row == kRowExit)
    {
        controller_.Resume();
        if (exitItem_.onSelect)
        {
            exitItem_.onSelect();
        }
    }
}

/** @brief 一時停止中なら暗幕と今のページを積む（一時停止中でなければ何もしない） */
void PauseMenu::Draw(const Input* input)
{
    if (!controller_.IsPaused())
    {
        return;
    }
    UILayout& layout = UILayout::Get(layoutName_);
    overlay_->SetColor(layout.Color("pause.background_color", kOverlayColor));
    overlay_->Update();
    overlay_->Draw();
    page_->Draw(*this, input);
}

/** @brief メニューページの描画（項目・現在値・見出し） */
void PauseMenu::DrawMenu()
{
    UILayout& layout = UILayout::Get(layoutName_);
    menu_.Draw();

    const float valueScale = layout.Float("pause.volume_scale", kValueTextScale);
    const Vector4 valueColor = layout.Color("pause.volume_color", kValueTextColor);
    const GameSettings& settings = GameSettingsManager::GetInstance()->Get();
    auto drawVolume = [&](const char* key, const Vector2& defaultPosition, float volume) {
        const Vector2 position = layout.Pos(key, defaultPosition);
        fontRenderer_->DrawString(SceneShared::VolumeLabel(volume), position.x, position.y, valueScale, valueColor);
    };
    drawVolume("pause.bgm_volume_pos", kBgmVolumeTextPosition, settings.bgmVolume);
    drawVolume("pause.se_volume_pos", kSeVolumeTextPosition, settings.seVolume);
    const Vector2 autoComboPosition = layout.Pos("pause.auto_combo_pos", kAutoComboTextPosition);
    fontRenderer_->DrawString(SceneShared::AutoComboLabel(), autoComboPosition.x, autoComboPosition.y, valueScale, valueColor);

    const Vector2 titlePosition = layout.Pos("pause.title_pos", kTitlePosition);
    fontRenderer_->DrawString("PAUSED", titlePosition.x, titlePosition.y,
        layout.Float("pause.title_scale", kTitleScale), layout.Color("pause.title_color", kTitleColor));
}

} // namespace engine::game
