/**
 * @file ResultBackdrop.cpp
 * @brief クリアの結果画面の背景（帯・透かし・破片）の色づくりと動きを実装するファイル
 */
#include "ResultBackdrop.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/result_backdrop.json";
constexpr const char* kWhiteTexture = "Resources/white.png";
constexpr int kMaxStripes = 32; // 帯のスプライトを最初に作っておく数（設定の本数はこれまで）
constexpr int kMaxShards = 64; // 破片のスプライトを最初に作っておく数
constexpr float kScreenWidth = static_cast<float>(WinApp::kClientWidth);
constexpr float kScreenHeight = static_cast<float>(WinApp::kClientHeight);
// 人の目の明るさの感じ方に合わせた重み（彩度を下げる時の灰色の求め方）
constexpr float kLumaR = 0.299f;
constexpr float kLumaG = 0.587f;
constexpr float kLumaB = 0.114f;

/** @brief 結果画面の背景の調整値 */
struct BackdropTuning {
    // 色（ランクの色の彩度をどこまで残すか、各部分の明るさ）
    float saturation = 0.35f; ///< ランクの色の彩度を残す割合（0で灰色。上げすぎると帯がぎらつく）
    float baseBrightness = 0.09f; ///< 下地の明るさ（ランクの色に掛ける）
    float stripeBrightness = 0.15f; ///< 帯の明るさ（下地との差が小さいほど落ち着く）
    float stripeAlpha = 0.9f; ///< 帯の不透明度
    float stripeWidth = 90.0f; ///< 帯の太さ（ピクセル）
    float stripeGap = 150.0f; ///< 帯と帯の間（ピクセル）
    float stripeAngleDegrees = -24.0f; ///< 帯の傾き（度）
    float stripeSpeed = 18.0f; ///< 帯が流れる速さ（1秒あたりのピクセル。速いと目が疲れる）
    // ランクの文字の透かし
    float watermarkScale = 26.0f; ///< 透かしの文字の大きさ（文字の倍率）
    float watermarkBrightness = 0.45f;
    float watermarkAlpha = 0.12f;
    float watermarkCenterX = 900.0f; ///< 透かしの文字の中心（画面の座標）
    float watermarkCenterY = 360.0f;
    // 漂う破片
    int shardCount = 36;
    float shardMinSize = 6.0f;
    float shardMaxSize = 18.0f;
    float shardFallSpeed = 22.0f; ///< 落ちる速さ（1秒あたりのピクセル）
    float shardDrift = 10.0f; ///< 横へ流れる速さの幅
    float shardSpin = 0.6f; ///< 回る速さの幅（1秒あたりのラジアン）
    float shardMinAlpha = 0.08f;
    float shardMaxAlpha = 0.22f;
    float shardWhiteness = 0.7f; ///< 破片の色を白へ寄せる割合（ランクの色を強く出しすぎない）
    float twinkleSpeed = 1.0f; ///< きらめきの速さ（1秒あたりのラジアン。速くすると点滅に見えるので上げすぎない）
    float twinkleDepth = 0.25f; ///< きらめきで濃さが変わる割合
    // 結果の文字の後ろの暗い板
    float panelX = 360.0f; ///< 板の左上（画面の座標）
    float panelY = 160.0f;
    float panelWidth = 560.0f;
    float panelHeight = 380.0f;
    float panelAlpha = 0.45f;
    // ランクが無い時の色
    Vector4 neutralColor = { 0.55f, 0.6f, 0.75f, 1.0f };
};

constexpr JsonField<BackdropTuning> kBackdropFields[] = {
    { "color", "saturation", &BackdropTuning::saturation },
    { "color", "baseBrightness", &BackdropTuning::baseBrightness },
    { "color", "neutralColor", &BackdropTuning::neutralColor },
    { "stripe", "brightness", &BackdropTuning::stripeBrightness },
    { "stripe", "alpha", &BackdropTuning::stripeAlpha },
    { "stripe", "width", &BackdropTuning::stripeWidth },
    { "stripe", "gap", &BackdropTuning::stripeGap },
    { "stripe", "angleDegrees", &BackdropTuning::stripeAngleDegrees },
    { "stripe", "speed", &BackdropTuning::stripeSpeed },
    { "watermark", "scale", &BackdropTuning::watermarkScale },
    { "watermark", "brightness", &BackdropTuning::watermarkBrightness },
    { "watermark", "alpha", &BackdropTuning::watermarkAlpha },
    { "watermark", "centerX", &BackdropTuning::watermarkCenterX },
    { "watermark", "centerY", &BackdropTuning::watermarkCenterY },
    { "shard", "count", &BackdropTuning::shardCount },
    { "shard", "minSize", &BackdropTuning::shardMinSize },
    { "shard", "maxSize", &BackdropTuning::shardMaxSize },
    { "shard", "fallSpeed", &BackdropTuning::shardFallSpeed },
    { "shard", "drift", &BackdropTuning::shardDrift },
    { "shard", "spin", &BackdropTuning::shardSpin },
    { "shard", "minAlpha", &BackdropTuning::shardMinAlpha },
    { "shard", "maxAlpha", &BackdropTuning::shardMaxAlpha },
    { "shard", "whiteness", &BackdropTuning::shardWhiteness },
    { "shard", "twinkleSpeed", &BackdropTuning::twinkleSpeed },
    { "shard", "twinkleDepth", &BackdropTuning::twinkleDepth },
    { "panel", "x", &BackdropTuning::panelX },
    { "panel", "y", &BackdropTuning::panelY },
    { "panel", "width", &BackdropTuning::panelWidth },
    { "panel", "height", &BackdropTuning::panelHeight },
    { "panel", "alpha", &BackdropTuning::panelAlpha },
};

/** @brief result_backdrop.json から調整値を読む（壊れた値でも描画が破綻しないよう整える） */
BackdropTuning LoadTuning() {
    BackdropTuning tuning = ReadJsonFields(JsonHelper::Load(kConfigPath), kBackdropFields);
    tuning.saturation = std::clamp(tuning.saturation, 0.0f, 1.0f);
    tuning.stripeWidth = (std::max)(tuning.stripeWidth, 1.0f);
    tuning.stripeGap = (std::max)(tuning.stripeGap, 0.0f);
    tuning.shardCount = std::clamp(tuning.shardCount, 0, kMaxShards);
    tuning.shardMaxSize = (std::max)(tuning.shardMaxSize, tuning.shardMinSize);
    tuning.shardMaxAlpha = (std::max)(tuning.shardMaxAlpha, tuning.shardMinAlpha);
    tuning.twinkleDepth = std::clamp(tuning.twinkleDepth, 0.0f, 1.0f);
    return tuning;
}

/** @brief 読み込み済みの調整値の置き場（初回に読み、調整値エディタで保存した時に読み直す） */
BackdropTuning& MutableTune() {
    static BackdropTuning sTuning = LoadTuning();
    return sTuning;
}

/** @brief 読み込み済みの調整値 */
const BackdropTuning& Tune() {
    return MutableTune();
}

/** @brief 色の明るさを brightness 倍し、不透明度を alpha にする */
Vector4 Shade(const Vector4& tone, float brightness, float alpha) {
    return { tone.x * brightness, tone.y * brightness, tone.z * brightness, alpha };
}

/** @brief 帯の並びの1周期（帯の太さ＋間） */
float StripePeriod(const BackdropTuning& tune) {
    return tune.stripeWidth + tune.stripeGap;
}
} // namespace

/** @brief 帯と破片のスプライトを作る */
void ResultBackdrop::Initialize(engine::graphics::SpriteCommon* spriteCommon) {
    auto makeSprite = [&]() {
        auto sprite = std::make_unique<engine::graphics::Sprite>();
        sprite->Initialize(spriteCommon, kWhiteTexture);
        return sprite;
    };
    base_ = makeSprite();
    base_->SetPosition({ 0.0f, 0.0f });
    base_->SetSize({ kScreenWidth, kScreenHeight });
    panel_ = makeSprite();
    stripes_.clear();
    for (int i = 0; i < kMaxStripes; ++i) {
        stripes_.push_back(makeSprite());
        // 帯は中心を軸に傾ける
        stripes_.back()->SetAnchorPoint({ 0.5f, 0.5f });
    }
    shardSprites_.clear();
    for (int i = 0; i < kMaxShards; ++i) {
        shardSprites_.push_back(makeSprite());
        shardSprites_.back()->SetAnchorPoint({ 0.5f, 0.5f });
    }
    shards_.assign(static_cast<size_t>(kMaxShards), Shard {});
}

/** @brief 結果画面を出し始める時に呼ぶ（破片を散らし直し、色を決める） */
void ResultBackdrop::Start(const Vector4& rankColor) {
    // ランクの色をそのまま使うと帯がぎらつくので、灰色へ寄せて彩度を落とした色を基準にする
    const BackdropTuning& tune = Tune();
    const float luma = rankColor.x * kLumaR + rankColor.y * kLumaG + rankColor.z * kLumaB;
    auto desaturate = [&](float channel) {
        return luma + (channel - luma) * tune.saturation;
    };
    tone_ = { desaturate(rankColor.x), desaturate(rankColor.y), desaturate(rankColor.z), 1.0f };
    time_ = 0.0f;
    for (Shard& shard : shards_) {
        RespawnShard(shard, false);
    }
}

/** @brief 帯と破片を動かす */
void ResultBackdrop::Update(float dt) {
    const BackdropTuning& tune = Tune();
    time_ += dt;
    const float margin = tune.shardMaxSize;
    for (int i = 0; i < tune.shardCount; ++i) {
        Shard& shard = shards_[static_cast<size_t>(i)];
        shard.position.x += shard.velocity.x * dt;
        shard.position.y += shard.velocity.y * dt;
        shard.rotation += shard.spin * dt;
        // 下へ抜けたら上から、横へ抜けたら反対側から入れ直す
        if (shard.position.y > kScreenHeight + margin) {
            RespawnShard(shard, true);
        } else if (shard.position.x < -margin) {
            shard.position.x = kScreenWidth + margin;
        } else if (shard.position.x > kScreenWidth + margin) {
            shard.position.x = -margin;
        }
    }
}

/** @brief 下地・帯・破片・文字の後ろの暗い板を描く */
void ResultBackdrop::Draw() {
    const BackdropTuning& tune = Tune();
    // 下地（ランクの色をごく暗くした色）
    base_->SetColor(Shade(tone_, tune.baseBrightness, 1.0f));
    base_->Update();
    base_->Draw();

    // 斜めの帯（下地より少しだけ明るい色で、ゆっくり横へ流す）
    const float period = StripePeriod(tune);
    const float angle = tune.stripeAngleDegrees * gameConstants::kDegToRad;
    // 傾けても画面の隅まで届く長さにする（画面の対角線）
    const float length = std::sqrt(kScreenWidth * kScreenWidth + kScreenHeight * kScreenHeight);
    // 傾けた分だけ帯の足元が横へずれるので、その分も含めて画面の左右の外から並べる
    const float spread = std::abs(std::tan(angle)) * kScreenHeight + period;
    const float offset = std::fmod(time_ * tune.stripeSpeed, period);
    const Vector4 stripeColor = Shade(tone_, tune.stripeBrightness, tune.stripeAlpha);
    int used = 0;
    for (float x = -spread + offset; x < kScreenWidth + spread && used < kMaxStripes; x += period, ++used) {
        auto& stripe = stripes_[static_cast<size_t>(used)];
        stripe->SetPosition({ x, kScreenHeight * 0.5f });
        stripe->SetSize({ tune.stripeWidth, length });
        stripe->SetRotation(angle);
        stripe->SetColor(stripeColor);
        stripe->Update();
        stripe->Draw();
    }

    // 漂う破片（白っぽく薄く。濃さはゆっくり揺らすだけで、点滅はさせない）
    const Vector4 shardTone = {
        1.0f + (tone_.x - 1.0f) * (1.0f - tune.shardWhiteness),
        1.0f + (tone_.y - 1.0f) * (1.0f - tune.shardWhiteness),
        1.0f + (tone_.z - 1.0f) * (1.0f - tune.shardWhiteness),
        1.0f,
    };
    for (int i = 0; i < tune.shardCount; ++i) {
        const Shard& shard = shards_[static_cast<size_t>(i)];
        const float twinkle = 1.0f - tune.twinkleDepth * 0.5f * (1.0f + std::sin(time_ * tune.twinkleSpeed + shard.phase));
        auto& sprite = shardSprites_[static_cast<size_t>(i)];
        sprite->SetPosition(shard.position);
        sprite->SetSize(shard.size);
        sprite->SetRotation(shard.rotation);
        sprite->SetColor({ shardTone.x, shardTone.y, shardTone.z, shard.alpha * twinkle });
        sprite->Update();
        sprite->Draw();
    }

    // 結果の文字の後ろを暗くして、背景より文字を目立たせる
    panel_->SetPosition({ tune.panelX, tune.panelY });
    panel_->SetSize({ tune.panelWidth, tune.panelHeight });
    panel_->SetColor({ 0.0f, 0.0f, 0.0f, tune.panelAlpha });
    panel_->Update();
    panel_->Draw();
}

/** @brief 大きなランクの文字の透かしを文字の描画予約に積む */
void ResultBackdrop::QueueWatermark(FontRenderer& font, const std::string& rankLetter) const {
    if (rankLetter.empty()) {
        return;
    }
    const BackdropTuning& tune = Tune();
    const float width = static_cast<float>(FontRenderer::kCharW) * tune.watermarkScale * static_cast<float>(rankLetter.size());
    const float height = static_cast<float>(FontRenderer::kCharH) * tune.watermarkScale;
    font.DrawString(rankLetter, tune.watermarkCenterX - width * 0.5f, tune.watermarkCenterY - height * 0.5f,
        tune.watermarkScale, Shade(tone_, tune.watermarkBrightness, tune.watermarkAlpha));
}

/** @brief ランクが無い時に使う既定の色を返す */
Vector4 ResultBackdrop::NeutralColor() {
    return Tune().neutralColor;
}

/** @brief 調整値をファイルから読み直す */
void ResultBackdrop::ReloadTuning() {
    MutableTune() = LoadTuning();
}

/** @brief 破片を1つ作り直す（atTop なら画面の上から、そうでなければ画面のどこか） */
void ResultBackdrop::RespawnShard(Shard& shard, bool atTop) {
    const BackdropTuning& tune = Tune();
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);
    const float size = tune.shardMinSize + (tune.shardMaxSize - tune.shardMinSize) * unit(rng_);
    // 破片らしく、縦横の比を少しばらつかせた細長い形にする
    constexpr float kMinAspect = 0.35f;
    shard.size = { size, size * (kMinAspect + (1.0f - kMinAspect) * unit(rng_)) };
    shard.position = { kScreenWidth * unit(rng_), atTop ? -size : kScreenHeight * unit(rng_) };
    // 大きい破片ほどゆっくり落とし、奥行きがあるように見せる
    const float fall = tune.shardFallSpeed * (0.5f + unit(rng_));
    shard.velocity = { tune.shardDrift * signedUnit(rng_), fall };
    shard.rotation = gameConstants::kPi * signedUnit(rng_);
    shard.spin = tune.shardSpin * signedUnit(rng_);
    shard.alpha = tune.shardMinAlpha + (tune.shardMaxAlpha - tune.shardMinAlpha) * unit(rng_);
    shard.phase = gameConstants::kPi * 2.0f * unit(rng_);
}

} // namespace engine::game
