/**
 * @file StageLiberation.cpp
 * @brief マップへ戻った時のステージ解放演出（StageLiberation）の段階ごとの処理と、stage_liberation.json の読み込み
 */
#include "StageLiberation.h"
#include "AudioBridge.h"
#include "Easing.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "ScreenFlash.h"
#include "StringUtility.h"
#include <cmath>
#include <iterator>
#include <string>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/stage_liberation.json";
constexpr const char* kPillarTexture = "Resources/Effects/gradationLine.png";
constexpr const char* kOrbTexture = "Resources/Effects/circle2.png";
constexpr const char* kStageNumberToken = "{n}"; // 解放の文字のうち、ステージ番号に置き換える部分

/** @brief ステージ解放演出の調整値（stage_liberation.json。初期値はJSONに項目が無い時の既定値） */
struct LiberationTuning {
    // クリアした入口が光り出す
    float glowSeconds = 0.7f; ///< 入口が光り切るまでの長さ
    Vector4 glowColor = { 1.0f, 0.95f, 0.7f, 1.0f }; ///< 光り切った入口の色
    // 光の柱と閃光
    Vector4 flashColor = { 1.0f, 0.97f, 0.85f, 0.9f }; ///< 画面全体の閃光
    float flashSeconds = 0.5f;
    float pillarSeconds = 1.4f; ///< 光の柱が立って消えるまでの長さ
    float pillarWidth = 260.0f; ///< 光の柱の太さ（広がり切った時、画面の座標）
    Vector4 pillarColor = { 1.0f, 0.92f, 0.6f, 0.85f };
    float orbSize = 420.0f; ///< 入口からあふれる光の玉の大きさ（広がり切った時、画面の座標）
    Vector4 orbColor = { 1.0f, 0.95f, 0.75f, 0.9f };
    float washSeconds = 1.1f; ///< 画面全体を覆う光が引くまでの長さ
    Vector4 washColor = { 1.0f, 0.95f, 0.75f, 0.45f };
    float popScale = 0.35f; ///< 入口がふくらむ量（元の大きさに足す割合）
    float popSeconds = 0.45f;
    std::string sound; ///< 光った瞬間に鳴らす音（空なら鳴らさない）
    float soundVolume = 1.0f;
    // 解放の文字
    std::string text = "ステージ{n} クリア！"; ///< {n} にステージ番号（1から）が入る
    float textY = 150.0f; ///< 文字の高さ（画面の座標）
    float textScale = 2.6f;
    Vector4 textColor = { 1.0f, 0.9f, 0.35f, 1.0f };
    float textPopSeconds = 0.5f; ///< 文字が弾んで落ち着くまでの長さ
    float burstHoldSeconds = 1.3f; ///< 光ってから次の入口へカメラを移すまでの長さ
    // 次の入口へ
    float panSeconds = 0.9f; ///< カメラが次の入口へ移る長さ
    float openSeconds = 0.5f; ///< 次の入口が開いた色になるまでの長さ
    float openHoldSeconds = 0.4f; ///< 開いてから操作を戻すまでの長さ
    std::string openSound; ///< 次の入口が開いた瞬間に鳴らす音（空なら鳴らさない）
};

constexpr JsonField<LiberationTuning> kLiberationFields[] = {
    { "glow", "seconds", &LiberationTuning::glowSeconds },
    { "glow", "color", &LiberationTuning::glowColor },
    { "burst", "flashColor", &LiberationTuning::flashColor },
    { "burst", "flashSeconds", &LiberationTuning::flashSeconds },
    { "burst", "pillarSeconds", &LiberationTuning::pillarSeconds },
    { "burst", "pillarWidth", &LiberationTuning::pillarWidth },
    { "burst", "pillarColor", &LiberationTuning::pillarColor },
    { "burst", "orbSize", &LiberationTuning::orbSize },
    { "burst", "orbColor", &LiberationTuning::orbColor },
    { "burst", "washSeconds", &LiberationTuning::washSeconds },
    { "burst", "washColor", &LiberationTuning::washColor },
    { "burst", "popScale", &LiberationTuning::popScale },
    { "burst", "popSeconds", &LiberationTuning::popSeconds },
    { "burst", "sound", &LiberationTuning::sound },
    { "burst", "soundVolume", &LiberationTuning::soundVolume },
    { "burst", "holdSeconds", &LiberationTuning::burstHoldSeconds },
    { "text", "format", &LiberationTuning::text },
    { "text", "y", &LiberationTuning::textY },
    { "text", "scale", &LiberationTuning::textScale },
    { "text", "color", &LiberationTuning::textColor },
    { "text", "popSeconds", &LiberationTuning::textPopSeconds },
    { "open", "panSeconds", &LiberationTuning::panSeconds },
    { "open", "seconds", &LiberationTuning::openSeconds },
    { "open", "holdSeconds", &LiberationTuning::openHoldSeconds },
    { "open", "sound", &LiberationTuning::openSound },
};

/** @brief stage_liberation.json からステージ解放演出の調整値を読み込む */
LiberationTuning LoadTuning() {
    LiberationTuning tuning;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("StageLiberation: ") + kConfigPath + " が読めません。既定値で動きます");
        return tuning;
    }
    ReadJsonFields(root, kLiberationFields, tuning);
    return tuning;
}

/** @brief 読み込み済みの調整値の置き場（初回に読み、調整値エディタで保存した時に読み直す） */
LiberationTuning& MutableTune() {
    static LiberationTuning tuning = LoadTuning();
    return tuning;
}

/** @brief 読み込み済みの調整値 */
const LiberationTuning& Tune() {
    return MutableTune();
}

/** @brief 音のパスが書いてあれば鳴らす */
void PlayLiberationSound(const std::string& path) {
    if (!path.empty()) {
        AudioBridge::GetInstance()->PlaySE(path, Tune().soundVolume);
    }
}

/** @brief 一度ふくらんで元へ戻る量（0〜1、山なり） */
float PopAmount(float elapsed, float duration) {
    return std::sin(easing::Progress(elapsed, duration) * gameConstants::kPi);
}
} // namespace

// ══════════════════════════════════════════════════════
// 段階
// ══════════════════════════════════════════════════════

/** @brief 演出の1段階（状態はStageLiberation側が持ち、段階は時間の進め方と見せ方だけを決める） */
class StageLiberation::IPhase {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IPhase() = default;
    /** @brief この状態へ入る際の初期設定を適用する */
    virtual void Enter(StageLiberation&) const { }
    /** @return この段階を終えたら true */
    virtual bool Update(StageLiberation& liberation) const = 0;
};

/** @brief カメラをクリアした入口に合わせ、入口をだんだん光らせる */
class StageLiberation::GlowPhase final : public StageLiberation::IPhase {
public:
    /** @brief 入口が光り切ったら終える */
    bool Update(StageLiberation& liberation) const override {
        liberation.cameraX_ = liberation.setup_.clearedX;
        liberation.glow_ = easing::EaseInQuad(easing::Progress(liberation.phaseTimer_, Tune().glowSeconds));
        return liberation.phaseTimer_ >= Tune().glowSeconds;
    }
};

/** @brief 閃光と光の柱で画面全体を光らせ、解放の文字を弾ませる */
class StageLiberation::BurstPhase final : public StageLiberation::IPhase {
public:
    /** @brief 閃光を出して音を鳴らす（入口は光の柱が消えるまで光ったままにする） */
    void Enter(StageLiberation& liberation) const override {
        const LiberationTuning& tune = Tune();
        liberation.glow_ = 1.0f;
        liberation.pillar_ = 1.0f;
        ScreenFlash::GetInstance()->Request(tune.flashColor, tune.flashSeconds);
        PlayLiberationSound(tune.sound);
    }
    /** @brief 光の柱が太って消え、画面の光が引き、文字が弾んで落ち着く（決めた長さ見せたら終える） */
    bool Update(StageLiberation& liberation) const override {
        const LiberationTuning& tune = Tune();
        const float t = liberation.phaseTimer_;
        // 光の柱は一気に太くなり、立ち切ってからゆっくり薄れる
        const float pillar = easing::Progress(t, tune.pillarSeconds);
        liberation.pillarWidth_ = tune.pillarWidth * easing::EaseOutExpo(pillar);
        liberation.orbSize_ = tune.orbSize * easing::EaseOutExpo(pillar);
        liberation.pillar_ = 1.0f - easing::EaseInQuad(pillar);
        liberation.wash_ = 1.0f - easing::EaseOutCubic(easing::Progress(t, tune.washSeconds));
        liberation.clearedPop_ = PopAmount(t, tune.popSeconds);
        liberation.textScale_ = easing::EaseOutBack(easing::Progress(t, tune.textPopSeconds));
        liberation.textAlpha_ = easing::Progress(t, tune.textPopSeconds);
        return t >= tune.burstHoldSeconds;
    }
};

/** @brief 次の入口へカメラを移す（次の入口が無ければすぐ終える） */
class StageLiberation::PanPhase final : public StageLiberation::IPhase {
public:
    /** @brief クリアした入口から次の入口へ、滑らかにカメラを動かす */
    bool Update(StageLiberation& liberation) const override {
        if (liberation.setup_.nextIndex < 0) {
            return true;
        }
        const float t = easing::EaseInOutCubic(easing::Progress(liberation.phaseTimer_, Tune().panSeconds));
        const StageLiberation::Setup& setup = liberation.setup_;
        liberation.cameraX_ = setup.clearedX + (setup.nextX - setup.clearedX) * t;
        liberation.textAlpha_ = 1.0f - t;
        return t >= 1.0f;
    }
};

/** @brief 次の入口を閉じた色から開いた色へ変え、一度ふくらませる */
class StageLiberation::OpenPhase final : public StageLiberation::IPhase {
public:
    /** @brief 開いた音を鳴らす */
    void Enter(StageLiberation& liberation) const override {
        if (liberation.setup_.nextIndex >= 0) {
            PlayLiberationSound(Tune().openSound);
        }
    }
    /** @brief 開き切って少し待ったら終える */
    bool Update(StageLiberation& liberation) const override {
        const LiberationTuning& tune = Tune();
        liberation.textAlpha_ = 0.0f;
        liberation.open_ = easing::EaseOutCubic(easing::Progress(liberation.phaseTimer_, tune.openSeconds));
        liberation.openPop_ = PopAmount(liberation.phaseTimer_, tune.openSeconds);
        return liberation.phaseTimer_ >= tune.openSeconds + tune.openHoldSeconds;
    }
};

namespace {
/** @brief 段階の並び（光り出す → 光が弾ける → 次の入口へ → 次の入口が開く） */
const StageLiberation::IPhase* PhaseAt(size_t index) {
    static const StageLiberation::GlowPhase glow;
    static const StageLiberation::BurstPhase burst;
    static const StageLiberation::PanPhase pan;
    static const StageLiberation::OpenPhase open;
    static const StageLiberation::IPhase* const kPhases[] = { &glow, &burst, &pan, &open };
    return index < std::size(kPhases) ? kPhases[index] : nullptr;
}
} // namespace

// ══════════════════════════════════════════════════════
// 進行
// ══════════════════════════════════════════════════════

/** @brief 光の柱と画面を覆う光のスプライトを作る（マップの Initialize で1回） */
void StageLiberation::Initialize(SpriteCommon* spriteCommon) {
    // 光の柱は画面の上端から下ろすので上端の中央を、光の玉は中心を基準に置く
    pillarSprite_ = std::make_unique<Sprite>();
    pillarSprite_->Initialize(spriteCommon, kPillarTexture);
    pillarSprite_->SetAnchorPoint({ 0.5f, 0.0f });
    orbSprite_ = std::make_unique<Sprite>();
    orbSprite_->Initialize(spriteCommon, kOrbTexture);
    orbSprite_->SetAnchorPoint({ 0.5f, 0.5f });
    washSprite_ = std::make_unique<Sprite>();
    washSprite_->Initialize(spriteCommon, "Resources/white.png");
    washSprite_->SetPosition({ 0.0f, 0.0f });
    washSprite_->SetSize({ gameConstants::kScreenWidth, gameConstants::kScreenHeight });
}

/** @brief 演出を最初から始める */
void StageLiberation::Start(const Setup& setup) {
    setup_ = setup;
    phaseIndex_ = 0;
    phaseTimer_ = 0.0f;
    playing_ = true;
    cameraX_ = setup.clearedX;
    glow_ = 0.0f;
    pillar_ = 0.0f;
    pillarWidth_ = 0.0f;
    orbSize_ = 0.0f;
    wash_ = 0.0f;
    textScale_ = 0.0f;
    textAlpha_ = 0.0f;
    clearedPop_ = 0.0f;
    open_ = 0.0f;
    openPop_ = 0.0f;
    PhaseAt(phaseIndex_)->Enter(*this);
}

/** @brief 1フレーム進める（決定・攻撃・ジャンプで残りを飛ばせる） */
void StageLiberation::Update(const Input* input) {
    if (!playing_) {
        return;
    }
    if (input != nullptr && input->TriggerCinematicSkip()) {
        Finish();
        return;
    }
    phaseTimer_ += gameConstants::kFrameDeltaTime;
    if (PhaseAt(phaseIndex_)->Update(*this)) {
        AdvancePhase();
    }
}

/** @brief 段階を次へ進める（最後まで行ったら終える） */
void StageLiberation::AdvancePhase() {
    ++phaseIndex_;
    phaseTimer_ = 0.0f;
    const IPhase* next = PhaseAt(phaseIndex_);
    if (next == nullptr) {
        Finish();
        return;
    }
    next->Enter(*this);
}

/** @brief 演出を終えて、カメラと入口を最後の状態にする */
void StageLiberation::Finish() {
    // 飛ばした時も、クリアした入口は光ったまま、次の入口は開いた状態で操作へ戻す
    playing_ = false;
    cameraX_ = setup_.nextIndex >= 0 ? setup_.nextX : setup_.clearedX;
    glow_ = 1.0f;
    pillar_ = 0.0f;
    wash_ = 0.0f;
    textAlpha_ = 0.0f;
    clearedPop_ = 0.0f;
    open_ = 1.0f;
    openPop_ = 0.0f;
}

/** @brief 入口の色を、演出の進み具合に合わせて差し替える */
Vector4 StageLiberation::PortalColor(int index, const Vector4& normal, const Vector4& locked) const {
    // クリアした入口は光り出してから光の柱が消えるまで光の色にし、次の入口は閉じた色から開いた色へ
    if (index == setup_.clearedIndex && playing_) {
        const float light = phaseIndex_ == 0 ? glow_ : pillar_;
        return Lerp(normal, Tune().glowColor, light);
    }
    if (index == setup_.nextIndex && playing_) {
        return Lerp(locked, normal, open_);
    }
    return normal;
}

/** @brief 入口の大きさに掛ける倍率（光る瞬間と開く瞬間に一度ふくらむ） */
float StageLiberation::PortalScale(int index) const {
    if (!playing_) {
        return 1.0f;
    }
    if (index == setup_.clearedIndex) {
        return 1.0f + Tune().popScale * clearedPop_;
    }
    if (index == setup_.nextIndex) {
        return 1.0f + Tune().popScale * openPop_;
    }
    return 1.0f;
}

/** @brief クリア済みの入口の色（解放されて光が残った色） */
Vector4 StageLiberation::ClearedColor() {
    // 光り切った色を少し落ち着かせ、まだクリアしていない入口と見分けやすくする
    constexpr float kSettledBrightness = 0.75f;
    const Vector4& glow = Tune().glowColor;
    return { glow.x * kSettledBrightness, glow.y * kSettledBrightness, glow.z * kSettledBrightness, glow.w };
}

/** @brief ワールドX座標を画面のX座標へ直す */
float StageLiberation::ToScreenX(float worldX, float cameraX) {
    return (worldX - cameraX) / gameConstants::kCameraHalfW * gameConstants::kScreenCenterX + gameConstants::kScreenCenterX;
}

/** @brief 光の柱と画面を覆う光を描く */
void StageLiberation::Draw(float cameraX) {
    if (!playing_) {
        return;
    }
    const LiberationTuning& tune = Tune();
    // 光の柱を画面の上端から縦いっぱいに下ろし、入口からあふれる光の玉を重ねる
    if (pillar_ > 0.0f) {
        const float portalX = ToScreenX(setup_.clearedX, cameraX);
        Vector4 color = tune.pillarColor;
        color.w *= pillar_;
        pillarSprite_->SetPosition({ portalX, 0.0f });
        pillarSprite_->SetSize({ pillarWidth_, gameConstants::kScreenHeight });
        pillarSprite_->SetColor(color);
        pillarSprite_->Update();
        pillarSprite_->Draw();
        Vector4 orbColor = tune.orbColor;
        orbColor.w *= pillar_;
        orbSprite_->SetPosition({ portalX, setup_.portalScreenY });
        orbSprite_->SetSize({ orbSize_, orbSize_ });
        orbSprite_->SetColor(orbColor);
        orbSprite_->Update();
        orbSprite_->Draw();
    }
    // 画面全体を覆う光
    if (wash_ > 0.0f) {
        Vector4 color = tune.washColor;
        color.w *= wash_;
        washSprite_->SetColor(color);
        washSprite_->Update();
        washSprite_->Draw();
    }
}

/** @brief 解放の文字を積む */
void StageLiberation::QueueText(FontRenderer& font, float cameraX) const {
    if (!playing_ || textAlpha_ <= 0.0f) {
        return;
    }
    // 文字はクリアした入口の真上に、弾む大きさで中央揃えにする
    const LiberationTuning& tune = Tune();
    std::string text = tune.text;
    const size_t token = text.find(kStageNumberToken);
    if (token != std::string::npos) {
        text.replace(token, std::char_traits<char>::length(kStageNumberToken), std::to_string(setup_.clearedIndex + 1));
    }
    const std::wstring wide = StringUtility::ConvertString(text);
    const float scale = tune.textScale * textScale_;
    const Vector2 size = FontRenderer::MeasureStringW(wide, scale);
    Vector4 color = tune.textColor;
    color.w *= textAlpha_;
    font.DrawStringW(wide, ToScreenX(setup_.clearedX, cameraX) - size.x * 0.5f, tune.textY - size.y * 0.5f, scale, color);
}

/** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
void StageLiberation::ReloadTuning() {
    MutableTune() = LoadTuning();
}

} // namespace engine::game
