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
    Vector2 start; ///< 始点座標
    Vector2 end; ///< 終点座標
    Vector4 color = { 0.85f, 0.95f, 1.0f, 1.0f }; ///< 色（アルファはフェードで上書き）
    float thickness = 6.0f; ///< 線の太さ（ピクセル）
    float duration = 0.18f; ///< 表示時間（秒）
};

/**
 * @brief 画面上に一瞬だけ表示される斬撃線（複数本）を生成・更新・描画するシングルトン
 */
class SlashMark {
public:
    /** @brief シングルトンインスタンスを取得する */
    static SlashMark* GetInstance();

    /**
     * @brief 斬撃線用Spriteの生成に使う共通設定を受け取って初期化する
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

    /** @brief 全斬撃線を描画コマンドとして積む */
    void Draw();

    /**
     * @brief 表示中の全斬撃線を指定色で一斉に光らせ、指定時間で消えるようにする
     * @param color    閃光の色
     * @param duration 閃光からフェードアウトまでの時間（秒）
     */
    void FlashAll(const Vector4& color, float duration);

    /** @brief 全斬撃線とプールを解放する（GPUの完了を待った後、シーン終了時にだけ呼ぶ） */
    void Clear();

private:
    /** @brief 斬線が未生成の管理器を作る */
    SlashMark() = default;

    /** @brief 内部管理用エントリ（斬撃線1本分のSpriteと経過時間） */
    struct Entry {
        std::unique_ptr<Sprite> sprite;
        Vector4 baseColor;
        float timer = 0.0f;
        float duration = 0.18f;
    };

    /** @brief 1枚分のスプライトを生成してエントリに登録する */
    void SpawnLayer(const SlashMarkParams& params, float thickness, const Vector4& color);

    /** @brief プールから空きスプライトを取り出す（無ければ新しく作る） */
    std::unique_ptr<Sprite> AcquireSprite();

    SpriteCommon* spriteCommon_ = nullptr;
    std::vector<Entry> entries_;
    /// 消えた斬撃線のスプライト置き場
    /// @note 前のフレームの描画がGPUで実行中のうちにSpriteのバッファを解放すると壊れるため、
    ///       時間切れでも破棄せずここへ戻して使い回す
    std::vector<std::unique_ptr<Sprite>> spritePool_;
};

} // namespace engine::game
