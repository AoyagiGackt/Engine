/**
 * @file ResultBackdrop.h
 * @brief クリアの結果画面の背景（ランクの色で流れる斜めの帯・大きなランクの文字の透かし・漂うガラスの破片）を描くファイル
 * @note 色はランクの色をそのまま使わず、彩度を下げて暗くした色にする。帯と下地の明るさの差も小さくし、
 *       鮮やかな色どうしがぶつかって目がちらつく（ハレーション）のを避け、手前の結果の文字を一番目立たせる。
 *       数値は Resources/Config/result_backdrop.json で調整する
 */
#pragma once
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Vector2.h"
#include "Vector4.h"
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace engine::game {

class FontRenderer;

/** @brief クリアの結果画面の背景 */
class ResultBackdrop {
public:
    /** @brief 帯と破片のスプライトを作る（シーンの Initialize で1回） */
    void Initialize(engine::graphics::SpriteCommon* spriteCommon);

    /**
     * @brief 結果画面を出し始める時に呼ぶ（破片を散らし直し、色を決める）
     * @param rankColor 取ったランクの色（ラン外のクリアなど、ランクが無い時は設定の既定の色を渡す）
     */
    void Start(const Vector4& rankColor);

    /** @brief 帯と破片を動かす（毎フレーム） */
    void Update(float dt);

    /** @brief 下地・帯・破片・文字の後ろの暗い板を描く（SpriteCommon の共通設定を済ませた後、文字より先に呼ぶ） */
    void Draw();

    /** @brief 大きなランクの文字の透かしを文字の描画予約に積む（結果の文字より先に積む） */
    void QueueWatermark(FontRenderer& font, const std::string& rankLetter) const;

    /** @brief ランクが無い時に使う既定の色を返す */
    static Vector4 NeutralColor();

    /** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
    static void ReloadTuning();

private:
    /** @brief 漂う破片1つ */
    struct Shard {
        Vector2 position;
        Vector2 velocity; ///< 1秒あたりの移動量（ピクセル）
        float rotation = 0.0f;
        float spin = 0.0f; ///< 1秒あたりの回転（ラジアン）
        Vector2 size;
        float alpha = 0.0f; ///< 基準の濃さ
        float phase = 0.0f; ///< きらめきの位相（破片ごとにずらす）
    };

    /** @brief 破片を1つ作り直す（atTop なら画面の上から、そうでなければ画面のどこか） */
    void RespawnShard(Shard& shard, bool atTop);

    std::unique_ptr<engine::graphics::Sprite> base_; ///< 下地
    std::unique_ptr<engine::graphics::Sprite> panel_; ///< 結果の文字の後ろの暗い板
    std::vector<std::unique_ptr<engine::graphics::Sprite>> stripes_;
    std::vector<std::unique_ptr<engine::graphics::Sprite>> shardSprites_;
    std::vector<Shard> shards_;
    std::mt19937 rng_ { std::random_device {}() };
    Vector4 tone_ = { 0.5f, 0.5f, 0.5f, 1.0f }; ///< ランクの色の彩度を落とした色（明るさは使う所ごとに掛ける）
    float time_ = 0.0f;
};

} // namespace engine::game
