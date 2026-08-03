/**
 * @file TitleScene.cpp
 * @brief タイトル画面のメニュー表示と入力待ち・シーン遷移（TitleScene）の実装
 */
#include "TitleScene.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "WeaponManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

void TitleScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    titleSprite_ = std::make_unique<Sprite>();
    titleSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    titleSprite_->SetPosition({ 0.0f, 0.0f });
    titleSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) });

    titleTextSprite_ = std::make_unique<Sprite>();
    titleTextSprite_->Initialize(spriteCommon_.get(), "Resources/title/title.png");
    titleTextSprite_->SetPosition({ 0.0f, 0.0f });
    titleTextSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) });

    fontRenderer_.Initialize(spriteCommon_.get());

    menu_.Initialize(spriteCommon_.get(), &fontRenderer_);
    menu_.SetLayout(440.0f, 520.0f, 400.0f, 60.0f);
    menu_.SetItems({
        { "NEW GAME" },
        { "CONTINUE", SaveDataManager::GetInstance()->HasContinue() },
        { "TRAINING" },
    });
}

void TitleScene::Update()
{
    fontRenderer_.Reset();

    menu_.Update(input_);
    if (menu_.ConsumeConfirm(input_)) {
        switch (menu_.GetSelectedIndex()) {
        case 0: // NEW GAME
            RunData::GetInstance()->StartNewRun();
            WeaponManager::GetInstance()->Reset();
            SaveDataManager::GetInstance()->ClearContinue();
            SceneManager::GetInstance()->ChangeScene("MAP", 0.15f, 0.2f);
            break;
        case 1: // CONTINUE
            SaveDataManager::GetInstance()->LoadContinue(*RunData::GetInstance());
            SceneManager::GetInstance()->ChangeScene("MAP", 0.15f, 0.2f);
            break;
        case 2: // TRAINING
            SceneManager::GetInstance()->ChangeScene("TRAINING", 0.4f, 0.4f);
            break;
        default:
            break;
        }
    }

    titleSprite_->Update();
    titleTextSprite_->Update();
}

void TitleScene::Draw()
{
    spriteCommon_->CommonDrawSettings();
    titleSprite_->Draw();
    titleTextSprite_->Draw();
    menu_.Draw();
    fontRenderer_.Draw();
}

void TitleScene::Finalize()
{
}
