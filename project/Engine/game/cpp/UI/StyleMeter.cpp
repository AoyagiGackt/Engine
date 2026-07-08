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
    const char* label;
    float       threshold; ///< このポイント以上でこのランク
    Vector4     color;
};
// DMC式: ランク名の頭文字がランク文字と一致するとそれっぽい
constexpr RankDef kRanks[StyleMeter::kRankCount] = {
    { "D",   "DULL",             0.0f, { 0.55f, 0.55f, 0.55f, 1.0f } },
    { "C",   "CRAZY!",         120.0f, { 0.85f, 0.85f, 0.85f, 1.0f } },
    { "B",   "BADASS!!",       270.0f, { 0.30f, 0.72f, 1.00f, 1.0f } },
    { "A",   "AMAZING!",       440.0f, { 0.20f, 1.00f, 0.40f, 1.0f } },
    { "S",   "STYLISH!!",      620.0f, { 1.00f, 0.90f, 0.10f, 1.0f } },
    { "SS",  "SENSATIONAL!!",  790.0f, { 1.00f, 0.55f, 0.10f, 1.0f } },
    { "SSS", "SUPREME!!!",     940.0f, { 1.00f, 0.30f, 0.30f, 1.0f } },
};

// HUD レイアウト（右上アンカー）
constexpr float kRightEdge  = 1256.0f;
constexpr float kRankY      = 56.0f;
constexpr float kRankScale  = 5.0f;
constexpr float kBarW       = 190.0f;
constexpr float kBarH       = 7.0f;

} // namespace

void StyleMeter::Initialize(SpriteCommon* spriteCommon)
{
    barBg_ = std::make_unique<Sprite>();
    barBg_->Initialize(spriteCommon, "Resources/white.png");
    barBg_->SetColor({ 0.08f, 0.08f, 0.12f, 0.8f });

    barFg_ = std::make_unique<Sprite>();
    barFg_->Initialize(spriteCommon, "Resources/white.png");
}

int StyleMeter::GetRankIndex() const
{
    int rank = 0;
    for (int i = kRankCount - 1; i >= 0; --i) {
        if (points_ >= kRanks[i].threshold) { rank = i; break; }
    }
    return rank;
}

void StyleMeter::RegisterHit(const std::string& moveId, float basePoints)
{
    // 同じ技の連発ペナルティ: 熱が高いほど点が入らない（1 → 0.56 → 0.38 → 0.29 ...）
    float heat = moveHeat_[moveId];
    float mult = 1.0f / (1.0f + 0.8f * heat);

    // 直前と違う技ならバリエーションボーナス
    if (!lastMoveId_.empty() && moveId != lastMoveId_) { mult *= 1.3f; }

    points_ = std::clamp(points_ + basePoints * mult, 0.0f, kMaxPoints);
    moveHeat_[moveId] = heat + 1.0f;
    lastMoveId_       = moveId;

    hitCount_++;
    bestChain_   = (std::max)(bestChain_, hitCount_);
    chainTimer_  = kChainKeep;
    noHitTimer_  = 0.0f;
    hudAlpha_    = 1.0f;
    hitPopTimer_ = 0.18f;
}

void StyleMeter::Update(float dt)
{
    // 技の熱冷まし
    for (auto& [id, heat] : moveHeat_) {
        heat = (std::max)(heat - kHeatCool * dt, 0.0f);
    }

    // ポイント減衰（攻撃をやめて少し経ってから。高ランクほど維持が難しい）
    noHitTimer_ += dt;
    if (noHitTimer_ > kDecayGrace && points_ > 0.0f) {
        points_ = (std::max)(points_ - dt * (40.0f + points_ * 0.12f), 0.0f);
    }

    // ヒットチェーン切れ
    chainTimer_ -= dt;
    if (chainTimer_ <= 0.0f) {
        chainTimer_ = 0.0f;
        hitCount_   = 0;
        if (points_ <= 0.0f) {
            hudAlpha_ = (std::max)(hudAlpha_ - dt * 2.0f, 0.0f);
        }
    }

    // ランク変動演出（上がっても下がっても弾ませ、上がった時だけ長め）
    int rank = GetRankIndex();
    if (rank != prevRank_) {
        rankFlashTimer_ = (rank > prevRank_) ? 0.35f : 0.15f;
        prevRank_       = rank;
    }
    rankFlashTimer_ = (std::max)(rankFlashTimer_ - dt, 0.0f);
    hitPopTimer_    = (std::max)(hitPopTimer_ - dt, 0.0f);
}

void StyleMeter::UpdateHud(FontRenderer& font)
{
    if (hudAlpha_ <= 0.0f) { return; }

    const int      rank = GetRankIndex();
    const RankDef& def  = kRanks[rank];

    // ランク文字（ランクアップ直後は大きく弾む）
    float pop   = (rankFlashTimer_ > 0.0f) ? Easing::EaseOutBack(1.0f - rankFlashTimer_ / 0.35f) : 1.0f;
    float scale = kRankScale * (0.75f + 0.25f * pop);
    Vector4 rc  = def.color;
    // 弾んでいる間は白へ寄せて発光しているように見せる
    float flash = (rankFlashTimer_ > 0.2f) ? (rankFlashTimer_ - 0.2f) / 0.15f : 0.0f;
    rc = { rc.x + (1.0f - rc.x) * flash, rc.y + (1.0f - rc.y) * flash, rc.z + (1.0f - rc.z) * flash, hudAlpha_ };

    float rankW = FontRenderer::kCharW * scale * static_cast<float>(std::strlen(def.letter));
    font.DrawString(def.letter, kRightEdge - rankW, kRankY, scale, rc);

    // ランク名ラベル
    constexpr float kLabelScale = 1.4f;
    float labelW = FontRenderer::kCharW * kLabelScale * static_cast<float>(std::strlen(def.label));
    font.DrawString(def.label, kRightEdge - labelW, kRankY + 84.0f,
        kLabelScale, { def.color.x, def.color.y, def.color.z, hudAlpha_ * 0.9f });

    // ヒットチェーン数（加算の瞬間だけ少し大きく）
    if (hitCount_ > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d HITS", hitCount_);
        float hs = 1.8f + ((hitPopTimer_ > 0.0f) ? 0.5f * (hitPopTimer_ / 0.18f) : 0.0f);
        float hw = FontRenderer::kCharW * hs * static_cast<float>(std::strlen(buf));
        font.DrawString(buf, kRightEdge - hw, kRankY + 118.0f, hs, { 1.0f, 1.0f, 1.0f, hudAlpha_ });
    }
    if (bestChain_ > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "BEST %d", bestChain_);
        constexpr float kBS = 1.1f;
        float bw = FontRenderer::kCharW * kBS * static_cast<float>(std::strlen(buf));
        font.DrawString(buf, kRightEdge - bw, kRankY + 152.0f, kBS, { 0.7f, 0.7f, 0.7f, hudAlpha_ * 0.8f });
    }

    // ランク内の進捗バー（次のランクまでの割合）
    float lo = def.threshold;
    float hi = (rank + 1 < kRankCount) ? kRanks[rank + 1].threshold : kMaxPoints;
    float t  = std::clamp((points_ - lo) / (std::max)(hi - lo, 1.0f), 0.0f, 1.0f);

    barBg_->SetPosition({ kRightEdge - kBarW, kRankY + 104.0f });
    barBg_->SetSize({ kBarW, kBarH });
    Vector4 bg = { 0.08f, 0.08f, 0.12f, 0.8f * hudAlpha_ };
    barBg_->SetColor(bg);
    barBg_->Update();

    barFg_->SetPosition({ kRightEdge - kBarW, kRankY + 104.0f });
    barFg_->SetSize({ kBarW * t, kBarH });
    barFg_->SetColor({ def.color.x, def.color.y, def.color.z, 0.95f * hudAlpha_ });
    barFg_->Update();
}

void StyleMeter::DrawHud()
{
    if (hudAlpha_ <= 0.0f) { return; }
    barBg_->Draw();
    barFg_->Draw();
}
