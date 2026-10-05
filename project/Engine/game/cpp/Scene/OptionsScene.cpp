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
#include <cstdio>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr float kVolumeStep = 0.1f;
constexpr Vector4 kBackgroundColor = { 0.05f, 0.05f, 0.08f, 1.0f };
constexpr const char* kLayoutName = "options";
constexpr Vector2 kMenuPosition = { 440.0f, 300.0f };
constexpr Vector2 kMenuItemSize = { 400.0f, 70.0f };
constexpr float kPercentScale = 100.0f;
constexpr float kRoundingOffset = 0.5f;
constexpr Vector2 kBgmVolumeTextPosition = { 900.0f, 321.0f };
constexpr Vector2 kSeVolumeTextPosition = { 900.0f, 391.0f };
constexpr float kVolumeTextScale = 1.5f;
constexpr Vector4 kVolumeTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector2 kHelpTextPosition = { 340.0f, 560.0f };
constexpr float kHelpTextScale = 1.0f;
constexpr Vector4 kHelpTextColor = { 0.7f, 0.7f, 0.7f, 1.0f };
}

void OptionsScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) });
    bgSprite_->SetColor(kBackgroundColor);

    fontRenderer_.Initialize(spriteCommon_.get());

    menu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    menu_.BindLayout(kLayoutName, "menu", kMenuPosition, kMenuItemSize);
    menu_.SetItems({
        { "BGM VOLUME" },
        { "SE VOLUME" },
        { "BACK" },
    });

    finished_ = false;
}

void OptionsScene::Finalize()
{
}

void OptionsScene::AdjustVolumeAtCursor(float delta)
{
    const int row = menu_.GetSelectedIndex();
    if (row != kRowBgmVolume && row != kRowSeVolume) {
        return;
    }

    SceneShared::AdjustVolume(audio_, row == kRowBgmVolume, delta);
}

void OptionsScene::Update()
{
    menu_.Update(input_);

    if (input_->TriggerKey(DIK_A) || input_->TriggerKey(DIK_LEFT)) {
        AdjustVolumeAtCursor(-kVolumeStep);
    }
    if (input_->TriggerKey(DIK_D) || input_->TriggerKey(DIK_RIGHT)) {
        AdjustVolumeAtCursor(kVolumeStep);
    }

    if (menu_.ConsumeConfirm(input_) && menu_.GetSelectedIndex() == kRowBack) {
        SceneFlow::GetInstance()->Transition("OPTIONS", "back", "TITLE");
    }
    if (input_->TriggerKey(DIK_ESCAPE) || input_->TriggerKey(DIK_BACKSPACE)) {
        audio_->PlayMenuSelect();
        SceneFlow::GetInstance()->Transition("OPTIONS", "back", "TITLE");
    }
}

void OptionsScene::Draw()
{
    // エディタ表示中はUpdate()が止まるため、文字コマンドの破棄と配置の反映は毎フレーム必ず通るここで行う
    fontRenderer_.Reset();
    UILayout& layout = UILayout::Get(kLayoutName);
    bgSprite_->SetColor(layout.Color("background.color", kBackgroundColor));
    bgSprite_->Update();

    spriteCommon_->CommonDrawSettings();
    bgSprite_->Draw();
    menu_.Draw();

    const float volumeScale = layout.Float("volume.scale", kVolumeTextScale);
    const Vector4 volumeColor = layout.Color("volume.color", kVolumeTextColor);
    const Vector2 bgmPosition = layout.Pos("volume.bgm_pos", kBgmVolumeTextPosition);
    const Vector2 sePosition = layout.Pos("volume.se_pos", kSeVolumeTextPosition);
    const auto& settings = GameSettingsManager::GetInstance()->Get();
    char bgmBuf[32];
    char seBuf[32];
    snprintf(bgmBuf, sizeof(bgmBuf), "< %3d%% >", static_cast<int>(settings.bgmVolume * kPercentScale + kRoundingOffset));
    snprintf(seBuf, sizeof(seBuf), "< %3d%% >", static_cast<int>(settings.seVolume * kPercentScale + kRoundingOffset));
    fontRenderer_.DrawString(bgmBuf, bgmPosition.x, bgmPosition.y, volumeScale, volumeColor);
    fontRenderer_.DrawString(seBuf, sePosition.x, sePosition.y, volumeScale, volumeColor);

    const Vector2 helpPosition = layout.Pos("help.pos", kHelpTextPosition);
    fontRenderer_.DrawString("A/D or Left/Right: adjust   Space: confirm   ESC: back",
        helpPosition.x, helpPosition.y, layout.Float("help.scale", kHelpTextScale), layout.Color("help.color", kHelpTextColor));

    fontRenderer_.Draw();
}
