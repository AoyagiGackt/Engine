/**
 * @file Quaternion.h
 * @brief クォータニオン（回転を表す四元数）とその補間関数を定義するファイル
 */
#pragma once
#include <cmath>

/** @brief クォータニオン（四元数）回転をあらわす */
struct Quaternion
{
    float x;
    float y;
    float z;
    float w;
};

/**
 * @brief クォータニオンの球面線形補間 (Slerp)
 * @param q1 開始クォータニオン
 * @param q2 終了クォータニオン
 * @param t  補間係数 (0.0f ~ 1.0f)
 */
inline Quaternion Slerp(const Quaternion &q1, Quaternion q2, float t)
{
    float dot = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w;

    // 内積が負なら片方を反転して最短経路を取る
    if (dot < 0.0f) {
        q2 = { -q2.x, -q2.y, -q2.z, -q2.w };
        dot = -dot;
    }

    // 角度がほぼゼロなら線形補間で代用
    if (dot > 0.9995f) {
        return {
            q1.x + t * (q2.x - q1.x),
            q1.y + t * (q2.y - q1.y),
            q1.z + t * (q2.z - q1.z),
            q1.w + t * (q2.w - q1.w)
        };
    }

    float theta0 = std::acos(dot);
    float theta = theta0 * t;
    float sinTheta = std::sin(theta);
    float sinTheta0 = std::sin(theta0);

    float s1 = std::cos(theta) - dot * sinTheta / sinTheta0;
    float s2 = sinTheta / sinTheta0;

    return {
        s1 * q1.x + s2 * q2.x,
        s1 * q1.y + s2 * q2.y,
        s1 * q1.z + s2 * q2.z,
        s1 * q1.w + s2 * q2.w
    };
}
