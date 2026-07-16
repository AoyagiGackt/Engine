#include "Model.h"
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
#include <map>
#include <sstream>
#include <tuple>
using namespace engine;
using namespace engine::graphics;

using namespace Microsoft::WRL;

void Model::Initialize(ModelCommon* modelCommon, const std::string& modelFilePath, const std::string& textureFilePath)
{
    modelCommon_ = modelCommon;
    textureFilePath_ = textureFilePath;

    TextureManager::GetInstance()->LoadTexture(textureFilePath);
    isCubemap_ = TextureManager::GetInstance()->GetMetaData(textureFilePath).IsCubemap();

    // 拡張子で読み込み関数を切り替える
    auto dotPos = modelFilePath.find_last_of('.');
    ENGINE_ASSERT(dotPos != std::string::npos && "モデルファイルパスに拡張子がありません");
    std::string ext = modelFilePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == "obj") {
        LoadObjFile(modelFilePath);
    } else {
        LoadGltfFile(modelFilePath);
    }

    ID3D12Device* device = modelCommon_->GetDxCommon()->GetDevice();
    size_t sizeInBytes = sizeof(VertexData) * vertices_.size();

    D3D12_HEAP_PROPERTIES uploadHeapProperties { D3D12_HEAP_TYPE_UPLOAD };

    D3D12_RESOURCE_DESC resourceDesc { };
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&vertexResource_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    VertexData* data = nullptr;
    hr = vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&data));
    ENGINE_ASSERT(SUCCEEDED(hr));
    std::copy(vertices_.begin(), vertices_.end(), data);
    vertexResource_->Unmap(0, nullptr);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeInBytes);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    // インデックスバッファ作成
    size_t indexSizeInBytes = sizeof(uint32_t) * indices_.size();

    D3D12_RESOURCE_DESC indexResourceDesc { };
    indexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    indexResourceDesc.Width = indexSizeInBytes;
    indexResourceDesc.Height = 1;
    indexResourceDesc.DepthOrArraySize = 1;
    indexResourceDesc.MipLevels = 1;
    indexResourceDesc.SampleDesc.Count = 1;
    indexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
        &indexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&indexResource_));
    ENGINE_ASSERT(SUCCEEDED(hr));

    uint32_t* indexData = nullptr;
    hr = indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    ENGINE_ASSERT(SUCCEEDED(hr));
    std::copy(indices_.begin(), indices_.end(), indexData);
    indexResource_->Unmap(0, nullptr);

    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = static_cast<UINT>(indexSizeInBytes);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

void Model::Draw(ModelCommon* modelCommon)
{
    ID3D12GraphicsCommandList* commandList = modelCommon->GetDxCommon()->GetCommandList();

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->IASetIndexBuffer(&indexBufferView_);

    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandle);

    // スロット5（TextureCube）  環境マップが指定されていればそちらを、なければ通常テクスチャを流用
    if (!envCubemapFilePath_.empty()) {
        D3D12_GPU_DESCRIPTOR_HANDLE cubeHandle = TextureManager::GetInstance()->GetSrvHandleGPU(envCubemapFilePath_);
        commandList->SetGraphicsRootDescriptorTable(5, cubeHandle);
    } else {
        commandList->SetGraphicsRootDescriptorTable(5, textureSrvHandle);
    }

    commandList->DrawIndexedInstanced(static_cast<UINT>(indices_.size()), 1, 0, 0, 0);
}

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

void Model::ParseObjFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    ENGINE_ASSERT(file.is_open());

    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;

    // (v, vt, vn) の組み合わせ → vertices_ 内のインデックスへのマップ
    std::map<std::tuple<int, int, int>, uint32_t> indexMap;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string identifier;
        ss >> identifier;

        if (identifier == "v") {
            Vector4 position;
            ss >> position.x >> position.y >> position.z;
            position.w = 1.0f;
            positions.push_back(position);
        } else if (identifier == "vt") {
            Vector2 texcoord;
            ss >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        } else if (identifier == "vn") {
            Vector3 normal;
            ss >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        } else if (identifier == "f") {
            std::vector<uint32_t> faceIndices;
            std::string s;

            while (ss >> s) {
                std::stringstream ss2(s);
                std::string indexStr;
                int idx[3] = { 0, 0, 0 };
                int count = 0;

                while (std::getline(ss2, indexStr, '/')) {
                    if (!indexStr.empty()) {
                        idx[count] = std::stoi(indexStr);
                    }
                    count++;
                }

                auto key = std::make_tuple(idx[0], idx[1], idx[2]);
                auto it = indexMap.find(key);
                if (it != indexMap.end()) {
                    faceIndices.push_back(it->second);
                } else {
                    uint32_t newIndex = static_cast<uint32_t>(vertices_.size());
                    indexMap[key] = newIndex;
                    faceIndices.push_back(newIndex);

                    VertexData vd { };
                    if (idx[0] > 0) {
                        vd.position = positions[idx[0] - 1];
                    }
                    if (idx[1] > 0) {
                        vd.texcoord = texcoords[idx[1] - 1];
                    }
                    if (idx[2] > 0) {
                        vd.normal = normals[idx[2] - 1];
                    }
                    vertices_.push_back(vd);
                }
            }

            // ファン状に三角形へ分割してインデックスを登録
            for (size_t i = 1; i < faceIndices.size() - 1; ++i) {
                indices_.push_back(faceIndices[0]);
                indices_.push_back(faceIndices[i]);
                indices_.push_back(faceIndices[i + 1]);
            }
        }
    }
}

void Model::ComputeTangents()
{
    // タンジェント計算（三角形ごとに dPos/dUV から求めてアキュムレート）
    std::vector<Vector3> tangentAccum(vertices_.size(), { 0.0f, 0.0f, 0.0f });
    for (size_t i = 0; i + 2 < indices_.size(); i += 3) {
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
        if (std::abs(denom) < 1e-6f) {
            continue;
        }
        float invR = 1.0f / denom;
        Vector3 t = {
            (e1.x * dv2 - e2.x * dv1) * invR,
            (e1.y * dv2 - e2.y * dv1) * invR,
            (e1.z * dv2 - e2.z * dv1) * invR
        };
        for (uint32_t idx : { i0, i1, i2 }) {
            tangentAccum[idx].x += t.x;
            tangentAccum[idx].y += t.y;
            tangentAccum[idx].z += t.z;
        }
    }
    for (size_t i = 0; i < vertices_.size(); ++i) {
        Vector3 n = vertices_[i].normal;
        Vector3 t = tangentAccum[i];
        // Gram-Schmidt 直交化
        float dot = n.x * t.x + n.y * t.y + n.z * t.z;
        t = { t.x - n.x * dot, t.y - n.y * dot, t.z - n.z * dot };
        float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        if (len > 1e-6f) {
            t.x /= len;
            t.y /= len;
            t.z /= len;
        }
        vertices_[i].tangent = t;
    }
}

void Model::LoadGltfFile(const std::string& filePath)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder);

    ENGINE_ASSERT(scene && scene->mNumMeshes > 0);

    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        uint32_t baseVertex = static_cast<uint32_t>(vertices_.size());

        // 頂点データをそのまま追加
        for (uint32_t vIdx = 0; vIdx < mesh->mNumVertices; ++vIdx) {
            VertexData vd { };
            vd.position = {
                mesh->mVertices[vIdx].x,
                mesh->mVertices[vIdx].y,
                mesh->mVertices[vIdx].z,
                1.0f
            };
            if (mesh->HasNormals()) {
                vd.normal = {
                    mesh->mNormals[vIdx].x,
                    mesh->mNormals[vIdx].y,
                    mesh->mNormals[vIdx].z
                };
            }
            if (mesh->HasTextureCoords(0)) {
                vd.texcoord = {
                    mesh->mTextureCoords[0][vIdx].x,
                    mesh->mTextureCoords[0][vIdx].y
                };
            }
            if (mesh->HasTangentsAndBitangents()) {
                vd.tangent = {
                    mesh->mTangents[vIdx].x,
                    mesh->mTangents[vIdx].y,
                    mesh->mTangents[vIdx].z
                };
            }
            vertices_.push_back(vd);
        }

        // Assimpのフェイスインデックスをそのままインデックスバッファへ追加
        for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            const aiFace& face = mesh->mFaces[faceIndex];
            for (uint32_t i = 0; i < face.mNumIndices; ++i) {
                indices_.push_back(baseVertex + face.mIndices[i]);
            }
        }
    }
}
