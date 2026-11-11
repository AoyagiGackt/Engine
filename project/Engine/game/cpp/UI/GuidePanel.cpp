/**
 * @file GuidePanel.cpp
 * @brief 操作説明の板の描き方と、ステージの最初に一度だけ出す操作説明（controls_guide.json）
 */
#include "GuidePanel.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "StringUtility.h"
#include "UILayout.h"
#include <algorithm>

namespace engine::game {
namespace {
// 板の既定の見た目（UILayout の guide.* で上書きできる）
constexpr Vector2 kGuideCenter = { gameConstants::kScreenCenterX, gameConstants::kScreenCenterY };
constexpr float kGuideScale = 1.1f;
constexpr float kGuideTitleScale = 1.6f;
constexpr float kGuidePadding = 28.0f;
constexpr float kGuideGap = 18.0f;
constexpr Vector4 kGuidePanelColor = { 0.02f, 0.01f, 0.05f, 0.92f };
constexpr Vector4 kGuideTitleColor = { 0.85f, 0.75f, 1.0f, 1.0f };
constexpr Vector4 kGuideBodyColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kGuideConfirmColor = { 1.0f, 0.9f, 0.4f, 1.0f };
constexpr float kMinFadeSeconds = 0.01f; ///< 出入りの長さが0の時に割り算で困らないための下限

constexpr const char* kConfigPath = "Resources/Config/controls_guide.json";
constexpr const char* kStageLayoutName = "gameplay"; ///< ステージの操作説明の配置（F2 で動かせる）

/** @brief ステージの最初の操作説明の調整値（controls_guide.json。初期値はJSONに項目が無い時の既定値） */
struct StageGuideTuning {
    std::string level = "Resources/Levels/level01.json"; ///< 操作説明を出すレベル（空なら出さない）
    std::string title = "操作説明";
    std::string lines = "{Move}  移動\n{Jump}  ジャンプ\n{Attack}  攻撃（続けて押すとコンボ）\n{Down}+{Attack}  打ち上げ\n{Shoot}  射撃\n{Dodge}  回避（攻撃に合わせるとジャスト回避）\n{Skill}  武器の固有技\n{Steal}  倒した敵の武器を奪う\n{Awaken}  覚醒    {Finisher}  フィニッシャー\n{Pause}  ポーズ・音量設定";
    std::string confirm = "{Attack} / 決定キー で始める";
    float minSeconds = 0.6f; ///< 開いてから決定を受け付けるまで
    float fadeSeconds = 0.3f; ///< 出入りの長さ
};

constexpr JsonField<StageGuideTuning> kStageGuideFields[] = {
    { "stage", "level", &StageGuideTuning::level },
    { "stage", "title", &StageGuideTuning::title },
    { "stage", "lines", &StageGuideTuning::lines },
    { "stage", "confirm", &StageGuideTuning::confirm },
    { "stage", "minSeconds", &StageGuideTuning::minSeconds },
    { "stage", "fadeSeconds", &StageGuideTuning::fadeSeconds },
};

/** @brief controls_guide.json を読む（無ければ既定値） */
const StageGuideTuning& StageTune() {
    static const StageGuideTuning kTuning = ReadJsonFields(JsonHelper::Load(kConfigPath), kStageGuideFields);
    return kTuning;
}

/** @brief アプリを起動してから、ステージの操作説明をもう見せたか */
bool sStageGuideShown = false;

/** @brief 不透明度を掛けた色 */
Vector4 Faded(const Vector4& color, float alpha) {
    return { color.x, color.y, color.z, color.w * alpha };
}
} // namespace

/** @brief 板と文字を FontRenderer へ積む */
void GuidePanel::Queue(FontRenderer& font, const std::string& layoutName, const Text& text, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (text.title.empty() || alpha <= 0.0f) {
        return;
    }
    UILayout& layout = UILayout::Get(layoutName);
    // 文の大きさから板の大きさを決め、決めた中心に置く
    const float bodyScale = (std::max)(layout.Float("guide.scale", kGuideScale), 0.0f);
    const float titleScale = (std::max)(layout.Float("guideTitle.scale", kGuideTitleScale), 0.0f);
    const float padding = (std::max)(layout.Float("guide.padding", kGuidePadding), 0.0f);
    const float gap = (std::max)(layout.Float("guide.gap", kGuideGap), 0.0f);
    const Vector2 titleSize = FontRenderer::MeasureStringW(text.title, titleScale);
    const Vector2 bodySize = FontRenderer::MeasureStringW(text.body, bodyScale);
    const Vector2 confirmSize = FontRenderer::MeasureStringW(text.confirm, bodyScale);
    const float width = (std::max)({ titleSize.x, bodySize.x, confirmSize.x }) + padding + padding;
    const float height = titleSize.y + bodySize.y + confirmSize.y + gap + gap + padding + padding;
    const Vector2 center = layout.Pos("guide.center", kGuideCenter);
    const Vector2 topLeft = { center.x - width * 0.5f, center.y - height * 0.5f };
    font.DrawPanel(topLeft.x, topLeft.y, width, height, Faded(layout.Color("guide.panel", kGuidePanelColor), alpha));
    // 見出しは中央、本文は左揃え、始め方は中央に置く
    float y = topLeft.y + padding;
    font.DrawStringW(text.title, center.x - titleSize.x * 0.5f, y, titleScale, Faded(layout.Color("guideTitle.color", kGuideTitleColor), alpha));
    y += titleSize.y + gap;
    font.DrawStringW(text.body, topLeft.x + padding, y, bodyScale, Faded(layout.Color("guide.color", kGuideBodyColor), alpha));
    y += bodySize.y + gap;
    font.DrawStringW(text.confirm, center.x - confirmSize.x * 0.5f, y, bodyScale,
        Faded(layout.Color("guideConfirm.color", kGuideConfirmColor), alpha));
}

/** @brief このレベルで出すなら出す準備をする */
void StageControlsGuide::Arm(const std::string& levelPath) {
    armed_ = !sStageGuideShown && !StageTune().level.empty() && levelPath == StageTune().level;
}

/** @brief 1フレーム進める（開いていて閉じ始めていない間は true） */
bool StageControlsGuide::Update(const Input& input, bool canOpen) {
    const StageGuideTuning& tune = StageTune();
    const float fadeStep = gameConstants::kFrameDeltaTime / (std::max)(tune.fadeSeconds, kMinFadeSeconds);
    // 開始演出が終わったら開く
    if (armed_ && canOpen) {
        armed_ = false;
        open_ = true;
        closing_ = false;
        timer_ = 0.0f;
        sStageGuideShown = true;
    }
    if (!open_) {
        return false;
    }
    timer_ += gameConstants::kFrameDeltaTime;
    // 閉じ始めたら薄くして、消え切ったら終わり（その間はもう操作できる）
    if (closing_) {
        alpha_ = (std::max)(alpha_ - fadeStep, 0.0f);
        open_ = alpha_ > 0.0f;
        return false;
    }
    alpha_ = (std::min)(alpha_ + fadeStep, 1.0f);
    // 出してすぐは受け付けない（読まずに閉じないように）
    if (timer_ >= tune.minSeconds && (input.TriggerMenuConfirm() || input.TriggerAction(Input::Action::kAttack))) {
        // 閉じる時に押したボタンで攻撃が出ないよう、このフレームまではゲームを止めておく
        closing_ = true;
    }
    return true;
}

/** @brief 開いていれば板を積む */
void StageControlsGuide::Draw(FontRenderer& font, const Input& input) const {
    if (!open_) {
        return;
    }
    const StageGuideTuning& tune = StageTune();
    const GuidePanel::Text text = {
        input.ExpandPrompts(StringUtility::ConvertString(tune.title)),
        input.ExpandPrompts(StringUtility::ConvertString(tune.lines)),
        input.ExpandPrompts(StringUtility::ConvertString(tune.confirm)),
    };
    GuidePanel::Queue(font, kStageLayoutName, text, alpha_);
}

} // namespace engine::game
