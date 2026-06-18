/**
 * @file BezierSpline.h
 * @brief ベジェ曲線 / Catmull-Rom スプラインユーティリティ（ヘッダーオンリー）
 *
 * 【含まれる機能】
 *   BezierSpline::QuadraticBezier  ... 2次ベジェ曲線（3制御点）
 *   BezierSpline::CubicBezier      ... 3次ベジェ曲線（4制御点）
 *   BezierSpline::Spline           ... Catmull-Rom スプライン（任意個の制御点）
 *
 * 【Catmull-Rom の特徴】
 *   - 全ての制御点を曲線が必ず通る（ベジェは通らない）
 *   - 隣接する制御点から接線を自動計算するので制御点を置くだけでなめらかな曲線になる
 *   - カメラパス・敵の巡回路・エフェクト軌跡に最適
 *
 * 【使い方（3次ベジェ）】
 *   Vector3 pos = BezierSpline::CubicBezier(p0, p1, p2, p3, 0.5f); // t=0〜1
 *
 * 【使い方（Catmull-Rom スプライン）】
 *   BezierSpline::Spline spline;
 *   spline.AddPoint({ 0, 0, 0 });  // 制御点を 4 つ以上追加
 *   spline.AddPoint({ 1, 2, 0 });
 *   spline.AddPoint({ 3, 1, 0 });
 *   spline.AddPoint({ 4, 3, 0 });
 *   Vector3 pos = spline.Evaluate(0.5f); // t=0（始点）〜1（終点）で全体をサンプリング
 */
#pragma once
#include "MakeAffine.h"
#include <vector>
#include <cassert>
#include <cmath>

namespace BezierSpline {

// =====================================================
// 2次ベジェ曲線
// =====================================================
/**
 * @brief 2次ベジェ曲線の評価（3制御点）
 * @param p0 始点
 * @param p1 制御点（曲線は通らない）
 * @param p2 終点
 * @param t  パラメータ（0.0=始点, 1.0=終点）
 * @return 曲線上の点
 */
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

// =====================================================
// 3次ベジェ曲線
// =====================================================
/**
 * @brief 3次ベジェ曲線の評価（4制御点）
 * @param p0 始点
 * @param p1 始点側の制御点（曲線は通らない）
 * @param p2 終点側の制御点（曲線は通らない）
 * @param p3 終点
 * @param t  パラメータ（0.0=始点, 1.0=終点）
 * @return 曲線上の点
 */
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

/**
 * @brief Catmull-Rom スプライン（任意個の制御点、すべての点を通る）
 *
 * 制御点を AddPoint() で追加し、Evaluate(t) で曲線上の点を取得する。
 * t=0 が最初のセグメントの始点、t=1 が最後のセグメントの終点に対応する。
 *
 * @note 制御点は最低 4 つ必要（最初と最後は補間に使われ曲線は通らない）
 */
class Spline {
public:
    /// @brief 制御点を末尾に追加する
    void AddPoint(const Vector3& p) { points_.push_back(p); }

    /// @brief 全制御点を削除する
    void Clear()                    { points_.clear(); }

    /// @brief 現在の制御点数を返す
    int  PointCount() const         { return (int)points_.size(); }

    /**
     * @brief スプライン全体を t=[0,1] でサンプリングする
     * @param t パラメータ（0.0=始点セグメントの先頭, 1.0=終点セグメントの末尾）
     * @return 曲線上の点
     * @note 制御点が 4 つ未満の場合は assert でエラーになる
     */
    Vector3 Evaluate(float t) const
    {
        assert(points_.size() >= 4 && "Catmull-Rom スプラインは制御点が 4 つ以上必要");

        // セグメント数は (制御点数 - 3) 個（最初と最後は補間専用のため）
        int n    = (int)points_.size() - 1;
        float ft = t * (float)(n - 2); // グローバル t をセグメントインデックスに変換
        int   i  = (int)ft;
        if (i < 1)     i = 1;     // 最初のセグメントにクランプ
        if (i > n - 2) i = n - 2; // 最後のセグメントにクランプ
        float local = ft - (float)i; // セグメント内のローカル t

        return CatmullRom(points_[i - 1], points_[i], points_[i + 1], points_[i + 2], local);
    }

    /**
     * @brief i 番目のセグメントのみを t=[0,1] でサンプリングする
     * @param i      セグメントインデックス（1 〜 points_.size()-2 の範囲）
     * @param t      セグメント内のパラメータ（0.0〜1.0）
     * @return 曲線上の点
     */
    Vector3 EvaluateSegment(int i, float t) const
    {
        assert(i >= 1 && i < (int)points_.size() - 1 && "セグメントインデックスが範囲外");
        return CatmullRom(points_[i - 1], points_[i], points_[i + 1], points_[i + 2], t);
    }

    /// @brief 制御点列への参照を取得する（デバッグ描画などに使用）
    const std::vector<Vector3>& GetPoints() const { return points_; }

private:
    /**
     * @brief Catmull-Rom スプラインの 1 セグメントを計算する
     * @param p0 前の制御点（セグメントには含まれない、接線計算に使用）
     * @param p1 セグメントの始点（曲線はここを通る）
     * @param p2 セグメントの終点（曲線はここを通る）
     * @param p3 次の制御点（セグメントには含まれない、接線計算に使用）
     * @param t  セグメント内パラメータ（0.0〜1.0）
     */
    static Vector3 CatmullRom(const Vector3& p0, const Vector3& p1,
                               const Vector3& p2, const Vector3& p3, float t)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        // Catmull-Rom の係数（α=0.5 の場合の標準公式）
        // P(t) = 0.5 * [ 2P1 + (-P0+P2)t + (2P0-5P1+4P2-P3)t^2 + (-P0+3P1-3P2+P3)t^3 ]
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

    std::vector<Vector3> points_; ///< 制御点列
};

} // namespace BezierSpline
