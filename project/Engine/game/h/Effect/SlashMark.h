/**
 * @file SlashMark.h
 * @brief 画面上に一瞬だけ残る斬撃線エフェクトを描画するファイル
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

/** @brief 斬撃線1本分の生成パラメータ（座標はスクリーン座標・ピクセル） */
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
     * @brief 初期化斬撃線用Spriteの生成に使う共通設定を受け取る
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

    /**
     * @brief 表示中の全斬撃線を指定色で一斉に光らせ、指定時間で消えるようにする
     * @param color    閃光の色
     * @param duration 閃光からフェードアウトまでの時間（秒）
     */
    void FlashAll(const Vector4& color, float duration);

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

    /// @brief 1枚分のスプライトを生成してエントリに登録する
    void SpawnLayer(const SlashMarkParams& params, float thickness, const Vector4& color);

    SpriteCommon*      spriteCommon_ = nullptr;
    std::vector<Entry> entries_;
};

} // namespace engine::game
