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
 * @brief スケルトンのジョイント（球）とボーン（ビルボードクワッド）をワールド空間にデバッグ表示するクラス
 */
class SkeletonOverlayRenderer {
public:
    /**
     * @brief ルートシグネチャ・PSO・ジョイント用球メッシュ・ボーン用動的頂点バッファを構築する
     * @param dxCommon デバイス取得・シェーダーコンパイルに使う DirectX 基盤
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /**
     * @brief 深度テスト無効でスケルトンのジョイント（白球）とボーン（白ビルボード）を描画する
     * @param skeleton     描画対象のスケルトン（ジョイントのスケルトン空間行列を使用）
     * @param worldMatrix  スケルトンが属するオブジェクトのワールド行列
     * @param camera       ビルボード方向・VP行列の計算に使うカメラ（nullptr なら何もしない）
     */
    void Draw(const Skeleton& skeleton, const Matrix4x4& worldMatrix, Camera* camera);

private:
    /**
     * @brief オーバーレイ描画用の位置のみの頂点（球・ボーン共通の入力レイアウト）
     */
    struct OverlayVertex {
        float x, y, z, w;
    };
    /**
     * @brief VS の 32bit ルート定数に渡す WVP 行列と描画色（球・ボーン1回分の描画ごとに更新）
     */
    struct OverlayConstants {
        Matrix4x4 wvp;
        Vector4 color;
    };

    /**
     * @brief ジョイント表示用の単位球メッシュ（UV球）の頂点・インデックスを生成しGPUバッファへ書き込む
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
