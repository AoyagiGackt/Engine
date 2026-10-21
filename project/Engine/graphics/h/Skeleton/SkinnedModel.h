/**
 * @file SkinnedModel.h
 * @brief スキンメッシュのモデル（SkinnedModel）の宣言
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
    static constexpr int kMaxBoneInfluences = 4;

public:
    /**
     * @brief 1頂点分のスキニング用データ（位置・UV・法線・最大4本分のボーン影響）
     */
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
        uint32_t boneIndices[kMaxBoneInfluences];
        float boneWeights[kMaxBoneInfluences];
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

    /** @brief テクスチャのファイルパスを返す */
    const std::string& GetTextureFilePath() const {
        return textureFilePath_;
    }
    /** @brief 逆バインド行列の一覧を返す */
    const std::vector<Matrix4x4>& GetInverseBindMatrices() const {
        return inverseBindMatrices_;
    }
    /** @brief ボーン名の一覧を返す */
    const std::vector<std::string>& GetBoneNames() const {
        return boneNames_;
    }
    /** @brief 頂点バッファのリソースを返す */
    ID3D12Resource* GetVertexResource() const {
        return vertexResource_.Get();
    }
    /** @brief 頂点数を返す */
    UINT GetVertexCount() const {
        return static_cast<UINT>(vertices_.size());
    }

private:
    /** @brief pakの頂点と付随データを復元して有効性を確認する */
    bool LoadPackedGeometry(const std::vector<uint8_t>& packed);
    /** @brief pak作成中に頂点と付随データを保存する */
    void RecordPackedGeometry(const std::string& packKey) const;

    void LoadGltfFile(engine::DirectXCommon* dxCommon, const std::string& filePath);

    std::string textureFilePath_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ { };

    std::vector<VertexData> vertices_;
    std::vector<Matrix4x4> inverseBindMatrices_;
    std::vector<std::string> boneNames_;
};

} // namespace engine::graphics
