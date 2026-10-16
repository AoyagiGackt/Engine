/**
 * @file StyleMeter.cpp
 * @brief スタイリッシュランク（D〜SSS）の採点処理とHUD描画（StyleMeter）の実装
 */
#include "StyleMeter.h"
#include "Easing.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "UILayout.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace engine;
using namespace engine::game;

namespace {

struct RankDef {
    const char* letter;
    float threshold; ///< このポイント以上でこのランク（style_meter.json の rankThresholds で上書きできる）
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

/** @brief 採点・減衰・表示時間の調整値（Resources/Config/style_meter.json） */
struct StyleTuning {
    float maxPoints = 1000.0f;
    float chainKeep = 3.0f; ///< ヒットチェーンが切れるまでの秒数
    float decayGrace = 1.2f; ///< 攻撃をやめてから減衰が始まるまでの秒数
    float heatCool = 0.30f; ///< 技の熱が1秒あたりに冷める量
    float heatPenaltyScale = 0.8f; ///< 連発ペナルティの強さ（熱1あたりの倍率減衰）
    float variationBonusMult = 1.3f; ///< 直前と違う技を出した時の加点倍率
    float decayBase = 40.0f; ///< ポイント減衰の基礎速度（秒あたり）
    float decayPointScale = 0.12f; ///< ポイントが高いほど減衰を速める係数
    float hudFadeSpeed = 2.0f; ///< チェーンが切れてポイントも尽きた後にHUDを消す速さ
    float hitPopSeconds = 0.18f; ///< ヒット数が加算の瞬間に弾む長さ
    int hitHighlightCount = 10; ///< この数以上で強調表示する
    float rankUpFlashSeconds = 0.35f;
    float rankDownFlashSeconds = 0.15f;
    float rankWhiteFlashStart = 0.2f; ///< 演出の残り時間がこれ以上の間だけ白く光らせる
    std::array<float, StyleMeter::kRankCount> rankThresholds {};
};

constexpr JsonField<StyleTuning> kStyleFields[] = {
    { nullptr, "maxPoints", &StyleTuning::maxPoints },
    { nullptr, "chainKeep", &StyleTuning::chainKeep },
    { nullptr, "decayGrace", &StyleTuning::decayGrace },
    { nullptr, "heatCool", &StyleTuning::heatCool },
    { nullptr, "heatPenaltyScale", &StyleTuning::heatPenaltyScale },
    { nullptr, "variationBonusMult", &StyleTuning::variationBonusMult },
    { nullptr, "decayBase", &StyleTuning::decayBase },
    { nullptr, "decayPointScale", &StyleTuning::decayPointScale },
    { nullptr, "hudFadeSpeed", &StyleTuning::hudFadeSpeed },
    { nullptr, "hitPopSeconds", &StyleTuning::hitPopSeconds },
    { nullptr, "hitHighlightCount", &StyleTuning::hitHighlightCount },
    { nullptr, "rankUpFlashSeconds", &StyleTuning::rankUpFlashSeconds },
    { nullptr, "rankDownFlashSeconds", &StyleTuning::rankDownFlashSeconds },
    { nullptr, "rankWhiteFlashStart", &StyleTuning::rankWhiteFlashStart },
};

StyleTuning LoadStyleTuning()
{
    StyleTuning tuning;
    for (int i = 0; i < StyleMeter::kRankCount; ++i) {
        tuning.rankThresholds[i] = kRanks[i].threshold;
    }
    const nlohmann::json root = JsonHelper::Load("Resources/Config/style_meter.json");
    ReadJsonFields(root, kStyleFields, tuning);
    // ランクの境目はランク文字をキーにした表（例: "rankThresholds": { "C": 120 }）
    const auto thresholds = root.is_object() ? root.find("rankThresholds") : root.end();
    if (root.is_object() && thresholds != root.end() && thresholds->is_object()) {
        for (int i = 0; i < StyleMeter::kRankCount; ++i) {
            const auto value = thresholds->find(kRanks[i].letter);
            if (value != thresholds->end() && value->is_number()) {
                tuning.rankThresholds[i] = value->get<float>();
            }
        }
    }

    // 壊れた値でも採点と演出の割り算が破綻しないよう整える
    constexpr float kMinSeconds = std::numeric_limits<float>::epsilon();
    tuning.rankThresholds[0] = 0.0f;
    for (int i = 1; i < StyleMeter::kRankCount; ++i) {
        tuning.rankThresholds[i] = (std::max)(tuning.rankThresholds[i], tuning.rankThresholds[i - 1]);
    }
    tuning.maxPoints = (std::max)(tuning.maxPoints, tuning.rankThresholds[StyleMeter::kRankCount - 1]);
    tuning.hitPopSeconds = (std::max)(tuning.hitPopSeconds, kMinSeconds);
    tuning.rankUpFlashSeconds = (std::max)(tuning.rankUpFlashSeconds, kMinSeconds);
    tuning.rankDownFlashSeconds = (std::max)(tuning.rankDownFlashSeconds, 0.0f);
    tuning.rankWhiteFlashStart = std::clamp(tuning.rankWhiteFlashStart, 0.0f, tuning.rankUpFlashSeconds);
    tuning.hitHighlightCount = (std::max)(tuning.hitHighlightCount, 1);
    return tuning;
}

/** @brief 読み込み済みの調整値（初回に読む。変更は次回起動時に反映） */
const StyleTuning& StyleTune()
{
    static const StyleTuning tuning = LoadStyleTuning();
    return tuning;
}

// HUD レイアウト（既定値。全シーン共通のHUDレイアウト Resources/Config/UI/hud.json で上書きできる）
constexpr const char* kLayoutName = "hud";
constexpr Vector2 kRankAnchor = { 1256.0f, 56.0f }; // ランク文字の右端X・上端Y
constexpr float kRankScale = 5.0f;
constexpr Vector2 kBarSize = { 190.0f, 7.0f };
constexpr float kBarOffsetY = 84.0f; // ランク文字の上端から進捗バーまでの距離
constexpr Vector4 kBarBackgroundColor = { 0.08f, 0.08f, 0.12f, 0.8f };
constexpr float kBarForegroundAlpha = 0.95f;

// ランク変動の演出
constexpr float kRankPopMinScale = 0.75f; // 弾み始めの大きさ（通常サイズ比）

// ヒットチェーン数の表示
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
        if (points_ >= StyleTune().rankThresholds[i]) {
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
    const StyleTuning& tune = StyleTune();
    float mult = 1.0f / (1.0f + tune.heatPenaltyScale * heat);

    // 直前と違う技ならバリエーションボーナス
    if (!lastMoveId_.empty() && moveId != lastMoveId_) {
        mult *= tune.variationBonusMult;
    }

    points_ = std::clamp(points_ + basePoints * mult, 0.0f, tune.maxPoints);
    moveHeat_[moveId] = heat + 1.0f;
    lastMoveId_ = moveId;

    hitCount_++;
    bestChain_ = (std::max)(bestChain_, hitCount_);
    chainTimer_ = tune.chainKeep;
    noHitTimer_ = 0.0f;
    hudAlpha_ = 1.0f;
    hitPopTimer_ = tune.hitPopSeconds;
}

void StyleMeter::SetNormalizedPoints(float t)
{
    points_ = std::clamp(t, 0.0f, 1.0f) * StyleTune().maxPoints;
    if (points_ > 0.0f) {
        hudAlpha_ = 1.0f;
    }
}

Vector4 StyleMeter::GetRankColor() const
{
    const RankDef& rank = kRanks[GetRankIndex()];
    return UILayout::Get(kLayoutName).Color(std::string("style_rank.color_") + rank.letter, rank.color);
}

void StyleMeter::Update(float dt)
{
    const StyleTuning& tune = StyleTune();
    justRankedUp_ = false;

    // 技の熱冷まし
    for (auto& [id, heat] : moveHeat_) {
        heat = (std::max)(heat - tune.heatCool * dt, 0.0f);
    }

    // ポイント減衰（攻撃をやめて少し経ってから。高ランクほど維持が難しい）
    noHitTimer_ += dt;
    if (noHitTimer_ > tune.decayGrace && points_ > 0.0f) {
        points_ = (std::max)(points_ - dt * (tune.decayBase + points_ * tune.decayPointScale), 0.0f);
    }

    // ヒットチェーン切れ
    chainTimer_ -= dt;
    if (chainTimer_ <= 0.0f) {
        chainTimer_ = 0.0f;
        hitCount_ = 0;
        if (points_ <= 0.0f) {
            hudAlpha_ = (std::max)(hudAlpha_ - dt * tune.hudFadeSpeed, 0.0f);
        }
    }

    // ランク変動演出（上がっても下がっても弾ませ、上がった時だけ長め）
    int rank = GetRankIndex();
    if (rank != prevRank_) {
        rankFlashTimer_ = (rank > prevRank_)
            ? tune.rankUpFlashSeconds
            : tune.rankDownFlashSeconds;
        justRankedUp_ = rank > prevRank_;
        prevRank_ = rank;
    }
    rankFlashTimer_ = (std::max)(rankFlashTimer_ - dt, 0.0f);
    hitPopTimer_ = (std::max)(hitPopTimer_ - dt, 0.0f);
}

void StyleMeter::UpdateHud(FontRenderer& font)
{
    const StyleTuning& tune = StyleTune();
    const float rankUpFlash = tune.rankUpFlashSeconds;
    const float rankWhiteStart = tune.rankWhiteFlashStart;
    const float hitPopSeconds = tune.hitPopSeconds;
    const int hitHighlightCount = tune.hitHighlightCount;
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector4 barBackgroundColor = layout.Color("bar.background_color", kBarBackgroundColor);
    const float barForegroundAlpha = layout.Float("bar.foreground_alpha", kBarForegroundAlpha);
    const float hitScaleBase = layout.Float("hit.scale_base", kHitScaleBase);
    const float hitTextOffsetY = layout.Float("hit.text_offset_y", kHitTextOffsetY);
    const float bestChainOffsetY = layout.Float("best_chain.offset_y", kBestChainOffsetY);
    const float bestChainScale = layout.Float("best_chain.scale", kBestChainScale);
    const float bestChainGray = layout.Float("best_chain.gray", kBestChainGray);
    const float bestChainAlpha = layout.Float("best_chain.alpha", kBestChainAlpha);
    // 表示していない間もエディタで位置を動かせるよう、早期リターンより前に読んで登録しておく
    const Vector2 anchor = layout.Pos("style_rank.pos", kRankAnchor);
    const float rankScale = layout.Float("style_rank.scale", kRankScale);
    const Vector2 barSize = layout.Vec2("style_rank.bar_size", kBarSize);
    const float barOffsetY = layout.Float("style_rank.bar_offset_y", kBarOffsetY);
    if (hudAlpha_ <= 0.0f) {
        return;
    }

    const int rank = GetRankIndex();
    const RankDef& def = kRanks[rank];

    // ランク文字（ランクアップ直後は大きく弾む）
    float pop = (rankFlashTimer_ > 0.0f) ? Easing::EaseOutBack(1.0f - rankFlashTimer_ / rankUpFlash) : 1.0f;
    const float popMinScale = layout.Float("style_rank.pop_min_scale", kRankPopMinScale);
    float scale = rankScale * (popMinScale + (1.0f - popMinScale) * pop);
    const Vector4 rankColor = layout.Color(std::string("style_rank.color_") + def.letter, def.color);
    Vector4 rc = rankColor;
    // 弾んでいる間は白へ寄せて発光しているように見せる
    float flash = (rankWhiteStart < rankUpFlash && rankFlashTimer_ > rankWhiteStart)
        ? (rankFlashTimer_ - rankWhiteStart) / (rankUpFlash - rankWhiteStart)
        : 0.0f;
    rc = { rc.x + (1.0f - rc.x) * flash, rc.y + (1.0f - rc.y) * flash, rc.z + (1.0f - rc.z) * flash, rc.w * hudAlpha_ };

    float rankW = FontRenderer::kCharW * scale * static_cast<float>(std::strlen(def.letter));
    font.DrawString(def.letter, anchor.x - rankW, anchor.y, scale, rc);

    // ヒットチェーン数（加算の瞬間だけ少し大きく）
    if (hitCount_ > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), hitCount_ >= hitHighlightCount ? "%d HITS!" : "%d HITS", hitCount_);
        const float scalePerHit = layout.Float("hit.scale_per_hit", kHitScalePerHit);
        const float maxGrowth = layout.Float("hit.max_growth", kHitScaleMaxGrowth);
        const float popScale = layout.Float("hit.pop_scale", kHitPopScale);
        const float chainScale = (std::min)(static_cast<float>(hitCount_) * scalePerHit, maxGrowth);
        float hs = hitScaleBase + chainScale + ((hitPopTimer_ > 0.0f) ? popScale * (hitPopTimer_ / hitPopSeconds) : 0.0f);
        float hw = FontRenderer::kCharW * hs * static_cast<float>(std::strlen(buf));
        Vector4 hitColor = hitCount_ >= hitHighlightCount
            ? layout.Color("hit.highlight_color", { kHitHighlightColor.x, kHitHighlightColor.y, kHitHighlightColor.z, 1.0f })
            : Vector4 { 1.0f, 1.0f, 1.0f, 1.0f };
        hitColor.w *= hudAlpha_;
        font.DrawString(buf, anchor.x - hw, anchor.y + hitTextOffsetY, hs, hitColor);
    }
    if (bestChain_ > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "BEST %d", bestChain_);
        float bw = FontRenderer::kCharW * bestChainScale * static_cast<float>(std::strlen(buf));
        font.DrawString(buf, anchor.x - bw, anchor.y + bestChainOffsetY, bestChainScale,
            { bestChainGray, bestChainGray, bestChainGray, hudAlpha_ * bestChainAlpha });
    }

    // ランク内の進捗バー（次のランクまでの割合）
    float lo = tune.rankThresholds[rank];
    float hi = (rank + 1 < kRankCount) ? tune.rankThresholds[rank + 1] : tune.maxPoints;
    float t = std::clamp((points_ - lo) / (std::max)(hi - lo, 1.0f), 0.0f, 1.0f);

    barBg_->SetPosition({ anchor.x - barSize.x, anchor.y + barOffsetY });
    barBg_->SetSize({ barSize.x, barSize.y });
    Vector4 bg = { barBackgroundColor.x, barBackgroundColor.y, barBackgroundColor.z, barBackgroundColor.w * hudAlpha_ };
    barBg_->SetColor(bg);
    barBg_->Update();

    barFg_->SetPosition({ anchor.x - barSize.x, anchor.y + barOffsetY });
    barFg_->SetSize({ barSize.x * t, barSize.y });
    barFg_->SetColor({ rankColor.x, rankColor.y, rankColor.z, barForegroundAlpha * hudAlpha_ });
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
