/**
 * @file SkinnedModel.cpp
 * @brief スキンメッシュのモデル（SkinnedModel）の読み込みの実装
 */
#include "SkinnedModel.h"
#include "PipelineHelper.h"
#include "AssimpVertexReader.h"
#include "AssetPack.h"
#include "EngineAssert.h"
#include "TextureManager.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <map>

namespace engine::graphics {
namespace {

// 三角形の頂点数
constexpr uint32_t kTriangleVertexCount = 3;

}

using namespace Microsoft::WRL;

// assimp の mOffsetMatrix (右手・列ベクトル規則) を
// このプロジェクトの Matrix4x4 (左手・行ベクトル規則) に変換する
// Skeleton.cpp::ConvertAiNode と同じ Decompose → flip → MakeAffineMatrix の手順を踏む
static Matrix4x4 ConvertOffsetMatrix(const aiMatrix4x4& m) {
    aiVector3D scale, translate;
    aiQuaternion rotate;
    m.Decompose(scale, rotate, translate);

    // 右手系 → 左手系（X 軸反転） ConvertAiNode と同一の変換
    Vector3 s = { scale.x, scale.y, scale.z };
    Quaternion r = { rotate.x, -rotate.y, -rotate.z, rotate.w };
    Vector3 t = { -translate.x, translate.y, translate.z };

    return MakeAffineMatrix(s, r, t);
}

/** @brief スキンメッシュのモデルを読み込んで描画資源を用意する */
void SkinnedModel::Initialize(DirectXCommon* dxCommon,
    const std::string& gltfFilePath,
    const std::string& textureFilePath) {
    // 貼るテクスチャを先に読み込んでおく
    textureFilePath_ = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath);

    // pakにボーン付き頂点・逆バインド行列・ボーン名があれば、assimpの解析を省く
    const std::string packKey = "skin:" + gltfFilePath;
    std::vector<uint8_t> packed;
    if (!AssetPack::GetInstance()->Read(packKey, packed) || !LoadPackedGeometry(packed)) {
        vertices_.clear();
        inverseBindMatrices_.clear();
        boneNames_.clear();
        LoadGltfFile(dxCommon, gltfFilePath);
        // pak作成中なら、解析結果を次回から読めるよう同じ並びで記録する
        RecordPackedGeometry(packKey);
    }

    // 頂点が1つも無いモデルは描けないので止める
    size_t sizeInBytes = sizeof(VertexData) * vertices_.size();
    ENGINE_ASSERT(sizeInBytes > 0);

    // 頂点バッファをアップロードヒープに作り、頂点をそのまま書き込む
    vertexResource_ = pipelineHelper::CreateUploadBufferFrom(dxCommon->GetDevice(), vertices_);

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeInBytes);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

/** @brief 頂点バッファとテクスチャ SRV をセットして DrawInstanced を発行する */
void SkinnedModel::Draw(ID3D12GraphicsCommandList* cmd) {
    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);

    D3D12_GPU_DESCRIPTOR_HANDLE texHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);
    cmd->SetGraphicsRootDescriptorTable(pipelineHelper::modelRootSlot::kTexture, texHandle);
    // スロット5(t2)は SkinnedObject3d::Draw が環境マップまたはフォールバックをバインドする

    cmd->DrawInstanced(static_cast<UINT>(vertices_.size()), 1, 0, 0);
}

/** @brief glTF ファイルからメッシュとスキンの情報を読み込む */
void SkinnedModel::LoadGltfFile(DirectXCommon* /*dxCommon*/, const std::string& filePath) {
    Assimp::Importer importer;
    // MakeLeftHanded / FlipWindingOrder は使わず手動で座標変換する
    // これにより Skeleton.cpp::LoadNodeHierarchyFromFile と同一の変換規則になる
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals | aiProcess_LimitBoneWeights);
    ENGINE_ASSERT(scene && scene->mNumMeshes > 0);

    // パス1: ボーン名 → index, inverse bind matrix を収集
    std::map<std::string, uint32_t> boneMap;
    for (uint32_t mi = 0; mi < scene->mNumMeshes; ++mi) {
        aiMesh* mesh = scene->mMeshes[mi];
        for (uint32_t bi = 0; bi < mesh->mNumBones; ++bi) {
            aiBone* bone = mesh->mBones[bi];
            std::string name = bone->mName.C_Str();
            if (boneMap.find(name) == boneMap.end()) {
                uint32_t idx = static_cast<uint32_t>(boneNames_.size());
                boneMap[name] = idx;
                boneNames_.push_back(name);
                // Decompose → 右手→左手変換 → MakeAffineMatrix で再構成
                inverseBindMatrices_.push_back(ConvertOffsetMatrix(bone->mOffsetMatrix));
            }
        }
    }

    // パス2: 三角形リストを構築（ボーン重み付き）
    for (uint32_t mi = 0; mi < scene->mNumMeshes; ++mi) {
        aiMesh* mesh = scene->mMeshes[mi];
        uint32_t numVerts = mesh->mNumVertices;

        std::vector<uint32_t> indices(numVerts * kMaxBoneInfluences, 0);
        std::vector<float> weights(numVerts * kMaxBoneInfluences, 0.0f);
        std::vector<uint32_t> counts(numVerts, 0);

        for (uint32_t bi = 0; bi < mesh->mNumBones; ++bi) {
            aiBone* bone = mesh->mBones[bi];
            uint32_t boneIdx = boneMap[bone->mName.C_Str()];
            for (uint32_t wi = 0; wi < bone->mNumWeights; ++wi) {
                uint32_t vIdx = bone->mWeights[wi].mVertexId;
                float w = bone->mWeights[wi].mWeight;
                if (counts[vIdx] < kMaxBoneInfluences) {
                    indices[vIdx * kMaxBoneInfluences + counts[vIdx]] = boneIdx;
                    weights[vIdx * kMaxBoneInfluences + counts[vIdx]] = w;
                    ++counts[vIdx];
                }
            }
        }

        for (uint32_t fi = 0; fi < mesh->mNumFaces; ++fi) {
            const aiFace& face = mesh->mFaces[fi];
            ENGINE_ASSERT(face.mNumIndices == kTriangleVertexCount); // Triangulate 済み

            // X 軸反転後にワインディングが逆転するため、インデックス 1 と 2 を入れ替える
            const uint32_t order[kTriangleVertexCount] = { face.mIndices[0], face.mIndices[2], face.mIndices[1] };

            for (uint32_t i = 0; i < kTriangleVertexCount; ++i) {
                uint32_t vIdx = order[i];
                VertexData vd { };
                ReadAssimpVertex(vd, *mesh, vIdx, -1.0f);
                for (int k = 0; k < kMaxBoneInfluences; ++k) {
                    vd.boneIndices[k] = indices[vIdx * kMaxBoneInfluences + k];
                    vd.boneWeights[k] = weights[vIdx * kMaxBoneInfluences + k];
                }
                vertices_.push_back(vd);
            }
        }
    }
}

/** @brief pakの頂点と付随データを復元して有効性を確認する */
bool SkinnedModel::LoadPackedGeometry(const std::vector<uint8_t>& packed) {
    // 保存した順に読み、不完全なデータは呼び出し元で破棄する
    BinaryReader reader(packed);
    vertices_ = reader.ReadVector<VertexData>();
    inverseBindMatrices_ = reader.ReadVector<Matrix4x4>();
    const uint64_t boneCount = reader.Read<uint64_t>();
    boneNames_.clear();
    for (uint64_t i = 0; i < boneCount && reader.IsValid(); ++i) {
        boneNames_.push_back(reader.ReadString());
    }
    return reader.IsValid() && !vertices_.empty();
}

/** @brief pak作成中に頂点と付随データを保存する */
void SkinnedModel::RecordPackedGeometry(const std::string& packKey) const {
    // 通常の読み込み時には記録を行わない
    if (!AssetPack::GetInstance()->IsCooking()) {
        return;
    }
    BinaryWriter writer;
    writer.WriteVector(vertices_);
    writer.WriteVector(inverseBindMatrices_);
    writer.Write(static_cast<uint64_t>(boneNames_.size()));
    for (const std::string& name : boneNames_) {
        writer.WriteString(name);
    }
    AssetPack::GetInstance()->Record(packKey, writer.TakeBytes());
}

} // namespace engine::graphics
