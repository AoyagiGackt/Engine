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
 *   diagnosticsDraw::SetCamera(camera, WinApp::kClientWidth, WinApp::kClientHeight);
 *
 *   // 2) 描画したいものを好きなだけ呼ぶ
 *   diagnosticsDraw::DrawAABB   (aabb,    diagnosticsDraw::kColorGreen);
 *   diagnosticsDraw::DrawSphere (sphere,  diagnosticsDraw::kColorRed);
 *   diagnosticsDraw::DrawCapsule(capsule, diagnosticsDraw::kColorCyan);
 *   diagnosticsDraw::DrawRay    (ray, diagnosticsDraw::kDefaultRayLength, diagnosticsDraw::kColorYellow);
 *   diagnosticsDraw::DrawCollider(obj->GetCollider(), diagnosticsDraw::kColorWhite);
 *
 * ■ カラー指定
 *   IM_COL32(R, G, B, A)  または  diagnosticsDraw::kColorGreen など定数を使用してください
 */
#pragma once
#include <iterator>
#include "CollisionConfig.h" // AABB, Sphere, Capsule, Collider, Ray
#include "GameConstants.h"
#include "MakeAffine.h"

namespace engine::game::diagnosticsDraw {
/** @brief 線の太さの既定値（px） */
inline constexpr float kDefaultThickness = 1.5f;
/** @brief 球のリングの分割数の既定値 */
inline constexpr int kDefaultSphereSegments = 20;
/** @brief カプセルの端のリングの分割数の既定値 */
inline constexpr int kDefaultCapsuleSegments = 16;
/** @brief レイを描く長さの既定値（ワールド単位） */
inline constexpr float kDefaultRayLength = 10.0f;
/** @brief 十字マーカーの腕の長さの既定値（ワールド単位） */
inline constexpr float kDefaultCrossSize = 0.3f;
} // namespace engine::game::DiagnosticsDraw

// USE_IMGUI が有効な場合のみ実装を提供する
#ifdef USE_IMGUI
#include "imgui.h"
#include <cmath>

namespace engine::game::diagnosticsDraw {
/** @brief 補助軸を切り替えて外積の縮退を避けるしきい値 */
inline constexpr float kParallelAxisThreshold = 0.9f;
/** @brief これより小さい同次座標 w はカメラの後ろとして描かない */
inline constexpr float kMinClipW = 1e-4f;
/** @brief これより短いカプセルの軸は点とみなして球で描く */
inline constexpr float kMinCapsuleAxisLength = 1e-6f;
/** @brief カプセルの胴体に引く線の本数 */
inline constexpr int kCapsuleBodyLineCount = 4;
/** @brief レイの矢頭の長さ（レイの長さに対する割合） */
inline constexpr float kArrowHeadLengthRatio = 0.08f;
/** @brief 矢頭の横線の開き（矢頭の長さに対する割合） */
inline constexpr float kArrowHeadSpreadRatio = 0.5f;

// よく使うカラー定数 (IM_COL32: ABGR バイト順)
constexpr ImU32 kColorGreen = IM_COL32(0, 255, 0, 255);
constexpr ImU32 kColorRed = IM_COL32(255, 0, 0, 255);
constexpr ImU32 kColorBlue = IM_COL32(0, 100, 255, 255);
constexpr ImU32 kColorYellow = IM_COL32(255, 255, 0, 255);
constexpr ImU32 kColorCyan = IM_COL32(0, 255, 255, 255);
constexpr ImU32 kColorMagenta = IM_COL32(255, 0, 255, 255);
constexpr ImU32 kColorWhite = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kColorOrange = IM_COL32(255, 150, 0, 255);

namespace detail {
    /** @brief デバッグ描画の状態（SetCamera と SetImageViewport で設定する） */
    struct DrawState {
        Matrix4x4 viewProjection = { };
        float screenWidth = gameConstants::kScreenWidth;
        float screenHeight = gameConstants::kScreenHeight;
        float thickness = kDefaultThickness;
        ImVec2 imageOrigin = { 0.0f, 0.0f };
        ImVec2 imageSize = { 0.0f, 0.0f };
        ImDrawList* imageDrawList = nullptr;
    };

    /** @brief デバッグ描画の状態を返す（すべての呼び出しで共有する） */
    inline DrawState& State() {
        static DrawState sState;
        return sState;
    }

    /**
     * @brief ワールド座標 → ImGui スクリーン座標に変換する
     * @return false なら画面外またはカメラ後方 → 描画しない
     */
    inline bool WorldToScreen(const Vector3& world, ImVec2& out) {
        const DrawState& state = State();
        const Matrix4x4& vp = state.viewProjection;
        // 行ベクトル規約  clip = world * VP
        float cx = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
        float cy = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
        float cw = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];

        // カメラの後ろは描かない
        if (cw < kMinClipW) {
            return false;
        }

        float ndcX = cx / cw;
        float ndcY = cy / cw;

        // NDC [-1,1] → スクリーン [0, width/height]  (Y 反転)
        out.x = (ndcX + 1.0f) * 0.5f * state.screenWidth;
        out.y = (1.0f - ndcY) * 0.5f * state.screenHeight;
        // 小窓に描く時は小窓の範囲へ縮める
        if (state.imageDrawList) {
            out.x = state.imageOrigin.x + out.x * state.imageSize.x / state.screenWidth;
            out.y = state.imageOrigin.y + out.y * state.imageSize.y / state.screenHeight;
        }
        return true;
    }

    /** @brief 方向に垂直な単位ベクトルを1本返す（方向と平行に近い補助軸は避ける） */
    inline Vector3 Perpendicular(const Vector3& direction) {
        const Vector3 helper = std::abs(direction.x) < kParallelAxisThreshold
            ? Vector3 { 1.0f, 0.0f, 0.0f }
            : Vector3 { 0.0f, 1.0f, 0.0f };
        return Normalize(Cross(direction, helper));
    }
} // namespace detail

/** @brief 小窓に合わせて診断線の描画先と投影範囲を切り替える */
inline void SetImageViewport(ImVec2 origin = {}, ImVec2 size = {}, ImDrawList* drawList = nullptr) {
    detail::DrawState& state = detail::State();
    state.imageOrigin = origin;
    state.imageSize = size;
    state.imageDrawList = drawList;
}

/** @brief デバッグ表示を描く ImGui の描画リストを返す */
inline ImDrawList* GetDrawList() {
    ImDrawList* imageDrawList = detail::State().imageDrawList;
    return imageDrawList ? imageDrawList : ImGui::GetForegroundDrawList();
}

// 公開 API

/**
 * @brief 毎フレーム先頭で呼ぶ。使用するカメラとスクリーンサイズをセットする
 * @param vp         Camera::GetViewProjectionMatrix() の戻り値
 * @param screenW    画面の幅 px（WinApp::kClientWidth）
 * @param screenH    画面の高さ px（WinApp::kClientHeight）
 * @param thickness  線の太さ（既定は kDefaultThickness）
 */
inline void SetCamera(const Matrix4x4& vp, float screenW, float screenH, float thickness = kDefaultThickness) {
    detail::DrawState& state = detail::State();
    state.viewProjection = vp;
    state.screenWidth = screenW;
    state.screenHeight = screenH;
    state.thickness = thickness;
}

/**
 * @brief ワールド座標をスクリーン座標に変換する（ギズモのピッキング等、外部から使う用）
 * @return false ならカメラ後方 → 呼び出し側は描画/判定をスキップすること
 */
inline bool WorldToScreen(const Vector3& world, ImVec2& outScreen) {
    return detail::WorldToScreen(world, outScreen);
}

/**
 * @brief 3D 空間の 2 点間にラインを描く
 * @param from  始点（ワールド座標）
 * @param to    終点（ワールド座標）
 * @param color IM_COL32(R,G,B,A) で指定
 */
inline void DrawLine(const Vector3& from, const Vector3& to, ImU32 color = kColorGreen) {
    const detail::DrawState& state = detail::State();
    ImDrawList* dl = GetDrawList();
    ImVec2 s, e;
    if (detail::WorldToScreen(from, s) && detail::WorldToScreen(to, e)) {
        // 小窓に描く時は小窓の外へはみ出さないよう切り抜く
        if (state.imageDrawList) {
            const ImVec2 origin = state.imageOrigin;
            const ImVec2 size = state.imageSize;
            dl->PushClipRect(origin, { origin.x + size.x, origin.y + size.y }, true);
        }
        dl->AddLine(s, e, color, state.thickness);
        if (state.imageDrawList) {
            dl->PopClipRect();
        }
    }
}

/**
 * @brief AABB をワイヤーフレームで描く（12 エッジ）
 */
inline void DrawAABB(const AABB& aabb, ImU32 color = kColorGreen) {
    const Vector3& mn = aabb.min;
    const Vector3& mx = aabb.max;

    // 8 頂点
    const Vector3 v[] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z },
        { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
        { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z },
        { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z }
    };
    // 12 エッジ  手前の面・奥の面・4 本の柱（頂点の番号の組）
    static constexpr int kEdges[][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };
    for (const auto& e : kEdges) {
        DrawLine(v[e[0]], v[e[1]], color);
    }
}

/**
 * @brief 球を 3 方向のリング（XY / XZ / YZ 平面）で描く
 * @param segments リングの分割数（多いほど滑らか、既定は kDefaultSphereSegments）
 */
inline void DrawSphere(const Sphere& sphere, ImU32 color = kColorGreen, int segments = kDefaultSphereSegments) {
    const float r = sphere.radius;
    const Vector3& c = sphere.center;
    const float step = gameConstants::kTwoPi / static_cast<float>(segments);

    // 分割ごとに3つの平面のリングを1区間ずつ描く
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
inline void DrawCapsule(const Capsule& capsule, ImU32 color = kColorCyan, int segments = kDefaultCapsuleSegments) {
    Vector3 axis = {
        capsule.end.x - capsule.start.x,
        capsule.end.y - capsule.start.y,
        capsule.end.z - capsule.start.z
    };
    float axisLen = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);

    // 退化（点）→ 球として描画
    if (axisLen < kMinCapsuleAxisLength) {
        DrawSphere({ capsule.start, capsule.radius }, color, segments);
        return;
    }

    Vector3 axisN = { axis.x / axisLen, axis.y / axisLen, axis.z / axisLen };

    // 軸に垂直な基底を 2 本作る
    const Vector3 perp1 = detail::Perpendicular(axisN);
    const Vector3 perp2 = Cross(axisN, perp1);

    const float r = capsule.radius;
    const float pi2 = gameConstants::kTwoPi;

    // 角度 angle の方向へ半径ぶんずらした量
    const auto offsetAt = [&](float angle) {
        const float ca = std::cos(angle);
        const float sa = std::sin(angle);
        return Vector3 {
            (perp1.x * ca + perp2.x * sa) * r,
            (perp1.y * ca + perp2.y * sa) * r,
            (perp1.z * ca + perp2.z * sa) * r
        };
    };

    // 胴体のライン
    for (int i = 0; i < kCapsuleBodyLineCount; ++i) {
        const Vector3 offset = offsetAt((pi2 / kCapsuleBodyLineCount) * i);
        DrawLine(
            { capsule.start.x + offset.x, capsule.start.y + offset.y, capsule.start.z + offset.z },
            { capsule.end.x + offset.x, capsule.end.y + offset.y, capsule.end.z + offset.z },
            color);
    }

    // 両端のリング（軸直交平面）
    const auto drawRing = [&](const Vector3& center) {
        for (int i = 0; i < segments; ++i) {
            const Vector3 o0 = offsetAt((pi2 / segments) * i);
            const Vector3 o1 = offsetAt((pi2 / segments) * (i + 1));
            DrawLine(
                { center.x + o0.x, center.y + o0.y, center.z + o0.z },
                { center.x + o1.x, center.y + o1.y, center.z + o1.z },
                color);
        }
    };

    drawRing(capsule.start);
    drawRing(capsule.end);
}

/**
 * @brief レイを矢印として描く
 * @param ray    描画するレイ（direction は正規化済みであること）
 * @param length 描画する長さ（ワールド単位）
 * @param color  色
 */
inline void DrawRay(const Ray& ray, float length = kDefaultRayLength, ImU32 color = kColorYellow) {
    Vector3 to = {
        ray.origin.x + ray.direction.x * length,
        ray.origin.y + ray.direction.y * length,
        ray.origin.z + ray.direction.z * length
    };
    DrawLine(ray.origin, to, color);

    // 矢頭（短い横線 2 本）
    const float headLen = length * kArrowHeadLengthRatio;
    const float spread = headLen * kArrowHeadSpreadRatio;
    const Vector3& d = ray.direction;
    const Vector3 side = detail::Perpendicular(d);

    // バックポイント（先端から手前に引く）
    Vector3 backPt = {
        to.x - d.x * headLen,
        to.y - d.y * headLen,
        to.z - d.z * headLen
    };
    DrawLine(to, { backPt.x + side.x * spread, backPt.y + side.y * spread, backPt.z + side.z * spread }, color);
    DrawLine(to, { backPt.x - side.x * spread, backPt.y - side.y * spread, backPt.z - side.z * spread }, color);
}

/**
 * @brief Collider を形状に応じて描画する（AABB→緑, Sphere→赤, Capsule→シアン）
 * @param color 既定では形状ごとの色。任意の色に上書きしたければ指定する
 */
inline void DrawCollider(const Collider& collider, ImU32 color = 0) {
    using Handler = void (*)(const Collider&, ImU32);
    static constexpr Handler kHandlers[] = {
        [](const Collider& value, ImU32 tint) {
            DrawAABB(value.aabb, tint ? tint : kColorGreen);
        },
        [](const Collider& value, ImU32 tint) {
            DrawSphere(value.sphere, tint ? tint : kColorRed);
        },
        [](const Collider& value, ImU32 tint) {
            DrawCapsule(value.capsule, tint ? tint : kColorCyan);
        },
    };
    static_assert(std::size(kHandlers) == kColliderShapeCount);
    // 形状の番号で描く関数を表から選ぶ
    const auto index = static_cast<size_t>(collider.shape);
    if (index < std::size(kHandlers)) {
        kHandlers[index](collider, color);
    }
}

/**
 * @brief 十字マーカーを描く（ヒット点の可視化などに）
 * @param pos  中心座標
 * @param size 十字の腕の長さ
 * @param color 色
 */
inline void DrawCross(const Vector3& pos, float size = kDefaultCrossSize, ImU32 color = kColorWhite) {
    DrawLine({ pos.x - size, pos.y, pos.z }, { pos.x + size, pos.y, pos.z }, color);
    DrawLine({ pos.x, pos.y - size, pos.z }, { pos.x, pos.y + size, pos.z }, color);
    DrawLine({ pos.x, pos.y, pos.z - size }, { pos.x, pos.y, pos.z + size }, color);
}

} // namespace engine::game::DiagnosticsDraw

#else // !USE_IMGUI

namespace engine::game::diagnosticsDraw {
// カラー定数（型は unsigned int, 値は 0 のスタブ）
constexpr unsigned int kColorGreen = 0;
constexpr unsigned int kColorRed = 0;
constexpr unsigned int kColorBlue = 0;
constexpr unsigned int kColorYellow = 0;
constexpr unsigned int kColorCyan = 0;
constexpr unsigned int kColorMagenta = 0;
constexpr unsigned int kColorWhite = 0;
constexpr unsigned int kColorOrange = 0;

/** @brief 毎フレーム先頭で呼ぶ。使用するカメラとスクリーンサイズをセットする */
inline void SetCamera(const engine::Matrix4x4&, float, float, float = kDefaultThickness) { }
/** @brief 3D 空間の 2 点間にラインを描く */
inline void DrawLine(const engine::Vector3&, const engine::Vector3&, unsigned int = 0) { }
/** @brief AABB をワイヤーフレームで描く（12 エッジ） */
inline void DrawAABB(const engine::AABB&, unsigned int = 0) { }
/** @brief 球を 3 方向のリング（XY / XZ / YZ 平面）で描く */
inline void DrawSphere(const engine::Sphere&, unsigned int = 0, int = kDefaultSphereSegments) { }
/** @brief カプセルを描く（胴体 4 ライン + 両端のリング） */
inline void DrawCapsule(const engine::Capsule&, unsigned int = 0, int = kDefaultCapsuleSegments) { }
/** @brief レイを矢印として描く */
inline void DrawRay(const engine::Ray&, float = kDefaultRayLength, unsigned int = 0) { }
/** @brief Collider を形状に応じて描画する（AABB→緑, Sphere→赤, Capsule→シアン） */
inline void DrawCollider(const engine::Collider&, unsigned int = 0) { }
/** @brief 十字マーカーを描く（ヒット点の可視化などに） */
inline void DrawCross(const engine::Vector3&, float = kDefaultCrossSize, unsigned int = 0) { }
} // namespace engine::game::DiagnosticsDraw

#endif // USE_IMGUI

// 既存コードの diagnosticsDraw::DrawXxx 呼び出しとのソース互換性を維持する。
namespace diagnosticsDraw = engine::game::diagnosticsDraw;
