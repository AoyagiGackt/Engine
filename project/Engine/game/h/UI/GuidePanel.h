/**
 * @file GuidePanel.h
 * @brief 画面中央に出す操作説明の板（見出し・本文・始め方）と、ステージの最初に一度だけ出す操作説明（StageControlsGuide）
 * @note 板の描き方は共通（GuidePanel::Queue）。ラスボス戦の操作説明もこの描き方を使う
 */
#pragma once
#include "Vector2.h"
#include "Vector4.h"
#include <string>

namespace engine {
class Input;
}

namespace engine::game {

class FontRenderer;

/** @brief 画面中央の操作説明の板（下敷き・見出し・本文・始め方）を描く */
class GuidePanel {
public:
    /** @brief 板に出す文（{操作名} は呼ぶ側で表記にしておく） */
    struct Text {
        std::wstring title; ///< 見出し（空なら板を出さない）
        std::wstring body; ///< 本文（改行で行を分ける）
        std::wstring confirm; ///< 板の下に出す始め方の案内
    };
    /**
     * @brief 板と文字を FontRenderer へ積む（文字の描画は呼ぶ側の FontRenderer::Draw）
     * @param layoutName 配置を上書きする UILayout の名前（guide.* / guideTitle.* / guideConfirm.* の項目）
     * @param alpha      不透明度（0〜1）
     */
    static void Queue(FontRenderer& font, const std::string& layoutName, const Text& text, float alpha);
};

/**
 * @brief ステージの最初に一度だけ操作説明を出し、決定キーで閉じるまでゲームを止める
 * @note 文と出すレベルは Resources/Config/controls_guide.json。アプリを起動している間に一度だけ出す
 */
class StageControlsGuide {
public:
    /** @brief このレベルで出すなら出す準備をする（開始演出が終わったら開く） */
    void Arm(const std::string& levelPath);
    /**
     * @brief 1フレーム進める（開くのを待っている・開いている間は true を返し、呼ぶ側はゲームを止める）
     * @param canOpen 開いてよいか（開始演出の最中は false）
     */
    bool Update(const Input& input, bool canOpen);
    /** @brief 開いていれば板を積む */
    void Draw(FontRenderer& font, const Input& input) const;
    /** @brief 板を出していて、まだ閉じ始めていないか（この間は後ろの HUD を隠す） */
    bool IsShowing() const { return open_ && !closing_; }

private:
    bool armed_ = false; ///< 開くのを待っているか
    bool open_ = false; ///< 開いているか（閉じ始めても薄くなり切るまで描く）
    bool closing_ = false; ///< 決定が押されて薄くなっている途中か
    float timer_ = 0.0f; ///< 開いてからの秒数
    float alpha_ = 0.0f; ///< 板の不透明度
};

} // namespace engine::game
