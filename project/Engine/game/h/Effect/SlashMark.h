/**
 * @file SlashMark.h
 * @brief 画面上に一瞬だけ残る斬撃線エフェクトを描画するファイル
 *
 * 【概要】
 *   2点間を結ぶ細長い Sprite を生成し、短時間表示してからフェードアウトさせる。
 *   「敵を中心に複数本の斬撃線が走る」大技演出などに使う。
 *
 * 【使い方】
 *   // 初期化（一度だけ）
 *   SlashMark::GetInstance()->Initialize(spriteCommon_.get());
 *
 *   // 斬撃線をスポーン
 *   SlashMarkParams p;
 *   p.start = { center.x - 3.0f, center.y - 2.0f };
 *   p.end   = { center.x + 3.0f, center.y + 2.0f };
 *   SlashMark::GetInstance()->Spawn(p);
 *
 *   // 毎フレーム（Update と Draw）
 *   SlashMark::GetInstance()->Update(dt);
 *   SlashMark::GetInstance()->Draw(); // spriteCommon_->CommonDrawSettings() の後に呼ぶ
 *
 *   // シーン切り替え時に全斬撃線を削除
 *   SlashMark::GetInstance()->Clear();
 */
#pragma once
#include "MakeAffine.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
#include <vector>
namespace engine::game {
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/** @brief 斬撃線1本分の生成パラメータ（座標はスクリーン/ワールド共通の2D座標系） */
struct SlashMarkParams {
    Vector2 start;                                    ///< 始点座標
    Vector2 end;                                      ///< 終点座標
    Vector4 color     = { 0.85f, 0.95f, 1.0f, 1.0f }; ///< 色（アルファはフェードで上書き）
    float   thickness = 6.0f;                          ///< 線の太さ（ピクセル）
    float   duration  = 0.18f;                         ///< 表示時間（秒）
};

class SlashMark {
public:
    /// @brief シングルトンインスタンスを取得する
    static SlashMark* GetInstance();

    /**
     * @brief 初期化。斬撃線用Spriteの生成に使う共通設定を受け取る
     * @param spriteCommon 2D描画の共通設定オブジェクトのポインタ
     */
    void Initialize(SpriteCommon* spriteCommon);

    /**
     * @brief 新しい斬撃線を登録する
     * @param params 表示パラメータ（SlashMarkParams 参照）
     */
    void Spawn(const SlashMarkParams& params);

    /**
     * @brief 毎フレームタイマーを更新し、時間切れの斬撃線を削除する
     * @param dt デルタタイム（秒）
     */
    void Update(float dt);

    /// @brief 全斬撃線を描画コマンドとして積む
    void Draw();

    /// @brief 全斬撃線を即座に削除する（シーン切り替え時などに呼ぶ）
    void Clear();

private:
    SlashMark() = default;

    /// @brief 内部管理用エントリ（斬撃線1本分のSpriteと経過時間）
    struct Entry {
        std::unique_ptr<Sprite> sprite;
        Vector4                 baseColor;
        float                   timer    = 0.0f;
        float                   duration = 0.18f;
    };

    SpriteCommon*      spriteCommon_ = nullptr;
    std::vector<Entry> entries_;
};

} // namespace engine::game
