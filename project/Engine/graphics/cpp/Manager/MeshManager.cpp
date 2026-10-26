/**
 * @file MeshManager.cpp
 * @brief 組み込みメッシュ（球・立方体・平面）の生成と管理（MeshManager）の実装
 */
#include "MeshManager.h"
#include <cmath>

namespace engine::graphics {
namespace {

// 球面の分割と角度
constexpr int kSphereSubdivisions = 16;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

/** @brief 球のメッシュ（頂点とインデックス）を作る */
MeshData GenerateSphereMesh(int subdivision = kSphereSubdivisions, float radius = 1.0f) {
    MeshData mesh;
    mesh.transform = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };


    for (int lat = 0; lat < subdivision; ++lat) {
        float lat0 = kPi * (float(lat) / subdivision - 0.5f);
        float lat1 = kPi * (float(lat + 1) / subdivision - 0.5f);

        for (int lon = 0; lon < subdivision; ++lon) {
            float lon0 = kTwoPi * float(lon) / subdivision;
            float lon1 = kTwoPi * float(lon + 1) / subdivision;

            Vector4 p00 = { radius * cosf(lat0) * cosf(lon0), radius * sinf(lat0), radius * cosf(lat0) * sinf(lon0), 1.0f };
            Vector4 p01 = { radius * cosf(lat0) * cosf(lon1), radius * sinf(lat0), radius * cosf(lat0) * sinf(lon1), 1.0f };
            Vector4 p10 = { radius * cosf(lat1) * cosf(lon0), radius * sinf(lat1), radius * cosf(lat1) * sinf(lon0), 1.0f };
            Vector4 p11 = { radius * cosf(lat1) * cosf(lon1), radius * sinf(lat1), radius * cosf(lat1) * sinf(lon1), 1.0f };

            Vector2 uv00 = { float(lon) / subdivision, 1.0f - float(lat) / subdivision };
            Vector2 uv01 = { float(lon + 1) / subdivision, 1.0f - float(lat) / subdivision };
            Vector2 uv10 = { float(lon) / subdivision, 1.0f - float(lat + 1) / subdivision };
            Vector2 uv11 = { float(lon + 1) / subdivision, 1.0f - float(lat + 1) / subdivision };

            mesh.vertices.push_back({ p00, uv00, { p00.x, p00.y, p00.z } });
            mesh.vertices.push_back({ p10, uv10, { p10.x, p10.y, p10.z } });
            mesh.vertices.push_back({ p11, uv11, { p11.x, p11.y, p11.z } });

            mesh.vertices.push_back({ p00, uv00, { p00.x, p00.y, p00.z } });
            mesh.vertices.push_back({ p11, uv11, { p11.x, p11.y, p11.z } });
            mesh.vertices.push_back({ p01, uv01, { p01.x, p01.y, p01.z } });
        }
    }
    return mesh;
}

/** @brief 立方体のメッシュ（頂点とインデックス）を作る */
MeshData GenerateCubeMesh(float size = 1.0f) {
    MeshData mesh;
    mesh.transform = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    const float s = size * 0.5f;

    /** @brief 立方体の1面を構成する頂点とUVの定義 */
    struct Face {
        Vector4 p0, p1, p2, p3;
        Vector2 uv0, uv1, uv2, uv3;
        Vector3 normal;
    };

    Face faces[6] = {
        // +X
        { { s, -s, -s, 1 }, { s, -s, s, 1 }, { s, s, s, 1 }, { s, s, -s, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { 1, 0, 0 } },
        // -X
        { { -s, -s, s, 1 }, { -s, -s, -s, 1 }, { -s, s, -s, 1 }, { -s, s, s, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 0, 0 } },
        // +Y
        { { -s, s, s, 1 }, { s, s, s, 1 }, { s, s, -s, 1 }, { -s, s, -s, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 1, 0 } },
        // -Y
        { { -s, -s, -s, 1 }, { s, -s, -s, 1 }, { s, -s, s, 1 }, { -s, -s, s, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, -1, 0 } },
        // +Z
        { { -s, -s, s, 1 }, { s, -s, s, 1 }, { s, s, s, 1 }, { -s, s, s, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0, 1 } },
        // -Z
        { { s, -s, -s, 1 }, { -s, -s, -s, 1 }, { -s, s, -s, 1 }, { s, s, -s, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0, -1 } },
    };

    for (const auto& f : faces) {
        mesh.vertices.push_back({ f.p0, f.uv0, f.normal });
        mesh.vertices.push_back({ f.p1, f.uv1, f.normal });
        mesh.vertices.push_back({ f.p2, f.uv2, f.normal });

        mesh.vertices.push_back({ f.p0, f.uv0, f.normal });
        mesh.vertices.push_back({ f.p2, f.uv2, f.normal });
        mesh.vertices.push_back({ f.p3, f.uv3, f.normal });
    }

    return mesh;
}

/** @brief 平面のメッシュ（頂点とインデックス）を作る */
MeshData GeneratePlaneMesh(float width = 1.0f, float height = 1.0f) {
    MeshData mesh;
    mesh.transform = { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    float w = width * 0.5f;
    float h = height * 0.5f;
    Vector4 p0 = { -w, 0, -h, 1.0f };
    Vector4 p1 = { w, 0, -h, 1.0f };
    Vector4 p2 = { w, 0, h, 1.0f };
    Vector4 p3 = { -w, 0, h, 1.0f };
    Vector2 uv0 = { 0, 0 };
    Vector2 uv1 = { 1, 0 };
    Vector2 uv2 = { 1, 1 };
    Vector2 uv3 = { 0, 1 };
    Vector3 normal = { 0, 1, 0 };

    mesh.vertices.push_back({ p0, uv0, normal });
    mesh.vertices.push_back({ p1, uv1, normal });
    mesh.vertices.push_back({ p2, uv2, normal });

    mesh.vertices.push_back({ p0, uv0, normal });
    mesh.vertices.push_back({ p2, uv2, normal });
    mesh.vertices.push_back({ p3, uv3, normal });

    return mesh;
}
}

/** @brief 組み込みのメッシュ（球・立方体・平面）を用意して初期化する */
MeshManager::MeshManager()
    : currentMeshType_(MeshType::kSphere) {
    InitMeshes();
}

/** @brief 描画に使用するメッシュの種類を変更する */
void MeshManager::SetCurrentMeshType(MeshType type) {
    currentMeshType_ = type;
}
/** @brief 現在選択されているメッシュの種類を取得する */
MeshType MeshManager::GetCurrentMeshType() const {
    return currentMeshType_;
}
/** @brief 指定したメッシュの変換値をコピーして返す */
Transform MeshManager::GetMeshTransform(MeshType type) const {
    return meshes_[static_cast<size_t>(type)].transform;
}
/** @brief GPU資源を保持したまま変換値を更新する */
void MeshManager::SetMeshTransform(MeshType type, const Transform& transform) {
    meshes_[static_cast<size_t>(type)].transform = transform;
}

/** @brief 初期化時に各基本図形（球体、立方体、平面）の頂点データを計算してリストに登録する */
void MeshManager::InitMeshes() {
    meshes_.clear();
    meshes_.push_back(GenerateSphereMesh());
    meshes_.push_back(GenerateCubeMesh());
    meshes_.push_back(GeneratePlaneMesh());
}

/** @brief MeshManagerの唯一のインスタンスを取得する */
MeshManager* MeshManager::GetInstance() {
    static MeshManager sInstance;
    return &sInstance;
}

/** @brief マネージャーの終了処理を行う（リストのクリアなど） */
void MeshManager::Finalize() {
    meshes_.clear();
}
} // namespace engine::graphics
