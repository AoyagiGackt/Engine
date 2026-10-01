/**
 * @file OptionsScene.cpp
 * @brief オプション画面（BGM/SE音量調整）の入力処理と描画（OptionsScene）の実装
 */
#include "OptionsScene.h"
#include "GameSettings.h"
#include "SceneFlow.h"
#include "WinApp.h"
#include <algorithm>
#include <cstdio>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr float kVolumeStep = 0.1f;
constexpr Vector4 kBackgroundColor = { 0.05f, 0.05f, 0.08f, 1.0f };
constexpr float kMenuX = 440.0f;
constexpr float kMenuY = 300.0f;
constexpr float kMenuWidth = 400.0f;
constexpr float kMenuItemHeight = 70.0f;
constexpr float kPercentScale = 100.0f;
constexpr float kRoundingOffset = 0.5f;
constexpr float kVolumeTextX = 900.0f;
constexpr float kBgmVolumeTextY = 321.0f;
constexpr float kSeVolumeTextY = 391.0f;
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
    menu_.SetLayout(kMenuX, kMenuY, kMenuWidth, kMenuItemHeight);
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

    GameSettings& settings = GameSettingsManager::GetInstance()->Get();
    const float previousVolume = row == kRowBgmVolume ? settings.bgmVolume : settings.seVolume;
    if (row == kRowBgmVolume) {
        settings.bgmVolume = std::clamp(settings.bgmVolume + delta, 0.0f, 1.0f);
        audio_->SetBGMVolume(settings.bgmVolume);
    } else {
        settings.seVolume = std::clamp(settings.seVolume + delta, 0.0f, 1.0f);
        audio_->SetSEVolume(settings.seVolume);
    }
    const float currentVolume = row == kRowBgmVolume ? settings.bgmVolume : settings.seVolume;
    if (currentVolume != previousVolume) {
        audio_->PlayMenuChoice();
        GameSettingsManager::GetInstance()->Save();
    }
}

void OptionsScene::Update()
{
    fontRenderer_.Reset();

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

    bgSprite_->Update();
}

void OptionsScene::Draw()
{
    spriteCommon_->CommonDrawSettings();
    bgSprite_->Draw();
    menu_.Draw();

    const auto& settings = GameSettingsManager::GetInstance()->Get();
    char bgmBuf[32];
    char seBuf[32];
    snprintf(bgmBuf, sizeof(bgmBuf), "< %3d%% >", static_cast<int>(settings.bgmVolume * kPercentScale + kRoundingOffset));
    snprintf(seBuf, sizeof(seBuf), "< %3d%% >", static_cast<int>(settings.seVolume * kPercentScale + kRoundingOffset));
    fontRenderer_.DrawString(bgmBuf, kVolumeTextX, kBgmVolumeTextY, kVolumeTextScale, kVolumeTextColor);
    fontRenderer_.DrawString(seBuf, kVolumeTextX, kSeVolumeTextY, kVolumeTextScale, kVolumeTextColor);
    fontRenderer_.DrawString("A/D or Left/Right: adjust   Space: confirm   ESC: back",
        kHelpTextPosition.x, kHelpTextPosition.y, kHelpTextScale, kHelpTextColor);

    fontRenderer_.Draw();
}
