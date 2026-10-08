/**
 * @file ShopScene.cpp
 * @brief ローグライトのスキル選択ショップ画面（ShopScene）の表示と選択処理の実装
 */
#include "ShopScene.h"
#include "GameConstants.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <random>
#include <string>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
constexpr Vector4 kBackgroundColor = { 0.04f, 0.04f, 0.06f, 1.0f };
constexpr Vector2 kCardSize = { 340.0f, 220.0f };
constexpr Vector4 kCardColor = { 0.10f, 0.26f, 0.16f, 1.0f };
constexpr float kDoneDisplaySeconds = 1.5f;

// よく使う文字色
constexpr Vector4 kGreenText = { 0.3f, 1.0f, 0.5f, 1.0f };
constexpr Vector4 kGoldText = { 1.0f, 0.85f, 0.2f, 1.0f };
constexpr Vector4 kGrayText = { 0.5f, 0.5f, 0.5f, 1.0f };

// 見出し・所持金
constexpr Vector2 kTitlePosition = { 530.0f, 22.0f };
constexpr float kTitleScale = 2.4f;
constexpr Vector2 kHpTextPosition = { 20.0f, 28.0f };
constexpr Vector2 kGoldTextPosition = { 20.0f, 52.0f };
constexpr float kStatusTextScale = 1.5f;
constexpr Vector4 kHpTextColor = { 0.3f, 1.0f, 0.4f, 1.0f };

// 取得完了・スキップ表示
constexpr Vector2 kAcquiredTextPosition = { 390.0f, 300.0f };
constexpr Vector2 kAcquiredSkillPosition = { 460.0f, 380.0f };
constexpr Vector2 kSkippedTextPosition = { 460.0f, 340.0f };
constexpr float kResultTextScale = 2.5f;
constexpr float kAcquiredSkillScale = 2.2f;

// 全スキル取得済み表示
constexpr Vector2 kAllAcquiredPosition = { 380.0f, 320.0f };
constexpr float kAllAcquiredScale = 2.2f;
constexpr Vector2 kNextFloorPosition = { 410.0f, 380.0f };
constexpr float kNextFloorScale = 1.6f;
constexpr Vector4 kNextFloorColor = { 0.6f, 0.6f, 0.6f, 1.0f };

// 選択見出し
constexpr Vector2 kPromptPosition = { 420.0f, 110.0f };
constexpr float kPromptScale = 1.8f;
constexpr Vector4 kPromptColor = { 0.85f, 0.85f, 0.85f, 1.0f };

// カード内の文字（カード左上からの位置）
constexpr float kCardTextPaddingX = 10.0f;
constexpr float kCardKeyOffsetY = 8.0f;
constexpr float kCardKeyScale = 2.0f;
constexpr float kCardNameOffsetY = 62.0f;
constexpr float kCardNameScale = 1.7f;
constexpr float kCardDescOffsetY = 110.0f;
constexpr float kCardDescScale = 1.05f;
constexpr Vector4 kCardDescColor = { 0.82f, 0.82f, 0.82f, 1.0f };
constexpr float kCardCodeOffsetY = 185.0f;
constexpr float kCardCodeScale = 0.9f;
constexpr Vector4 kCardCodeColor = { 0.4f, 0.4f, 0.4f, 1.0f };

// 所持スキル一覧
constexpr Vector2 kOwnedLabelPosition = { 20.0f, 638.0f };
constexpr float kOwnedLabelScale = 1.2f;
constexpr Vector4 kOwnedLabelColor = { 0.6f, 0.85f, 1.0f, 1.0f };
constexpr float kOwnedListX = 200.0f;
constexpr float kOwnedListY = 640.0f;
constexpr float kOwnedSkillScale = 1.1f;
constexpr Vector4 kOwnedSkillColor = { 0.9f, 0.85f, 0.3f, 1.0f };

// 操作説明
constexpr Vector2 kHelpPosition = { 20.0f, 688.0f };
constexpr float kHelpScale = 1.05f;
constexpr Vector4 kHelpColor = { 0.42f, 0.42f, 0.42f, 1.0f };
}

void ShopScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });
    bgSprite_->SetColor(kBackgroundColor);

    cardSprite_ = std::make_unique<Sprite>();
    cardSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    cardSprite_->SetSize(kCardSize);

    fontRenderer_.Initialize(spriteCommon_.get());

    // 未所持スキルからランダムに最大3つ選ぶ
    auto* rd = RunData::GetInstance();
    std::vector<RunData::Skill> pool;
    for (int i = 0; i < RunData::kSkillCount; ++i) {
        auto sk = static_cast<RunData::Skill>(i);
        if (!rd->HasSkill(sk)) {
            pool.push_back(sk);
        }
    }

    std::mt19937 rng(static_cast<unsigned>(rd->GetFloor() * 37 + rd->GetSkills().size() * 13 + 7));
    std::shuffle(pool.begin(), pool.end(), rng);

    offerCount_ = (std::min)(3, static_cast<int>(pool.size()));
    for (int i = 0; i < offerCount_; ++i) {
        offered_[i] = pool[i];
    }

    done_ = false;
    doneTimer_ = 0.0f;
    chosen_ = -1;
}

void ShopScene::Finalize()
{
}

void ShopScene::Update()
{
    if (done_) {
        doneTimer_ -= GameConstants::kFrameDeltaTime;
        if (doneTimer_ <= 0.0f) {
            RunData::GetInstance()->AdvanceFloor();
            SceneFlow::GetInstance()->Transition("SHOP", "done", "MAP");
        }
        return;
    }

    if (offerCount_ == 0) {
        done_ = true;
        doneTimer_ = 1.0f;
        return;
    }

    auto* rd = RunData::GetInstance();

    for (int i = 0; i < offerCount_; ++i) {
        if (input_->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) {
            chosen_ = i;
            rd->AddSkill(offered_[i]);
            done_ = true;
            doneTimer_ = kDoneDisplaySeconds;
            return;
        }
    }

    if (input_->TriggerKey(DIK_BACK)) {
        chosen_ = -1;
        done_ = true;
        doneTimer_ = 0.5f;
    }
}

void ShopScene::Draw()
{
    auto* rd = RunData::GetInstance();

    spriteCommon_->CommonDrawSettings();
    bgSprite_->Update();
    bgSprite_->Draw();

    fontRenderer_.Reset();

    // ── タイトル ──
    fontRenderer_.DrawStringW(L"ショップ", kTitlePosition.x, kTitlePosition.y, kTitleScale, kGreenText);

    // HP / ゴールド
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "HP:%d/%d", rd->GetHp(), rd->GetMaxHp());
        fontRenderer_.DrawString(buf, kHpTextPosition.x, kHpTextPosition.y, kStatusTextScale, kHpTextColor);
        snprintf(buf, sizeof(buf), "Gold:%dG", rd->GetGold());
        fontRenderer_.DrawString(buf, kGoldTextPosition.x, kGoldTextPosition.y, kStatusTextScale, kGoldText);
    }

    // ── 完了表示 ──
    if (done_) {
        if (chosen_ >= 0) {
            fontRenderer_.DrawStringW(L"スキルを取得した!", kAcquiredTextPosition.x, kAcquiredTextPosition.y, kResultTextScale, kGreenText);
            fontRenderer_.DrawStringW(RunData::GetSkillText(offered_[chosen_]).nameJp, kAcquiredSkillPosition.x, kAcquiredSkillPosition.y,
                kAcquiredSkillScale, kGoldText);
        } else {
            fontRenderer_.DrawStringW(L"スキップした", kSkippedTextPosition.x, kSkippedTextPosition.y, kResultTextScale, kGrayText);
        }
        fontRenderer_.Draw();
        return;
    }

    // ── 全スキル取得済み ──
    if (offerCount_ == 0) {
        fontRenderer_.DrawStringW(L"全スキルを取得済み!", kAllAcquiredPosition.x, kAllAcquiredPosition.y, kAllAcquiredScale, kGoldText);
        fontRenderer_.DrawStringW(L"次のフロアへ進みます...", kNextFloorPosition.x, kNextFloorPosition.y, kNextFloorScale, kNextFloorColor);
        fontRenderer_.Draw();
        return;
    }

    // ── 選択見出し ──
    fontRenderer_.DrawStringW(L"スキルを1つ選んでください", kPromptPosition.x, kPromptPosition.y, kPromptScale, kPromptColor);

    // ── スキルカード ──
    static constexpr float kCardY = 195.0f;
    static constexpr float kCardGap = 360.0f;
    float startX = (GameConstants::kScreenWidth - kCardGap * (offerCount_ - 1) - kCardSize.x) * 0.5f;

    for (int i = 0; i < offerCount_; ++i) {
        float cx = startX + i * kCardGap;

        // カード背景
        cardSprite_->SetPosition({ cx, kCardY });
        cardSprite_->SetColor(kCardColor);
        cardSprite_->Update();
        cardSprite_->Draw();

        // キーラベル
        char key[4];
        snprintf(key, sizeof(key), "[%d]", i + 1);
        fontRenderer_.DrawString(key, cx + kCardTextPaddingX, kCardY + kCardKeyOffsetY, kCardKeyScale, kGreenText);

        // スキル名（日本語）
        fontRenderer_.DrawStringW(RunData::GetSkillText(offered_[i]).nameJp,
            cx + kCardTextPaddingX, kCardY + kCardNameOffsetY, kCardNameScale, kGoldText);

        // スキル効果説明（日本語）
        fontRenderer_.DrawStringW(RunData::GetSkillText(offered_[i]).descJp,
            cx + kCardTextPaddingX, kCardY + kCardDescOffsetY, kCardDescScale, kCardDescColor);

        // 英語コード名（小さく・補足として）
        const char* eng = RunData::SkillName(offered_[i]);
        fontRenderer_.DrawString(eng, cx + kCardTextPaddingX, kCardY + kCardCodeOffsetY, kCardCodeScale, kCardCodeColor);
    }

    // ── 所持スキル ──
    {
        fontRenderer_.DrawStringW(L"所持スキル:", kOwnedLabelPosition.x, kOwnedLabelPosition.y, kOwnedLabelScale, kOwnedLabelColor);
        if (rd->GetSkills().empty()) {
            fontRenderer_.DrawStringW(L"なし", kOwnedListX, kOwnedLabelPosition.y, kOwnedLabelScale, kGrayText);
        } else {
            float sx = kOwnedListX;
            for (auto sk : rd->GetSkills()) {
                const wchar_t* jn = RunData::GetSkillText(sk).nameJp;
                fontRenderer_.DrawStringW(jn, sx, kOwnedListY, kOwnedSkillScale, kOwnedSkillColor);
                sx += static_cast<float>(wcslen(jn) + 1) * FontRenderer::kJpCharW * kOwnedSkillScale;
            }
        }
    }

    // ── 操作説明 ──
    fontRenderer_.DrawStringW(L"1/2/3キー:スキル選択  Backspaceキー:スキップして次のフロアへ",
        kHelpPosition.x, kHelpPosition.y, kHelpScale, kHelpColor);

    fontRenderer_.Draw();
}
