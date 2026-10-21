/**
 * @file OptionsScene.cpp
 * @brief オプション画面（BGM/SE音量調整）の入力処理と描画（OptionsScene）の実装
 */
#include "OptionsScene.h"
#include "GameSettings.h"
#include "SceneFlow.h"
#include "SceneShared.h"
#include "UILayout.h"
#include "WinApp.h"
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr Vector4 kBackgroundColor = { 0.05f, 0.05f, 0.08f, 1.0f };
constexpr const char* kLayoutName = "options";
constexpr Vector2 kMenuPosition = { 440.0f, 300.0f };
constexpr Vector2 kMenuItemSize = { 400.0f, 70.0f };
constexpr Vector2 kBgmVolumeTextPosition = { 900.0f, 321.0f };
constexpr Vector2 kSeVolumeTextPosition = { 900.0f, 391.0f };
constexpr float kVolumeTextScale = 1.5f;
constexpr Vector4 kVolumeTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector2 kHelpTextPosition = { 340.0f, 630.0f }; // メニュー4行の下に置く
constexpr float kHelpTextScale = 1.0f;
constexpr Vector4 kHelpTextColor = { 0.7f, 0.7f, 0.7f, 1.0f };
}

/** @brief 設定画面のメニューと文字を用意する */
void OptionsScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) });
    bgSprite_->SetColor(kBackgroundColor);

    fontRenderer_.Initialize(spriteCommon_.get());
    commandList_.Initialize(&fontRenderer_, audio_);

    menu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    menu_.BindLayout(kLayoutName, "menu", kMenuPosition, kMenuItemSize);
    menu_.SetItems({
        { "BGM VOLUME" },
        { "SE VOLUME" },
        { "COMMAND LIST" },
        { "BACK" },
    });

    finished_ = false;
    page_ = &Settings();
}

/** @brief 設定画面の資源を解放する */
void OptionsScene::Finalize()
{
}

/** @brief 現在カーソルが合っている行が音量行なら音量を+delta（-1.0〜1.0）だけ増減する */
void OptionsScene::AdjustVolumeAtCursor(float delta)
{
    const int row = menu_.GetSelectedIndex();
    if (row != kRowBgmVolume && row != kRowSeVolume)
    {
        return;
    }

    SceneShared::AdjustVolume(audio_, row == kRowBgmVolume, delta);
}

// ══════════════════════════════════════════════════════
// ページ（設定の一覧 / コマンド表）
// ══════════════════════════════════════════════════════

/** @brief 設定項目の一覧を表示・操作するページ */
class OptionsScene::SettingsPage : public OptionsScene::IPage {
public:
    /** @brief 設定のページの入力を処理する */
    void Update(OptionsScene& scene) const override {
        scene.UpdateSettings();
    }
    /** @brief 設定のページを描画する */
    void Draw(OptionsScene& scene) const override {
        scene.DrawSettings();
    }
};

/** @brief コマンド表を表示して設定画面へ戻る入力を処理するページ */
class OptionsScene::CommandListPage : public OptionsScene::IPage {
public:
    /** @brief コマンド表のページの入力を処理する（戻るで設定へ戻る） */
    void Update(OptionsScene& scene) const override
    {
        if (scene.commandList_.Update(scene.input_))
        {
            scene.page_ = &Settings();
        }
    }
    /** @brief コマンド表のページを描画する */
    void Draw(OptionsScene& scene) const override {
        scene.commandList_.Draw(scene.input_);
    }
};

/** @brief 設定のページの共有インスタンスを返す */
const OptionsScene::IPage& OptionsScene::Settings()
{
    static const SettingsPage page;
    return page;
}

/** @brief コマンド表のページの共有インスタンスを返す */
const OptionsScene::IPage& OptionsScene::CommandList()
{
    static const CommandListPage page;
    return page;
}

/** @brief 設定画面を1フレーム進める */
void OptionsScene::Update()
{
    page_->Update(*this);
}

/** @brief 設定の一覧ページの入力処理 */
void OptionsScene::UpdateSettings()
{
    menu_.Update(input_);

    if (input_->TriggerMenuLeft())
    {
        AdjustVolumeAtCursor(-SceneShared::kVolumeStep);
    }
    if (input_->TriggerMenuRight())
    {
        AdjustVolumeAtCursor(SceneShared::kVolumeStep);
    }

    if (menu_.ConsumeConfirm(input_))
    {
        if (menu_.GetSelectedIndex() == kRowBack)
        {
            SceneFlow::GetInstance()->Transition("OPTIONS", "back", "TITLE");
        }
        else if (menu_.GetSelectedIndex() == kRowCommandList)
        {
            commandList_.Open();
            page_ = &CommandList();
            return;
        }
    }
    if (input_->TriggerMenuCancel())
    {
        audio_->PlayMenuSelect();
        SceneFlow::GetInstance()->Transition("OPTIONS", "back", "TITLE");
    }
}

/** @brief 設定画面を描画する */
void OptionsScene::Draw()
{
    // エディタ表示中はUpdate()が止まるため、文字コマンドの破棄と配置の反映は毎フレーム必ず通るここで行う
    fontRenderer_.Reset();
    UILayout& layout = UILayout::Get(kLayoutName);
    bgSprite_->SetColor(layout.Color("background.color", kBackgroundColor));
    bgSprite_->Update();

    spriteCommon_->CommonDrawSettings();
    bgSprite_->Draw();
    page_->Draw(*this);
    fontRenderer_.Draw();
}

/** @brief 設定の一覧ページの描画 */
void OptionsScene::DrawSettings()
{
    UILayout& layout = UILayout::Get(kLayoutName);
    menu_.Draw();

    const float volumeScale = layout.Float("volume.scale", kVolumeTextScale);
    const Vector4 volumeColor = layout.Color("volume.color", kVolumeTextColor);
    const Vector2 bgmPosition = layout.Pos("volume.bgm_pos", kBgmVolumeTextPosition);
    const Vector2 sePosition = layout.Pos("volume.se_pos", kSeVolumeTextPosition);
    const auto& settings = GameSettingsManager::GetInstance()->Get();
    fontRenderer_.DrawString(SceneShared::VolumeLabel(settings.bgmVolume), bgmPosition.x, bgmPosition.y, volumeScale, volumeColor);
    fontRenderer_.DrawString(SceneShared::VolumeLabel(settings.seVolume), sePosition.x, sePosition.y, volumeScale, volumeColor);

    const Vector2 helpPosition = layout.Pos("help.pos", kHelpTextPosition);
    fontRenderer_.DrawString(input_->IsUsingGamepad() ? "Left/Right: adjust   A: confirm   B: back" : "A/D or Left/Right: adjust   Space: confirm   Backspace: back",
        helpPosition.x, helpPosition.y, layout.Float("help.scale", kHelpTextScale), layout.Color("help.color", kHelpTextColor));
}

} // namespace engine::game
