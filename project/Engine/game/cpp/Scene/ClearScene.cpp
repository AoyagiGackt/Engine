/**
 * @file ClearScene.cpp
 * @brief ゲームクリア画面の表示・スコア確定・タイトルへの遷移（ClearScene）の実装
 */
#include "ClearScene.h"
#include "GameConstants.h"
#include "ImGuiManager.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "UILayout.h"
#include <string>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// 既定の配置（F2のエディタで動かした値は Resources/Config/UI/clear.json に保存される）
static constexpr const char* kLayoutName = "clear";

// "SCORE" ラベル
static constexpr Vector2 kScoreLabelPos = { 490.f, 130.f }; // 左上座標
static constexpr Vector2 kScoreLabelSize = { 300.f, 80.f }; // 表示サイズ

// 現在スコア数字（Xは桁数に合わせて画面中央へ揃える）
static constexpr Vector2 kScoreDigitSize = { 70.f, 100.f };
static constexpr float kScoreDigitGap = 5.f;
static constexpr float kScoreY = 230.f;
static constexpr Vector4 kBackgroundColor = { 1.0f, 1.0f, 1.0f, 1.0f };

// "RANKING" ラベル
static constexpr Vector2 kRankLabelPos = { 470.f, 345.f };
static constexpr Vector2 kRankLabelSize = { 340.f, 60.f };

// ランキング数字
static constexpr Vector2 kRankDigitSize = { 36.f, 52.f };
static constexpr float kRankDigitGap = 3.f;
static constexpr float kRankRowSpacing = 60.f;
static constexpr Vector2 kRankPosition = { 430.f, 420.f };

// 初期化

void ClearScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    // 背景（白）
    clearSprite_ = std::make_unique<Sprite>();
    clearSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    clearSprite_->SetPosition({ 0.0f, 0.0f });
    clearSprite_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });

    // "SCORE" ラベル
    scoreLabel_ = std::make_unique<Sprite>();
    scoreLabel_->Initialize(spriteCommon_.get(), "Resources/score/score.png");
    scoreLabel_->SetPosition(kScoreLabelPos);
    scoreLabel_->SetSize(kScoreLabelSize);

    // "RANKING" ラベル
    rankingLabel_ = std::make_unique<Sprite>();
    rankingLabel_->Initialize(spriteCommon_.get(), "Resources/ranking/ranking.png");
    rankingLabel_->SetPosition(kRankLabelPos);
    rankingLabel_->SetSize(kRankLabelSize);

    // スコア数字表示
    scoreDisplay_.Initialize(spriteCommon_.get());

    ScoreManager::GetInstance()->SubmitAndSave();

    // ローグライトのランを完走した場合のみ通算記録へ反映する（サンドボックステスト経由は対象外）
    auto* rd = RunData::GetInstance();
    if (rd->IsRunActive()) {
        SaveDataManager::GetInstance()->RecordRunResult(true, rd->GetFloor(), rd->GetGold());
    }
}

// 終了

void ClearScene::Finalize()
{
}

// 更新

void ClearScene::Update()
{
    if (input_->TriggerKey(DIK_SPACE)) {
        SceneFlow::GetInstance()->Transition("CLEAR", "title", "TITLE");
    }

    DrawScoreUI();
}

// 描画

void ClearScene::Draw()
{
    // エディタ表示中はUpdate()が止まるため、配置の反映は毎フレーム必ず通るここで行う
    UILayout& layout = UILayout::Get(kLayoutName);
    clearSprite_->SetColor(layout.Color("background.color", kBackgroundColor));
    clearSprite_->Update();
    scoreLabel_->SetPosition(layout.Pos("score_label.pos", kScoreLabelPos));
    scoreLabel_->SetSize(layout.Vec2("score_label.size", kScoreLabelSize));
    scoreLabel_->Update();
    rankingLabel_->SetPosition(layout.Pos("ranking_label.pos", kRankLabelPos));
    rankingLabel_->SetSize(layout.Vec2("ranking_label.size", kRankLabelSize));
    rankingLabel_->Update();

    spriteCommon_->CommonDrawSettings();

    // 背景
    clearSprite_->Draw();

    // "SCORE" ラベル
    scoreLabel_->Draw();

    // 現在スコア数字（中央揃え）
    scoreDisplay_.Reset();
    {
        int currentScore = ScoreManager::GetInstance()->GetCurrentScore();
        std::string s = std::to_string(currentScore < 0 ? 0 : currentScore);
        const Vector2 digitSize = layout.Vec2("score.digit_size", kScoreDigitSize);
        const float digitGap = layout.Float("score.digit_gap", kScoreDigitGap);
        const float scoreY = layout.Float("score.y", kScoreY);
        float totalW = s.size() * (digitSize.x + digitGap) - digitGap;
        float startX = (GameConstants::kScreenWidth - totalW) * 0.5f;
        scoreDisplay_.DrawNumber(currentScore, { startX, scoreY }, digitSize, digitGap);
    }

    // "RANKING" ラベル
    rankingLabel_->Draw();

    // ランキング数字
    const auto& ranking = ScoreManager::GetInstance()->GetRanking();
    scoreDisplay_.DrawRanking(ranking,
        ScoreManager::GetInstance()->GetCurrentScore(),
        layout.Pos("ranking.pos", kRankPosition),
        layout.Vec2("ranking.digit_size", kRankDigitSize), layout.Float("ranking.row_spacing", kRankRowSpacing));
}

// デバッグ UI（ImGui）

void ClearScene::DrawScoreUI()
{
#ifdef USE_IMGUI
    if (!imguiManager_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(300, 120), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::Begin("Diagnostics", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Score : %d", ScoreManager::GetInstance()->GetCurrentScore());
    ImGui::Separator();
    if (ImGui::Button("Reset All Scores")) {
        ScoreManager::GetInstance()->ResetAllScores();
    }
    ImGui::Separator();
    ImGui::Text("Press SPACE -> Title");
    ImGui::End();
#endif
}
