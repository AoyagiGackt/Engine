/**
 * @file UIMenu.h
 * @brief カーソル移動で選択する縦一列メニューの汎用ウィジェットを定義するファイル
 *
 * 【使い方】
 *   menu_.Initialize(spriteCommon_.get(), &fontRenderer_);
 *   menu_.SetLayout(440.0f, 300.0f, 400.0f, 70.0f); // x, y, 幅, 高さ（=行間）
 *   menu_.SetItems({ { "NEW GAME" }, { "CONTINUE", hasSave }, { "TRAINING" } });
 *
 *   // 毎フレーム
 *   menu_.Update(input_);
 *   if (menu_.ConsumeConfirm(input_)) {
 *       switch (menu_.GetSelectedIndex()) { ... }
 *   }
 *   menu_.Draw();
 */
#pragma once
#include "FontRenderer.h"
#include "Input.h"
#include "MakeAffine.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "UIButton.h"
#include <memory>
#include <vector>
namespace engine::game {
using engine::Input;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/**
 * @brief 縦一列に並んだ選択肢をカーソル移動・決定で操作する汎用メニュー
 * @note W/S・↑/↓キーで enabled な項目間をクランプ移動し、
 * SPACE/RETURN で決定する無効項目はスキップされ選択できない
 */
class UIMenu {
public:
    /**
     * @brief 初期化描画に必要な共通オブジェクトを受け取る
     * @param spriteCommon ハイライト用Spriteの生成に使う共通設定
     * @param fontRenderer ラベル・カーソル記号の描画に使うFontRenderer
     */
    void Initialize(SpriteCommon* spriteCommon, FontRenderer* fontRenderer);

    /**
     * @brief 表示位置とレイアウトを設定する
     * @param x         メニュー全体の左上X座標
     * @param y         メニュー全体の左上Y座標
     * @param itemWidth 各項目のハイライト幅
     * @param itemHeight 各項目の高さ（=行間としても使う）
     */
    void SetLayout(float x, float y, float itemWidth, float itemHeight);

    /**
     * @brief 選択項目一覧を設定する（先頭の enabled な項目へカーソルを合わせる）
     * @param items 表示する項目一覧
     */
    void SetItems(const std::vector<UIButton>& items);

    /**
     * @brief 毎フレーム更新カーソル移動とハイライト色の更新を行う
     * @param input 入力管理のポインタ
     */
    void Update(Input* input);

    /**
     * @brief 決定入力があったかを判定する
     * @param input 入力管理のポインタ
     * @return bool 選択中の項目が enabled かつ SPACE/RETURN が押された瞬間なら true
     */
    bool ConsumeConfirm(Input* input);

    /** @brief 現在選択中の項目インデックスを取得する */
    int GetSelectedIndex() const { return cursor_; }

    /** @brief ハイライトボックスとラベル・カーソル記号を描画する */
    void Draw();

private:
    /** @brief items_ と layout の両方が揃っていればハイライト用Spriteを再構築する */
    void RebuildBoxes();

    SpriteCommon* spriteCommon_ = nullptr;
    FontRenderer* fontRenderer_ = nullptr;

    std::vector<UIButton> items_;
    std::vector<std::unique_ptr<Sprite>> boxes_;

    int cursor_ = 0;

    float x_ = 0.0f;
    float y_ = 0.0f;
    float itemWidth_ = 0.0f;
    float itemHeight_ = 0.0f;
};

} // namespace engine::game
