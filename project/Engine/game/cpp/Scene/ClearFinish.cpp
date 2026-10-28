/**
 * @file ClearFinish.cpp
 * @brief クリアの決め演出（ClearFinish）の段階ごとの処理と、clear_finish.json の読み込み
 */
#include "ClearFinish.h"
#include "GameSounds.h"
#include "AudioBridge.h"
#include "Easing.h"
#include "GameConstants.h"
#include "GlassShatterEffect.h"
#include "HitEffectLibrary.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "Vector4.h"
#include <algorithm>
#include <cmath>
#include <string>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/clear_finish.json";
constexpr const char* kHoleTexture = "Resources/Effects/bulletHole.png";
constexpr const char* kSparkTexture = "Resources/Effects/bulletHitEffect.png";
constexpr size_t kUvSize = 2; // 弾痕の位置 [u, v] の要素数
const Vector2 kScreenCenterUV = { 0.5f, 0.5f };

/** @brief 決めの一太刀1本ぶんの設定 */
struct FinishSlash {
    float angleDegrees = -18.0f; ///< 斬線の傾き（画面の中心を通る）
    float delay = 0.0f; ///< 静止が明けてから振り始めるまでの秒数
};

/** @brief クリアの決め演出の調整値（clear_finish.json。初期値はJSONに項目が無い時の既定値） */
struct FinishTuning {
    // 決めの間の暗幕
    Vector4 dimColor = { 0.02f, 0.03f, 0.1f, 0.55f };
    float dimSeconds = 0.15f; ///< 暗幕が濃くなり切るまでの長さ
    // 細かい斬線（決めの一太刀の前に画面を斬り刻む）
    int cutCount = 16; ///< 本数（最大。スプライトはこの数だけ作る）
    int cutCountJitter = 4; ///< 本数を毎回この数までの範囲で減らす
    float cutInterval = 0.045f; ///< 次の斬線までの間隔
    float cutIntervalJitter = 0.02f; ///< 間隔のばらつき（±）
    float cutSpreadU = 0.32f; ///< 斬線が通る点の横の散らばり（画面の中心から±、画面の幅に対する割合）
    float cutSpreadV = 0.3f; ///< 縦の散らばり
    float cutSeconds = 0.05f; ///< 斬線が画面の端から端まで走る長さ
    float cutThickness = 4.0f; ///< 走った瞬間の芯の太さ
    float cutRemainThickness = 1.5f; ///< 走り終えた後に残る傷の太さ
    std::string cutFlash; ///< 斬線が走った瞬間の画面フラッシュ（空なら出さない）
    std::string cutImpact = "clear_cut"; ///< 斬線が走った瞬間のカメラ揺れ
    std::string cutSound; ///< 斬線が走った瞬間に鳴らす音（空なら鳴らさない）
    // 決めの一太刀
    float bladeSilenceSeconds = 0.45f; ///< 細かい斬線を入れ終えてから決めの一太刀までの静止
    float slashSeconds = 0.12f; ///< 斬線が画面の端から端まで走る長さ
    float slashThickness = 10.0f; ///< 振った瞬間の芯の太さ
    float slashRemainThickness = 3.0f; ///< 振り終えた後に残る傷の太さ
    float slashThinSeconds = 0.2f; ///< 走り終えてから傷の太さに細るまでの長さ（細かい斬線も同じ）
    float slashGlowScale = 4.0f; ///< 光の帯の太さ（芯の何倍か）
    Vector4 slashCoreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 slashGlowColor = { 0.7f, 0.9f, 1.0f, 0.35f };
    float slashHoldSeconds = 0.35f; ///< 決めの一太刀を振り終えてから割れ始めるまでの長さ
    float slashAngleJitter = 30.0f; ///< 決めの一太刀の傾きのばらつき（±度）
    float slashOffsetJitter = 0.1f; ///< 決めの一太刀が通る点を画面の中心からずらす量（±、画面に対する割合）
    std::string slashFlash = "clear_slash"; ///< 振った瞬間の画面フラッシュ（hit_effects.json の flashes）
    std::string slashImpact = "clear_slash"; ///< 振った瞬間のカメラ揺れ（impacts）
    std::string slashSound; ///< 振った瞬間に鳴らす音（空なら鳴らさない）
    // 弾の雨（決めの一発の前に画面へ浴びせる）
    int sprayCount = 14; ///< 弾の数（最大。スプライトはこの数だけ作る）
    int sprayCountJitter = 4; ///< 弾の数を毎回この数までの範囲で減らす
    float sprayInterval = 0.05f; ///< 次の弾までの間隔
    float sprayIntervalJitter = 0.02f; ///< 間隔のばらつき（±）
    float sprayMargin = 0.1f; ///< 弾痕を画面の端からどれだけ離すか（画面に対する割合）
    float sprayHoleScale = 0.6f; ///< 弾痕の大きさ（決めの一発の何倍か）
    std::string sprayFlash; ///< 弾が入った瞬間の画面フラッシュ（空なら出さない）
    std::string sprayImpact = "clear_spray"; ///< 弾が入った瞬間のカメラ揺れ
    std::string spraySound; ///< 弾が入った瞬間に鳴らす音（空なら鳴らさない）
    // 決めの一発
    float gunSilenceSeconds = 0.4f; ///< 弾を浴びせ終えてから決めの一発までの静止
    float shotInterval = 0.12f; ///< 決めの一発が複数ある時の間隔
    float holeSize = 220.0f; ///< 弾痕の大きさ（画面の座標。ひびの広がりも含めた画像の大きさ）
    float holePopScale = 1.15f; ///< 弾が入った瞬間の大きさ（元の何倍か）
    float holePopSeconds = 0.08f; ///< 元の大きさに縮むまでの長さ
    Vector4 holeColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float sparkScale = 0.6f; ///< 着弾の火花の大きさ（弾痕の何倍か）
    float sparkSeconds = 0.1f; ///< 着弾の火花が消えるまでの長さ
    Vector4 sparkColor = { 1.0f, 0.9f, 0.6f, 1.0f };
    float shotHoldSeconds = 0.35f; ///< 決めの一発が入ってから割れ始めるまでの長さ
    float shotJitter = 0.18f; ///< 決めの一発の位置のばらつき（±、画面に対する割合）
    std::string shotFlash = "clear_shot"; ///< 弾が入った瞬間の画面フラッシュ（flashes）
    std::string shotImpact = "clear_shot"; ///< 弾が入った瞬間のカメラ揺れ（impacts）
    std::string shotSound; ///< 弾が入った瞬間に鳴らす音（空なら鳴らさない）
    // 割れる
    float shatterSeconds = 3.2f; ///< ひびが広がって破片が散り、集まり直して結果画面になるまでの長さ
    float crackWidth = 0.005f; ///< ひびの線の太さ（画面の幅に対する割合）
    float shardSpeed = 0.9f; ///< 破片が落ちる速さ
    float soundVolume = 1.0f;
};

constexpr JsonField<FinishTuning> kFinishFields[] = {
    { nullptr, "dimColor", &FinishTuning::dimColor },
    { nullptr, "dimSeconds", &FinishTuning::dimSeconds },
    { "cuts", "count", &FinishTuning::cutCount },
    { "cuts", "countJitter", &FinishTuning::cutCountJitter },
    { "cuts", "interval", &FinishTuning::cutInterval },
    { "cuts", "intervalJitter", &FinishTuning::cutIntervalJitter },
    { "cuts", "spreadU", &FinishTuning::cutSpreadU },
    { "cuts", "spreadV", &FinishTuning::cutSpreadV },
    { "cuts", "seconds", &FinishTuning::cutSeconds },
    { "cuts", "thickness", &FinishTuning::cutThickness },
    { "cuts", "remainThickness", &FinishTuning::cutRemainThickness },
    { "cuts", "flash", &FinishTuning::cutFlash },
    { "cuts", "impact", &FinishTuning::cutImpact },
    { "cuts", "sound", &FinishTuning::cutSound },
    { "blade", "silenceSeconds", &FinishTuning::bladeSilenceSeconds },
    { "blade", "slashSeconds", &FinishTuning::slashSeconds },
    { "blade", "thickness", &FinishTuning::slashThickness },
    { "blade", "remainThickness", &FinishTuning::slashRemainThickness },
    { "blade", "thinSeconds", &FinishTuning::slashThinSeconds },
    { "blade", "glowScale", &FinishTuning::slashGlowScale },
    { "blade", "coreColor", &FinishTuning::slashCoreColor },
    { "blade", "glowColor", &FinishTuning::slashGlowColor },
    { "blade", "holdSeconds", &FinishTuning::slashHoldSeconds },
    { "blade", "angleJitter", &FinishTuning::slashAngleJitter },
    { "blade", "offsetJitter", &FinishTuning::slashOffsetJitter },
    { "blade", "flash", &FinishTuning::slashFlash },
    { "blade", "impact", &FinishTuning::slashImpact },
    { "blade", "sound", &FinishTuning::slashSound },
    { "spray", "count", &FinishTuning::sprayCount },
    { "spray", "countJitter", &FinishTuning::sprayCountJitter },
    { "spray", "interval", &FinishTuning::sprayInterval },
    { "spray", "intervalJitter", &FinishTuning::sprayIntervalJitter },
    { "spray", "margin", &FinishTuning::sprayMargin },
    { "spray", "holeScale", &FinishTuning::sprayHoleScale },
    { "spray", "flash", &FinishTuning::sprayFlash },
    { "spray", "impact", &FinishTuning::sprayImpact },
    { "spray", "sound", &FinishTuning::spraySound },
    { "gun", "silenceSeconds", &FinishTuning::gunSilenceSeconds },
    { "gun", "interval", &FinishTuning::shotInterval },
    { "gun", "holeSize", &FinishTuning::holeSize },
    { "gun", "popScale", &FinishTuning::holePopScale },
    { "gun", "popSeconds", &FinishTuning::holePopSeconds },
    { "gun", "holeColor", &FinishTuning::holeColor },
    { "gun", "sparkScale", &FinishTuning::sparkScale },
    { "gun", "sparkSeconds", &FinishTuning::sparkSeconds },
    { "gun", "sparkColor", &FinishTuning::sparkColor },
    { "gun", "holdSeconds", &FinishTuning::shotHoldSeconds },
    { "gun", "shotJitter", &FinishTuning::shotJitter },
    { "gun", "flash", &FinishTuning::shotFlash },
    { "gun", "impact", &FinishTuning::shotImpact },
    { "gun", "sound", &FinishTuning::shotSound },
    { "shatter", "seconds", &FinishTuning::shatterSeconds },
    { "shatter", "crackWidth", &FinishTuning::crackWidth },
    { "shatter", "shardSpeed", &FinishTuning::shardSpeed },
    { nullptr, "soundVolume", &FinishTuning::soundVolume },
};

/** @brief 調整値と、決めの一太刀・決めの一発の並びをまとめた読み込み結果 */
struct FinishConfig {
    FinishTuning tuning;
    std::vector<FinishSlash> slashes = { FinishSlash {} };
    std::vector<Vector2> shots = { kScreenCenterUV };
};

/** @brief blade.slashes（{ angleDegrees, delay } の配列）を読む（無ければ fallback を返す） */
std::vector<FinishSlash> ReadSlashes(const nlohmann::json& root, const std::vector<FinishSlash>& fallback) {
    const auto blade = root.find("blade");
    if (blade == root.end() || !blade->contains("slashes") || !(*blade)["slashes"].is_array()) {
        return fallback;
    }
    std::vector<FinishSlash> slashes;
    for (const auto& item : (*blade)["slashes"]) {
        FinishSlash slash;
        slash.angleDegrees = item.value("angleDegrees", slash.angleDegrees);
        slash.delay = item.value("delay", slash.delay);
        slashes.push_back(slash);
    }
    return slashes.empty() ? fallback : slashes;
}

/** @brief gun.shots（画面上の位置 [u, v] の配列、0〜1）を読む（無ければ fallback を返す） */
std::vector<Vector2> ReadShots(const nlohmann::json& root, const std::vector<Vector2>& fallback) {
    const auto gun = root.find("gun");
    if (gun == root.end() || !gun->contains("shots") || !(*gun)["shots"].is_array()) {
        return fallback;
    }
    std::vector<Vector2> shots;
    for (const auto& item : (*gun)["shots"]) {
        if (item.is_array() && item.size() >= kUvSize) {
            shots.push_back({ item[0].get<float>(), item[1].get<float>() });
        }
    }
    return shots.empty() ? fallback : shots;
}

/** @brief clear_finish.json からクリアの決め演出の設定を読み込む */
FinishConfig LoadConfig() {
    FinishConfig config;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("ClearFinish: ") + kConfigPath + " が読めません。既定値で動きます");
        return config;
    }
    ReadJsonFields(root, kFinishFields, config.tuning);
    config.slashes = ReadSlashes(root, config.slashes);
    config.shots = ReadShots(root, config.shots);
    // 本数は負にしない（スプライトを最初にこの数だけ作る）
    config.tuning.cutCount = (std::max)(config.tuning.cutCount, 0);
    config.tuning.sprayCount = (std::max)(config.tuning.sprayCount, 0);
    // 減らす数は 0〜本数 の範囲に収める
    config.tuning.cutCountJitter = std::clamp(config.tuning.cutCountJitter, 0, config.tuning.cutCount);
    config.tuning.sprayCountJitter = std::clamp(config.tuning.sprayCountJitter, 0, config.tuning.sprayCount);
    return config;
}

/** @brief 読み込み済みの設定（初回に読む。変更は次回起動時に反映） */
const FinishConfig& Config() {
    static const FinishConfig kConfig = LoadConfig();
    return kConfig;
}

/** @brief 読み込み済みの調整値を取得する */
const FinishTuning& Tune() {
    return Config().tuning;
}

/** @brief 斬線・弾痕が入った瞬間の揺れ・フラッシュ・音を出す（名前が空のものは出さない） */
void PlayStrikeEffects(const HitEffectPlayer& fx, const std::string& flash, const std::string& impact, const std::string& sound) {
    if (!flash.empty()) {
        fx.Flash(flash);
    }
    if (!impact.empty()) {
        fx.Impact(impact);
    }
    if (!sound.empty()) {
        AudioBridge::GetInstance()->PlaySE(sound, Tune().soundVolume);
    }
}

/** @brief 画面の対角の長さ（斬線をこれより長く伸ばし、傾けても端が見えないようにする） */
float ScreenDiagonal() {
    return std::hypot(gameConstants::kScreenWidth, gameConstants::kScreenHeight);
}

/** @brief 画面に対する0〜1の位置を画面の座標にする */
Vector2 ToScreen(const Vector2& uv) {
    return { uv.x * gameConstants::kScreenWidth, uv.y * gameConstants::kScreenHeight };
}

/** @brief min 以上 max 未満の乱数を1つ作る */
float RandomRange(std::mt19937& rng, float min, float max) {
    return std::uniform_real_distribution<float>(min, max)(rng);
}

/** @brief 最大の本数から 0〜jitter 本を減らした本数を返す（スプライトは最大の本数ぶんしか無いので増やさない） */
int RandomCount(std::mt19937& rng, int maxCount, int jitter) {
    return maxCount - std::uniform_int_distribution<int>(0, jitter)(rng);
}

/**
 * @brief 間隔にばらつきを持たせて、count 個ぶんの入る時刻を並べる
 * @return 最後の時刻（count が0なら0）
 */
float FillScatteredDelays(std::vector<float>& delays, int count, float interval, float jitter, std::mt19937& rng) {
    float time = 0.0f;
    for (int i = 0; i < count; ++i) {
        delays.push_back(time);
        // 次までの間隔は ±jitter でばらつかせ、同時に入ったり逆戻りしたりしないよう0未満にはしない
        time += (std::max)(interval + RandomRange(rng, -jitter, jitter), 0.0f);
    }
    return delays.empty() ? 0.0f : delays.back();
}
} // namespace

// ══════════════════════════════════════════════════════
// 斬線か弾痕か（倒し方ごとの違い）
// ══════════════════════════════════════════════════════

/** @brief 倒し方ごとに、入れる印の並び・入った時の演出・描き方・割り方を決める */
class ClearFinish::IStrikeStyle {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IStrikeStyle() = default;
    /** @brief 入れる順に並べた斬線・弾痕を作る */
    virtual std::vector<Mark> BuildMarks(std::mt19937& rng) const = 0;
    /** @brief 印が入った瞬間の揺れ・フラッシュ・音を出す */
    virtual void PlayHit(const HitEffectPlayer& fx, const Mark& mark) const = 0;
    /** @brief 印を1つ描く（since は入ってからの秒数） */
    virtual void Draw(ClearFinish& finish, size_t index, float since) const = 0;
    /** @brief 最後の印が入ってから割れ始めるまでの長さ */
    virtual float TailSeconds() const = 0;
    /** @brief 割る前に、割れ方（斬線に沿うか、起点から広がるか）を設定する */
    virtual void PrepareGlass(ClearFinish& finish, GlassShatterEffect& glass) const = 0;
};

/** @brief 剣など: 細かい斬線で画面を斬り刻み、静止の後に中心を通る一太刀 → 斬線に沿って割れる */
class ClearFinish::BladeStyle final : public ClearFinish::IStrikeStyle {
public:
    /** @brief 散らばった細かい斬線と、静止の後の決めの一太刀を並べる */
    std::vector<Mark> BuildMarks(std::mt19937& rng) const override {
        const FinishTuning& tune = Tune();
        std::vector<float> delays;
        const int cutCount = RandomCount(rng, tune.cutCount, tune.cutCountJitter);
        const float lastCut = FillScatteredDelays(delays, cutCount, tune.cutInterval, tune.cutIntervalJitter, rng);
        std::vector<Mark> marks;
        // 細かい斬線は画面の中心のまわりへ散らし、向きはどの方向にも振る
        for (float delay : delays) {
            Mark mark;
            mark.uv = { kScreenCenterUV.x + RandomRange(rng, -tune.cutSpreadU, tune.cutSpreadU),
                kScreenCenterUV.y + RandomRange(rng, -tune.cutSpreadV, tune.cutSpreadV) };
            mark.angle = RandomRange(rng, 0.0f, gameConstants::kPi);
            mark.delay = delay;
            marks.push_back(mark);
        }
        // 決めの一太刀は静止が明けてから、画面の中心の近くを通して振る（通る点と傾きを毎回ずらし、割れ方も毎回変える）
        const float finaleStart = delays.empty() ? 0.0f : lastCut + tune.cutSeconds + tune.bladeSilenceSeconds;
        for (const FinishSlash& slash : Config().slashes) {
            const Vector2 through = { kScreenCenterUV.x + RandomRange(rng, -tune.slashOffsetJitter, tune.slashOffsetJitter),
                kScreenCenterUV.y + RandomRange(rng, -tune.slashOffsetJitter, tune.slashOffsetJitter) };
            const float angleDegrees = slash.angleDegrees + RandomRange(rng, -tune.slashAngleJitter, tune.slashAngleJitter);
            marks.push_back({ through, angleDegrees * gameConstants::kDegToRad, finaleStart + slash.delay, true });
        }
        return marks;
    }
    /** @brief 細かい斬線は軽く、決めの一太刀は強く揺らす */
    void PlayHit(const HitEffectPlayer& fx, const Mark& mark) const override {
        const FinishTuning& tune = Tune();
        if (mark.finale) {
            PlayStrikeEffects(fx, tune.slashFlash, tune.slashImpact, tune.slashSound);
        } else {
            PlayStrikeEffects(fx, tune.cutFlash, tune.cutImpact, tune.cutSound);
        }
    }
    /** @brief 印の点を通る線を、片方の端からもう片方の端へ一気に走らせ、走り終えたら細い傷にする */
    void Draw(ClearFinish& finish, size_t index, float since) const override {
        const FinishTuning& tune = Tune();
        const Mark& mark = finish.marks_[index];
        // 太さと走る長さは、決めの一太刀か細かい斬線かで変える
        const float sweepSeconds = mark.finale ? tune.slashSeconds : tune.cutSeconds;
        const float thickness = mark.finale ? tune.slashThickness : tune.cutThickness;
        const float remain = mark.finale ? tune.slashRemainThickness : tune.cutRemainThickness;
        const float sweep = easing::EaseOutCubic(easing::Progress(since, sweepSeconds));
        const float thin = easing::EaseOutCubic(easing::Progress(since - sweepSeconds, tune.slashThinSeconds));
        const float width = thickness + (remain - thickness) * thin;
        // 線は通る点から両側へ画面の対角ぶん伸ばし、傾けても端が画面に入らないようにする
        const float length = ScreenDiagonal();
        const Vector2 through = ToScreen(mark.uv);
        const Vector2 start = {
            through.x - std::cos(mark.angle) * length * 0.5f,
            through.y - std::sin(mark.angle) * length * 0.5f
        };
        auto drawLine = [&](Sprite& sprite, float lineWidth, const Vector4& color) {
            sprite.SetPosition(start);
            sprite.SetSize({ length * sweep, lineWidth });
            sprite.SetRotation(mark.angle);
            sprite.SetColor(color);
            sprite.Update();
            sprite.Draw();
        };
        drawLine(*finish.slashGlows_[index], width * tune.slashGlowScale, tune.slashGlowColor);
        drawLine(*finish.slashCores_[index], width, tune.slashCoreColor);
    }
    /** @brief 決めの一太刀が走り、細り、少し待つまで */
    float TailSeconds() const override {
        const FinishTuning& tune = Tune();
        return tune.slashSeconds + tune.slashThinSeconds + tune.slashHoldSeconds;
    }
    /** @brief 入れた斬線すべてに沿って割り、決めの一太刀が通った所から順に崩す */
    void PrepareGlass(ClearFinish& finish, GlassShatterEffect& glass) const override {
        std::vector<GlassShatterEffect::CutLine> lines;
        for (const Mark& mark : finish.marks_) {
            lines.push_back({ mark.uv.x, mark.uv.y, mark.angle });
        }
        glass.SetCutLines(lines);
        glass.SetImpactPoints({});
        finish.impactUV_ = finish.marks_.empty() ? kScreenCenterUV : finish.marks_.back().uv;
    }
};

/** @brief 銃: 画面に弾を浴びせ、静止の後に決めの一発 → 最後の弾痕からひびが広がって割れる */
class ClearFinish::GunStyle final : public ClearFinish::IStrikeStyle {
public:
    /** @brief 散らばった弾の雨と、静止の後の決めの一発を並べる */
    std::vector<Mark> BuildMarks(std::mt19937& rng) const override {
        const FinishTuning& tune = Tune();
        std::vector<float> delays;
        const int sprayCount = RandomCount(rng, tune.sprayCount, tune.sprayCountJitter);
        const float lastSpray = FillScatteredDelays(delays, sprayCount, tune.sprayInterval, tune.sprayIntervalJitter, rng);
        std::vector<Mark> marks;
        // 弾の雨は画面の端を少し避けて散らし、弾痕の向きもばらつかせて同じ形が並ばないようにする
        for (float delay : delays) {
            Mark mark;
            mark.uv = { RandomRange(rng, tune.sprayMargin, 1.0f - tune.sprayMargin),
                RandomRange(rng, tune.sprayMargin, 1.0f - tune.sprayMargin) };
            mark.angle = RandomRange(rng, 0.0f, gameConstants::kTwoPi);
            mark.delay = delay;
            marks.push_back(mark);
        }
        // 決めの一発は静止が明けてから、JSONで決めた位置の近くへ順に入れる（毎回ずらしてひびの広がり方を変える。画面の端は避ける）
        const float finaleStart = delays.empty() ? 0.0f : lastSpray + tune.holePopSeconds + tune.gunSilenceSeconds;
        const auto& shots = Config().shots;
        const float low = tune.sprayMargin;
        const float high = 1.0f - tune.sprayMargin;
        for (size_t i = 0; i < shots.size(); ++i) {
            const Vector2 position = {
                std::clamp(shots[i].x + RandomRange(rng, -tune.shotJitter, tune.shotJitter), low, high),
                std::clamp(shots[i].y + RandomRange(rng, -tune.shotJitter, tune.shotJitter), low, high)
            };
            marks.push_back({ position, RandomRange(rng, 0.0f, gameConstants::kTwoPi),
                finaleStart + tune.shotInterval * static_cast<float>(i), true });
        }
        return marks;
    }
    /** @brief 弾の雨は軽く、決めの一発は強く揺らす */
    void PlayHit(const HitEffectPlayer& fx, const Mark& mark) const override {
        const FinishTuning& tune = Tune();
        if (mark.finale) {
            PlayStrikeEffects(fx, tune.shotFlash, tune.shotImpact, tune.shotSound);
        } else {
            PlayStrikeEffects(fx, tune.sprayFlash, tune.sprayImpact, tune.spraySound);
        }
    }
    /** @brief 弾痕を、入った瞬間は少し大きく出してすぐ元の大きさに縮め、その上に着弾の火花を一瞬だけ重ねる */
    void Draw(ClearFinish& finish, size_t index, float since) const override {
        const FinishTuning& tune = Tune();
        const Mark& mark = finish.marks_[index];
        const Vector2 center = ToScreen(mark.uv);
        const float settle = easing::EaseOutCubic(easing::Progress(since, tune.holePopSeconds));
        const float baseSize = tune.holeSize * (mark.finale ? 1.0f : tune.sprayHoleScale);
        const float size = baseSize * (tune.holePopScale + (1.0f - tune.holePopScale) * settle);
        Sprite& hole = *finish.holes_[index];
        hole.SetPosition(center);
        hole.SetSize({ size, size });
        hole.SetRotation(mark.angle);
        hole.SetColor(tune.holeColor);
        hole.Update();
        hole.Draw();
        // 火花は入った瞬間に最も大きく明るく、消えるまでの間に縮みながら薄れる
        const float fade = easing::EaseOutCubic(easing::Progress(since, tune.sparkSeconds));
        if (fade >= 1.0f) {
            return;
        }
        const float sparkSize = baseSize * tune.sparkScale * (1.0f - fade * 0.5f);
        Vector4 sparkColor = tune.sparkColor;
        sparkColor.w *= 1.0f - fade;
        Sprite& spark = *finish.sparks_[index];
        spark.SetPosition(center);
        spark.SetSize({ sparkSize, sparkSize });
        spark.SetRotation(mark.angle);
        spark.SetColor(sparkColor);
        spark.Update();
        spark.Draw();
    }
    /** @brief 決めの一発が縮み、少し待つまで */
    float TailSeconds() const override {
        const FinishTuning& tune = Tune();
        return tune.holePopSeconds + tune.shotHoldSeconds;
    }
    /** @brief すべての弾痕を起点に、放射状のひびを広げて弾痕の近くから割る */
    void PrepareGlass(ClearFinish& finish, GlassShatterEffect& glass) const override {
        glass.SetCutLines({});
        std::vector<GlassShatterEffect::ImpactPoint> points;
        for (const Mark& mark : finish.marks_) {
            points.push_back({ mark.uv.x, mark.uv.y });
        }
        glass.SetImpactPoints(points);
        finish.impactUV_ = finish.marks_.empty() ? kScreenCenterUV : finish.marks_.back().uv;
    }
};

// ══════════════════════════════════════════════════════
// 段階
// ══════════════════════════════════════════════════════

/** @brief 演出の1段階（状態はClearFinish側が持ち、段階は時間の進め方だけを決める） */
class ClearFinish::IPhase {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IPhase() = default;
    /** @brief この状態へ入る際の初期設定を適用する */
    virtual void Enter(ClearFinish&) const { }
    /** @return この段階を終えたら true */
    virtual bool Update(ClearFinish& finish) const = 0;
};

/** @brief 並べた斬線・弾痕を時刻どおりに入れ、最後の一撃から少し待って終える */
class ClearFinish::StrikePhase final : public ClearFinish::IPhase {
public:
    /** @brief 決めを始める */
    void Enter(ClearFinish& finish) const override {
        finish.striking_ = true;
    }
    /** @brief 時刻が来た印を入れ、最後の印の後の待ちが済んだら終える */
    bool Update(ClearFinish& finish) const override {
        finish.StartDueMarks();
        return finish.marksStarted_ >= finish.marks_.size()
            && finish.strikeTimer_ >= finish.LastMarkDelay() + finish.style_->TailSeconds();
    }
};

/** @brief 決めを入れた画面をガラスのように割り、破片を落として後ろの結果画面を見せる */
class ClearFinish::ShatterPhase final : public ClearFinish::IPhase {
public:
    /** @brief 割れ方と速さを決めて割り始める（割る前の画面はシーンが次の描画で撮る） */
    void Enter(ClearFinish& finish) const override {
        const FinishTuning& tune = Tune();
        finish.striking_ = false;
        GlassShatterEffect* glass = finish.services_.glass;
        if (glass == nullptr) {
            return;
        }
        finish.style_->PrepareGlass(finish, *glass);
        glass->SetImpactUV(finish.impactUV_.x, finish.impactUV_.y);
        glass->SetCrackWidth(tune.crackWidth);
        glass->SetShardSpeed(tune.shardSpeed);
        glass->SetDuration(tune.shatterSeconds);
        // 割れて散った破片が集まり直し、はまった所から後ろの結果画面が見える
        glass->SetReassemble(true);
        glass->Start();
        gameSounds::Play(SoundCue::kGlassShatter);
    }
    /** @brief 破片が落ち切ったら終える */
    bool Update(ClearFinish& finish) const override {
        GlassShatterEffect* glass = finish.services_.glass;
        if (glass == nullptr) {
            return true;
        }
        glass->Update(gameConstants::kFrameDeltaTime);
        return glass->IsFinished();
    }
};

/** @brief 割れ落ちた後の結果画面を決めた長さだけ見せる */
class ClearFinish::ResultPhase final : public ClearFinish::IPhase {
public:
    /** @brief 結果画面が出たことをシーンへ伝える（シーンはこれでクリアの曲を流す） */
    void Enter(ClearFinish& finish) const override {
        finish.resultShownPending_ = true;
    }
    /** @brief 結果画面を見せ終えたら終える */
    bool Update(ClearFinish& finish) const override {
        return finish.phaseTimer_ >= finish.setup_.resultSeconds;
    }
};

namespace {
/** @brief 段階の並び（決め → 割れる → 結果。倒し方の違いは IStrikeStyle が持つ） */
const std::vector<const ClearFinish::IPhase*>& Phases() {
    static const ClearFinish::StrikePhase kStrike;
    static const ClearFinish::ShatterPhase kShatter;
    static const ClearFinish::ResultPhase kResult;
    static const std::vector<const ClearFinish::IPhase*> kPhases = { &kStrike, &kShatter, &kResult };
    return kPhases;
}

/** @brief 倒し方に合う見せ方（Style の並び順で引く） */
const ClearFinish::IStrikeStyle& StyleFor(ClearFinish::Style style) {
    static const ClearFinish::BladeStyle kBlade;
    static const ClearFinish::GunStyle kGun;
    static const ClearFinish::IStrikeStyle* const kStyles[] = { &kBlade, &kGun };
    return *kStyles[static_cast<size_t>(style)];
}
} // namespace

// ══════════════════════════════════════════════════════
// 進行
// ══════════════════════════════════════════════════════

ClearFinish::ClearFinish() = default;
/** @brief unique_ptr で持つ不完全型のメンバを破棄するため、定義を cpp に置く */
ClearFinish::~ClearFinish() = default;

/** @brief 暗幕・斬線・弾痕のスプライトを作る（シーンの Initialize で1回） */
void ClearFinish::Initialize(SpriteCommon* spriteCommon) {
    // 途中で作ったり捨てたりしないよう、設定にある本数ぶんを最初に作っておく
    auto create = [spriteCommon](const char* texture) {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon, texture);
        return sprite;
    };
    dim_ = create("Resources/white.png");
    dim_->SetPosition({ 0.0f, 0.0f });
    dim_->SetSize({ gameConstants::kScreenWidth, gameConstants::kScreenHeight });
    const FinishConfig& config = Config();
    const size_t slashCount = static_cast<size_t>(config.tuning.cutCount) + config.slashes.size();
    for (size_t i = 0; i < slashCount; ++i) {
        // 斬線は始点（左端の中央）を基準に伸ばす
        slashGlows_.push_back(create("Resources/white.png"));
        slashCores_.push_back(create("Resources/white.png"));
        slashGlows_.back()->SetAnchorPoint({ 0.0f, 0.5f });
        slashCores_.back()->SetAnchorPoint({ 0.0f, 0.5f });
    }
    const size_t holeCount = static_cast<size_t>(config.tuning.sprayCount) + config.shots.size();
    for (size_t i = 0; i < holeCount; ++i) {
        // 弾痕と着弾の火花は中心を基準に置く
        holes_.push_back(create(kHoleTexture));
        holes_.back()->SetAnchorPoint({ 0.5f, 0.5f });
        sparks_.push_back(create(kSparkTexture));
        sparks_.back()->SetAnchorPoint({ 0.5f, 0.5f });
    }
}

/** @brief 演出を最初から始める */
void ClearFinish::Start(const Setup& setup, const Services& services) {
    setup_ = setup;
    services_ = services;
    style_ = &StyleFor(setup.style);
    phases_ = Phases();
    phaseIndex_ = 0;
    phaseTimer_ = 0.0f;
    active_ = true;
    playing_ = true;
    finishedPending_ = false;
    resultShownPending_ = false;
    striking_ = false;
    strikeTimer_ = 0.0f;
    dimAlpha_ = 0.0f;
    marksStarted_ = 0;
    impactUV_ = kScreenCenterUV;
    // 斬線・弾痕の位置と向きは始めるたびに作り直し、毎回違う斬られ方に見せる（乱数が無ければ固定の種で作る）
    std::mt19937 fallbackRng;
    marks_ = style_->BuildMarks(services_.rng != nullptr ? *services_.rng : fallbackRng);
    phases_[phaseIndex_]->Enter(*this);
}

/** @brief 結果画面が出た最初の1回だけ true を返す */
bool ClearFinish::ConsumeResultShown() {
    const bool shown = resultShownPending_;
    resultShownPending_ = false;
    return shown;
}

/** @brief 1フレーム進める */
void ClearFinish::Update() {
    if (!playing_) {
        return;
    }
    phaseTimer_ += gameConstants::kFrameDeltaTime;
    if (striking_) {
        strikeTimer_ += gameConstants::kFrameDeltaTime;
        UpdateDim();
    }
    if (phases_[phaseIndex_]->Update(*this)) {
        AdvancePhase();
    }
}

/** @brief 段階を次へ進める（最後まで行ったら終える） */
void ClearFinish::AdvancePhase() {
    ++phaseIndex_;
    phaseTimer_ = 0.0f;
    if (phaseIndex_ >= phases_.size()) {
        // 結果画面はシーンが切り替わるまで出し続ける
        playing_ = false;
        finishedPending_ = true;
        return;
    }
    phases_[phaseIndex_]->Enter(*this);
}

/** @brief 決めの間、画面を暗くしていく */
void ClearFinish::UpdateDim() {
    dimAlpha_ = easing::EaseOutCubic(easing::Progress(strikeTimer_, Tune().dimSeconds));
}

/** @brief 入る時刻が来た斬線・弾痕の揺れとフラッシュを出す */
void ClearFinish::StartDueMarks() {
    while (marksStarted_ < marks_.size() && strikeTimer_ >= marks_[marksStarted_].delay) {
        const HitEffectPlayer fx(services_.particles, services_.shaker, services_.rng);
        style_->PlayHit(fx, marks_[marksStarted_]);
        ++marksStarted_;
    }
}

/** @brief 最後の斬線・弾痕が入る時刻 */
float ClearFinish::LastMarkDelay() const {
    return marks_.empty() ? 0.0f : marks_.back().delay;
}

/** @brief 割る前のゲーム画面を描くべきか（決めの間と、割る前の画面を撮る1フレーム） */
bool ClearFinish::ShowsGameFrame() const {
    return striking_ || (services_.glass != nullptr && services_.glass->NeedCapture());
}

/** @brief 最後まで見せ終えた最初の1回だけ true を返す */
bool ClearFinish::ConsumeFinished() {
    const bool finished = finishedPending_;
    finishedPending_ = false;
    return finished;
}

/** @brief 暗幕・斬線・弾痕を描く（ゲーム画面の上、割る前の画面を撮る前に） */
void ClearFinish::DrawStrikeOverlay() {
    if (!active_ || !ShowsGameFrame()) {
        return;
    }
    Vector4 dimColor = Tune().dimColor;
    dimColor.w *= dimAlpha_;
    dim_->SetColor(dimColor);
    dim_->Update();
    dim_->Draw();
    // 入った印だけを、入ってからの秒数に合わせて描く
    for (size_t i = 0; i < marksStarted_; ++i) {
        style_->Draw(*this, i, strikeTimer_ - marks_[i].delay);
    }
}

} // namespace engine::game
