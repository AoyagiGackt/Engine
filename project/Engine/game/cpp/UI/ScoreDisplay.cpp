/**
 * @file ScoreDisplay.cpp
 * @brief 数値・ランキング一覧を桁ごとのスプライトで描画する（ScoreDisplay）の実装
 */
#include "ScoreDisplay.h"
#include "TextureManager.h"
#include <algorithm>
#include <cassert>
#include <string>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr int kRankColumnDigits = 2; // 順位番号の最大桁数
constexpr float kRankColumnGap = 8.0f;
constexpr float kScoreColumnGap = 16.0f;
constexpr float kDigitGap = 2.0f;
constexpr int kDecimalDigitCount = 10; // 数字テクスチャの種類（0〜9）
constexpr Vector4 kHighlightColor = { 1.0f, 0.9f, 0.2f, 1.0f };
}

// 数字画像のパスを返す
static std::string DigitPath(int digit)
{
    return "Resources/Time/" + std::to_string(digit) + ".png";
}

// 初期化

/** @brief 0〜9 の数字テクスチャを読み込み、プール用のスプライトを初期化する */
void ScoreDisplay::Initialize(SpriteCommon* spriteCommon)
{
    // 全桁テクスチャ (0〜9) をキャッシュに登録しておく
    for (int i = 0; i < kDecimalDigitCount; ++i)
    {
        TextureManager::GetInstance()->LoadTexture(DigitPath(i));
    }

    // スプライトを全て初期化
    for (auto& sp : pool_)
    {
        sp = std::make_unique<Sprite>();
        sp->Initialize(spriteCommon, DigitPath(0));
    }
}

// リセット（毎フレーム Draw 前に呼ぶ）

/** @brief スプライトプールの使用数を0に戻す（毎フレーム描画前に呼ぶ） */
void ScoreDisplay::Reset()
{
    poolUsed_ = 0;
}

// 描画

/** @brief 整数を1桁ずつ数字スプライトで並べて描画する（負の値は0として表示） */
void ScoreDisplay::DrawNumber(int value, Vector2 pos, Vector2 digitSize, float gap, const Vector4& color)
{
    std::string s = std::to_string(value < 0 ? 0 : value);
    float x = pos.x;
    for (char c : s)
    {
        Sprite* sp = AllocSprite();
        if (!sp)
        {
            return;
        }

        int d = c - '0';
        sp->SetTexture(DigitPath(d));
        sp->SetPosition({ x, pos.y });
        sp->SetSize(digitSize);
        sp->SetColor(color);
        sp->Update();
        sp->Draw();

        x += digitSize.x + gap;
    }
}

/** @brief ランキング一覧をスプライトで描画する */
void ScoreDisplay::DrawRanking(const std::vector<int>& ranking,
    int currentScore,
    Vector2 topLeft,
    Vector2 digitSize,
    float rowSpacing,
    int rowsPerColumn,
    float columnSpacing)
{
    rowsPerColumn = (std::max)(rowsPerColumn, 1);
    // 順位番号列の幅（最大2桁 + gap）
    const float rankColW = digitSize.x * kRankColumnDigits + kRankColumnGap;
    // スコア列の開始 X オフセット
    const float scoreOffX = rankColW + kScoreColumnGap;

    for (int i = 0; i < (int)ranking.size(); ++i)
    {
        const float x = topLeft.x + (i / rowsPerColumn) * columnSpacing;
        const float y = topLeft.y + (i % rowsPerColumn) * rowSpacing;

        // 順位番号
        DrawNumber(i + 1, { x, y }, digitSize, kDigitGap);

        // スコア
        bool isCurrentScore = (ranking[i] == currentScore);

        // ハイライト用にスコア描画前のプール位置を記録
        int scoreStartIdx = poolUsed_;
        DrawNumber(ranking[i], { x + scoreOffX, y }, digitSize, kDigitGap);

        // 現在スコアと一致するエントリを黄色でハイライト
        if (isCurrentScore)
        {
            for (int k = scoreStartIdx; k < poolUsed_; ++k)
            {
                pool_[k]->SetColor(kHighlightColor);
            }
        }
    }
}

// プライベート

/** @brief プールから次のスプライトを取り出す（使い切っていれば nullptr） */
Sprite* ScoreDisplay::AllocSprite()
{
    if (poolUsed_ >= kPoolSize)
    {
        return nullptr;
    }
    pool_[poolUsed_]->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
    return pool_[poolUsed_++].get();
}

} // namespace engine::game
