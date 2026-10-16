/**
 * @file ShopScene.cpp
 * @brief ローグライトのスキル選択ショップ画面（ShopScene）の表示と選択処理の実装
 */
#include "ShopScene.h"
#include "UILayout.h"
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
constexpr float kCardGap = 360.0f;
constexpr float kCardY = 195.0f;
constexpr const char* kLayoutName = "shop";
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
    UILayout& layout = UILayout::Get(kLayoutName);
    const float cardGap = layout.Float("card.gap", kCardGap);
    const float cardY = layout.Float("card.y", kCardY);
    const Vector4 backgroundColor = layout.Color("background.color", kBackgroundColor);
    const Vector2 cardSize = layout.Vec2("card.size", kCardSize);
    const Vector4 cardColor = layout.Color("card.color", kCardColor);
    const Vector4 greenText = layout.Color("text.green", kGreenText);
    const Vector4 goldText = layout.Color("text.gold", kGoldText);
    const Vector4 grayText = layout.Color("text.gray", kGrayText);
    const Vector2 titlePosition = layout.Pos("title.pos", kTitlePosition);
    const float titleScale = layout.Float("title.scale", kTitleScale);
    const Vector2 hpTextPosition = layout.Pos("hp.pos", kHpTextPosition);
    const Vector2 goldTextPosition = layout.Pos("gold.pos", kGoldTextPosition);
    const float statusTextScale = layout.Float("status.scale", kStatusTextScale);
    const Vector4 hpTextColor = layout.Color("hp.color", kHpTextColor);
    const Vector2 acquiredTextPosition = layout.Pos("acquired.pos", kAcquiredTextPosition);
    const Vector2 acquiredSkillPosition = layout.Pos("acquired.skill_pos", kAcquiredSkillPosition);
    const Vector2 skippedTextPosition = layout.Pos("skipped.pos", kSkippedTextPosition);
    const float resultTextScale = layout.Float("result.scale", kResultTextScale);
    const float acquiredSkillScale = layout.Float("acquired.skill_scale", kAcquiredSkillScale);
    const Vector2 allAcquiredPosition = layout.Pos("all_acquired.pos", kAllAcquiredPosition);
    const float allAcquiredScale = layout.Float("all_acquired.scale", kAllAcquiredScale);
    const Vector2 nextFloorPosition = layout.Pos("next_floor.pos", kNextFloorPosition);
    const float nextFloorScale = layout.Float("next_floor.scale", kNextFloorScale);
    const Vector4 nextFloorColor = layout.Color("next_floor.color", kNextFloorColor);
    const Vector2 promptPosition = layout.Pos("prompt.pos", kPromptPosition);
    const float promptScale = layout.Float("prompt.scale", kPromptScale);
    const Vector4 promptColor = layout.Color("prompt.color", kPromptColor);
    const float cardTextPaddingX = layout.Float("card.text_padding_x", kCardTextPaddingX);
    const float cardKeyOffsetY = layout.Float("card.key_offset_y", kCardKeyOffsetY);
    const float cardKeyScale = layout.Float("card.key_scale", kCardKeyScale);
    const float cardNameOffsetY = layout.Float("card.name_offset_y", kCardNameOffsetY);
    const float cardNameScale = layout.Float("card.name_scale", kCardNameScale);
    const float cardDescOffsetY = layout.Float("card.desc_offset_y", kCardDescOffsetY);
    const float cardDescScale = layout.Float("card.desc_scale", kCardDescScale);
    const Vector4 cardDescColor = layout.Color("card.desc_color", kCardDescColor);
    const float cardCodeOffsetY = layout.Float("card.code_offset_y", kCardCodeOffsetY);
    const float cardCodeScale = layout.Float("card.code_scale", kCardCodeScale);
    const Vector4 cardCodeColor = layout.Color("card.code_color", kCardCodeColor);
    const Vector2 ownedLabelPosition = layout.Pos("owned.label_position", kOwnedLabelPosition);
    const float ownedLabelScale = layout.Float("owned.label_scale", kOwnedLabelScale);
    const Vector4 ownedLabelColor = layout.Color("owned.label_color", kOwnedLabelColor);
    const float ownedListX = layout.Float("owned.list_x", kOwnedListX);
    const float ownedListY = layout.Float("owned.list_y", kOwnedListY);
    const float ownedSkillScale = layout.Float("owned.skill_scale", kOwnedSkillScale);
    const Vector4 ownedSkillColor = layout.Color("owned.skill_color", kOwnedSkillColor);
    const Vector2 helpPosition = layout.Pos("help.pos", kHelpPosition);
    const float helpScale = layout.Float("help.scale", kHelpScale);
    const Vector4 helpColor = layout.Color("help.color", kHelpColor);
    auto* rd = RunData::GetInstance();

    spriteCommon_->CommonDrawSettings();
    bgSprite_->SetColor(backgroundColor);
    bgSprite_->Update();
    bgSprite_->Draw();

    fontRenderer_.Reset();

    // ── タイトル ──
    fontRenderer_.DrawStringW(L"ショップ", titlePosition.x, titlePosition.y, titleScale, greenText);

    // HP / ゴールド
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "HP:%d/%d", rd->GetHp(), rd->GetMaxHp());
        fontRenderer_.DrawString(buf, hpTextPosition.x, hpTextPosition.y, statusTextScale, hpTextColor);
        snprintf(buf, sizeof(buf), "Gold:%dG", rd->GetGold());
        fontRenderer_.DrawString(buf, goldTextPosition.x, goldTextPosition.y, statusTextScale, goldText);
    }

    // ── 完了表示 ──
    if (done_) {
        if (chosen_ >= 0) {
            fontRenderer_.DrawStringW(L"スキルを取得した!", acquiredTextPosition.x, acquiredTextPosition.y, resultTextScale, greenText);
            fontRenderer_.DrawStringW(RunData::GetSkillText(offered_[chosen_]).nameJp, acquiredSkillPosition.x, acquiredSkillPosition.y,
                acquiredSkillScale, goldText);
        } else {
            fontRenderer_.DrawStringW(L"スキップした", skippedTextPosition.x, skippedTextPosition.y, resultTextScale, grayText);
        }
        fontRenderer_.Draw();
        return;
    }

    // ── 全スキル取得済み ──
    if (offerCount_ == 0) {
        fontRenderer_.DrawStringW(L"全スキルを取得済み!", allAcquiredPosition.x, allAcquiredPosition.y, allAcquiredScale, goldText);
        fontRenderer_.DrawStringW(L"次のフロアへ進みます...", nextFloorPosition.x, nextFloorPosition.y, nextFloorScale, nextFloorColor);
        fontRenderer_.Draw();
        return;
    }

    // ── 選択見出し ──
    fontRenderer_.DrawStringW(L"スキルを1つ選んでください", promptPosition.x, promptPosition.y, promptScale, promptColor);

    // ── スキルカード ──
    float startX = (GameConstants::kScreenWidth - cardGap * (offerCount_ - 1) - cardSize.x) * 0.5f;

    for (int i = 0; i < offerCount_; ++i) {
        float cx = startX + i * cardGap;

        // カード背景
        cardSprite_->SetSize(cardSize);
        cardSprite_->SetPosition({ cx, cardY });
        cardSprite_->SetColor(cardColor);
        cardSprite_->Update();
        cardSprite_->Draw();

        // キーラベル
        char key[4];
        snprintf(key, sizeof(key), "[%d]", i + 1);
        fontRenderer_.DrawString(key, cx + cardTextPaddingX, cardY + cardKeyOffsetY, cardKeyScale, greenText);

        // スキル名（日本語）
        fontRenderer_.DrawStringW(RunData::GetSkillText(offered_[i]).nameJp,
            cx + cardTextPaddingX, cardY + cardNameOffsetY, cardNameScale, goldText);

        // スキル効果説明（日本語）
        fontRenderer_.DrawStringW(RunData::GetSkillText(offered_[i]).descJp,
            cx + cardTextPaddingX, cardY + cardDescOffsetY, cardDescScale, cardDescColor);

        // 英語コード名（小さく・補足として）
        const char* eng = RunData::SkillName(offered_[i]);
        fontRenderer_.DrawString(eng, cx + cardTextPaddingX, cardY + cardCodeOffsetY, cardCodeScale, cardCodeColor);
    }

    // ── 所持スキル ──
    {
        fontRenderer_.DrawStringW(L"所持スキル:", ownedLabelPosition.x, ownedLabelPosition.y, ownedLabelScale, ownedLabelColor);
        if (rd->GetSkills().empty()) {
            fontRenderer_.DrawStringW(L"なし", ownedListX, ownedLabelPosition.y, ownedLabelScale, grayText);
        } else {
            float sx = ownedListX;
            for (auto sk : rd->GetSkills()) {
                const wchar_t* jn = RunData::GetSkillText(sk).nameJp;
                fontRenderer_.DrawStringW(jn, sx, ownedListY, ownedSkillScale, ownedSkillColor);
                sx += static_cast<float>(wcslen(jn) + 1) * FontRenderer::kJpCharW * ownedSkillScale;
            }
        }
    }

    // ── 操作説明 ──
    fontRenderer_.DrawStringW(L"1/2/3キー:スキル選択  Backspaceキー:スキップして次のフロアへ",
        helpPosition.x, helpPosition.y, helpScale, helpColor);

    fontRenderer_.Draw();
}
