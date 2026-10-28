/**
 * @file RevolvedMeshBase.h
 * @brief 分割数指定で生成する回転体状メッシュ（Cylinder/Ring）に共通するGPUリソース管理・描画処理を定義するファイル
 */
#pragma once
#include "Camera.h"
#include "MakeAffine.h"
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl/client.h>
namespace engine {
class DirectXCommon;
}

namespace engine::graphics {

/**
 * @brief 分割数（divisions）指定で生成する回転体状メッシュの共通GPUリソース管理・描画処理を提供する抽象基底クラス
 * @note ジオメトリ生成（RebuildVertices相当）とマテリアル定数バッファの内容は派生クラス（Cylinder/Ring）が持つ。
 *       このクラスはCPU側の頂点とマテリアルの保持、PSO生成の共通部分、Update/Drawの共通処理のみを担う。
 */
class RevolvedMeshBase {
public:
    /** @brief 未初期化の回転体メッシュを作る（Initialize で中身を用意する） */
    RevolvedMeshBase() = default;
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~RevolvedMeshBase() = default;
    /** @brief 派生クラスのマテリアルを指すポインタを持つため、コピーを禁止する */
    RevolvedMeshBase(const RevolvedMeshBase&) = delete;
    /** @brief 派生クラスのマテリアルを指すポインタを持つため、コピー代入を禁止する */
    RevolvedMeshBase& operator=(const RevolvedMeshBase&) = delete;

    /** @brief カメラのビュー射影から求めたWVP行列を派生クラスのマテリアル定数バッファへ反映する */
    void Update(Camera* camera);
    /** @brief ルートシグネチャ・PSO・頂点バッファ・マテリアル定数バッファを使って描画する */
    void Draw();

    /** @brief 位置を設定する */
    void SetPosition(const Vector3& pos) {
        position_ = pos;
    }
    /** @brief 回転を設定する */
    void SetRotation(const Vector3& rot) {
        rotation_ = rot;
    }
    /** @brief 拡大率を設定する */
    void SetScale(float scale) {
        scale_ = scale;
    }
    /** @brief 位置を返す */
    const Vector3& GetPosition() const {
        return position_;
    }

protected:
    /**
     * @brief 回転体メッシュの頂点1個分のレイアウト（HLSLの入力レイアウトと一致させること）
     */
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    /** @brief 1区間（四角形1枚＝三角形2枚）に使う頂点の数 */
    static constexpr int kVerticesPerDivision = 6;

    /** @brief 1区間の始まりと終わりの角度の sin / cos と、横方向の UV */
    struct SegmentAngles {
        float s;
        float c;
        float sNext;
        float cNext;
        float u;
        float uNext;
    };

    /** @brief 派生クラスが保持するマテリアル定数バッファへWVP行列を書き込む */
    virtual void SetWVP(const Matrix4x4& wvp) = 0;

    /** @brief index 番目の区間の角度と UV を返す（円周を divisions_ 等分する） */
    SegmentAngles SegmentAt(int index) const;

    /**
     * @brief index 番目の区間の四角形を、三角形2枚として頂点バッファへ書き込む
     * @param upperCur / upperNext UV の V=0 側の2点（区間の始まりと終わり）
     * @param lowerCur / lowerNext UV の V=1 側の2点
     * @param normalCur / normalNext 区間の始まりと終わりの法線
     */
    void WriteSegmentQuad(int index, const SegmentAngles& segment,
        const Vector4& upperCur, const Vector4& upperNext, const Vector4& lowerCur, const Vector4& lowerNext,
        const Vector3& normalCur, const Vector3& normalNext);

    /**
     * @brief テクスチャを読み込み、CPU側の頂点と定数データを用意する
     * @param materialCBSize     派生クラスのマテリアル定数バッファ構造体のサイズ
     * @param mappedMaterialOut  派生クラスが所有する、CPU側の質感データの参照先
     */
    void SetupCommonBuffers(engine::DirectXCommon* dxCommon, const std::string& textureFilePath,
        int divisions, size_t materialCBSize, void** mappedMaterialOut);

    /** @brief 回転体のバッファを確保し、共通のマテリアル初期値を設定する */
    template <typename Material>
    void SetupMaterialBuffers(engine::DirectXCommon* dxCommon, const std::string& textureFilePath,
        int divisions, Material*& material) {
        // 派生マテリアルのサイズを保って共通の行列と色を初期化する
        SetupCommonBuffers(dxCommon, textureFilePath, divisions, sizeof(Material),
            reinterpret_cast<void**>(&material));
        material->wvp = MakeIdentity4x4();
        material->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    }

    /**
     * @brief ルートシグネチャ・PSOを生成する（シェーダーパスとサンプラーのV方向モードのみ派生クラスが指定）
     */
    void CreatePipelineCommon(const std::wstring& vsPath, const std::wstring& psPath,
        D3D12_TEXTURE_ADDRESS_MODE addressV);

    int divisions_ = 32;
    int vertexCount_ = 0;

    engine::DirectXCommon* dxCommon_ = nullptr;
    std::string textureFilePath_;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    /** @brief 形状を変更するためのCPU側の頂点配列 */
    std::vector<VertexData> vertices_;
    D3D12_VERTEX_BUFFER_VIEW vbv_ { };
    VertexData* vertexData_ = nullptr;

    /** @brief 派生クラスが所有するCPU側の定数データ */
    const void* materialData_ = nullptr;
    /** @brief 転送する定数データのバイト数 */
    size_t materialBytes_ = 0;

    Vector3 position_ = { };
    Vector3 rotation_ = { };
    float scale_ = 1.0f;
};

} // namespace engine::graphics
