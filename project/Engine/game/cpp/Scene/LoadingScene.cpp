/**
 * @file LoadingScene.cpp
 * @brief 非同期シーンロード中の進捗バー・ドット演出表示と遷移先への切替（LoadingScene）の実装
 */
#include "LoadingScene.h"
#include "GameConstants.h"
#include "SceneManager.h"
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr Vector4 kDotIdleColor = { 1.0f, 1.0f, 1.0f, 0.25f };
constexpr Vector4 kDotActiveColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector2 kProgressBarPosition = { 390.0f, 450.0f };
constexpr Vector2 kProgressBarSize = { 500.0f, 12.0f };
constexpr Vector4 kProgressBackgroundColor = { 0.18f, 0.18f, 0.22f, 1.0f };
constexpr Vector4 kProgressForegroundColor = { 0.25f, 0.75f, 1.0f, 1.0f };
constexpr float kDotsCenterY = 400.0f;
}

void LoadingScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_ = dxCommon;

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    // 黒背景（white.png を黒く塗る）
    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });
    bgSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });

    // 3 つのドット（画面中央下あたり）
    static constexpr float kDotSize = 20.0f;
    static constexpr float kSpacing = 35.0f;
    static constexpr float kCenterX = GameConstants::kScreenCenterX - kDotSize / 2.0f;
    static constexpr float kCenterY = kDotsCenterY - kDotSize / 2.0f;

    for (int i = 0; i < kDotCount; ++i) {
        dotSprites_[i] = std::make_unique<Sprite>();
        dotSprites_[i]->Initialize(spriteCommon_.get(), "Resources/white.png");
        dotSprites_[i]->SetPosition({ kCenterX + (i - 1) * kSpacing, kCenterY });
        dotSprites_[i]->SetSize({ kDotSize, kDotSize });
        dotSprites_[i]->SetColor(kDotIdleColor); // 暗め
    }
    dotSprites_[0]->SetColor(kDotActiveColor); // 最初のドットだけ明るい

    progressBg_ = std::make_unique<Sprite>();
    progressBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    progressBg_->SetPosition(kProgressBarPosition);
    progressBg_->SetSize(kProgressBarSize);
    progressBg_->SetColor(kProgressBackgroundColor);

    progressFg_ = std::make_unique<Sprite>();
    progressFg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    progressFg_->SetPosition(kProgressBarPosition);
    progressFg_->SetSize({ 0.0f, kProgressBarSize.y });
    progressFg_->SetColor(kProgressForegroundColor);

    timer_ = 0.0f;
    dotTimer_ = 0.0f;
    activeDot_ = 0;
    sceneChangeRequested_ = false;
    failureHandled_ = false;
}

void LoadingScene::Update()
{
    constexpr float dt = GameConstants::kFrameDeltaTime;
    timer_ += dt;
    dotTimer_ += dt;

    // ドットアニメーション
    if (dotTimer_ >= kDotInterval) {
        dotTimer_ -= kDotInterval;
        dotSprites_[activeDot_]->SetColor(kDotIdleColor);
        activeDot_ = (activeDot_ + 1) % kDotCount;
        dotSprites_[activeDot_]->SetColor(kDotActiveColor);
    }

    bgSprite_->Update();
    for (int i = 0; i < kDotCount; ++i) {
        dotSprites_[i]->Update();
    }
    const float progress = SceneManager::GetInstance()->GetAsyncLoadProgress();
    progressBg_->Update();
    progressFg_->SetSize({ kProgressBarSize.x * progress, kProgressBarSize.y });
    progressFg_->Update();

    // 例外発生時にロード画面で永久待機せず、ログを残してタイトルへ戻す
    if (!failureHandled_ && SceneManager::GetInstance()->HasAsyncLoadFailed()) {
        failureHandled_ = true;
        SceneManager::GetInstance()->ChangeScene("TITLE");
        return;
    }

    // 最低表示時間が過ぎ、かつバックグラウンドロードが完了したら次のシーンへ（一度だけ）
    if (!sceneChangeRequested_ && timer_ >= kMinDisplayTime
        && SceneManager::GetInstance()->IsAsyncLoadReady()) {
        sceneChangeRequested_ = true;
        const std::string& target = SceneManager::GetInstance()->GetLoadingTarget();
        if (!target.empty()) {
            SceneManager::GetInstance()->ChangeScene(target);
        }
    }
}

void LoadingScene::Draw()
{
    spriteCommon_->CommonDrawSettings();
    bgSprite_->Draw();
    for (int i = 0; i < kDotCount; ++i) {
        dotSprites_[i]->Draw();
    }
    progressBg_->Draw();
    progressFg_->Draw();
}

void LoadingScene::Finalize()
{
}
