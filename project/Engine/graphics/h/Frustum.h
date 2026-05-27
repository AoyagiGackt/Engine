/**
 * @file Frustum.h
 * @brief フラスタム（視錐台）カリング — ヘッダーオンリー実装
 *
 * カメラの View×Projection 行列から 6 枚の平面を抽出し、
 * AABB・球体・Collider が視野内かどうかを O(6) で判定します。
 *
 * ■ 使い方
 *   // フレーム先頭（カメラ更新後）
 *   Frustum frustum;
 *   frustum.ExtractFromMatrix(camera->GetViewProjectionMatrix());
 *
 *   // 描画判定
 *   if (frustum.IsVisible(obj->GetCollider().GetBroadAABB())) {
 *       obj->Draw();
 *   }
 */
#pragma once
#include "MakeAffine.h"
#include "CollisionConfig.h"
#include <cmath>

/**
 * @brief 視錐台の 6 面を保持し、オブジェクトの可視性を判定するクラス
 *
 * ■ アルゴリズム
 *   Gribb-Hartmann 法（DirectX 行ベクトル規約 v_clip = v_world × VP）で平面抽出。
 *   NDC の z 範囲は DirectX の [0, 1] に対応。
 *
 * ■ AABB 判定: p-vertex テスト（保守的・偽陰性なし）
 *   各平面について「法線方向に最も張り出した頂点 = p-vertex」が平面の外にあれば
 *   AABB 全体がその平面の外側 → 視野外確定。
 *
 * ■ 球体判定: 中心と平面の符号付き距離 vs -半径
 *   正規化済み平面のため厳密な距離値が使える。
 */
class Frustum {
public:
    /**
     * @brief View × Projection 合成行列から 6 平面を抽出する
     * @param vp Camera::GetViewProjectionMatrix() の戻り値を渡してください
     *
     * 抽出後、各平面は自動的に正規化されます（球体判定に必要）。
     */
    void ExtractFromMatrix(const Matrix4x4& vp)
    {
        // ■ 行ベクトル規約: clip[j] = Σ_i  v[i] * VP[i][j]
        //   clip[0]+clip[3] >= 0  → Left  面内側条件
        //   係数 x = VP[0][0]+VP[0][3], y = VP[1][0]+VP[1][3] ... (以下同様)

        // Left:   clip.x + clip.w >= 0
        SetPlane(0, vp.m[0][0]+vp.m[0][3], vp.m[1][0]+vp.m[1][3],
                    vp.m[2][0]+vp.m[2][3], vp.m[3][0]+vp.m[3][3]);

        // Right:  -clip.x + clip.w >= 0
        SetPlane(1, -vp.m[0][0]+vp.m[0][3], -vp.m[1][0]+vp.m[1][3],
                    -vp.m[2][0]+vp.m[2][3], -vp.m[3][0]+vp.m[3][3]);

        // Bottom: clip.y + clip.w >= 0
        SetPlane(2, vp.m[0][1]+vp.m[0][3], vp.m[1][1]+vp.m[1][3],
                    vp.m[2][1]+vp.m[2][3], vp.m[3][1]+vp.m[3][3]);

        // Top:   -clip.y + clip.w >= 0
        SetPlane(3, -vp.m[0][1]+vp.m[0][3], -vp.m[1][1]+vp.m[1][3],
                    -vp.m[2][1]+vp.m[2][3], -vp.m[3][1]+vp.m[3][3]);

        // Near:   clip.z >= 0  (DirectX NDC: z ∈ [0,1])
        SetPlane(4, vp.m[0][2], vp.m[1][2], vp.m[2][2], vp.m[3][2]);

        // Far:   -clip.z + clip.w >= 0
        SetPlane(5, -vp.m[0][2]+vp.m[0][3], -vp.m[1][2]+vp.m[1][3],
                    -vp.m[2][2]+vp.m[2][3], -vp.m[3][2]+vp.m[3][3]);
    }

    // =========================================================
    // 可視性判定
    // =========================================================

    /**
     * @brief AABB が視野内に（一部でも）入っているか判定する
     * @return false なら視野外確定 → Draw() をスキップ可能
     *
     * p-vertex テストを使うため偽陰性（実際には見えるのに culled）は起きません。
     * 稀に偽陽性（視野外なのに visible と判定）が起きますが、描画の過剰はパフォーマンスより安全です。
     */
    bool IsVisible(const AABB& aabb) const
    {
        for (const auto& p : planes_) {
            // 法線方向に最も張り出した頂点（p-vertex）を選ぶ
            float px = p.normal.x >= 0.0f ? aabb.max.x : aabb.min.x;
            float py = p.normal.y >= 0.0f ? aabb.max.y : aabb.min.y;
            float pz = p.normal.z >= 0.0f ? aabb.max.z : aabb.min.z;
            // p-vertex が平面の外側なら AABB 全体が外側 → カリング
            if (p.normal.x*px + p.normal.y*py + p.normal.z*pz + p.d < 0.0f) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 球が視野内に（一部でも）入っているか判定する
     * @return false なら視野外確定
     *
     * ※ ExtractFromMatrix() 後に平面が正規化済みであることが前提です
     */
    bool IsVisible(const Sphere& sphere) const
    {
        for (const auto& p : planes_) {
            float dist = p.normal.x*sphere.center.x
                       + p.normal.y*sphere.center.y
                       + p.normal.z*sphere.center.z
                       + p.d;
            if (dist < -sphere.radius) { return false; }
        }
        return true;
    }

    /**
     * @brief Collider の形状に応じた可視性判定
     * @note Capsule と AABB は GetBroadAABB() で保守的に判定
     */
    bool IsVisible(const Collider& collider) const
    {
        if (collider.shape == ColliderShape::Sphere) {
            return IsVisible(collider.sphere);
        }
        return IsVisible(collider.GetBroadAABB());
    }

private:
    /// 内向き法線 + 符号付き距離で表現する平面
    struct Plane {
        Vector3 normal; ///< 正規化済み、視錐台内向き
        float   d;      ///< 原点からの符号付き距離（法線と同じスケール）
    };

    Plane planes_[6]; ///< [0]Left [1]Right [2]Bottom [3]Top [4]Near [5]Far

    /// 係数を設定し、法線を正規化して格納する
    void SetPlane(int i, float nx, float ny, float nz, float d)
    {
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len > 1e-8f) {
            float inv = 1.0f / len;
            planes_[i].normal = { nx*inv, ny*inv, nz*inv };
            planes_[i].d      = d * inv;
        } else {
            planes_[i].normal = { 0.0f, 0.0f, 0.0f };
            planes_[i].d      = 0.0f;
        }
    }
};
