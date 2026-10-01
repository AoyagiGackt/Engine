/**
 * @file StyleMeter.cpp
 * @brief スタイリッシュランク（D〜SSS）の採点処理とHUD描画（StyleMeter）の実装
 */
#include "StyleMeter.h"
#include "Easing.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
using namespace engine;
using namespace engine::game;

namespace {

struct RankDef {
    const char* letter;
    float threshold; ///< このポイント以上でこのランク
    Vector4 color;
};
constexpr RankDef kRanks[StyleMeter::kRankCount] = {
    { "D", 0.0f, { 0.55f, 0.55f, 0.55f, 1.0f } },
    { "C", 120.0f, { 0.85f, 0.85f, 0.85f, 1.0f } },
    { "B", 270.0f, { 0.30f, 0.72f, 1.00f, 1.0f } },
    { "A", 440.0f, { 0.20f, 1.00f, 0.40f, 1.0f } },
    { "S", 620.0f, { 1.00f, 0.90f, 0.10f, 1.0f } },
    { "SS", 790.0f, { 1.00f, 0.55f, 0.10f, 1.0f } },
    { "SSS", 940.0f, { 1.00f, 0.30f, 0.30f, 1.0f } },
};

// HUD レイアウト（右上アンカー）
constexpr float kRightEdge = 1256.0f;
constexpr float kRankY = 56.0f;
constexpr float kRankScale = 5.0f;
constexpr float kBarW = 190.0f;
constexpr float kBarH = 7.0f;
constexpr float kBarOffsetY = 84.0f; // ランク文字の上端から進捗バーまでの距離
constexpr Vector4 kBarBackgroundColor = { 0.08f, 0.08f, 0.12f, 0.8f };
constexpr float kBarForegroundAlpha = 0.95f;
constexpr float kHudFadeSpeed = 2.0f; // チェーンが切れてポイントも尽きた後にHUDを消す速さ

// ランク変動の演出
constexpr float kRankUpFlashSeconds = 0.35f;
constexpr float kRankDownFlashSeconds = 0.15f;
constexpr float kRankPopMinScale = 0.75f; // 弾み始めの大きさ（通常サイズ比）
constexpr float kRankWhiteFlashStart = 0.2f; // 演出の残り時間がこれ以上の間だけ白く光らせる

// ヒットチェーン数の表示
constexpr float kHitPopSeconds = 0.18f;
constexpr int kHitHighlightCount = 10; // この数以上で強調表示する
constexpr float kHitScaleBase = 1.8f;
constexpr float kHitScalePerHit = 0.035f;
constexpr float kHitScaleMaxGrowth = 0.7f;
constexpr float kHitPopScale = 0.5f;
constexpr float kHitTextOffsetY = 100.0f;
constexpr Vector3 kHitHighlightColor = { 1.0f, 0.75f, 0.18f };
constexpr float kBestChainOffsetY = 130.0f;
constexpr float kBestChainScale = 1.1f;
constexpr float kBestChainGray = 0.7f;
constexpr float kBestChainAlpha = 0.8f;

} // namespace

void StyleMeter::Initialize(SpriteCommon* spriteCommon)
{
    barBg_ = std::make_unique<Sprite>();
    barBg_->Initialize(spriteCommon, "Resources/white.png");
    barBg_->SetColor(kBarBackgroundColor);

    barFg_ = std::make_unique<Sprite>();
    barFg_->Initialize(spriteCommon, "Resources/white.png");
}

int StyleMeter::GetRankIndex() const
{
    int rank = 0;
    for (int i = kRankCount - 1; i >= 0; --i) {
        if (points_ >= kRanks[i].threshold) {
            rank = i;
            break;
        }
    }
    return rank;
}

void StyleMeter::RegisterHit(const std::string& moveId, float basePoints)
{
    // 同じ技の連発ペナルティ  熱が高いほど点が入らない（1 → 0.56 → 0.38 → 0.29 ...）
    float heat = moveHeat_[moveId];
    float mult = 1.0f / (1.0f + kHeatPenaltyScale * heat);

    // 直前と違う技ならバリエーションボーナス
    if (!lastMoveId_.empty() && moveId != lastMoveId_) {
        mult *= kVariationBonusMult;
    }

    points_ = std::clamp(points_ + basePoints * mult, 0.0f, kMaxPoints);
    moveHeat_[moveId] = heat + 1.0f;
    lastMoveId_ = moveId;

    hitCount_++;
    bestChain_ = (std::max)(bestChain_, hitCount_);
    chainTimer_ = kChainKeep;
    noHitTimer_ = 0.0f;
    hudAlpha_ = 1.0f;
    hitPopTimer_ = kHitPopSeconds;
}

void StyleMeter::SetNormalizedPoints(float t)
{
    points_ = std::clamp(t, 0.0f, 1.0f) * kMaxPoints;
    if (points_ > 0.0f) {
        hudAlpha_ = 1.0f;
    }
}

Vector4 StyleMeter::GetRankColor() const
{
    return kRanks[GetRankIndex()].color;
}

void StyleMeter::Update(float dt)
{
    justRankedUp_ = false;

    // 技の熱冷まし
    for (auto& [id, heat] : moveHeat_) {
        heat = (std::max)(heat - kHeatCool * dt, 0.0f);
    }

    // ポイント減衰（攻撃をやめて少し経ってから。高ランクほど維持が難しい）
    noHitTimer_ += dt;
    if (noHitTimer_ > kDecayGrace && points_ > 0.0f) {
        points_ = (std::max)(points_ - dt * (kDecayBase + points_ * kDecayPointScale), 0.0f);
    }

    // ヒットチェーン切れ
    chainTimer_ -= dt;
    if (chainTimer_ <= 0.0f) {
        chainTimer_ = 0.0f;
        hitCount_ = 0;
        if (points_ <= 0.0f) {
            hudAlpha_ = (std::max)(hudAlpha_ - dt * kHudFadeSpeed, 0.0f);
        }
    }

    // ランク変動演出（上がっても下がっても弾ませ、上がった時だけ長め）
    int rank = GetRankIndex();
    if (rank != prevRank_) {
        rankFlashTimer_ = (rank > prevRank_) ? kRankUpFlashSeconds : kRankDownFlashSeconds;
        justRankedUp_ = rank > prevRank_;
        prevRank_ = rank;
    }
    rankFlashTimer_ = (std::max)(rankFlashTimer_ - dt, 0.0f);
    hitPopTimer_ = (std::max)(hitPopTimer_ - dt, 0.0f);
}

void StyleMeter::UpdateHud(FontRenderer& font)
{
    if (hudAlpha_ <= 0.0f) {
        return;
    }

    const int rank = GetRankIndex();
    const RankDef& def = kRanks[rank];

    // ランク文字（ランクアップ直後は大きく弾む）
    float pop = (rankFlashTimer_ > 0.0f) ? Easing::EaseOutBack(1.0f - rankFlashTimer_ / kRankUpFlashSeconds) : 1.0f;
    float scale = kRankScale * (kRankPopMinScale + (1.0f - kRankPopMinScale) * pop);
    Vector4 rc = def.color;
    // 弾んでいる間は白へ寄せて発光しているように見せる
    float flash = (rankFlashTimer_ > kRankWhiteFlashStart)
        ? (rankFlashTimer_ - kRankWhiteFlashStart) / (kRankUpFlashSeconds - kRankWhiteFlashStart)
        : 0.0f;
    rc = { rc.x + (1.0f - rc.x) * flash, rc.y + (1.0f - rc.y) * flash, rc.z + (1.0f - rc.z) * flash, hudAlpha_ };

    float rankW = FontRenderer::kCharW * scale * static_cast<float>(std::strlen(def.letter));
    font.DrawString(def.letter, kRightEdge - rankW, kRankY, scale, rc);

    // ヒットチェーン数（加算の瞬間だけ少し大きく）
    if (hitCount_ > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), hitCount_ >= kHitHighlightCount ? "%d HITS!" : "%d HITS", hitCount_);
        const float chainScale = (std::min)(static_cast<float>(hitCount_) * kHitScalePerHit, kHitScaleMaxGrowth);
        float hs = kHitScaleBase + chainScale + ((hitPopTimer_ > 0.0f) ? kHitPopScale * (hitPopTimer_ / kHitPopSeconds) : 0.0f);
        float hw = FontRenderer::kCharW * hs * static_cast<float>(std::strlen(buf));
        const Vector4 hitColor = hitCount_ >= kHitHighlightCount
            ? Vector4 { kHitHighlightColor.x, kHitHighlightColor.y, kHitHighlightColor.z, hudAlpha_ }
            : Vector4 { 1.0f, 1.0f, 1.0f, hudAlpha_ };
        font.DrawString(buf, kRightEdge - hw, kRankY + kHitTextOffsetY, hs, hitColor);
    }
    if (bestChain_ > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "BEST %d", bestChain_);
        float bw = FontRenderer::kCharW * kBestChainScale * static_cast<float>(std::strlen(buf));
        font.DrawString(buf, kRightEdge - bw, kRankY + kBestChainOffsetY, kBestChainScale,
            { kBestChainGray, kBestChainGray, kBestChainGray, hudAlpha_ * kBestChainAlpha });
    }

    // ランク内の進捗バー（次のランクまでの割合）
    float lo = def.threshold;
    float hi = (rank + 1 < kRankCount) ? kRanks[rank + 1].threshold : kMaxPoints;
    float t = std::clamp((points_ - lo) / (std::max)(hi - lo, 1.0f), 0.0f, 1.0f);

    barBg_->SetPosition({ kRightEdge - kBarW, kRankY + kBarOffsetY });
    barBg_->SetSize({ kBarW, kBarH });
    Vector4 bg = { kBarBackgroundColor.x, kBarBackgroundColor.y, kBarBackgroundColor.z, kBarBackgroundColor.w * hudAlpha_ };
    barBg_->SetColor(bg);
    barBg_->Update();

    barFg_->SetPosition({ kRightEdge - kBarW, kRankY + kBarOffsetY });
    barFg_->SetSize({ kBarW * t, kBarH });
    barFg_->SetColor({ def.color.x, def.color.y, def.color.z, kBarForegroundAlpha * hudAlpha_ });
    barFg_->Update();
}

void StyleMeter::DrawHud()
{
    if (hudAlpha_ <= 0.0f) {
        return;
    }
    barBg_->Draw();
    barFg_->Draw();
}
