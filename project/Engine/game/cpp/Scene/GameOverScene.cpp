/**
 * @file GameOverScene.cpp
 * @brief ゲームオーバー画面の選択（リスタート/タイトル）と遷移処理（GameOverScene）の実装
 */
#include "GameOverScene.h"
#include "GameConstants.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "WeaponManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr Vector4 kOverlayColor = { 0.0f, 0.0f, 0.0f, 0.85f };
constexpr Vector2 kRestartOptionPosition = { 440.0f, 310.0f };
constexpr Vector2 kTitleOptionPosition = { 440.0f, 390.0f };
constexpr Vector2 kOptionSize = { 400.0f, 60.0f };
constexpr Vector4 kSelectedOptionColor = { 0.2f, 0.8f, 0.2f, 0.9f };
constexpr Vector4 kIdleOptionColor = { 0.4f, 0.4f, 0.4f, 0.7f };
}

// 初期化

void GameOverScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    // 半透明の黒背景
    overlay_ = std::make_unique<Sprite>();
    overlay_->Initialize(spriteCommon_.get(), "Resources/white.png");
    overlay_->SetPosition({ 0.0f, 0.0f });
    overlay_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });
    overlay_->SetColor(kOverlayColor);

    // 選択肢1: リスタート
    option1_ = std::make_unique<Sprite>();
    option1_->Initialize(spriteCommon_.get(), "Resources/white.png");
    option1_->SetPosition(kRestartOptionPosition);
    option1_->SetSize(kOptionSize);

    // 選択肢2: タイトルに戻る
    option2_ = std::make_unique<Sprite>();
    option2_->Initialize(spriteCommon_.get(), "Resources/white.png");
    option2_->SetPosition(kTitleOptionPosition);
    option2_->SetSize(kOptionSize);

    cursor_ = 0;

    // ローグライトのラン中に力尽きた場合のみ通算記録へ反映し、コンティニューデータを破棄する
    auto* rd = RunData::GetInstance();
    if (rd->IsRunActive()) {
        SaveDataManager::GetInstance()->RecordRunResult(false, rd->GetFloor(), rd->GetGold());
        SaveDataManager::GetInstance()->ClearContinue();
    }
}

// 終了

void GameOverScene::Finalize()
{
}

// 更新

void GameOverScene::Update()
{
    // カーソル移動
    if (input_->TriggerKey(DIK_W)) {
        cursor_ = 0;
    }
    if (input_->TriggerKey(DIK_S)) {
        cursor_ = 1;
    }

    // 決定
    if (input_->TriggerKey(DIK_SPACE) || input_->TriggerKey(DIK_RETURN)) {
        if (cursor_ == 0) {
            // リスタートHP0のままGAMEPLAYへ戻ると即ゲームオーバーになるため、新規ランとして開始し直す
            RunData::GetInstance()->StartNewRun();
            WeaponManager::GetInstance()->Reset();
            SceneFlow::GetInstance()->Transition("GAMEOVER", "restart", "MAP");
        } else {
            SceneFlow::GetInstance()->Transition("GAMEOVER", "title", "TITLE");
        }
    }

    // 選択中=緑、非選択=グレー
    option1_->SetColor(cursor_ == 0 ? kSelectedOptionColor : kIdleOptionColor);
    option2_->SetColor(cursor_ == 1 ? kSelectedOptionColor : kIdleOptionColor);

    overlay_->Update();
    option1_->Update();
    option2_->Update();
}

// 描画

void GameOverScene::Draw()
{
    spriteCommon_->CommonDrawSettings();
    overlay_->Draw();
    option1_->Draw();
    option2_->Draw();
}
