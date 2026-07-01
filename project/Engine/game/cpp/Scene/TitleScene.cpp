#include "TitleScene.h"
#include "SceneManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

void TitleScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_ = dxCommon;
    input_    = input;
    audio_    = audio;

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    titleSprite_ = std::make_unique<Sprite>();
    titleSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    titleSprite_->SetPosition({ 0.0f, 0.0f });
    titleSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) });

    titleTextSprite_ = std::make_unique<Sprite>();
    titleTextSprite_->Initialize(spriteCommon_.get(), "Resources/title/title.png");
    titleTextSprite_->SetPosition({ 0.0f, 0.0f });
    titleTextSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight) });

    fontRenderer_.Initialize(spriteCommon_.get());
}

void TitleScene::Update()
{
    fontRenderer_.Reset();

    if (input_->TriggerKey(DIK_SPACE) || input_->TriggerKey(DIK_RETURN)) {
        SceneManager::GetInstance()->ChangeScene("TRAINING", 0.4f, 0.4f);
    }

    titleSprite_->Update();
    titleTextSprite_->Update();

    fontRenderer_.DrawStringW(L"スペースキー  トレーニング開始",
        380.0f, 580.0f, 1.8f, { 0.1f, 0.1f, 0.1f, 0.85f });
}

void TitleScene::Draw()
{
    spriteCommon_->CommonDrawSettings();
    titleSprite_->Draw();
    titleTextSprite_->Draw();
    fontRenderer_.Draw();
}

void TitleScene::Finalize()
{
}
