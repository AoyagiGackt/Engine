/**
 * @file BlendMode.h
 * @brief 描画のブレンドモードの種類
 */
#pragma once
namespace engine::graphics {
enum class BlendMode {
    kNone, // ブレンドなし（不透明）
    kAlpha, // 通常の半透明（アルファブレンド）
    kAdd, // 加算合成（エフェクト等）
    kSubtract, // 減算合成
    kMultiply, // 乗算合成
    kCount // 総数
};

} // namespace engine::graphics
