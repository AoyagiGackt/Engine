/**
 * @file UIMenu.h
 * @brief カーソル移動で選択する縦一列メニューの汎用ウィジェットを定義するファイル
 *
 * 【使い方】
 *   menu_.Initialize(spriteCommon_.get(), &fontRenderer_);
 *   menu_.BindLayout("title", "menu", { 440.0f, 470.0f }, { 400.0f, 48.0f }); // 位置・1項目の大きさの既定値
 *   menu_.SetItems({ { "NEW GAME" }, { "CONTINUE", hasSave }, { "TRAINING" } });
 *
 *   // 毎フレーム
 *   menu_.Update(input_);
 *   if (menu_.ConsumeConfirm(input_)) {
 *       switch (menu_.GetSelectedIndex()) { ... }
 *   }
 *   menu_.Draw();
 *
 * 位置・大きさ・色は UILayout（Resources/Config/UI/<layoutName>.json）から読むので、
 * F2のステージエディタ上でドラッグやパネルの数値で調整できる。
 */
#pragma once
#include "Audio.h"
#include "FontRenderer.h"
#include "Input.h"
#include "MakeAffine.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "UIButton.h"
#include <memory>
#include <string>
#include <vector>
namespace engine::game {
using engine::Input;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/** @brief UIMenuの見た目のうちコード側で決める既定値（エディタで上書きできる） */
struct UIMenuStyle {
    float labelScale = 1.5f; ///< ラベルの文字の大きさ
    bool centerLabels = false; ///< trueならラベルを項目の中央に揃える（falseは左寄せ）
};

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
    void Initialize(SpriteCommon* spriteCommon, FontRenderer* fontRenderer, engine::Audio* audio = nullptr);

    /**
     * @brief 位置・大きさ・色を読むUILayoutを指定する
     * @param layoutName UILayoutの名前（Resources/Config/UI/<layoutName>.json）
     * @param group レイアウト内のグループ名（"<group>.pos" 等のキーで読む）
     * @param defaultPosition メニュー左上の既定位置
     * @param defaultItemSize 1項目の既定の大きさ（幅, 高さ=行間）
     * @param style 文字の大きさ・揃え方の既定値
     */
    void BindLayout(const std::string& layoutName, const std::string& group,
        const Vector2& defaultPosition, const Vector2& defaultItemSize, const UIMenuStyle& style = UIMenuStyle());

    /**
     * @brief 選択項目一覧を設定する（先頭の enabled な項目へカーソルを合わせる）
     * @param items 表示する項目一覧（ラベルはUTF-8。日本語も表示できる）
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

    /** @brief メニュー左上の現在位置（項目の横に値を添えて描く時に使う） */
    Vector2 GetPosition() const { return { x_, y_ }; }
    /** @brief 1項目の現在の大きさ（幅, 高さ=行間） */
    Vector2 GetItemSize() const { return { itemWidth_, itemHeight_ }; }

    /** @brief ハイライトボックスとラベル・カーソル記号を描画する */
    void Draw();

private:
    /** @brief UILayoutから位置・大きさ・色を読み直し、変わっていればハイライト枠を作り直す */
    void ApplyBoundLayout();
    /** @brief items_ と layout の両方が揃っていればハイライト用Spriteを再構築する */
    void RebuildBoxes();
    /** @brief 選択中・無効・通常に応じてハイライトの色を塗り直す */
    void RefreshBoxColors();

    SpriteCommon* spriteCommon_ = nullptr;
    FontRenderer* fontRenderer_ = nullptr;
    engine::Audio* audio_ = nullptr;

    std::vector<UIButton> items_;
    std::vector<std::wstring> labels_; ///< items_のラベルを描画用のワイド文字列にしたもの
    std::vector<std::unique_ptr<Sprite>> boxes_;

    int cursor_ = 0;

    std::string layoutName_;
    std::string group_;
    Vector2 defaultPosition_ {};
    Vector2 defaultItemSize_ {};
    UIMenuStyle defaultStyle_ {};

    float x_ = 0.0f;
    float y_ = 0.0f;
    float itemWidth_ = 0.0f;
    float itemHeight_ = 0.0f;
    float labelScale_ = 1.0f;
    float labelPaddingX_ = 0.0f;
    bool centerLabels_ = false;
    Vector4 selectedColor_ {};
    Vector4 idleColor_ {};
    Vector4 disabledColor_ {};
    Vector4 textColor_ {};
    Vector4 textDisabledColor_ {};
};

} // namespace engine::game
