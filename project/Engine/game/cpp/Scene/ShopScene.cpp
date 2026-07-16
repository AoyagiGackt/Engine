#include "ShopScene.h"
#include "GameConstants.h"
#include "SceneManager.h"
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <random>
#include <string>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// スキルの日本語名
static const wchar_t* SkillNameJP(RunData::Skill s)
{
    switch (s) {
    case RunData::Skill::BlinkPlus:
        return L"ブリンク強化";
    case RunData::Skill::ComboExtend:
        return L"コンボ延長";
    case RunData::Skill::FastFire:
        return L"速射";
    case RunData::Skill::AwakenBoost:
        return L"覚醒促進";
    case RunData::Skill::SpeedUp:
        return L"疾走";
    case RunData::Skill::HighJump:
        return L"跳躍強化";
    case RunData::Skill::JuggleExtend:
        return L"乱舞強化";
    case RunData::Skill::StylePersist:
        return L"スタイル維持";
    default:
        return L"？？？";
    }
}

// スキルの日本語説明
static const wchar_t* SkillDescJP(RunData::Skill s)
{
    switch (s) {
    case RunData::Skill::BlinkPlus:
        return L"ブリンク距離が 1.5倍になる";
    case RunData::Skill::ComboExtend:
        return L"コンボ最大数が 1段階増加";
    case RunData::Skill::FastFire:
        return L"弾の連射速度が 2倍になる";
    case RunData::Skill::AwakenBoost:
        return L"覚醒ゲージの蓄積速度が 1.5倍";
    case RunData::Skill::SpeedUp:
        return L"移動速度が 1.2倍になる";
    case RunData::Skill::HighJump:
        return L"ジャンプ力が 1.25倍になる";
    case RunData::Skill::JuggleExtend:
        return L"乱舞スラッシュ回数が 4回増加";
    case RunData::Skill::StylePersist:
        return L"スタイルメーターの減衰が 0.6倍";
    default:
        return L"";
    }
}

void ShopScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });
    bgSprite_->SetColor({ 0.04f, 0.04f, 0.06f, 1.0f });

    cardSprite_ = std::make_unique<Sprite>();
    cardSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    cardSprite_->SetSize({ 340.0f, 220.0f });

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
            SceneManager::GetInstance()->ChangeScene("MAP");
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
            doneTimer_ = 1.5f;
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
    fontRenderer_.DrawStringW(L"ショップ", 530.0f, 22.0f, 2.4f, { 0.3f, 1.0f, 0.5f, 1.0f });

    // HP / ゴールド
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "HP:%d/%d", rd->GetHp(), rd->GetMaxHp());
        fontRenderer_.DrawString(buf, 20.0f, 28.0f, 1.5f, { 0.3f, 1.0f, 0.4f, 1.0f });
        snprintf(buf, sizeof(buf), "Gold:%dG", rd->GetGold());
        fontRenderer_.DrawString(buf, 20.0f, 52.0f, 1.5f, { 1.0f, 0.85f, 0.2f, 1.0f });
    }

    // ── 完了表示 ──
    if (done_) {
        if (chosen_ >= 0) {
            fontRenderer_.DrawStringW(L"スキルを取得した!", 390.0f, 300.0f, 2.5f, { 0.3f, 1.0f, 0.5f, 1.0f });
            fontRenderer_.DrawStringW(SkillNameJP(offered_[chosen_]), 460.0f, 380.0f, 2.2f, { 1.0f, 0.85f, 0.2f, 1.0f });
        } else {
            fontRenderer_.DrawStringW(L"スキップした", 460.0f, 340.0f, 2.5f, { 0.5f, 0.5f, 0.5f, 1.0f });
        }
        fontRenderer_.Draw();
        return;
    }

    // ── 全スキル取得済み ──
    if (offerCount_ == 0) {
        fontRenderer_.DrawStringW(L"全スキルを取得済み!", 380.0f, 320.0f, 2.2f, { 1.0f, 0.85f, 0.2f, 1.0f });
        fontRenderer_.DrawStringW(L"次のフロアへ進みます...", 410.0f, 380.0f, 1.6f, { 0.6f, 0.6f, 0.6f, 1.0f });
        fontRenderer_.Draw();
        return;
    }

    // ── 選択見出し ──
    fontRenderer_.DrawStringW(L"スキルを1つ選んでください", 420.0f, 110.0f, 1.8f, { 0.85f, 0.85f, 0.85f, 1.0f });

    // ── スキルカード ──
    static constexpr float kCardY = 195.0f;
    static constexpr float kCardGap = 360.0f;
    float startX = (GameConstants::kScreenWidth - kCardGap * (offerCount_ - 1) - 340.0f) * 0.5f;

    for (int i = 0; i < offerCount_; ++i) {
        float cx = startX + i * kCardGap;

        // カード背景
        cardSprite_->SetPosition({ cx, kCardY });
        cardSprite_->SetColor({ 0.10f, 0.26f, 0.16f, 1.0f });
        cardSprite_->Update();
        cardSprite_->Draw();

        // キーラベル
        char key[4];
        snprintf(key, sizeof(key), "[%d]", i + 1);
        fontRenderer_.DrawString(key, cx + 10.0f, kCardY + 8.0f, 2.0f, { 0.3f, 1.0f, 0.5f, 1.0f });

        // スキル名（日本語）
        fontRenderer_.DrawStringW(SkillNameJP(offered_[i]),
            cx + 10.0f, kCardY + 62.0f, 1.7f, { 1.0f, 0.85f, 0.2f, 1.0f });

        // スキル効果説明（日本語）
        fontRenderer_.DrawStringW(SkillDescJP(offered_[i]),
            cx + 10.0f, kCardY + 110.0f, 1.05f, { 0.82f, 0.82f, 0.82f, 1.0f });

        // 英語コード名（小さく・補足として）
        const char* eng = RunData::SkillName(offered_[i]);
        fontRenderer_.DrawString(eng, cx + 10.0f, kCardY + 185.0f, 0.9f, { 0.4f, 0.4f, 0.4f, 1.0f });
    }

    // ── 所持スキル ──
    {
        fontRenderer_.DrawStringW(L"所持スキル:", 20.0f, 638.0f, 1.2f, { 0.6f, 0.85f, 1.0f, 1.0f });
        if (rd->GetSkills().empty()) {
            fontRenderer_.DrawStringW(L"なし", 200.0f, 638.0f, 1.2f, { 0.5f, 0.5f, 0.5f, 1.0f });
        } else {
            float sx = 200.0f;
            for (auto sk : rd->GetSkills()) {
                const wchar_t* jn = SkillNameJP(sk);
                fontRenderer_.DrawStringW(jn, sx, 640.0f, 1.1f, { 0.9f, 0.85f, 0.3f, 1.0f });
                sx += static_cast<float>(wcslen(jn) + 1) * FontRenderer::kJpCharW * 1.1f;
            }
        }
    }

    // ── 操作説明 ──
    fontRenderer_.DrawStringW(L"1/2/3キー:スキル選択  Backspaceキー:スキップして次のフロアへ",
        20.0f, 688.0f, 1.05f, { 0.42f, 0.42f, 0.42f, 1.0f });

    fontRenderer_.Draw();
}
