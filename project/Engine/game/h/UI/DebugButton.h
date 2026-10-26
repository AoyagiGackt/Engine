/**
 * @file DebugButton.h
 * @brief ゲーム中に演出をすぐ確かめるための確認用ボタン（決めたキーかクリックで押す）
 * @note 評価・確認用の一時的な機能。game_rules.json の表示スイッチが false の間は何もしない。
 *       位置と色は F2 のUIエディタで動かせる（Resources/Config/UI/gameplay.json の Desc::layoutGroup のグループ）
 */
#pragma once
#include "GameRules.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Vector2.h"
#include "Vector4.h"
#include <memory>
#include <string>

namespace engine {
class DirectXCommon;
class Input;
}

namespace engine::game {

class FontRenderer;

/** @brief 画面の上端に出す確認用ボタン */
class DebugButton {
public:
    /** @brief ボタンごとに変わる設定 */
    struct Desc {
        const wchar_t* label = L""; ///< ボタンの文字（押すキーも書いておく）
        unsigned char key = 0; ///< マウスの位置に関係なく押せるキー（DIK_ の番号）
        const char* layoutGroup = ""; ///< UIレイアウトで位置と色をしまうグループ名
        float defaultTop = 0.0f; ///< 既定では画面の上端からこれだけ下げ、左右中央に置く
        Vector4 hoverColor = { 0.65f, 0.12f, 0.12f, 0.85f }; ///< マウスが乗っている間の下地の色
        bool GameRulesData::* shownFlag = nullptr; ///< 出すかどうかを決める game_rules.json の項目
    };

    /** @brief ボタンの下地のスプライトを作る（シーンの Initialize で1回） */
    void Initialize(engine::graphics::SpriteCommon* spriteCommon, const Desc& desc);
    /**
     * @brief 配置を読み直し、マウスが乗っているかと押されたかを調べる
     * @return キーを押したか、ボタンをクリックしたフレームだけ true（出さない設定なら常に false）
     */
    bool Update(engine::Input* input, const engine::DirectXCommon* dxCommon);
    /** @brief UIレイアウトから位置と大きさを読み直す（F2 の編集中など Update を通らない時も、描く前に呼ぶ） */
    void RefreshLayout();
    /** @brief ボタンの下地を描く（SpriteCommon の共通設定を済ませた後に呼ぶ） */
    void Draw();
    /** @brief ボタンの文字を積む（fontRenderer.Reset() の後に呼ぶ） */
    void QueueText(FontRenderer& font) const;

private:
    /** @brief game_rules.json でこのボタンを出す設定になっているか */
    bool IsShown() const;
    /** @brief UIレイアウトでのこのボタンの項目名（グループ名.項目） */
    std::string Key(const char* item) const;

    Desc desc_;
    std::unique_ptr<engine::graphics::Sprite> panel_;
    Vector2 position_ {}; ///< ボタンの左上（画面の座標）
    Vector2 size_ {}; ///< 文字の大きさに余白を足したボタンの大きさ
    float labelScale_ = 1.0f; ///< 文字の大きさ（Update で配置と一緒に読み直す）
    bool hovered_ = false; ///< マウスが乗っているか（乗っている間は色を変える）
};

} // namespace engine::game
