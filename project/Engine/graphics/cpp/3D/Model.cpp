/**
 * @file Model.cpp
 * @brief モデルファイルの読み込みと頂点・マテリアルの用意（Model）の実装
 */
#include "Model.h"
#include "AssimpVertexReader.h"
#include "PipelineHelper.h"
#include "AssetPack.h"
#include "EngineAssert.h"
#include "ModelCommon.h"
#include "TextureManager.h"
#include <algorithm>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <sstream>
#include <tuple>

namespace engine::graphics {
namespace {

// 三角形のインデックス数とOBJの参照成分数
constexpr size_t kTriangleIndexCount = 3;
constexpr size_t kObjReferenceComponentCount = 3;

// 接線の縮退判定
constexpr float kTangentEpsilon = 1e-6f;

}

using namespace Microsoft::WRL;

/** @brief モデルとテクスチャを読み込み、頂点とインデックスのバッファを生成する */
void Model::Initialize(ModelCommon* modelCommon, const std::string& modelFilePath, const std::string& textureFilePath)
{
    // テクスチャを読み、キューブマップかどうかを覚えておく
    modelCommon_ = modelCommon;
    textureFilePath_ = textureFilePath;

    TextureManager::GetInstance()->LoadTexture(textureFilePath);
    isCubemap_ = TextureManager::GetInstance()->GetMetaData(textureFilePath).IsCubemap();

    // 拡張子で読み込み関数を切り替える
    // 拡張子を小文字で取り出す
    auto dotPos = modelFilePath.find_last_of('.');
    ENGINE_ASSERT(dotPos != std::string::npos && "モデルファイルパスに拡張子がありません");
    std::string ext = modelFilePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    // pakに頂点・インデックスがあれば、assimpやOBJの解析と法線・接線の計算を丸ごと省く
    const std::string packKey = "model:" + modelFilePath;
    std::vector<uint8_t> packed;
    if (!AssetPack::GetInstance()->Read(packKey, packed) || !LoadPackedGeometry(packed))
    {
        vertices_.clear();
        indices_.clear();
        // 拡張子と読み込み関数の対応（載っていない拡張子は assimp 経由で読む）
        struct Loader {
            const char* extension;
            void (Model::*load)(const std::string&);
        };
        static constexpr Loader kLoaders[] = {
            { "obj", &Model::LoadObjFile },
        };
        const auto loader = std::find_if(std::begin(kLoaders), std::end(kLoaders),
            [&ext](const Loader& entry) {
                return ext == entry.extension;
            });
        (this->*(loader != std::end(kLoaders) ? loader->load : &Model::LoadGltfFile))(modelFilePath);
        RecordPackedGeometry(packKey);
    }

    // 頂点をアップロードヒープのバッファへ書き込み、頂点バッファビューを作る
    ID3D12Device* device = modelCommon_->GetDxCommon()->GetDevice();
    size_t sizeInBytes = sizeof(VertexData) * vertices_.size();

    vertexResource_ = PipelineHelper::CreateUploadBufferFrom(device, vertices_);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeInBytes);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // インデックスバッファ作成
    size_t indexSizeInBytes = sizeof(uint32_t) * indices_.size();

    indexResource_ = PipelineHelper::CreateUploadBufferFrom(device, indices_);

    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = static_cast<UINT>(indexSizeInBytes);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

/** @brief モデルの描画コマンド（DrawIndexedInstanced）を積む */
void Model::Draw(ModelCommon* modelCommon)
{
    ID3D12GraphicsCommandList* commandList = modelCommon->GetDxCommon()->GetCommandList();

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);
    commandList->SetGraphicsRootDescriptorTable(PipelineHelper::ModelRootSlot::kTexture, textureSrvHandle);

    // スロット5（TextureCube）  環境マップが指定されていればそちらを、なければ通常テクスチャを流用
    if (!envCubemapFilePath_.empty())
    {
        D3D12_GPU_DESCRIPTOR_HANDLE cubeHandle = TextureManager::GetInstance()->GetSrvHandleGPU(envCubemapFilePath_);
        commandList->SetGraphicsRootDescriptorTable(PipelineHelper::ModelRootSlot::kEnvironment, cubeHandle);
    }
    else
    {
        commandList->SetGraphicsRootDescriptorTable(PipelineHelper::ModelRootSlot::kEnvironment, textureSrvHandle);
    }

    commandList->DrawIndexedInstanced(static_cast<UINT>(indices_.size()), 1, 0, 0, 0);
}

/** @brief シャドウパス用の描画（テクスチャバインドなし、頂点バッファのみ） */
void Model::DrawGeometryOnly(ModelCommon* modelCommon)
{
    ID3D12GraphicsCommandList* commandList = modelCommon->GetDxCommon()->GetCommandList();
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);
    commandList->DrawIndexedInstanced(static_cast<UINT>(indices_.size()), 1, 0, 0, 0);
}

// OBJファイル読み込み
void Model::LoadObjFile(const std::string& filePath)
{
    ParseObjFile(filePath);
    ComputeTangents();
}

/** @brief OBJファイルのテキストを解析し vertices_/indices_ を構築する */
void Model::ParseObjFile(const std::string& filePath)
{
    // OBJ は v/vt/vn で位置・UV・法線を、f で面を並べた行の集まり
    std::ifstream file(filePath);
    ENGINE_ASSERT(file.is_open());

    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;

    // (v, vt, vn) の組み合わせ → vertices_ 内のインデックスへのマップ
    std::map<std::tuple<int, int, int>, uint32_t> indexMap;

    /** @brief OBJファイルの行の識別子と読み取り処理の対応 */
    struct ObjLineHandler {
        const char* identifier;
        std::function<void(std::stringstream&)> parse;
    };
    const ObjLineHandler handlers[] = {
        { "v", [&](std::stringstream& ss) {
            Vector4 position;
            ss >> position.x >> position.y >> position.z;
            position.w = 1.0f;
            positions.push_back(position);
        } },
        { "vt", [&](std::stringstream& ss) {
            Vector2 texcoord;
            ss >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        } },
        { "vn", [&](std::stringstream& ss) {
            Vector3 normal;
            ss >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        } },
        { "f", [&](std::stringstream& ss) {
            std::vector<uint32_t> faceIndices;
            std::string s;

            // 面の各頂点 v/vt/vn を読み、同じ組み合わせは同じ頂点を使い回す（番号は1始まり）
            while (ss >> s)
            {
                std::stringstream ss2(s);
                std::string indexStr;
                int idx[kObjReferenceComponentCount] = { 0, 0, 0 };
                int count = 0;

                while (std::getline(ss2, indexStr, '/'))
                {
                    if (!indexStr.empty())
                    {
                        idx[count] = std::stoi(indexStr);
                    }
                    count++;
                }

                auto key = std::make_tuple(idx[0], idx[1], idx[2]);
                auto it = indexMap.find(key);
                if (it != indexMap.end())
                {
                    faceIndices.push_back(it->second);
                }
                else
                {
                    uint32_t newIndex = static_cast<uint32_t>(vertices_.size());
                    indexMap[key] = newIndex;
                    faceIndices.push_back(newIndex);

                    VertexData vd { };
                    if (idx[0] > 0)
                    {
                        vd.position = positions[idx[0] - 1];
                    }
                    if (idx[1] > 0)
                    {
                        vd.texcoord = texcoords[idx[1] - 1];
                    }
                    if (idx[2] > 0)
                    {
                        vd.normal = normals[idx[2] - 1];
                    }
                    vertices_.push_back(vd);
                }
            }

            // ファン状に三角形へ分割してインデックスを登録
            for (size_t i = 1; i < faceIndices.size() - 1; ++i)
            {
                indices_.push_back(faceIndices[0]);
                indices_.push_back(faceIndices[i]);
                indices_.push_back(faceIndices[i + 1]);
            }
        } },
    };
    // 1行ずつ先頭の識別子を見て、対応する読み取り処理に渡す
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string identifier;
        ss >> identifier;
        for (const auto& handler : handlers)
        {
            if (identifier == handler.identifier)
            {
                handler.parse(ss);
                break;
            }
        }
    }
}

/** @brief vertices_/indices_ から三角形ごとにタンジェントを計算し格納する */
void Model::ComputeTangents()
{
    // タンジェント計算（三角形ごとに dPos/dUV から求めてアキュムレート）
    std::vector<Vector3> tangentAccum(vertices_.size(), { 0.0f, 0.0f, 0.0f });
    for (size_t i = 0; i + 2 < indices_.size(); i += kTriangleIndexCount)
    {
        // 三角形の2辺の位置の差と UV の差から、UV の U 方向に当たる向きを求める
        uint32_t i0 = indices_[i], i1 = indices_[i + 1], i2 = indices_[i + 2];
        Vector3 p0 = { vertices_[i0].position.x, vertices_[i0].position.y, vertices_[i0].position.z };
        Vector3 p1 = { vertices_[i1].position.x, vertices_[i1].position.y, vertices_[i1].position.z };
        Vector3 p2 = { vertices_[i2].position.x, vertices_[i2].position.y, vertices_[i2].position.z };
        Vector2 uv0 = vertices_[i0].texcoord, uv1 = vertices_[i1].texcoord, uv2 = vertices_[i2].texcoord;

        Vector3 e1 = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        Vector3 e2 = { p2.x - p0.x, p2.y - p0.y, p2.z - p0.z };
        float du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
        float du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;

        float denom = du1 * dv2 - du2 * dv1;
        if (std::abs(denom) < kTangentEpsilon)
        {
            continue;
        }
        float invR = 1.0f / denom;
        Vector3 t = {
            (e1.x * dv2 - e2.x * dv1) * invR,
            (e1.y * dv2 - e2.y * dv1) * invR,
            (e1.z * dv2 - e2.z * dv1) * invR
        };
        // 三角形の3頂点に足し込み、共有する頂点では平均の向きになるようにする
        for (uint32_t idx : { i0, i1, i2 })
        {
            tangentAccum[idx].x += t.x;
            tangentAccum[idx].y += t.y;
            tangentAccum[idx].z += t.z;
        }
    }
    // 法線に直交するよう補正して長さ1にする
    for (size_t i = 0; i < vertices_.size(); ++i)
    {
        Vector3 n = vertices_[i].normal;
        Vector3 t = tangentAccum[i];
        // Gram-Schmidt 直交化
        float dot = n.x * t.x + n.y * t.y + n.z * t.z;
        t = { t.x - n.x * dot, t.y - n.y * dot, t.z - n.z * dot };
        float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        if (len > kTangentEpsilon)
        {
            t.x /= len;
            t.y /= len;
            t.z /= len;
        }
        vertices_[i].tangent = t;
    }
}

/** @brief Assimpを使ってGLTF/FBX等のメッシュを読み込む内部関数 */
void Model::LoadGltfFile(const std::string& filePath)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder);

    ENGINE_ASSERT(scene && scene->mNumMeshes > 0);

    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
    {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        uint32_t baseVertex = static_cast<uint32_t>(vertices_.size());

        // 頂点データをそのまま追加
        for (uint32_t vIdx = 0; vIdx < mesh->mNumVertices; ++vIdx)
        {
            VertexData vd { };
            ReadAssimpVertex(vd, *mesh, vIdx, 1.0f);
            if (mesh->HasTangentsAndBitangents())
            {
                vd.tangent = {
                    mesh->mTangents[vIdx].x,
                    mesh->mTangents[vIdx].y,
                    mesh->mTangents[vIdx].z
                };
            }
            vertices_.push_back(vd);
        }

        // Assimpのフェイスインデックスをそのままインデックスバッファへ追加
        for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
        {
            const aiFace& face = mesh->mFaces[faceIndex];
            for (uint32_t i = 0; i < face.mNumIndices; ++i)
            {
                indices_.push_back(baseVertex + face.mIndices[i]);
            }
        }
    }
}

/** @brief pakの頂点と付随データを復元して有効性を確認する */
bool Model::LoadPackedGeometry(const std::vector<uint8_t>& packed)
{
    // 保存した順に読み、不完全なデータは呼び出し元で破棄する
    BinaryReader reader(packed);
    vertices_ = reader.ReadVector<VertexData>();
    indices_ = reader.ReadVector<uint32_t>();
    return reader.IsValid() && !vertices_.empty();
}

/** @brief pak作成中に頂点と付随データを保存する */
void Model::RecordPackedGeometry(const std::string& packKey) const
{
    // 通常の読み込み時には記録を行わない
    if (!AssetPack::GetInstance()->IsCooking())
    {
        return;
    }
    BinaryWriter writer;
    writer.WriteVector(vertices_);
    writer.WriteVector(indices_);
    AssetPack::GetInstance()->Record(packKey, writer.TakeBytes());
}

} // namespace engine::graphics
