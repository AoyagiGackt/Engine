/**
 * @file DiagnosticsDraw.h
 * @brief ImGui を使った 3D ワイヤーフレームデバッグ描画 — ヘッダーオンリー実装
 *
 * USE_IMGUI が定義されている Development ビルドのみ有効
 * Release ビルドでは全関数が空のスタブになるのでコードを残したまま出荷できます
 *
 * ■ 使い方（毎フレーム ImGuiManager::Begin() と End() の間）
 *
 *   // 1) フレーム先頭でカメラをセット（1 回でよい）
 *   DiagnosticsDraw::SetCamera(camera, WinApp::kClientWidth, WinApp::kClientHeight);
 *
 *   // 2) 描画したいものを好きなだけ呼ぶ
 *   DiagnosticsDraw::DrawAABB   (aabb,    DiagnosticsDraw::kColorGreen);
 *   DiagnosticsDraw::DrawSphere (sphere,  DiagnosticsDraw::kColorRed);
 *   DiagnosticsDraw::DrawCapsule(capsule, DiagnosticsDraw::kColorCyan);
 *   DiagnosticsDraw::DrawRay    (ray, 10.0f, DiagnosticsDraw::kColorYellow);
 *   DiagnosticsDraw::DrawCollider(obj->GetCollider(), DiagnosticsDraw::kColorWhite);
 *
 * ■ カラー指定
 *   IM_COL32(R, G, B, A)  または  DiagnosticsDraw::kColorGreen など定数を使用してください
 */
#pragma once
#include "CollisionConfig.h" // AABB, Sphere, Capsule, Collider, Ray
#include "MakeAffine.h"

// USE_IMGUI が有効な場合のみ実装を提供する
#ifdef USE_IMGUI
#include "imgui.h"
#include <cmath>

// CollisionConfig.h の形状プリミティブと数学型は engine 名前空間内で定義されているため、
// グローバル名前空間の DiagnosticsDraw から使えるように取り込む
using engine::AABB;
using engine::Capsule;
using engine::Collider;
using engine::ColliderShape;
using engine::Matrix4x4;
using engine::Ray;
using engine::Sphere;
using engine::Vector3;

namespace engine::game::DiagnosticsDraw {

// よく使うカラー定数 (IM_COL32: ABGR バイト順)
constexpr ImU32 kColorGreen = IM_COL32(0, 255, 0, 255);
constexpr ImU32 kColorRed = IM_COL32(255, 0, 0, 255);
constexpr ImU32 kColorBlue = IM_COL32(0, 100, 255, 255);
constexpr ImU32 kColorYellow = IM_COL32(255, 255, 0, 255);
constexpr ImU32 kColorCyan = IM_COL32(0, 255, 255, 255);
constexpr ImU32 kColorMagenta = IM_COL32(255, 0, 255, 255);
constexpr ImU32 kColorWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kColorOrange = IM_COL32(255, 150, 0, 255);

// 内部状態（SetCamera で初期化）
namespace _Internal {
    inline Matrix4x4 vp_ = { };
    inline float screenW_ = 1280.0f;
    inline float screenH_ = 720.0f;
    inline float thickness_ = 1.5f;

    /**
     * @brief ワールド座標 → ImGui スクリーン座標に変換する
     * @return false なら画面外またはカメラ後方 → 描画しない
     */
    inline bool WorldToScreen(const Vector3& world, ImVec2& out)
    {
        // 行ベクトル規約  clip = world * VP
        float cx = world.x * vp_.m[0][0] + world.y * vp_.m[1][0] + world.z * vp_.m[2][0] + vp_.m[3][0];
        float cy = world.x * vp_.m[0][1] + world.y * vp_.m[1][1] + world.z * vp_.m[2][1] + vp_.m[3][1];
        float cw = world.x * vp_.m[0][3] + world.y * vp_.m[1][3] + world.z * vp_.m[2][3] + vp_.m[3][3];

        if (cw < 1e-4f) {
            return false;
        } // カメラ後方

        float ndcX = cx / cw;
        float ndcY = cy / cw;

        // NDC [-1,1] → スクリーン [0, width/height]  (Y 反転)
        out.x = (ndcX + 1.0f) * 0.5f * screenW_;
        out.y = (1.0f - ndcY) * 0.5f * screenH_;
        return true;
    }
} // namespace _Internal

// 公開 API

/**
 * @brief 毎フレーム先頭で呼ぶ使用するカメラとスクリーンサイズをセットする
 * @param vp         Camera::GetViewProjectionMatrix() の戻り値
 * @param screenW    画面の幅 px（WinApp::kClientWidth）
 * @param screenH    画面の高さ px（WinApp::kClientHeight）
 * @param thickness  線の太さ（デフォルト 1.5f）
 */
inline void SetCamera(const Matrix4x4& vp, float screenW, float screenH, float thickness = 1.5f)
{
    _Internal::vp_ = vp;
    _Internal::screenW_ = screenW;
    _Internal::screenH_ = screenH;
    _Internal::thickness_ = thickness;
}

/**
 * @brief ワールド座標をスクリーン座標に変換する（ギズモのピッキング等、外部から使う用）
 * @return false ならカメラ後方 → 呼び出し側は描画/判定をスキップすること
 */
inline bool WorldToScreen(const Vector3& world, ImVec2& outScreen)
{
    return _Internal::WorldToScreen(world, outScreen);
}

/**
 * @brief 3D 空間の 2 点間にラインを描く
 * @param from  始点（ワールド座標）
 * @param to    終点（ワールド座標）
 * @param color IM_COL32(R,G,B,A) で指定
 */
inline void DrawLine(const Vector3& from, const Vector3& to, ImU32 color = kColorGreen)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 s, e;
    if (_Internal::WorldToScreen(from, s) && _Internal::WorldToScreen(to, e)) {
        dl->AddLine(s, e, color, _Internal::thickness_);
    }
}

/**
 * @brief AABB をワイヤーフレームで描く（12 エッジ）
 */
inline void DrawAABB(const AABB& aabb, ImU32 color = kColorGreen)
{
    const Vector3& mn = aabb.min;
    const Vector3& mx = aabb.max;

    // 8 頂点
    Vector3 v[8] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z },
        { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
        { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z },
        { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z }
    };
    // 12 エッジ  bottom/top face, 4 pillars
    static const int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // near face
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, // far face
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } // pillars
    };
    for (auto& e : edges) {
        DrawLine(v[e[0]], v[e[1]], color);
    }
}

/**
 * @brief 球を 3 方向のリング（XY / XZ / YZ 平面）で描く
 * @param segments リングの分割数（多いほど滑らか、デフォルト 20）
 */
inline void DrawSphere(const Sphere& sphere, ImU32 color = kColorGreen, int segments = 20)
{
    const float r = sphere.radius;
    const Vector3& c = sphere.center;
    const float step = 2.0f * 3.14159265f / static_cast<float>(segments);

    for (int i = 0; i < segments; ++i) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);

        // XY plane
        DrawLine({ c.x + r * c0, c.y + r * s0, c.z },
            { c.x + r * c1, c.y + r * s1, c.z }, color);
        // XZ plane
        DrawLine({ c.x + r * c0, c.y, c.z + r * s0 },
            { c.x + r * c1, c.y, c.z + r * s1 }, color);
        // YZ plane
        DrawLine({ c.x, c.y + r * c0, c.z + r * s0 },
            { c.x, c.y + r * c1, c.z + r * s1 }, color);
    }
}

/**
 * @brief カプセルを描く（胴体 4 ライン + 両端のリング）
 * @param segments 端リングの分割数
 */
inline void DrawCapsule(const Capsule& capsule, ImU32 color = kColorCyan, int segments = 16)
{
    Vector3 axis = {
        capsule.end.x - capsule.start.x,
        capsule.end.y - capsule.start.y,
        capsule.end.z - capsule.start.z
    };
    float axisLen = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);

    if (axisLen < 1e-6f) {
        // 退化（点）→ 球として描画
        DrawSphere({ capsule.start, capsule.radius }, color, segments);
        return;
    }

    Vector3 axisN = { axis.x / axisLen, axis.y / axisLen, axis.z / axisLen };

    // 軸に垂直な基底を 2 本作る
    Vector3 perp1;
    if (std::abs(axisN.x) < 0.9f) {
        perp1 = Normalize(Cross(axisN, { 1.0f, 0.0f, 0.0f }));
    } else {
        perp1 = Normalize(Cross(axisN, { 0.0f, 1.0f, 0.0f }));
    }
    Vector3 perp2 = Cross(axisN, perp1);

    const float r = capsule.radius;
    const float pi2 = 2.0f * 3.14159265f;

    // 胴体のライン（4 本）
    for (int i = 0; i < 4; ++i) {
        float angle = (pi2 / 4.0f) * i;
        float ca = std::cos(angle), sa = std::sin(angle);
        Vector3 offset = {
            (perp1.x * ca + perp2.x * sa) * r,
            (perp1.y * ca + perp2.y * sa) * r,
            (perp1.z * ca + perp2.z * sa) * r
        };
        DrawLine(
            { capsule.start.x + offset.x, capsule.start.y + offset.y, capsule.start.z + offset.z },
            { capsule.end.x + offset.x, capsule.end.y + offset.y, capsule.end.z + offset.z },
            color);
    }

    // 両端のリング（軸直交平面）
    auto DrawRing = [&](const Vector3& center) {
        for (int i = 0; i < segments; ++i) {
            float a0 = (pi2 / segments) * i;
            float a1 = (pi2 / segments) * (i + 1);
            float c0 = std::cos(a0), s0 = std::sin(a0);
            float c1 = std::cos(a1), s1 = std::sin(a1);
            DrawLine(
                { center.x + (perp1.x * c0 + perp2.x * s0) * r,
                    center.y + (perp1.y * c0 + perp2.y * s0) * r,
                    center.z + (perp1.z * c0 + perp2.z * s0) * r },
                { center.x + (perp1.x * c1 + perp2.x * s1) * r,
                    center.y + (perp1.y * c1 + perp2.y * s1) * r,
                    center.z + (perp1.z * c1 + perp2.z * s1) * r },
                color);
        }
    };

    DrawRing(capsule.start);
    DrawRing(capsule.end);
}

/**
 * @brief レイを矢印として描く
 * @param ray    描画するレイ（direction は正規化済みであること）
 * @param length 描画する長さ（ワールド単位）
 * @param color  色
 */
inline void DrawRay(const Ray& ray, float length = 10.0f, ImU32 color = kColorYellow)
{
    Vector3 to = {
        ray.origin.x + ray.direction.x * length,
        ray.origin.y + ray.direction.y * length,
        ray.origin.z + ray.direction.z * length
    };
    DrawLine(ray.origin, to, color);

    // 矢頭（短い横線 2 本）
    float headLen = length * 0.08f;
    Vector3 d = ray.direction;
    // 垂直な方向を 1 本求める
    Vector3 side;
    if (std::abs(d.x) < 0.9f) {
        side = Normalize(Cross(d, { 1.0f, 0.0f, 0.0f }));
    } else {
        side = Normalize(Cross(d, { 0.0f, 1.0f, 0.0f }));
    }

    // バックポイント（先端から手前に引く）
    Vector3 backPt = {
        to.x - d.x * headLen,
        to.y - d.y * headLen,
        to.z - d.z * headLen
    };
    DrawLine(to, { backPt.x + side.x * headLen * 0.5f, backPt.y + side.y * headLen * 0.5f, backPt.z + side.z * headLen * 0.5f }, color);
    DrawLine(to, { backPt.x - side.x * headLen * 0.5f, backPt.y - side.y * headLen * 0.5f, backPt.z - side.z * headLen * 0.5f }, color);
}

/**
 * @brief Collider を形状に応じて描画する（AABB→緑, Sphere→赤, Capsule→シアン）
 * @param color デフォルトでは形状ごとの色任意色に上書きしたければ指定
 */
inline void DrawCollider(const Collider& collider, ImU32 color = 0)
{
    switch (collider.shape) {
    case ColliderShape::AABB:
        DrawAABB(collider.aabb,
            color ? color : kColorGreen);
        break;
    case ColliderShape::Sphere:
        DrawSphere(collider.sphere,
            color ? color : kColorRed);
        break;
    case ColliderShape::Capsule:
        DrawCapsule(collider.capsule,
            color ? color : kColorCyan);
        break;
    }
}

/**
 * @brief 十字マーカーを描く（ヒット点の可視化などに）
 * @param pos  中心座標
 * @param size 十字の腕の長さ
 * @param color 色
 */
inline void DrawCross(const Vector3& pos, float size = 0.3f, ImU32 color = kColorWhite)
{
    DrawLine({ pos.x - size, pos.y, pos.z }, { pos.x + size, pos.y, pos.z }, color);
    DrawLine({ pos.x, pos.y - size, pos.z }, { pos.x, pos.y + size, pos.z }, color);
    DrawLine({ pos.x, pos.y, pos.z - size }, { pos.x, pos.y, pos.z + size }, color);
}

} // namespace engine::game::DiagnosticsDraw

#else // !USE_IMGUI

namespace engine::game::DiagnosticsDraw {
// カラー定数（型は unsigned int, 値は 0 のスタブ）
constexpr unsigned int kColorGreen = 0;
constexpr unsigned int kColorRed = 0;
constexpr unsigned int kColorBlue = 0;
constexpr unsigned int kColorYellow = 0;
constexpr unsigned int kColorCyan = 0;
constexpr unsigned int kColorMagenta = 0;
constexpr unsigned int kColorWhite = 0;
constexpr unsigned int kColorOrange = 0;

inline void SetCamera(const engine::Matrix4x4&, float, float, float = 1.5f) { }
inline void DrawLine(const engine::Vector3&, const engine::Vector3&, unsigned int = 0) { }
inline void DrawAABB(const engine::AABB&, unsigned int = 0) { }
inline void DrawSphere(const engine::Sphere&, unsigned int = 0, int = 20) { }
inline void DrawCapsule(const engine::Capsule&, unsigned int = 0, int = 16) { }
inline void DrawRay(const engine::Ray&, float = 10.0f, unsigned int = 0) { }
inline void DrawCollider(const engine::Collider&, unsigned int = 0) { }
inline void DrawCross(const engine::Vector3&, float = 0.3f, unsigned int = 0) { }
} // namespace engine::game::DiagnosticsDraw

#endif // USE_IMGUI

// 既存コードの DiagnosticsDraw::DrawXxx 呼び出しとのソース互換性を維持する。
namespace DiagnosticsDraw = engine::game::DiagnosticsDraw;
