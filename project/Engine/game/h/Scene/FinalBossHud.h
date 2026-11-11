/** @file FinalBossHud.h
 * @brief 最終戦の体力と演出文字を表示する。
 */
#pragma once
#include "Sprite.h"
#include <array>
#include <memory>
#include <string>
namespace engine::game {
class FontRenderer;
/** @brief 呼び出し側の状態からボス戦の画面表示を構成する。 */
class FinalBossHud {
public:
    /** @brief 毎フレーム呼ぶ側が渡す表示の状態。 */
    struct State {
        float hpRatio = 1.0f;
        bool visibleBar = false;
        bool rage = false;
        float letterbox = 0.0f;
        float whiteVeil = 0.0f;
        std::wstring bossName;
        float nameReveal = 0.0f;
        std::wstring line;
        float lineAlpha = 0.0f;
        std::wstring guideTitle;   // 操作説明の見出し（空なら操作説明を出さない）
        std::wstring guide;        // 操作説明の本文（改行で行を分ける）
        std::wstring guideConfirm; // 操作説明の下に出す、始め方の案内
        float guideAlpha = 0.0f;   // 操作説明の不透明度 0〜1
    };
    /** @brief 全スプライトを確保し、文字描画先を保持する。 */
    void Initialize(engine::graphics::SpriteCommon* spriteCommon, FontRenderer* font);
    /** @brief 表示状態と遅れて減るバー、全スプライトを更新する。 */
    void Update(const State& state, float deltaTime);
    /** @brief スプライトを描き、文字を予約する。フォントのDrawは呼び出し側が行う。 */
    void Draw();
private:
    static constexpr std::size_t kSpriteCount = 7;
    /** @brief 単色スプライトの位置と大きさ、色を設定する。 */
    void SetPanel(std::size_t index, const Vector2& position, const Vector2& size, const Vector4& color);
    /** @brief 戦闘の前の操作説明（下敷きの板・見出し・本文・始め方）を予約する。 */
    void QueueGuide();
    /** @brief 配置の上書きと白い幕を反映して日本語文字を予約する。 */
    void QueueText(const std::string& key, const std::wstring& text, const Vector2& position,
        float scale, float alpha, float anchor);
    std::array<std::unique_ptr<engine::graphics::Sprite>, kSpriteCount> sprites_;
    FontRenderer* font_ = nullptr;
    State state_ {};
    float delayedHp_ = 1.0f;
    float delayLeft_ = 0.0f;
    bool initialized_ = false;
};
} // namespace engine::game
