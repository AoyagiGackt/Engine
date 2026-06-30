// ベジェ曲線 / Catmull-Rom スプラインユーティリティ（ヘッダーオンリー）
#pragma once
#include "MakeAffine.h"
#include <vector>
#include <cassert>
#include <cmath>

namespace BezierSpline {

// 2次ベジェ曲線（3制御点）
inline Vector3 QuadraticBezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, float t)
{
    float u  = 1.0f - t;
    // B(t) = (1-t)^2 * P0 + 2*(1-t)*t * P1 + t^2 * P2
    return {
        u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x,
        u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y,
        u * u * p0.z + 2.0f * u * t * p1.z + t * t * p2.z,
    };
}

// 3次ベジェ曲線（4制御点）
inline Vector3 CubicBezier(const Vector3& p0, const Vector3& p1,
                            const Vector3& p2, const Vector3& p3, float t)
{
    float u  = 1.0f - t;
    float u2 = u * u;
    float u3 = u2 * u;
    float t2 = t * t;
    float t3 = t2 * t;
    // B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
    return {
        u3 * p0.x + 3.0f * u2 * t * p1.x + 3.0f * u * t2 * p2.x + t3 * p3.x,
        u3 * p0.y + 3.0f * u2 * t * p1.y + 3.0f * u * t2 * p2.y + t3 * p3.y,
        u3 * p0.z + 3.0f * u2 * t * p1.z + 3.0f * u * t2 * p2.z + t3 * p3.z,
    };
}

// Catmull-Rom スプライン（全制御点を通る、最低4点必要）
class Spline {
public:
    void AddPoint(const Vector3& p) { points_.push_back(p); }
    void Clear()                    { points_.clear(); }
    int  PointCount() const         { return (int)points_.size(); }
    Vector3 Evaluate(float t) const
    {
        assert(points_.size() >= 4 && "Catmull-Rom スプラインは制御点が 4 つ以上必要");

        int n    = (int)points_.size() - 1;
        float ft = t * (float)(n - 2); // グローバル t をセグメントインデックスに変換
        int   i  = (int)ft;
        if (i < 1)     { i = 1; }     // 最初のセグメントにクランプ
        if (i > n - 2) { i = n - 2; } // 最後のセグメントにクランプ
        float local = ft - (float)i; // セグメント内のローカル t

        return CatmullRom(points_[i - 1], points_[i], points_[i + 1], points_[i + 2], local);
    }

    Vector3 EvaluateSegment(int i, float t) const
    {
        assert(i >= 1 && i < (int)points_.size() - 1 && "セグメントインデックスが範囲外");
        return CatmullRom(points_[i - 1], points_[i], points_[i + 1], points_[i + 2], t);
    }

    const std::vector<Vector3>& GetPoints() const { return points_; }

private:
    static Vector3 CatmullRom(const Vector3& p0, const Vector3& p1,
                               const Vector3& p2, const Vector3& p3, float t)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        auto calc = [&](float a, float b, float c, float d) -> float {
            return 0.5f * (2.0f * b
                + (-a + c) * t
                + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2
                + (-a + 3.0f * b - 3.0f * c + d) * t3);
        };

        return {
            calc(p0.x, p1.x, p2.x, p3.x),
            calc(p0.y, p1.y, p2.y, p3.y),
            calc(p0.z, p1.z, p2.z, p3.z),
        };
    }

    std::vector<Vector3> points_;
};

} // namespace BezierSpline
