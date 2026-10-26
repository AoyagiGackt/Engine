/**
 * @file ModelThumbnailBaker.cpp
 * @brief 正射影と深度バッファでモデルをCPU描画する
 */
#include "ModelThumbnailBaker.h"
#include "TextureManager.h"
#include <DirectXTex.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <objbase.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

namespace engine::graphics {
namespace {
constexpr uint32_t kSize = 96;
constexpr size_t kChannels = 4;
constexpr unsigned int kCorners = 3;
constexpr float kEpsilon = 1e-6f;
constexpr float kHalf = 0.5f;
constexpr float kMargin = 0.9f;
constexpr float kAmbient = 0.35f;
constexpr float kDiffuse = 0.65f;
constexpr float kGray = 0.8f;
constexpr float kByteMax = 255.0f;
constexpr float kDiagonal = 0.70710678f;
constexpr float kUp = 0.40824829f;
constexpr float kUpY = 0.81649658f;
constexpr float kDepth = 0.57735027f;
/** @brief 視点へ移した頂点 */
struct Vertex {
    aiVector3D position;
    aiVector3D normal;
    aiVector3D uv;
};
/** @brief 塗る三角形1つ */
struct Triangle {
    std::array<Vertex, kCorners> vertices;
    unsigned int material;
};
/** @brief マテリアル1つぶんの色（テクスチャがあればそこから拾う） */
struct Surface {
    aiColor4D color { kGray, kGray, kGray, 1.0f };
    DirectX::ScratchImage image;
};
/** @brief UTF-8文字列をパスに変換する */
std::filesystem::path ToPath(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
/** @brief 固定の斜め上視点に変換する */
aiVector3D View(const aiVector3D& p) {
    return { (p.x - p.z) * kDiagonal, -p.x * kUp + p.y * kUpY - p.z * kUp,
        (p.x + p.y + p.z) * kDepth };
}
/** @brief ノード変換を反映して全メッシュを集める */
void Collect(const aiScene& scene, const aiNode& node, const aiMatrix4x4& parent,
    std::vector<Triangle>& triangles) {
    const aiMatrix4x4 transform = parent * node.mTransformation;
    aiMatrix3x3 normals(transform);
    normals.Inverse().Transpose();
    for (unsigned int i = 0; i < node.mNumMeshes; ++i) {
        const aiMesh& mesh = *scene.mMeshes[node.mMeshes[i]];
        for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
            const aiFace& face = mesh.mFaces[f];
            if (face.mNumIndices != kCorners) {
                continue;
            }
            Triangle triangle {};
            triangle.material = mesh.mMaterialIndex;
            for (unsigned int v = 0; v < kCorners; ++v) {
                const unsigned int index = face.mIndices[v];
                triangle.vertices[v] = { View(transform * mesh.mVertices[index]),
                    mesh.HasNormals() ? normals * mesh.mNormals[index] : aiVector3D {},
                    mesh.HasTextureCoords(0) ? mesh.mTextureCoords[0][index] : aiVector3D {} };
            }
            triangles.push_back(triangle);
        }
    }
    for (unsigned int i = 0; i < node.mNumChildren; ++i) {
        Collect(scene, *node.mChildren[i], transform, triangles);
    }
}
/** @brief 読み込んだ画像をRGBA8に揃える */
void ConvertImage(DirectX::ScratchImage& source, Surface& surface) {
    if (source.GetMetadata().format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        surface.image = std::move(source);
    } else {
        const HRESULT result = DirectX::Convert(source.GetImages(), source.GetImageCount(), source.GetMetadata(),
            DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
            DirectX::TEX_THRESHOLD_DEFAULT, surface.image);
        if (FAILED(result)) {
            surface.image.Release();
        }
    }
}
/** @brief WICで外部テクスチャを読む */
void LoadImage(const std::filesystem::path& path, Surface& surface) {
    DirectX::ScratchImage source;
    if (SUCCEEDED(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, source))) {
        ConvertImage(source, surface);
    }
}
/** @brief 重心UVの代表色を返す */
aiColor4D Sample(const Surface& surface, const Triangle& triangle) {
    const auto* image = surface.image.GetImage(0, 0, 0);
    if (!image) {
        return surface.color;
    }
    aiVector3D uv;
    for (const auto& vertex : triangle.vertices) {
        uv += vertex.uv;
    }
    uv /= static_cast<float>(kCorners);
    const float u = uv.x - std::floor(uv.x);
    const float v = uv.y - std::floor(uv.y);
    const size_t x = (std::min)(image->width - 1, static_cast<size_t>(u * static_cast<float>(image->width)));
    const size_t y = (std::min)(image->height - 1, static_cast<size_t>(v * static_cast<float>(image->height)));
    const auto* pixel = image->pixels + y * image->rowPitch + x * kChannels;
    return { pixel[0] / kByteMax, pixel[1] / kByteMax, pixel[2] / kByteMax, pixel[3] / kByteMax };
}
/** @brief 符号付き面積を返す */
float Edge(const aiVector3D& a, const aiVector3D& b, float x, float y) {
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
/** @brief 透明背景へモデルを描く */
bool Bake(const std::string& model, const std::string& texture, std::vector<uint8_t>& pixels) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(model, aiProcess_Triangulate | aiProcess_FlipUVs
        | aiProcess_GenSmoothNormals | aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder);
    if (!scene || !scene->mRootNode) {
        return false;
    }
    std::vector<Triangle> triangles;
    Collect(*scene, *scene->mRootNode, aiMatrix4x4 {}, triangles);
    if (triangles.empty()) {
        return false;
    }
    std::vector<Surface> surfaces(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        scene->mMaterials[i]->Get(AI_MATKEY_COLOR_DIFFUSE, surfaces[i].color);
        if (!texture.empty()) {
            LoadImage(ToPath(texture), surfaces[i]);
        } else {
            aiString name;
            if (scene->mMaterials[i]->GetTexture(aiTextureType_DIFFUSE, 0, &name) == AI_SUCCESS) {
                const aiTexture* embedded = scene->GetEmbeddedTexture(name.C_Str());
                if (embedded && embedded->mHeight == 0) {
                    DirectX::ScratchImage source;
                    if (SUCCEEDED(DirectX::LoadFromWICMemory(reinterpret_cast<const uint8_t*>(embedded->pcData),
                        embedded->mWidth, DirectX::WIC_FLAGS_NONE, nullptr, source))) {
                        ConvertImage(source, surfaces[i]);
                    }
                } else if (!embedded) {
                    LoadImage(ToPath(model).parent_path() / ToPath(name.C_Str()), surfaces[i]);
                }
            }
        }
    }
    aiVector3D low((std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), 0.0f);
    aiVector3D high((std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(), 0.0f);
    for (const auto& triangle : triangles) {
        for (const auto& vertex : triangle.vertices) {
            low.x = (std::min)(low.x, vertex.position.x);
            low.y = (std::min)(low.y, vertex.position.y);
            high.x = (std::max)(high.x, vertex.position.x);
            high.y = (std::max)(high.y, vertex.position.y);
        }
    }
    const float size = static_cast<float>(kSize);
    const float scale = size * kMargin / (std::max)(kEpsilon, (std::max)(high.x - low.x, high.y - low.y));
    const aiVector3D center = (low + high) * kHalf;
    pixels.assign(static_cast<size_t>(kSize) * kSize * kChannels, 0);
    std::vector<float> depths(static_cast<size_t>(kSize) * kSize, (std::numeric_limits<float>::lowest)());
    const aiVector3D light(-kUp, kUpY, -kUp);
    for (auto triangle : triangles) {
        const Surface fallback;
        const Surface& surface = triangle.material < surfaces.size() ? surfaces[triangle.material] : fallback;
        const aiColor4D color = Sample(surface, triangle);
        if (color.a <= 0.0f) {
            continue;
        }
        aiVector3D normal;
        for (auto& vertex : triangle.vertices) {
            normal += vertex.normal;
            vertex.position.x = (vertex.position.x - center.x) * scale + size * kHalf;
            vertex.position.y = -(vertex.position.y - center.y) * scale + size * kHalf;
        }
        normal.NormalizeSafe();
        const float shade = kAmbient + kDiffuse * (std::max)(0.0f, normal * light);
        const auto& a = triangle.vertices[0].position;
        const auto& b = triangle.vertices[1].position;
        const auto& c = triangle.vertices[2].position;
        const float area = Edge(a, b, c.x, c.y);
        if (std::abs(area) < kEpsilon) {
            continue;
        }
        const int minX = static_cast<int>(std::floor((std::max)(0.0f, (std::min)({ a.x, b.x, c.x }))));
        const int minY = static_cast<int>(std::floor((std::max)(0.0f, (std::min)({ a.y, b.y, c.y }))));
        const int maxX = static_cast<int>((std::min)(size - 1.0f, std::ceil((std::max)({ a.x, b.x, c.x }))));
        const int maxY = static_cast<int>((std::min)(size - 1.0f, std::ceil((std::max)({ a.y, b.y, c.y }))));
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const float px = static_cast<float>(x) + kHalf;
                const float py = static_cast<float>(y) + kHalf;
                const float w0 = Edge(b, c, px, py) / area;
                const float w1 = Edge(c, a, px, py) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }
                const float depth = w0 * a.z + w1 * b.z + w2 * c.z;
                const size_t index = static_cast<size_t>(y) * kSize + static_cast<size_t>(x);
                if (depth <= depths[index]) {
                    continue;
                }
                depths[index] = depth;
                const std::array<float, kChannels> rgba = { color.r * shade, color.g * shade, color.b * shade, color.a };
                for (size_t channel = 0; channel < kChannels; ++channel) {
                    pixels[index * kChannels + channel] = static_cast<uint8_t>(std::clamp(rgba[channel], 0.0f, 1.0f) * kByteMax);
                }
            }
        }
    }
    return true;
}
} // namespace

/** @brief 未登録の組み合わせを予約し、完成済みならIDを返す */
uint64_t ModelThumbnailBaker::RequestThumbnail(const std::string& model, const std::string& texture) {
    const std::string key = "__model_thumbnail/" + std::to_string(model.size()) + ":" + model + texture;
    const auto [it, inserted] = cache_.try_emplace(key, 0);
    if (inserted) {
        pending_.push_back({ model, texture, key });
    }
    return it->second;
}

/** @brief 失敗もキャッシュに残し、読み込みと描画は別スレッドで行ってGPUへの転送だけをここで行う */
void ModelThumbnailBaker::Update() {
    // 作り終えた1件があればGPUへ送る（まだ作っている途中なら待たずに戻る）
    if (inFlight_.valid() && inFlight_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Result result = inFlight_.get();
        if (result.succeeded) {
            auto* textures = TextureManager::GetInstance();
            textures->LoadFromRawRGBA8(result.key, result.pixels.data(), kSize, kSize);
            textures->FlushUploads();
            cache_[result.key] = textures->GetSrvHandleGPU(result.key).ptr;
        }
    }
    // 手が空いていれば次の1件を別スレッドで作り始める
    if (inFlight_.valid() || pending_.empty()) {
        return;
    }
    Request request = std::move(pending_.front());
    pending_.pop_front();
    inFlight_ = std::async(std::launch::async, [request = std::move(request)]() {
        // WICでテクスチャを読むので、このスレッドでもCOMを使えるようにする
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Result result;
        result.key = request.key;
        result.succeeded = Bake(request.model, request.texture, result.pixels);
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return result;
    });
}
} // namespace engine::graphics
