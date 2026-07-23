/**
 * @file RevolvedMeshBase.h
 * @brief 分割数指定で生成する回転体状メッシュ（Cylinder/Ring）に共通するGPUリソース管理・描画処理を定義するファイル
 */
#pragma once
#include "Camera.h"
#include "MakeAffine.h"
#include <d3d12.h>
#include <string>
#include <wrl/client.h>
namespace engine {
class DirectXCommon;
}

namespace engine::graphics {

/**
 * @brief 分割数（divisions）指定で生成する回転体状メッシュの共通GPUリソース管理・描画処理を提供する抽象基底クラス
 * @note ジオメトリ生成（RebuildVertices相当）とマテリアル定数バッファの内容は派生クラス（Cylinder/Ring）が持つ。
 *       このクラスは頂点/マテリアルバッファの確保、PSO生成の共通部分、Update/Drawの共通処理のみを担う。
 */
class RevolvedMeshBase {
public:
    virtual ~RevolvedMeshBase() = default;

    /** @brief カメラのビュー射影から求めたWVP行列を派生クラスのマテリアル定数バッファへ反映する */
    void Update(Camera* camera);
    /** @brief ルートシグネチャ・PSO・頂点バッファ・マテリアル定数バッファを使って描画する */
    void Draw();

    void SetPosition(const Vector3& pos) { position_ = pos; }
    void SetRotation(const Vector3& rot) { rotation_ = rot; }
    void SetScale(float scale) { scale_ = scale; }
    const Vector3& GetPosition() const { return position_; }

protected:
    /**
     * @brief 回転体メッシュの頂点1個分のレイアウト（HLSLの入力レイアウトと一致させること）
     */
    struct VertexData {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
    };

    /** @brief 派生クラスが保持するマテリアル定数バッファへWVP行列を書き込む */
    virtual void SetWVP(const Matrix4x4& wvp) = 0;

    /**
     * @brief テクスチャ読み込み・頂点バッファ・マテリアル定数バッファの確保を行う（Initialize共通部）
     * @param materialCBSize     派生クラスのマテリアル定数バッファ構造体のサイズ
     * @param mappedMaterialOut  Map後のマテリアル定数バッファ書き込みポインタの受け取り先
     */
    void SetupCommonBuffers(engine::DirectXCommon* dxCommon, const std::string& textureFilePath,
        int divisions, size_t materialCBSize, void** mappedMaterialOut);

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
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_ { };
    VertexData* vertexData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialBuffer_;

    Vector3 position_ = { };
    Vector3 rotation_ = { };
    float scale_ = 1.0f;
};

} // namespace engine::graphics
