/**
 * @file StageIntro.cpp
 * @brief ステージ開始演出（StageIntro）の段階ごとの処理と、stage_intro.json の読み込み
 */
#include "StageIntro.h"
#include "Easing.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "Input.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "Player.h"
#include "StringUtility.h"
#include "UILayout.h"
#include <algorithm>
#include <cmath>
#include <set>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/stage_intro.json";

// 文字の既定の配置（F2のエディタで動かした値は Resources/Config/UI/gameplay.json の intro グループに保存される）
constexpr const char* kLayoutName = "gameplay";
constexpr float kBossLabelY = 200.0f;
constexpr float kBossLabelScale = 2.0f;
constexpr Vector4 kBossLabelColor = { 1.0f, 0.85f, 0.35f, 1.0f };
constexpr float kWeaponNameY = 240.0f;
constexpr float kWeaponNameScale = 4.0f;
constexpr float kGoY = 300.0f;
constexpr float kGoScale = 8.0f;
constexpr Vector4 kGoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr float kGoHidden = -1.0f; // goTimer_ がこの値なら GO! の文字を出していない

/** @brief 開始演出の調整値（stage_intro.json。初期値はJSONに項目が無い時の既定値） */
struct IntroTuning {
    // 画面を斬り開く
    float closedSeconds = 0.25f; ///< 真っ暗なまま待つ長さ
    float slashSeconds = 0.16f; ///< 斬線が画面を横切る長さ
    float openSeconds = 0.4f; ///< 幕が上下へ開き切るまでの長さ
    float slashThickness = 6.0f;
    float slashAngle = -0.12f; ///< 斬線の傾き（ラジアン）
    Vector4 slashColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 curtainColor = { 0.02f, 0.02f, 0.04f, 1.0f };
    std::string slashFlash = "intro_slash"; ///< 斬った瞬間の画面フラッシュ（hit_effects.json の flashes）
    // ステージ全体の見渡し（ステージの上下が収まる引きで、端から端へ横に流す）
    float overviewHoldSeconds = 0.4f; ///< 流し始める前に止めて見せる長さ
    float overviewPanSeconds = 2.2f; ///< ステージの端から寄る先まで流す長さ
    float overviewZoomSeconds = 0.6f; ///< 流し終えてから、ふだんの距離まで寄る長さ
    float overviewMaxZoom = 3.6f; ///< 引きの上限（ふだんの距離の何倍まで引くか）。空の球の半径が約95・遠くを切る距離が100なので、距離24の3.6倍=約86までにする
    // 奪う武器の紹介（そのステージの初回だけ）
    float bossHoldSeconds = 1.6f; ///< ボスを映したまま文字を見せる長さ
    float bossPanSeconds = 0.5f; ///< ボスから開始位置へカメラを戻す長さ
    float textFadeSeconds = 0.2f;
    std::string bossLabel = "この武器を奪え";
    // 降下と着地
    float dropHeight = 9.0f; ///< 着地位置のどれだけ上から落ちてくるか
    float dropSeconds = 0.45f;
    float hideHeight = 80.0f; ///< 降下するまでプレイヤーを置いておく高さ（引いた画にも映らない所）
    float goSeconds = 0.8f; ///< GO の文字を出しておく長さ
    float goPopSeconds = 0.25f; ///< GO の文字が弾んで大きさが落ち着くまでの長さ
    std::string landImpact = "intro_land"; ///< 着地のヒットストップとカメラ揺れ（impacts）
    std::string landRing = "intro_land"; ///< 着地の衝撃リング（rings）
    std::string landSparks = "land_dust"; ///< 着地の砂ぼこり（sparks）
    std::string goFlash = "intro_go"; ///< GO の瞬間の画面フラッシュ（flashes）
    float skipLockSeconds = 0.2f; ///< 開始直後のこの間はボタンで飛ばせない（前の画面の決定ボタンを拾わないように）
};

constexpr JsonField<IntroTuning> kIntroFields[] = {
    { "slash", "closedSeconds", &IntroTuning::closedSeconds },
    { "slash", "slashSeconds", &IntroTuning::slashSeconds },
    { "slash", "openSeconds", &IntroTuning::openSeconds },
    { "slash", "thickness", &IntroTuning::slashThickness },
    { "slash", "angle", &IntroTuning::slashAngle },
    { "slash", "color", &IntroTuning::slashColor },
    { "slash", "curtainColor", &IntroTuning::curtainColor },
    { "slash", "flash", &IntroTuning::slashFlash },
    { "overview", "holdSeconds", &IntroTuning::overviewHoldSeconds },
    { "overview", "panSeconds", &IntroTuning::overviewPanSeconds },
    { "overview", "zoomSeconds", &IntroTuning::overviewZoomSeconds },
    { "overview", "maxZoom", &IntroTuning::overviewMaxZoom },
    { "boss", "holdSeconds", &IntroTuning::bossHoldSeconds },
    { "boss", "panSeconds", &IntroTuning::bossPanSeconds },
    { "boss", "textFadeSeconds", &IntroTuning::textFadeSeconds },
    { "boss", "label", &IntroTuning::bossLabel },
    { "drop", "height", &IntroTuning::dropHeight },
    { "drop", "seconds", &IntroTuning::dropSeconds },
    { "drop", "hideHeight", &IntroTuning::hideHeight },
    { "land", "goSeconds", &IntroTuning::goSeconds },
    { "land", "goPopSeconds", &IntroTuning::goPopSeconds },
    { "land", "impact", &IntroTuning::landImpact },
    { "land", "ring", &IntroTuning::landRing },
    { "land", "sparks", &IntroTuning::landSparks },
    { "land", "flash", &IntroTuning::goFlash },
    { nullptr, "skipLockSeconds", &IntroTuning::skipLockSeconds },
};

/** @brief 演出の段階の並び（sequence）と、そのステージの初回だけ出す段階（firstVisitOnly） */
struct IntroSequence {
    std::vector<std::string> phases = { "slash", "overview", "boss", "drop", "land" };
    std::set<std::string> firstVisitOnly = { "boss" };
};

/** @brief JSON配列から文字列の一覧を読む（無ければ fallback を返す） */
std::vector<std::string> StringList(const nlohmann::json& root, const char* key, const std::vector<std::string>& fallback)
{
    const auto it = root.find(key);
    if (it == root.end() || !it->is_array())
    {
        return fallback;
    }
    std::vector<std::string> values;
    for (const auto& item : *it)
    {
        if (item.is_string())
        {
            values.push_back(item.get<std::string>());
        }
    }
    return values;
}

/** @brief 開始演出の調整値と段階の並びをまとめた読み込み結果 */
struct IntroConfig {
    IntroTuning tuning;
    IntroSequence sequence;
};

/** @brief stage_intro.json から開始演出の設定を読み込む */
IntroConfig LoadConfig()
{
    IntroConfig config;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object())
    {
        Logger::LogWarning(std::string("StageIntro: ") + kConfigPath + " が読めません。既定値で動きます");
        return config;
    }
    ReadJsonFields(root, kIntroFields, config.tuning);
    config.sequence.phases = StringList(root, "sequence", config.sequence.phases);
    const std::vector<std::string> firstVisit = StringList(root, "firstVisitOnly",
        { config.sequence.firstVisitOnly.begin(), config.sequence.firstVisitOnly.end() });
    config.sequence.firstVisitOnly = { firstVisit.begin(), firstVisit.end() };
    return config;
}

/** @brief 読み込み済みの設定（初回に読む。変更は次回起動時に反映） */
const IntroConfig& Config()
{
    static const IntroConfig config = LoadConfig();
    return config;
}

/** @brief 読み込み済みの調整値を取得する */
const IntroTuning& Tune() {
    return Config().tuning;
}

/** @brief 経過秒を 0〜1 の進み具合にする（長さ0以下なら終わったものとして1） */
float Progress(float elapsed, float duration)
{
    return duration > 0.0f ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;
}

/** @brief 2つのベクトルを要素ごとに割合 t で補間する */
Vector3 LerpVector(const Vector3& a, const Vector3& b, float t)
{
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

/** @brief 見たことのあるステージ（アプリを起動している間だけ覚える） */
std::set<std::string>& VisitedStages()
{
    static std::set<std::string> visited;
    return visited;
}

/** @brief 文字列を画面中央に揃えて積む */
void DrawCentered(FontRenderer& font, const std::wstring& text, float y, float scale, const Vector4& color)
{
    const Vector2 size = FontRenderer::MeasureStringW(text, scale);
    font.DrawStringW(text, GameConstants::kScreenCenterX - size.x * 0.5f, y, scale, color);
}
} // namespace

// ══════════════════════════════════════════════════════
// 段階
// ══════════════════════════════════════════════════════

/** @brief 演出の1段階（状態はStageIntro側が持ち、段階は時間の進め方と見せ方だけを決める） */
class StageIntro::IPhase {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IPhase() = default;
    /** @brief この状態へ入る際の初期設定を適用する */
    virtual void Enter(StageIntro&) const { }
    /** @return この段階を終えたら true */
    virtual bool Update(StageIntro& intro) const = 0;
    /** @brief 文字を出さない段階では何もしない */
    virtual void QueueText(const StageIntro&, FontRenderer&) const { }
    /** @brief 演出を始める時に、この段階が来るまでの見え方を整える（最初のカメラ位置・プレイヤーを隠すなど） */
    virtual void BeforeSequence(StageIntro&) const { }
};

/** @brief 真っ暗な画面を斬線が横切り、幕が上下へ開いてステージが見える */
class StageIntro::SlashPhase final : public StageIntro::IPhase {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(StageIntro& intro) const override
    {
        intro.curtainOpen_ = 0.0f;
        intro.slashProgress_ = 0.0f;
        intro.slashAlpha_ = 1.0f;
        intro.slashFlashed_ = false;
    }
    /** @brief 画面を斬り開く演出を進める（終わったら true） */
    bool Update(StageIntro& intro) const override
    {
        const IntroTuning& tune = Tune();
        const float sinceClosed = intro.phaseTimer_ - tune.closedSeconds;
        if (sinceClosed < 0.0f)
        {
            return false;
        }
        intro.slashProgress_ = Easing::EaseOutCubic(Progress(sinceClosed, tune.slashSeconds));
        const float sinceSlash = sinceClosed - tune.slashSeconds;
        if (sinceSlash < 0.0f)
        {
            return false;
        }
        if (!intro.slashFlashed_)
        {
            intro.slashFlashed_ = true;
            const HitEffectPlayer fx(intro.services_.particles, intro.services_.shaker, intro.services_.rng);
            fx.Flash(tune.slashFlash);
        }
        const float open = Progress(sinceSlash, tune.openSeconds);
        intro.curtainOpen_ = Easing::EaseOutCubic(open);
        intro.slashAlpha_ = 1.0f - open;
        return open >= 1.0f;
    }
};

/**
 * @brief カメラを少し引いてステージを端から端へ横に流し、次に映す場所（ボスか開始位置）で寄る
 * @note 引きはステージの上下がちょうど収まるまでにとどめ、カメラ側の範囲制限と合わせてステージの外を映さない
 */
class StageIntro::OverviewPhase final : public StageIntro::IPhase {
public:
    /** @brief 後ろの段階で決まる寄る先を使って、見渡すカメラの動きを決める */
    void BeforeSequence(StageIntro& intro) const override
    {
        // 準備は後ろの段階から行うので、ここでは寄る先（zoomInTarget_）が決まっている
        const IntroTuning& tune = Tune();
        const Setup& setup = intro.setup_;
        const float fitHeight = setup.stageHeight / (GameConstants::kCameraHalfH * 2.0f);
        const float fitWidth = (setup.stageRight - setup.stageLeft) / (GameConstants::kCameraHalfW * 2.0f);
        intro.overviewZoom_ = std::clamp((std::min)(fitHeight, fitWidth), 1.0f, (std::max)(tune.overviewMaxZoom, 1.0f));
        // 寄る先から遠い方の端から流し始める
        const float targetX = intro.zoomInTarget_.x;
        const float startX = std::abs(setup.stageLeft - targetX) > std::abs(setup.stageRight - targetX)
            ? setup.stageLeft
            : setup.stageRight;
        intro.overviewFocus_ = { startX, setup.stageCenterY, setup.landing.z };
        intro.hasOverview_ = true;
    }
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(StageIntro& intro) const override
    {
        intro.cameraFocus_ = intro.overviewFocus_;
        intro.cameraZoom_ = intro.overviewZoom_;
    }
    /** @brief ステージ全体を見渡すカメラの動きを進める（終わったら true） */
    bool Update(StageIntro& intro) const override
    {
        const IntroTuning& tune = Tune();
        const float sinceHold = intro.phaseTimer_ - tune.overviewHoldSeconds;
        const float pan = Easing::EaseInOutSine(Progress(sinceHold, tune.overviewPanSeconds));
        const float zoomIn = Easing::EaseInOutCubic(Progress(sinceHold - tune.overviewPanSeconds, tune.overviewZoomSeconds));
        const Vector3& from = intro.overviewFocus_;
        const Vector3& to = intro.zoomInTarget_;
        intro.cameraFocus_ = { from.x + (to.x - from.x) * pan, from.y + (to.y - from.y) * zoomIn, from.z };
        intro.cameraZoom_ = intro.overviewZoom_ + (1.0f - intro.overviewZoom_) * zoomIn;
        return zoomIn >= 1.0f;
    }
};

/** @brief ボスを映して、倒すと奪える武器の名前を見せてから開始位置へカメラを戻す */
class StageIntro::BossPhase final : public StageIntro::IPhase {
public:
    /** @brief ボスがいれば、カメラの寄る先をボスにする */
    void BeforeSequence(StageIntro& intro) const override
    {
        // 幕が開いた時（見渡す段階があれば寄った先）にもうボスが映っているよう、ボスを向いておく
        if (intro.setup_.hasBoss)
        {
            intro.zoomInTarget_ = intro.setup_.bossFocus;
        }
    }
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(StageIntro& intro) const override
    {
        intro.textAlpha_ = 0.0f;
        intro.cameraZoom_ = 1.0f;
        if (intro.setup_.hasBoss)
        {
            intro.cameraFocus_ = intro.setup_.bossFocus;
        }
    }
    /** @brief ボスと奪える武器の紹介を進める（終わったら true） */
    bool Update(StageIntro& intro) const override
    {
        if (!intro.setup_.hasBoss)
        {
            return true;
        }
        const IntroTuning& tune = Tune();
        const float t = intro.phaseTimer_;
        // 文字は出だしで浮かび上がり、カメラが戻り始めたら消えていく
        const float fadeIn = Progress(t, tune.textFadeSeconds);
        const float fadeOut = 1.0f - Progress(t - tune.bossHoldSeconds, tune.textFadeSeconds);
        intro.textAlpha_ = (std::min)(fadeIn, fadeOut);
        const float pan = Easing::EaseInOutSine(Progress(t - tune.bossHoldSeconds, tune.bossPanSeconds));
        intro.cameraFocus_ = LerpVector(intro.setup_.bossFocus, intro.setup_.landing, pan);
        return t >= tune.bossHoldSeconds + tune.bossPanSeconds;
    }
    /** @brief ボスの名前と奪える武器の文字を出す */
    void QueueText(const StageIntro& intro, FontRenderer& font) const override
    {
        if (!intro.setup_.hasBoss || intro.textAlpha_ <= 0.0f)
        {
            return;
        }
        UILayout& layout = UILayout::Get(kLayoutName);
        Vector4 labelColor = layout.Color("intro.boss_label_color", kBossLabelColor);
        labelColor.w *= intro.textAlpha_;
        DrawCentered(font, StringUtility::ConvertString(Tune().bossLabel), layout.Float("intro.boss_label_y", kBossLabelY),
            layout.Float("intro.boss_label_scale", kBossLabelScale), labelColor);
        Vector4 nameColor = intro.setup_.weaponColor;
        nameColor.w *= intro.textAlpha_;
        DrawCentered(font, intro.setup_.weaponName, layout.Float("intro.weapon_name_y", kWeaponNameY),
            layout.Float("intro.weapon_name_scale", kWeaponNameScale), nameColor);
    }
};

/** @brief プレイヤーが上空から開始位置へ落ちてくる */
class StageIntro::DropPhase final : public StageIntro::IPhase {
public:
    /** @brief 降下するまでプレイヤーを画面に映らない高さへ隠しておく */
    void BeforeSequence(StageIntro& intro) const override
    {
        // 降下するまではプレイヤーを、引いた画にも映らない高さへ隠しておく
        Vector3 hidden = intro.setup_.landing;
        hidden.y += Tune().hideHeight;
        intro.SetPlayerPosition(hidden);
    }
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(StageIntro& intro) const override
    {
        intro.cameraFocus_ = intro.setup_.landing;
        intro.cameraZoom_ = 1.0f;
    }
    /** @brief プレイヤーの降下を進め、着地したら衝撃の演出を出す（終わったら true） */
    bool Update(StageIntro& intro) const override
    {
        const IntroTuning& tune = Tune();
        const float fall = Easing::EaseInQuad(Progress(intro.phaseTimer_, tune.dropSeconds));
        Vector3 position = intro.setup_.landing;
        position.y += tune.dropHeight * (1.0f - fall);
        intro.SetPlayerPosition(position);
        return fall >= 1.0f;
    }
};

/** @brief 着地の衝撃を出して GO を見せる */
class StageIntro::LandPhase final : public StageIntro::IPhase {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(StageIntro& intro) const override
    {
        const IntroTuning& tune = Tune();
        intro.SetPlayerPosition(intro.setup_.landing);
        intro.cameraFocus_ = intro.setup_.landing;
        intro.goTimer_ = 0.0f;
        const HitEffectPlayer fx(intro.services_.particles, intro.services_.shaker, intro.services_.rng);
        fx.Impact(tune.landImpact);
        fx.Ring(tune.landRing, intro.setup_.landing);
        fx.Sparks(tune.landSparks, intro.setup_.landing);
        fx.Flash(tune.goFlash);
    }
    /** @brief GO! を出した時点で段階を終え、すぐ操作できるようにする（文字は StageIntro が演出の後も出し続ける） */
    bool Update(StageIntro&) const override
    {
        return true;
    }
};

namespace {
/** @brief stage_intro.json の sequence に書く段階名と段階の対応 */
const StageIntro::IPhase* PhaseNamed(const std::string& name)
{
    static const StageIntro::SlashPhase slash;
    static const StageIntro::OverviewPhase overview;
    static const StageIntro::BossPhase boss;
    static const StageIntro::DropPhase drop;
    static const StageIntro::LandPhase land;
    /** @brief 開始演出の設定名と状態オブジェクトの対応 */
    struct NamedPhase {
        const char* name;
        const StageIntro::IPhase* phase;
    };
    const NamedPhase kPhases[] = {
        { "slash", &slash },
        { "overview", &overview },
        { "boss", &boss },
        { "drop", &drop },
        { "land", &land },
    };
    for (const NamedPhase& entry : kPhases)
    {
        if (name == entry.name)
        {
            return entry.phase;
        }
    }
    Logger::LogWarning("StageIntro: 知らない段階名です: " + name);
    return nullptr;
}
} // namespace

// ══════════════════════════════════════════════════════
// 進行
// ══════════════════════════════════════════════════════

StageIntro::StageIntro() = default;
/** @brief unique_ptr で持つ不完全型のメンバを破棄するため、定義を cpp に置く */
StageIntro::~StageIntro() = default;

/** @brief 幕と斬線のスプライトを作る（シーンの Initialize で1回） */
void StageIntro::Initialize(SpriteCommon* spriteCommon)
{
    auto create = [spriteCommon] {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(spriteCommon, "Resources/white.png");
        return sprite;
    };
    curtainTop_ = create();
    curtainBottom_ = create();
    slashLine_ = create();
    slashLine_->SetAnchorPoint({ 0.5f, 0.5f });
}

/** @brief 演出を最初から始める（このステージを見るのが2回目以降なら初回だけの段階を飛ばす） */
void StageIntro::Start(const Setup& setup, const Services& services)
{
    // 設定を受け取り、組み立て直す段階の並びを空にする
    setup_ = setup;
    services_ = services;
    phases_.clear();
    // 2回目以降のステージでは、初回だけの段階を外す
    const bool firstVisit = VisitedStages().insert(setup.stageKey).second;
    const IntroSequence& sequence = Config().sequence;
    for (const std::string& name : sequence.phases)
    {
        if (!firstVisit && sequence.firstVisitOnly.contains(name))
        {
            continue;
        }
        if (const IPhase* phase = PhaseNamed(name))
        {
            phases_.push_back(phase);
        }
    }

    // 幕・斬線・文字・カメラの状態を最初に戻す（段階が1つも無ければ何もしない）
    phaseIndex_ = 0;
    phaseTimer_ = 0.0f;
    totalTimer_ = 0.0f;
    curtainOpen_ = 1.0f;
    slashAlpha_ = 0.0f;
    textAlpha_ = 0.0f;
    goTimer_ = kGoHidden;
    cameraFocus_ = setup.landing;
    cameraZoom_ = 1.0f;
    zoomInTarget_ = setup.landing;
    hasOverview_ = false;
    playing_ = !phases_.empty();
    if (!playing_)
    {
        return;
    }
    // 後ろの段階から準備する（前の段階が、後ろの段階の決めた位置へつなげられるように）
    for (auto phase = phases_.rbegin(); phase != phases_.rend(); ++phase)
    {
        (*phase)->BeforeSequence(*this);
    }
    // 降ってくる時から、奪いに行くボスの方（いなければステージの奥＝右）を向かせておく
    if (services_.player != nullptr)
    {
        const Vector3 lookAt = setup.hasBoss ? setup.bossFocus : Vector3 { setup.stageRight, setup.landing.y, setup.landing.z };
        services_.player->FaceTarget(lookAt);
    }
    // プレイヤーを降ってくる前の姿勢にする
    UpdatePlayerPose();
    // 最初の画（幕が開いた時に見えるもの）は、見渡す段階があれば引いた画、無ければ最初に寄る先
    cameraFocus_ = hasOverview_ ? overviewFocus_ : zoomInTarget_;
    cameraZoom_ = hasOverview_ ? overviewZoom_ : 1.0f;
    phases_[phaseIndex_]->Enter(*this);
}

/** @brief 1フレーム進める（決定・攻撃ボタンで最後まで飛ばせる。演出の後も GO! の文字の時間は進める） */
void StageIntro::Update(const Input* input)
{
    AdvanceGoText();
    if (!playing_)
    {
        return;
    }
    totalTimer_ += GameConstants::kFrameDeltaTime;
    const bool skipPressed = input != nullptr
        && (input->TriggerMenuConfirm() || input->TriggerAction(Input::Action::Attack) || input->TriggerAction(Input::Action::Jump));
    if (skipPressed && totalTimer_ >= Tune().skipLockSeconds)
    {
        // 飛ばした時は GO! も出さずにそのまま始める
        Finish();
        goTimer_ = kGoHidden;
        return;
    }
    phaseTimer_ += GameConstants::kFrameDeltaTime;
    if (phases_[phaseIndex_]->Update(*this))
    {
        AdvancePhase();
    }
    UpdatePlayerPose();
}

/** @brief 段階を次へ進める（最後まで行ったら終える） */
void StageIntro::AdvancePhase()
{
    ++phaseIndex_;
    phaseTimer_ = 0.0f;
    if (phaseIndex_ >= phases_.size())
    {
        Finish();
        return;
    }
    phases_[phaseIndex_]->Enter(*this);
}

/** @brief GO! の文字を出してからの時間を進め、出し終えたら消す */
void StageIntro::AdvanceGoText()
{
    if (goTimer_ < 0.0f)
    {
        return;
    }
    goTimer_ += GameConstants::kFrameDeltaTime;
    if (goTimer_ >= Tune().goSeconds)
    {
        goTimer_ = kGoHidden;
    }
}

/** @brief 残りを飛ばして、操作できる状態にする（出ている GO! の文字は残す） */
void StageIntro::Finish()
{
    playing_ = false;
    curtainOpen_ = 1.0f;
    slashAlpha_ = 0.0f;
    textAlpha_ = 0.0f;
    cameraFocus_ = setup_.landing;
    cameraZoom_ = 1.0f;
    SetPlayerPosition(setup_.landing);
    UpdatePlayerPose();
}

/** @brief 開始演出中のプレイヤーの位置を設定する */
void StageIntro::SetPlayerPosition(const Vector3& position)
{
    if (services_.player != nullptr)
    {
        services_.player->SetPosition(position);
    }
}

/** @brief 演出中のプレイヤーの向き・アニメ・武器の付け方を整える（入力と物理は進めない） */
void StageIntro::UpdatePlayerPose()
{
    if (services_.player == nullptr)
    {
        return;
    }
    // 着地点より上にいる間は落下のアニメ、着地したら待機のアニメにする
    constexpr float kGroundedTolerance = 0.01f;
    const bool airborne = services_.player->GetPosition().y > setup_.landing.y + kGroundedTolerance;
    services_.player->UpdateCinematicPose(airborne);
}

/** @brief 演出の文字を積む（シーンの Update 中、fontRenderer.Reset() の後に呼ぶ。演出の後も GO! が残っていれば積む） */
void StageIntro::QueueText(FontRenderer& font) const
{
    if (playing_ && phaseIndex_ < phases_.size())
    {
        phases_[phaseIndex_]->QueueText(*this, font);
    }
    QueueGoText(font);
}

/** @brief GO! の文字を積む（大きく弾んで出て、最後の文字のフェード時間で消える） */
void StageIntro::QueueGoText(FontRenderer& font) const
{
    if (goTimer_ < 0.0f)
    {
        return;
    }
    const IntroTuning& tune = Tune();
    UILayout& layout = UILayout::Get(kLayoutName);
    const float pop = Easing::EaseOutBack(Progress(goTimer_, tune.goPopSeconds));
    Vector4 color = layout.Color("intro.go_color", kGoColor);
    color.w *= 1.0f - Progress(goTimer_ - (tune.goSeconds - tune.textFadeSeconds), tune.textFadeSeconds);
    DrawCentered(font, L"GO!", layout.Float("intro.go_y", kGoY), layout.Float("intro.go_scale", kGoScale) * pop, color);
}

/** @brief 幕と斬線を描く（SpriteCommon の共通設定を済ませた後に呼ぶ） */
void StageIntro::Draw()
{
    if (!playing_)
    {
        return;
    }
    const IntroTuning& tune = Tune();
    const float halfHeight = GameConstants::kScreenHeight * 0.5f;
    if (curtainOpen_ < 1.0f)
    {
        // 上下の幕を画面の外へ押し出していく
        const float offset = halfHeight * curtainOpen_;
        auto drawCurtain = [&](Sprite& curtain, float top) {
            curtain.SetPosition({ 0.0f, top });
            curtain.SetSize({ GameConstants::kScreenWidth, halfHeight });
            curtain.SetColor(tune.curtainColor);
            curtain.Update();
            curtain.Draw();
        };
        drawCurtain(*curtainTop_, -offset);
        drawCurtain(*curtainBottom_, halfHeight + offset);
    }
    if (slashAlpha_ > 0.0f && slashProgress_ > 0.0f)
    {
        // 画面の対角より長く伸ばし、傾けても端が見えないようにする
        const float fullLength = std::hypot(GameConstants::kScreenWidth, GameConstants::kScreenHeight);
        Vector4 color = tune.slashColor;
        color.w *= slashAlpha_;
        slashLine_->SetPosition({ GameConstants::kScreenCenterX, GameConstants::kScreenCenterY });
        slashLine_->SetSize({ fullLength * slashProgress_, tune.slashThickness });
        slashLine_->SetRotation(tune.slashAngle);
        slashLine_->SetColor(color);
        slashLine_->Update();
        slashLine_->Draw();
    }
}

} // namespace engine::game
