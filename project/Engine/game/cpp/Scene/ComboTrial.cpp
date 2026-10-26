/**
 * @file ComboTrial.cpp
 * @brief コンボ練習（ComboTrial）の手順の判定・札の表示と、combo_trials.json の読み込み
 */
#include "ComboTrial.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "Player.h"
#include "StringUtility.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <iterator>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kConfigPath = "Resources/Config/combo_trials.json";
constexpr const char* kWhiteTexture = "Resources/white.png";
constexpr size_t kMaxSteps = 12; ///< 1つのコンボで並べられる手順の上限（札をこの数だけ作っておく）
constexpr float kDefaultIdleSeconds = 1.6f; ///< 手順の間がこれより空いたら最初からやり直す
constexpr float kSuccessSeconds = 1.4f; ///< 成功の文字を出す長さ（その後は同じコンボをもう一度）
constexpr float kFailSeconds = 0.5f; ///< やり直しの合図を出す長さ

// 札の並び（画面上中央）
constexpr float kNameY = 56.0f;
constexpr float kNameScale = 1.3f;
constexpr float kCardY = 84.0f;
constexpr float kCardScale = 1.2f;
constexpr float kCardPaddingX = 10.0f;
constexpr float kCardPaddingY = 6.0f;
constexpr float kCardGap = 26.0f; ///< 札と札の間（矢印を置く）
constexpr float kPanelPadding = 14.0f;
constexpr float kResultY = 132.0f;
constexpr float kResultScale = 2.4f;
constexpr float kHalf = 0.5f;
constexpr float kBothSides = 2.0f; ///< 余白を両側に足す
constexpr float kPulseSpeed = 8.0f;
constexpr float kPulseBase = 0.75f;
constexpr float kPulseAmplitude = 0.25f;

constexpr Vector4 kPanelColor = { 0.0f, 0.0f, 0.05f, 0.55f };
constexpr Vector4 kDoneColor = { 0.2f, 0.75f, 0.35f, 0.95f }; ///< 出し終えた手順
constexpr Vector4 kNextColor = { 1.0f, 0.8f, 0.2f, 0.95f }; ///< 次に出す手順
constexpr Vector4 kPendingColor = { 0.3f, 0.32f, 0.4f, 0.9f }; ///< まだの手順
constexpr Vector4 kFailColor = { 0.85f, 0.25f, 0.25f, 0.95f }; ///< やり直しの合図
constexpr Vector4 kCardTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kNameColor = { 0.8f, 0.9f, 1.0f, 1.0f };
constexpr Vector4 kArrowColor = { 0.8f, 0.8f, 0.85f, 0.9f };
constexpr Vector4 kSuccessColor = { 1.0f, 0.9f, 0.3f, 1.0f };
constexpr const wchar_t* kArrowText = L">";
constexpr const wchar_t* kSuccessText = L"成功！";
constexpr const wchar_t* kFailText = L"もう一度";

/** @brief 行動ごとの json での名前と、札に出す操作（{操作名} は最後に使った機器のボタン名に置き換わる） */
struct ActionInfo {
    const char* key;
    const wchar_t* label;
};
constexpr ActionInfo kActions[] = {
    { "attack", L"{Attack}" },
    { "launch", L"{Down}+{Attack}" },
    { "jump", L"{Jump}" },
    { "airAttack", L"空中{Attack}" },
    { "shoot", L"{Shoot}" },
    { "skill", L"{Skill}" },
    { "dodge", L"{Dodge}" },
};
static_assert(std::size(kActions) == static_cast<size_t>(ComboTrial::Action::kCount), "Action と表の並びをそろえる");

/** @brief json での名前から行動を探す（見つからなければ false） */
bool FindAction(const std::string& key, ComboTrial::Action& action) {
    for (size_t index = 0; index < std::size(kActions); ++index) {
        if (key == kActions[index].key) {
            action = static_cast<ComboTrial::Action>(index);
            return true;
        }
    }
    return false;
}

/** @brief このフレームにプレイヤーがその行動をしたか */
bool Performed(const Player& player, ComboTrial::Action action) {
    switch (action) {
    case ComboTrial::Action::kAttack:
        return player.JustComboHit() && player.IsOnGround() && !player.JustLaunched();
    case ComboTrial::Action::kLaunch:
        return player.JustLaunched();
    case ComboTrial::Action::kJump:
        return player.JustJumped();
    case ComboTrial::Action::kAirAttack:
        return player.JustComboHit() && !player.IsOnGround();
    case ComboTrial::Action::kShoot:
        return player.JustFired();
    case ComboTrial::Action::kSkill:
        return player.JustWeaponSkillHit();
    case ComboTrial::Action::kDodge:
        return player.JustDodged();
    default:
        return false;
    }
}

/** @brief 札に出す文字（[ボタン名]） */
std::wstring CardText(ComboTrial::Action action, const Input* input) {
    const std::wstring raw = std::wstring(L"[") + kActions[static_cast<size_t>(action)].label + L"]";
    return input != nullptr ? input->ExpandPrompts(raw) : raw;
}

/** @brief 札の大きさ（文字の大きさに余白を足す） */
Vector2 CardSize(const std::wstring& text) {
    const Vector2 size = FontRenderer::MeasureStringW(text, kCardScale);
    return { size.x + kCardPaddingX * kBothSides, size.y + kCardPaddingY * kBothSides };
}

/** @brief 札を並べた列全体の幅 */
float RowWidth(const ComboTrial::Trial& trial, const Input* input) {
    float width = 0.0f;
    for (size_t index = 0; index < trial.steps.size(); ++index) {
        width += CardSize(CardText(trial.steps[index], input)).x;
        if (index + 1 < trial.steps.size()) {
            width += kCardGap;
        }
    }
    return width;
}

/** @brief combo_trials.json から練習するコンボを読む（手順が無いものと知らない行動は飛ばす） */
std::vector<ComboTrial::Trial> LoadTrials() {
    std::vector<ComboTrial::Trial> trials;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object() || !root.contains("trials") || !root["trials"].is_array()) {
        Logger::LogWarning(std::string("ComboTrial: ") + kConfigPath + " が読めません");
        return trials;
    }
    for (const nlohmann::json& entry : root["trials"]) {
        ComboTrial::Trial trial;
        trial.name = entry.value("name", std::string());
        for (const nlohmann::json& step : entry.value("steps", nlohmann::json::array())) {
            ComboTrial::Action action = ComboTrial::Action::kAttack;
            if (step.is_string() && FindAction(step.get<std::string>(), action) && trial.steps.size() < kMaxSteps) {
                trial.steps.push_back(action);
            } else {
                Logger::LogWarning("ComboTrial: 使えない手順を飛ばしました: " + step.dump());
            }
        }
        if (!trial.steps.empty()) {
            trials.push_back(trial);
        }
    }
    return trials;
}
} // namespace

/** @brief 手順の札のスプライトを用意し、combo_trials.json を読む */
void ComboTrial::Initialize(SpriteCommon* spriteCommon) {
    trials_ = LoadTrials();
    // 途中で作ったり捨てたりしないよう、札は上限の数だけ最初に作る
    for (size_t index = 0; index < kMaxSteps; ++index) {
        cards_.push_back(std::make_unique<Sprite>());
        cards_.back()->Initialize(spriteCommon, kWhiteTexture);
    }
    panel_ = std::make_unique<Sprite>();
    panel_->Initialize(spriteCommon, kWhiteTexture);
    panel_->SetColor(kPanelColor);
}

/** @brief index 番目のコンボの練習を始める */
void ComboTrial::Start(size_t index) {
    if (index >= trials_.size()) {
        return;
    }
    current_ = index;
    active_ = true;
    successTimer_ = 0.0f;
    ResetProgress();
}

/** @brief 練習をやめる */
void ComboTrial::Stop() {
    active_ = false;
}

/** @brief 手順を最初に戻す */
void ComboTrial::ResetProgress() {
    done_ = 0;
    idleTimer_ = 0.0f;
}

/** @brief プレイヤーのこのフレームの行動を見て手順を進める */
void ComboTrial::Observe(const Player& player) {
    if (!active_ || TimeManager::GetInstance()->IsHitStopped()) {
        return;
    }
    pulseTimer_ += gameConstants::kFrameDeltaTime;
    failTimer_ = (std::max)(failTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
    // 成功を見せている間は進めず、見せ終えたら同じコンボをもう一度
    if (successTimer_ > 0.0f) {
        successTimer_ -= gameConstants::kFrameDeltaTime;
        if (successTimer_ <= 0.0f) {
            ResetProgress();
        }
        return;
    }
    const Trial& trial = trials_[current_];
    if (Performed(player, trial.steps[done_])) {
        ++done_;
        idleTimer_ = 0.0f;
        if (done_ >= trial.steps.size()) {
            successTimer_ = kSuccessSeconds;
        }
        return;
    }
    // 1つ目を出した後に間が空きすぎたら最初から
    if (done_ > 0) {
        idleTimer_ += gameConstants::kFrameDeltaTime;
        if (idleTimer_ >= kDefaultIdleSeconds) {
            ResetProgress();
            failTimer_ = kFailSeconds;
        }
    }
}

/** @brief 札の位置と色を今の進み具合に合わせる */
void ComboTrial::LayoutCards(const Input* input) const {
    const Trial& trial = trials_[current_];
    const float rowWidth = RowWidth(trial, input);
    float x = (gameConstants::kScreenWidth - rowWidth) * kHalf;
    const float pulse = kPulseBase + kPulseAmplitude * std::sin(pulseTimer_ * kPulseSpeed);
    for (size_t index = 0; index < cards_.size(); ++index) {
        Sprite& card = *cards_[index];
        if (index >= trial.steps.size()) {
            continue;
        }
        const Vector2 size = CardSize(CardText(trial.steps[index], input));
        Vector4 color = kPendingColor;
        if (failTimer_ > 0.0f) {
            color = kFailColor;
        } else if (index < done_) {
            color = kDoneColor;
        } else if (index == done_) {
            color = { kNextColor.x * pulse, kNextColor.y * pulse, kNextColor.z * pulse, kNextColor.w };
        }
        card.SetPosition({ x, kCardY });
        card.SetSize(size);
        card.SetColor(color);
        card.Update();
        x += size.x + kCardGap;
    }
    panel_->SetPosition({ (gameConstants::kScreenWidth - rowWidth) * kHalf - kPanelPadding, kNameY - kPanelPadding });
    panel_->SetSize({ rowWidth + kPanelPadding * kBothSides, kCardY - kNameY + CardSize(L"[]").y + kPanelPadding * kBothSides });
    panel_->Update();
}

/** @brief 練習中のコンボ名・手順の札の文字・成功の文字を積む */
void ComboTrial::QueueText(FontRenderer& font, const Input* input) const {
    if (!active_) {
        return;
    }
    LayoutCards(input);
    const Trial& trial = trials_[current_];
    font.DrawStringCenteredW(StringUtility::ConvertString(trial.name), kNameY, kNameScale, kNameColor);
    float x = (gameConstants::kScreenWidth - RowWidth(trial, input)) * kHalf;
    for (size_t index = 0; index < trial.steps.size(); ++index) {
        const std::wstring text = CardText(trial.steps[index], input);
        const Vector2 size = CardSize(text);
        font.DrawStringW(text, x + kCardPaddingX, kCardY + kCardPaddingY, kCardScale, kCardTextColor);
        if (index + 1 < trial.steps.size()) {
            const float arrowWidth = FontRenderer::MeasureStringW(kArrowText, kCardScale).x;
            font.DrawStringW(kArrowText, x + size.x + (kCardGap - arrowWidth) * kHalf, kCardY + kCardPaddingY, kCardScale, kArrowColor);
        }
        x += size.x + kCardGap;
    }
    if (successTimer_ > 0.0f) {
        font.DrawStringCenteredW(kSuccessText, kResultY, kResultScale, kSuccessColor);
    } else if (failTimer_ > 0.0f) {
        font.DrawStringCenteredW(kFailText, kResultY, kResultScale, kFailColor);
    }
}

/** @brief 手順の札を描く */
void ComboTrial::Draw() {
    if (!active_) {
        return;
    }
    panel_->Draw();
    const size_t count = (std::min)(trials_[current_].steps.size(), cards_.size());
    for (size_t index = 0; index < count; ++index) {
        cards_[index]->Draw();
    }
}

} // namespace engine::game
