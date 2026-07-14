/**
 * @file Vector3.h
 * @brief 3次元ベクトル構造体と、それに関する基本演算を定義するファイル
 */
#pragma once
#include <cmath>

/** @brief 3次元ベクトル */
struct Vector3 {
    float x;
    float y;
    float z;
};

/**
 * @brief 3次元ベクトルの長さを求める
 */
inline float Length(const Vector3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

/**
 * @brief 2点間の距離を求める
 */
inline float Distance(const Vector3& p1, const Vector3& p2)
{
    Vector3 diff = { p1.x - p2.x, p1.y - p2.y, p1.z - p2.z };
    return Length(diff);
}

/**
 * @brief 指定した値をminとmaxの範囲に収める
 */
inline float Clamp(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

/**
 * @brief ベクトルの正規化（長さを1にする）
 * @note 移動ベクトルを一定の速度にする際などに使用します
 */
inline Vector3 Normalize(const Vector3& v)
{
    float len = Length(v);
    if (len != 0.0f) {
        return { v.x / len, v.y / len, v.z / len };
    }
    return v;
}

/**
 * @brief ベクトルの内積 (Dot Product)
 * @note 2つのベクトルがどれくらい同じ方向を向いているかを判定します
 */
inline float Dot(const Vector3& v1, const Vector3& v2)
{
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

/**
 * @brief ベクトルの外積 (Cross Product)
 * @note 2つのベクトルに垂直なベクトルを求めます（面の法線計算などに使用）
 */
inline Vector3 Cross(const Vector3& v1, const Vector3& v2)
{
    return {
        v1.y * v2.z - v1.z * v2.y,
        v1.z * v2.x - v1.x * v2.z,
        v1.x * v2.y - v1.y * v2.x
    };
}

/** @brief ベクトルの引き算 (v1 - v2) */
inline Vector3 Subtract(const Vector3& v1, const Vector3& v2)
{
    return { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

/**
 * @brief ベクトルの線形補間
 */
inline Vector3 Lerp(const Vector3& start, const Vector3& end, float t)
{
    return { start.x + (end.x - start.x) * t,
        start.y + (end.y - start.y) * t,
        start.z + (end.z - start.z) * t };
}

/** @brief ベクトルの足し算 */
inline Vector3 operator+(const Vector3& v1, const Vector3& v2)
{
    return { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

/** @brief ベクトルのスカラー倍 */
inline Vector3 operator*(const Vector3& v, float s)
{
    return { v.x * s, v.y * s, v.z * s };
}
