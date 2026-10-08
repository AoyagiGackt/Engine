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
#include "UILayout.h"
#include "WeaponManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
// 既定の配置（F2のエディタで動かした値は Resources/Config/UI/gameover.json に保存される）
constexpr const char* kLayoutName = "gameover";
constexpr Vector4 kOverlayColor = { 0.0f, 0.0f, 0.0f, 0.85f };
constexpr Vector2 kMenuPosition = { 440.0f, 310.0f };
constexpr Vector2 kMenuItemSize = { 400.0f, 60.0f };
constexpr float kMenuLabelScale = 2.0f;

constexpr Vector2 kTitleTextPosition = { 496.0f, 170.0f }; // 文言の幅から求めた画面中央
constexpr float kTitleTextScale = 4.0f;
constexpr Vector4 kTitleTextColor = { 1.0f, 0.25f, 0.25f, 1.0f };
constexpr Vector2 kHintTextPosition = { 524.0f, 480.0f }; // 文言の幅から求めた画面中央
constexpr float kHintTextScale = 1.0f;
constexpr Vector4 kHintTextColor = { 0.8f, 0.8f, 0.8f, 1.0f };
constexpr const wchar_t* kTitleText = L"GAME OVER";
constexpr const wchar_t* kHintText = L"W/S: 選択   Space/Enter: 決定";
constexpr const wchar_t* kHintTextGamepad = L"十字キー/スティック: 選択   A: 決定";

constexpr int kRowRestart = 0;
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

    fontRenderer_.Initialize(spriteCommon_.get());

    UIMenuStyle menuStyle;
    menuStyle.labelScale = kMenuLabelScale;
    menuStyle.centerLabels = true;
    menu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    menu_.BindLayout(kLayoutName, "menu", kMenuPosition, kMenuItemSize, menuStyle);
    menu_.SetItems({ { "リスタート" }, { "タイトルに戻る" } });

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
    menu_.Update(input_);
    if (!menu_.ConsumeConfirm(input_)) {
        return;
    }
    if (menu_.GetSelectedIndex() == kRowRestart) {
        // リスタートHP0のままGAMEPLAYへ戻ると即ゲームオーバーになるため、新規ランとして開始し直す
        RunData::GetInstance()->StartNewRun();
        WeaponManager::GetInstance()->ResetForNewRun();
        SceneFlow::GetInstance()->Transition("GAMEOVER", "restart", "MAP");
    } else {
        SceneFlow::GetInstance()->Transition("GAMEOVER", "title", "TITLE");
    }
}

// 描画

void GameOverScene::Draw()
{
    // エディタ表示中はUpdate()が止まるため、文字コマンドの破棄と配置の反映は毎フレーム必ず通るここで行う
    fontRenderer_.Reset();
    UILayout& layout = UILayout::Get(kLayoutName);
    overlay_->SetColor(layout.Color("background.color", kOverlayColor));
    overlay_->Update();

    spriteCommon_->CommonDrawSettings();
    overlay_->Draw();
    menu_.Draw();

    const Vector2 titlePosition = layout.Pos("title.pos", kTitleTextPosition);
    fontRenderer_.DrawStringW(kTitleText, titlePosition.x, titlePosition.y,
        layout.Float("title.scale", kTitleTextScale), layout.Color("title.color", kTitleTextColor));
    const Vector2 hintPosition = layout.Pos("hint.pos", kHintTextPosition);
    fontRenderer_.DrawStringW(input_->IsUsingGamepad() ? kHintTextGamepad : kHintText, hintPosition.x, hintPosition.y,
        layout.Float("hint.scale", kHintTextScale), layout.Color("hint.color", kHintTextColor));
    fontRenderer_.Draw();
}
