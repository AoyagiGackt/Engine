/**
 * @file AssimpVertexReader.h
 * @brief 通常モデルとスキンモデルに共通する頂点属性の読み込み
 */
#pragma once
#include <assimp/mesh.h>
#include <cstdint>
namespace engine::graphics {
/** @brief Assimpの位置・法線・UVを読み、X軸の変換を適用する */
template <typename Vertex>
void ReadAssimpVertex(Vertex& vertex, const aiMesh& mesh, uint32_t index, float xScale) {
    // インポート時の座標系に合わせて位置と法線のX成分を変換する
    vertex.position = { mesh.mVertices[index].x * xScale, mesh.mVertices[index].y, mesh.mVertices[index].z, 1.0f };
    if (mesh.HasNormals()) {
        vertex.normal = { mesh.mNormals[index].x * xScale, mesh.mNormals[index].y, mesh.mNormals[index].z };
    }
    if (mesh.HasTextureCoords(0)) {
        vertex.texcoord = { mesh.mTextureCoords[0][index].x, mesh.mTextureCoords[0][index].y };
    }
}
} // namespace engine::graphics
