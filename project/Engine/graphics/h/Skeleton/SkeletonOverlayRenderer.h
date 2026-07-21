/**
 * @file SkeletonOverlayRenderer.h
 * @brief SkeletonOverlayRendererの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#ifdef USE_IMGUI

#include "DirectXCommon.h"
#include "MakeAffine.h"
#include "Skeleton.h"
#include <vector>
#include <wrl/client.h>
namespace engine::graphics {

class Camera;

// 3Dワールド空間にスケルトンを描画するデバッグクラス
// - ジョイント   白い球
// - ボーン       カメラ向きのビルボードクワッド（白）
// 深度テスト無効でメッシュに埋まっていても常に表示
/**
 * @brief SkeletonOverlayRenderer に関する型を提供する
 * @details SkeletonOverlayRenderer が扱うデータと操作の責務をまとめる
 */
class SkeletonOverlayRenderer {
public:
    /**
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @return なし
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /**
     * @brief Draw に対応する内容を描画する
     * @param skeleton 処理に使用する値
     * @param worldMatrix 処理に使用する値
     * @param camera 処理に使用する値
     * @return なし
     */
    void Draw(const Skeleton& skeleton, const Matrix4x4& worldMatrix, Camera* camera);

private:
    /**
     * @brief OverlayVertex に関する型を提供する
     * @details OverlayVertex が扱うデータと操作の責務をまとめる
     */
    struct OverlayVertex {
        float x, y, z, w;
    };
    /**
     * @brief OverlayConstants に関する型を提供する
     * @details OverlayConstants が扱うデータと操作の責務をまとめる
     */
    struct OverlayConstants {
        Matrix4x4 wvp;
        Vector4 color;
    };

    /**
     * @brief BuildSphere に対応する処理を実行する
     * @return なし
     */
    void BuildSphere();

    static constexpr int kStacks = 6;
    static constexpr int kSlices = 8;
    static constexpr float kSphereRadius = 0.04f;
    static constexpr float kBoneHalfWidth = 0.015f; // ボーンの太さ（ワールド単位）
    static constexpr int kMaxBones = 256;

    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoTri_;

    Microsoft::WRL::ComPtr<ID3D12Resource> sphereVB_;
    Microsoft::WRL::ComPtr<ID3D12Resource> sphereIB_;
    D3D12_VERTEX_BUFFER_VIEW sphereVBV_ { };
    D3D12_INDEX_BUFFER_VIEW sphereIBV_ { };
    uint32_t sphereIndexCount_ = 0;

    // ボーン用ビルボードクワッド（毎フレーム CPU で構築）
    Microsoft::WRL::ComPtr<ID3D12Resource> lineVB_;
    Microsoft::WRL::ComPtr<ID3D12Resource> lineIB_;
    D3D12_VERTEX_BUFFER_VIEW lineVBV_ { };
    D3D12_INDEX_BUFFER_VIEW lineIBV_ { };
    OverlayVertex* lineMapped_ = nullptr;
    uint16_t* lineIdxMapped_ = nullptr;
};

} // namespace engine::graphics

#endif // USE_IMGUI
