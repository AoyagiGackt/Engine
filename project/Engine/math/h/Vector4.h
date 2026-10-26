/**
 * @file Vector4.h
 * @brief 4次元ベクトル構造体を定義するファイル
 */
#pragma once
#include "Vector3.h"

namespace engine {

/** @brief 4次元ベクトル */
struct Vector4 {
    float x;
    float y;
    float z;
    float w;
};

/** @brief RGBA配列を色ベクトルへ変換する */
inline Vector4 ColorFromArray(const float (&color)[4]) {
    return { color[0], color[1], color[2], color[3] };
}
/** @brief RGBAベクトルからRGB成分を取り出す */
inline Vector3 ColorRgb(const Vector4& color) {
    return { color.x, color.y, color.z };
}
/** @brief RGBA配列からRGB成分を取り出す */
inline Vector3 ColorRgb(const float (&color)[4]) {
    return ColorRgb(ColorFromArray(color));
}
/** @brief 2つの4次元ベクトル（色など）を t（0〜1）の割合で混ぜる */
inline Vector4 Lerp(const Vector4& start, const Vector4& end, float t) {
    return {
        start.x + (end.x - start.x) * t,
        start.y + (end.y - start.y) * t,
        start.z + (end.z - start.z) * t,
        start.w + (end.w - start.w) * t
    };
}

} // namespace engine
