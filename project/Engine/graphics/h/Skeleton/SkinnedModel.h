/**
 * @file SkinnedModel.h
 * @brief SkinnedModelの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <string>
#include <vector>
#include <wrl/client.h>
namespace engine::graphics {

// ボーン重み付き頂点データを持つ GLTF スキンメッシュ
/**
 * @brief assimp で読み込んだ GLTF スキンメッシュの頂点バッファとボーン情報を保持するクラス
 * @details ボーンインデックス・ウェイト付き頂点、逆バインド行列、ボーン名一覧を管理する
 */
class SkinnedModel {
public:
    /**
     * @brief 1頂点分のスキニング用データ（位置・UV・法線・最大4本分のボーン影響）
     */
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
        uint32_t boneIndices[4];
        float boneWeights[4];
    };

    void Initialize(engine::DirectXCommon* dxCommon,
        const std::string& gltfFilePath,
        const std::string& textureFilePath);

    // コマンドリストに頂点バッファとテクスチャ SRV をセットして DrawInstanced を発行する
    // スロット 2 (テクスチャ) と 5 (キューブマップ枠) を書き込む
    /**
     * @brief 頂点バッファとテクスチャ SRV をセットして DrawInstanced を発行する
     * @param cmd コマンドを記録するコマンドリスト
     */
    void Draw(ID3D12GraphicsCommandList* cmd);

    const std::string& GetTextureFilePath() const { return textureFilePath_; }
    const std::vector<Matrix4x4>& GetInverseBindMatrices() const { return inverseBindMatrices_; }
    const std::vector<std::string>& GetBoneNames() const { return boneNames_; }
    ID3D12Resource* GetVertexResource() const { return vertexResource_.Get(); }
    UINT GetVertexCount() const { return static_cast<UINT>(vertices_.size()); }

private:
    void LoadGltfFile(engine::DirectXCommon* dxCommon, const std::string& filePath);

    std::string textureFilePath_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ { };

    std::vector<VertexData> vertices_;
    std::vector<Matrix4x4> inverseBindMatrices_;
    std::vector<std::string> boneNames_;
};

} // namespace engine::graphics
