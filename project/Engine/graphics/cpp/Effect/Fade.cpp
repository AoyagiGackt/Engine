/**
 * @file Fade.cpp
 * @brief 濃紺の帯と金色のラインによる画面遷移演出
 */
#include "Fade.h"
#include "GraphicsTuning.h"
#include "EngineConstants.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>

namespace engine::graphics {
namespace {

// 縁の配色と補間係数
constexpr float kEdgeRed = 0.82f;
constexpr float kEdgeGreen = 0.66f;
constexpr float kEdgeBlue = 0.38f;
constexpr float kSmootherstepQuintic = 6.0f;
constexpr float kSmootherstepQuartic = 15.0f;
constexpr float kSmootherstepCubic = 10.0f;

}

namespace {

// 帯と縁の描画設定
constexpr float kBandStagger = 0.22f;
constexpr float kEdgeWidth = 3.0f;
constexpr float kBandShadeStep = 0.002f;
constexpr float kBandRed = 0.018f;
constexpr float kBandGreen = 0.026f;
constexpr float kBandBlue = 0.045f;
constexpr float kEdgeOpacity = 0.85f;
constexpr float kMaxBandStagger = 0.99f;

} // namespace

/** @brief フェードの進行と帯の配置を段階ごとに抽象化する状態 */
class Fade::IState {
public:
    /** @brief 派生状態を正しく終了する */
    virtual ~IState() = default;
    /** @brief 状態へ入るときの処理を行う */
    virtual void Enter(Fade&) const { }
    /** @brief 現在の段階を1フレーム進める */
    virtual void Update(Fade&) const { }
    /** @brief 状態を離れるときの処理を行う */
    virtual void Exit(Fade&) const { }
    /** @brief 呼び出し側へ公開する状態識別子を返す */
    virtual Status Id() const = 0;
    /** @brief 帯の左端の座標を求める */
    virtual float Position(float width, float eased, bool covered) const = 0;
    /** @brief 帯の左端から縁までの距離を求める */
    virtual float EdgeOffset(float, float) const {
        return 0.0f;
    }
};

/** @brief フェードを進めず直前の被覆状態を保つ待機状態 */
class Fade::IdleState final : public IState {
public:
    /** @brief 待機状態の識別子を返す */
    Status Id() const override {
        return Status::kNone;
    }
    /** @brief 帯の位置を返す（覆っていれば画面全体、開いていれば画面の左外） */
    float Position(float width, float, bool covered) const override {
        return covered ? 0.0f : -width;
    }
};

/** @brief 帯を画面右へ退けて画面を開く状態 */
class Fade::FadeInState final : public IState {
public:
    /** @brief 帯を開く方向へ進める */
    void Update(Fade& fade) const override {
        fade.Advance(false);
    }
    /** @brief フェードインの識別子を返す */
    Status Id() const override {
        return Status::kFadeIn;
    }
    /** @brief 進み具合に応じて帯を画面右へ退けた位置を返す */
    float Position(float width, float eased, bool) const override {
        return width * eased;
    }
};

/** @brief 帯を画面左から進めて画面を覆う状態 */
class Fade::FadeOutState final : public IState {
public:
    /** @brief 帯を覆う方向へ進める */
    void Update(Fade& fade) const override {
        fade.Advance(true);
    }
    /** @brief フェードアウトの識別子を返す */
    Status Id() const override {
        return Status::kFadeOut;
    }
    /** @brief 進み具合に応じて帯を画面左から進めた位置を返す */
    float Position(float width, float eased, bool) const override {
        return -width * (1.0f - eased);
    }
    /** @brief 帯の端の飾り線を、帯の右端へ寄せる量を返す */
    float EdgeOffset(float width, float edgeWidth) const override {
        return width - edgeWidth;
    }
};

/** @brief フェード待機の共有状態を返す */
const Fade::IState& Fade::Idle() {
    static const IdleState kState;
    return kState;
}
/** @brief フェードインの共有状態を返す */
const Fade::IState& Fade::FadingIn() {
    static const FadeInState kState;
    return kState;
}
/** @brief フェードアウトの共有状態を返す */
const Fade::IState& Fade::FadingOut() {
    static const FadeOutState kState;
    return kState;
}

/** @brief 旧状態を終了して新状態へ入る */
void Fade::ChangeState(const IState& next) {
    state_->Exit(*this);
    state_ = &next;
    state_->Enter(*this);
}

/** @brief フェードの完了を状態オブジェクトから取得する */
bool Fade::IsFinished() const {
    return state_ == &Idle();
}
/** @brief 公開API用の状態識別子を取得する */
Fade::Status Fade::GetStatus() const {
    return state_->Id();
}

/** @brief 初期化 */
void Fade::Initialize(SpriteCommon* spriteCommon) {
    spriteCommon_ = spriteCommon;
    ChangeState(Idle());
    covered_ = false;
    for (size_t i = 0; i < kBandCount; ++i) {
        bands_[i] = std::make_unique<Sprite>();
        bands_[i]->Initialize(spriteCommon, "Resources/white.png");
        edges_[i] = std::make_unique<Sprite>();
        edges_[i]->Initialize(spriteCommon, "Resources/white.png");
    }
    UpdateSprites();
}

/** @brief フェード開始 */
void Fade::Start(Status status, float duration) {
    static const IState* const kStates[] = { &Idle(), &FadingIn(), &FadingOut() };
    const size_t index = static_cast<size_t>(status);
    ChangeState(index < std::size(kStates) ? *kStates[index] : Idle());
    duration_ = std::isfinite(duration) ? (std::max)(duration, 0.0f) : 0.0f;
    counter_ = 0.0f;
    if (status == Status::kNone) {
        covered_ = false;
    }
    UpdateSprites();
}

/** @brief 現在の段階に1フレームの更新を委譲する */
void Fade::Update() {
    state_->Update(*this);
}

/** @brief 経過時間と描画を更新してから完了状態へ遷移する */
void Fade::Advance(bool coverWhenFinished) {
    counter_ = (std::min)(counter_ + engineConstants::kFrameDeltaTime, duration_);
    UpdateSprites();
    if (counter_ >= duration_) {
        covered_ = coverWhenFinished;
        ChangeState(Idle());
    }
}

/** @brief 現在状態の補間値を帯と縁へ反映する */
void Fade::UpdateSprites() {
    const float bandStagger = GraphicsTuning::Float("fade.bandStagger", kBandStagger, 0.0f, kMaxBandStagger);
    const float edgeWidth = GraphicsTuning::Float("fade.edgeWidth", kEdgeWidth);
    const float bandShadeStep = GraphicsTuning::Float("fade.bandShadeStep", kBandShadeStep);
    const Vector4 bandColor = GraphicsTuning::Color("fade.bandColor", { kBandRed, kBandGreen, kBandBlue, 1.0f });
    const Vector4 edgeColor = GraphicsTuning::Color("fade.edgeColor", { kEdgeRed, kEdgeGreen, kEdgeBlue, kEdgeOpacity });
    const float width = static_cast<float>(WinApp::kClientWidth);
    const float height = static_cast<float>(WinApp::kClientHeight) / kBandCount;
    const float progress = duration_ > 0.0f ? std::clamp(counter_ / duration_, 0.0f, 1.0f) : 1.0f;
    for (size_t i = 0; i < kBandCount; ++i) {
        // 上から順に開始をずらし、加速と減速を滑らかにする。
        const float delay = bandStagger * static_cast<float>(i) / (kBandCount - 1);
        const float t = std::clamp((progress - delay) / (1.0f - bandStagger), 0.0f, 1.0f);
        const float eased = t * t * t * (t * (t * kSmootherstepQuintic - kSmootherstepQuartic) + kSmootherstepCubic);
        const float x = state_->Position(width, eased, covered_);
        const float shade = static_cast<float>(i) * bandShadeStep;
        bands_[i]->SetPosition({ x, height * i });
        bands_[i]->SetSize({ width, height + 1.0f });
        bands_[i]->SetColor({ bandColor.x + shade, bandColor.y + shade, bandColor.z + shade, bandColor.w });
        bands_[i]->Update();

        // 金色の縁は帯の内側に置き、開始・終了時には消す。
        const float edgeX = x + state_->EdgeOffset(width, edgeWidth);
        edges_[i]->SetPosition({ edgeX, height * i });
        edges_[i]->SetSize({ edgeWidth, height + 1.0f });
        edges_[i]->SetColor({ edgeColor.x, edgeColor.y, edgeColor.z, (t > 0.0f && t < 1.0f) ? edgeColor.w : 0.0f });
        edges_[i]->Update();
    }
}

/** @brief 描画処理 */
void Fade::Draw() {
    if (!spriteCommon_ || (IsFinished() && !covered_)) {
        return;
    }
    spriteCommon_->CommonDrawSettings();
    for (size_t i = 0; i < kBandCount; ++i) {
        bands_[i]->Draw();
    }
    for (size_t i = 0; i < kBandCount; ++i) {
        if (edges_[i]->GetColor().w > 0.0f) {
            edges_[i]->Draw();
        }
    }
}

} // namespace engine::graphics
