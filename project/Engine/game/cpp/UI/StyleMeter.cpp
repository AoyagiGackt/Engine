/**
 * @file StyleMeter.cpp
 * @brief スタイリッシュランク（D〜SSS）の採点処理とHUD描画（StyleMeter）の実装
 */
#include "StyleMeter.h"
#include "TextBufferCapacity.h"
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

namespace engine::game {
namespace {

/** @brief スタイルランクのしきい値と表示情報 */
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
    float finisherSeconds = 0.9f; ///< FINISH! を出しておく秒数
    float finisherPopSeconds = 0.3f; ///< FINISH! が弾んで登場する秒数
    float finisherFadeSeconds = 0.3f; ///< FINISH! が消えていく秒数（表示の最後）
    float timerBarBlinkSeconds = 0.16f; ///< 残り時間バーが切れかけの時に点滅する周期
    float timerBarWarningRatio = 0.3f; ///< 残りがこの割合以下で残り時間バーを赤く点滅させる
    std::array<float, StyleMeter::kRankCount> rankThresholds {};
};

constexpr JsonField<StyleTuning> kStyleFields[] = {
    { nullptr, "maxPoints", &StyleTuning::maxPoints },
    { nullptr, "finisherSeconds", &StyleTuning::finisherSeconds },
    { nullptr, "finisherPopSeconds", &StyleTuning::finisherPopSeconds },
    { nullptr, "finisherFadeSeconds", &StyleTuning::finisherFadeSeconds },
    { nullptr, "timerBarBlinkSeconds", &StyleTuning::timerBarBlinkSeconds },
    { nullptr, "timerBarWarningRatio", &StyleTuning::timerBarWarningRatio },
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

/** @brief style_meter.json からスタイルの調整値を読み込む */
StyleTuning LoadStyleTuning() {
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
    tuning.chainKeep = (std::max)(tuning.chainKeep, kMinSeconds);
    tuning.finisherSeconds = (std::max)(tuning.finisherSeconds, kMinSeconds);
    tuning.finisherPopSeconds = std::clamp(tuning.finisherPopSeconds, kMinSeconds, tuning.finisherSeconds);
    tuning.finisherFadeSeconds = std::clamp(tuning.finisherFadeSeconds, kMinSeconds, tuning.finisherSeconds);
    tuning.timerBarBlinkSeconds = (std::max)(tuning.timerBarBlinkSeconds, kMinSeconds);
    tuning.timerBarWarningRatio = std::clamp(tuning.timerBarWarningRatio, 0.0f, 1.0f);
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

/** @brief 読み込み済みの調整値の置き場（初回に読み、調整値エディタで保存した時に読み直す） */
StyleTuning& MutableStyleTune() {
    static StyleTuning tuning = LoadStyleTuning();
    return tuning;
}

/** @brief 読み込み済みの調整値 */
const StyleTuning& StyleTune() {
    return MutableStyleTune();
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
constexpr Vector2 kHitTimerBarSize = { 150.0f, 4.0f };
constexpr Vector2 kHitTimerBarOffset = { 0.0f, 3.0f };
constexpr Vector4 kHitTimerBarColor = { 1.0f, 0.75f, 0.18f, 0.95f };
constexpr Vector4 kHitTimerBarWarningColor = { 1.0f, 0.15f, 0.08f, 1.0f };
constexpr float kHitTimerBarMinAlpha = 0.3f;
constexpr float kBestChainTimerGap = 6.0f;
constexpr Vector2 kFinisherOffset = { -280.0f, 96.0f };
constexpr float kFinisherScale = 2.7f;
constexpr float kFinisherPopScale = 0.6f;
constexpr const char* kFinisherText = "FINISH!";

} // namespace

/** @brief ランクゲージのバー（背景/前景）スプライトを生成する */
void StyleMeter::Initialize(SpriteCommon* spriteCommon) {
    barBg_ = std::make_unique<Sprite>();
    barBg_->Initialize(spriteCommon, "Resources/white.png");
    barBg_->SetColor(kBarBackgroundColor);

    barFg_ = std::make_unique<Sprite>();
    barFg_->Initialize(spriteCommon, "Resources/white.png");
    hitTimerBarBg_ = std::make_unique<Sprite>();
    hitTimerBarBg_->Initialize(spriteCommon, "Resources/white.png");
    hitTimerBarFg_ = std::make_unique<Sprite>();
    hitTimerBarFg_->Initialize(spriteCommon, "Resources/white.png");
}

/** @brief 今のスタイルランクの番号を返す */
int StyleMeter::GetRankIndex() const {
    int rank = 0;
    for (int i = kRankCount - 1; i >= 0; --i) {
        if (points_ >= StyleTune().rankThresholds[i]) {
            rank = i;
            break;
        }
    }
    return rank;
}

/** @brief 攻撃ヒットを採点に登録する */
void StyleMeter::RegisterHit(const std::string& moveId, float basePoints) {
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

/** @brief 外部で採点済みの正規化スタイル値(0.0〜1.0)を表示へ反映する */
void StyleMeter::SetNormalizedPoints(float t) {
    points_ = std::clamp(t, 0.0f, 1.0f) * StyleTune().maxPoints;
    if (points_ > 0.0f) {
        hudAlpha_ = 1.0f;
    }
}

/** @brief 現在のランクに対応する表示色を返す（ランクアップ演出の色をHUDと一致させるのに使う） */
Vector4 StyleMeter::GetRankColor() const {
    const RankDef& rank = kRanks[GetRankIndex()];
    return UILayout::Get(kLayoutName).Color(std::string("style_rank.color_") + rank.letter, rank.color);
}

/** @brief ランクの文字（"S" など）に対応する表示色を返す（知らない文字なら D の色） */
Vector4 StyleMeter::ColorForRank(const std::string& letter) {
    const RankDef* found = &kRanks[0];
    for (const RankDef& rank : kRanks) {
        if (letter == rank.letter) {
            found = &rank;
            break;
        }
    }
    return UILayout::Get(kLayoutName).Color(std::string("style_rank.color_") + found->letter, found->color);
}

/** @brief ポイント減衰・技の熱冷まし・表示アニメを1フレーム進める */
void StyleMeter::Update(float dt) {
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
    finisherTimer_ = (std::max)(finisherTimer_ - dt, 0.0f);
    if (finisherTimer_ > 0.0f) {
        hudAlpha_ = 1.0f;
    }
}

/** @brief 右上HUDの文字列をフォントレンダラーへ積み、バースプライトの位置を更新する */
void StyleMeter::UpdateHud(FontRenderer& font) {
    // 演出の時間は調整値から、表示位置・大きさ・色は UI レイアウトから読む
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

    // 今のランクの文字と色
    const int rank = GetRankIndex();
    const RankDef& def = kRanks[rank];

    // ランク文字（ランクアップ直後は大きく弾む）
    float pop = (rankFlashTimer_ > 0.0f) ? easing::EaseOutBack(1.0f - rankFlashTimer_ / rankUpFlash) : 1.0f;
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
    float bestChainY = anchor.y + bestChainOffsetY;
    if (hitCount_ > 0) {
        char buf[textBufferCapacity::kShortText];
        std::snprintf(buf, sizeof(buf), hitCount_ >= hitHighlightCount ? "%d HITS!" : "%d HITS", hitCount_);
        const float scalePerHit = layout.Float("hit.scale_per_hit", kHitScalePerHit);
        const float maxGrowth = layout.Float("hit.max_growth", kHitScaleMaxGrowth);
        const float popScale = layout.Float("hit.pop_scale", kHitPopScale);
        const float chainScale = (std::min)(static_cast<float>(hitCount_) * scalePerHit, maxGrowth);
        float hs = hitScaleBase + chainScale + ((hitPopTimer_ > 0.0f)
            ? popScale * (1.0f - easing::EaseOutElastic(1.0f - hitPopTimer_ / hitPopSeconds)) : 0.0f);
        float hw = FontRenderer::kCharW * hs * static_cast<float>(std::strlen(buf));
        Vector4 hitColor = hitCount_ >= hitHighlightCount
            ? layout.Color("hit.highlight_color", { kHitHighlightColor.x, kHitHighlightColor.y, kHitHighlightColor.z, 1.0f })
            : Vector4 { 1.0f, 1.0f, 1.0f, 1.0f };
        hitColor.w *= hudAlpha_;
        font.DrawString(buf, anchor.x - hw, anchor.y + hitTextOffsetY, hs, hitColor);

        // 文字の弾みに追従して直下に残り時間を表示する
        const Vector2 timerSize = layout.Vec2("hit.timer_bar_size", kHitTimerBarSize);
        const Vector2 timerOffset = layout.Vec2("hit.timer_bar_offset", kHitTimerBarOffset);
        const Vector2 timerPos = { anchor.x - timerSize.x + timerOffset.x,
            anchor.y + hitTextOffsetY + FontRenderer::kCharH * hs + timerOffset.y };
        const float remaining = std::clamp(chainTimer_ / tune.chainKeep, 0.0f, 1.0f);
        Vector4 timerColor = layout.Color("hit.timer_bar_color", kHitTimerBarColor);
        if (remaining <= tune.timerBarWarningRatio) {
            timerColor = layout.Color("hit.timer_bar_warning_color", kHitTimerBarWarningColor);
            const float blink = (1.0f + std::cos(chainTimer_ / tune.timerBarBlinkSeconds * gameConstants::kTwoPi)) * 0.5f;
            const float minAlpha = std::clamp(layout.Float("hit.timer_bar_blink_min_alpha", kHitTimerBarMinAlpha), 0.0f, 1.0f);
            timerColor.w *= minAlpha + (1.0f - minAlpha) * blink;
        }
        Vector4 timerBg = layout.Color("hit.timer_bar_background_color", kBarBackgroundColor);
        timerBg.w *= hudAlpha_;
        timerColor.w *= hudAlpha_;
        hitTimerBarBg_->SetPosition(timerPos);
        hitTimerBarBg_->SetSize(timerSize);
        hitTimerBarBg_->SetColor(timerBg);
        hitTimerBarBg_->Update();
        hitTimerBarFg_->SetPosition(timerPos);
        hitTimerBarFg_->SetSize({ timerSize.x * remaining, timerSize.y });
        hitTimerBarFg_->SetColor(timerColor);
        hitTimerBarFg_->Update();
        bestChainY = (std::max)(bestChainY, timerPos.y + timerSize.y
            + layout.Float("hit.timer_bar_best_gap", kBestChainTimerGap));
    }
    if (finisherTimer_ > 0.0f) {
        const float progress = std::clamp((tune.finisherSeconds - finisherTimer_) / tune.finisherPopSeconds, 0.0f, 1.0f);
        const float finishScale = layout.Float("hit.finisher_scale", kFinisherScale)
            * (1.0f + layout.Float("hit.finisher_pop_scale", kFinisherPopScale) * (1.0f - easing::EaseOutElastic(progress)));
        const Vector2 offset = layout.Vec2("hit.finisher_offset", kFinisherOffset);
        Vector4 color = layout.Color("hit.highlight_color", { kHitHighlightColor.x, kHitHighlightColor.y, kHitHighlightColor.z, 1.0f });
        const float flash = 1.0f - progress;
        color = { color.x + (1.0f - color.x) * flash, color.y + (1.0f - color.y) * flash,
            color.z + (1.0f - color.z) * flash,
            color.w * std::clamp(finisherTimer_ / tune.finisherFadeSeconds, 0.0f, 1.0f) };
        const float width = FontRenderer::kCharW * finishScale * static_cast<float>(std::strlen(kFinisherText));
        font.DrawString(kFinisherText, anchor.x + offset.x - width, anchor.y + offset.y, finishScale, color);
    }
    if (bestChain_ > 0) {
        char buf[textBufferCapacity::kShortText];
        std::snprintf(buf, sizeof(buf), "BEST %d", bestChain_);
        float bw = FontRenderer::kCharW * bestChainScale * static_cast<float>(std::strlen(buf));
        font.DrawString(buf, anchor.x - bw, bestChainY, bestChainScale,
            { bestChainGray, bestChainGray, bestChainGray, hudAlpha_ * bestChainAlpha });
    }

    // ランク内の進捗バー（次のランクまでの割合）
    // ランク内の進捗バー（次のランクまでの割合）
    float lo = tune.rankThresholds[rank];
    float hi = (rank + 1 < kRankCount) ? tune.rankThresholds[rank + 1] : tune.maxPoints;
    float t = std::clamp((points_ - lo) / (std::max)(hi - lo, 1.0f), 0.0f, 1.0f);

    // 背景と前景の2本で進捗を見せ、前景はランクの色にする
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

/** @brief ランクゲージのバースプライトを描画する（SpriteCommon 設定済みの状態で呼ぶ） */
void StyleMeter::DrawHud() {
    if (hudAlpha_ <= 0.0f) {
        return;
    }
    barBg_->Draw();
    barFg_->Draw();
    if (hitCount_ > 0) {
        hitTimerBarBg_->Draw();
        hitTimerBarFg_->Draw();
    }
}

/** @brief コンボの締め演出を開始する */
void StyleMeter::RegisterFinisher() {
    finisherTimer_ = StyleTune().finisherSeconds;
    hudAlpha_ = 1.0f;
}

/** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
void StyleMeter::ReloadTuning() {
    MutableStyleTune() = LoadStyleTuning();
}

} // namespace engine::game
