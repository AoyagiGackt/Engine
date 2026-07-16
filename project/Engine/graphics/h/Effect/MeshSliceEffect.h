/**
 * @file MeshSliceEffect.h
 * @brief モデルを複数の平面で切断し、破片が「静止→断面が光ってずれる→一斉に飛散」する大技演出を描画するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace engine::graphics {

class Camera;
class Model;

/**
 * @brief メッシュ切断演出
 * @note Start() でモデルのCPU側頂点をランダムな平面群で切断して破片を生成し、
 *       静止 → 断面が発光しながらずれる → 一斉に飛散して消える、の順に再生する
 *       描画は自前のPSOで行うため、対象モデル本体は演出中に非表示にすること
 */
class MeshSliceEffect {
public:
    void Initialize(engine::DirectXCommon* dxCommon);

    /**
     * @brief モデルを切断して再生を開始する
     * @param model    切断対象のモデル（CPU側頂点とテクスチャパスを参照する）
     * @param worldPos 破片群のワールド原点（対象モデルの表示位置）
     * @param scale    対象モデルの表示スケール
     * @param seed     切断面の乱数シード
     */
    void Start(const Model* model, const Vector3& worldPos, const Vector3& scale, uint32_t seed);

    /**
     * @brief タイムラインと破片の運動を進め、定数バッファを書き込む毎フレーム呼ぶ
     * @param dt     デルタタイム（秒）
     * @param camera WVP計算に使うカメラ
     */
    void Update(float dt, Camera* camera);

    /** @brief 全破片の描画コマンドを積む3D描画パス内で呼ぶ */
    void Draw();

    /** @brief 再生を打ち切って非アクティブに戻す */
    void Reset();

    bool IsActive() const { return active_; }

    /** @brief 破片が飛散するフェーズに入ったか */
    bool IsBursting() const;

    /** @brief 画面暗転オーバーレイの推奨ウェイト（静止〜ずれ中は1、飛散中は減衰、非アクティブは0） */
    float GetOverlayWeight() const;

private:
    // タイムライン（秒）
    static constexpr float kHoldTime = 0.20f; ///< 切断済みだが静止している時間
    static constexpr float kSlideTime = 0.45f; ///< 断面が光りながらずれる時間
    static constexpr float kBurstTime = 0.90f; ///< 飛散して消えるまでの時間

    static constexpr int kPlaneCount = 5; ///< 切断面の数
    static constexpr int kMaxPieces = 40; ///< 破片数の上限
    static constexpr uint32_t kCBSlotSize = 256; ///< 破片1個分の定数バッファスロット

    /** @brief 破片メッシュの頂点（HLSLの入力レイアウトと一致させること） */
    struct SliceVertex {
        Vector4 position;
        Vector2 texcoord;
        Vector3 normal;
        float cap; ///< 1なら切断面（発光する）
    };

    /** @brief cbuffer のメモリレイアウト（HLSL の PieceParams と一致させること） */
    struct PieceParams {
        Matrix4x4 wvp;
        Matrix4x4 world;
        Vector4 color; ///< rgb=ティント a=不透明度
        Vector4 glow; ///< rgb=断面色 w=発光強度
    };

    /** @brief 切断途中の破片（3頂点で1三角形のフラット配列） */
    struct PieceBuild {
        std::vector<SliceVertex> tris;
    };

    /** @brief 確定した破片1個分の描画範囲と運動状態 */
    struct Piece {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        Vector3 centroid = { }; ///< ローカル空間の重心（回転の支点）
        Vector3 slideDir = { }; ///< ずれ・飛散の方向
        Vector3 velocity = { };
        Vector3 angularVel = { };
        Vector3 offset = { };
        Vector3 rotation = { };
    };

    void CreatePipeline();

    /// @brief モデルを平面群で切断し、破片リストと頂点列を構築する
    void BuildPieces(const Model* model, std::mt19937& rng, std::vector<SliceVertex>& outVertices);

    /// @brief 頂点列をGPUバッファへ転送する（容量不足時は作り直す）
    void UploadVertices(const std::vector<SliceVertex>& vertices);

    /// @brief 破片を平面（法線n・通過点p0）で表裏に分割し、切断面のフタも生成する
    static void SplitPiece(const PieceBuild& src, const Vector3& n, const Vector3& p0,
        PieceBuild& outFront, PieceBuild& outBack);

    /// @brief 平面をまたぐ三角形を片側だけ切り出す交点は outCutPoints に追加される
    static void ClipTriangle(const SliceVertex tri[3], const float dist[3], bool keepPositive,
        std::vector<SliceVertex>& outTris, std::vector<Vector3>* outCutPoints);

    static SliceVertex LerpVertex(const SliceVertex& a, const SliceVertex& b, float t);

    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_ { };
    uint32_t vertexCapacity_ = 0;
    SliceVertex* vertexData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> pieceCB_;
    uint8_t* pieceCBData_ = nullptr;

    std::vector<Piece> pieces_;
    std::string textureFilePath_;

    Vector3 worldPos_ = { };
    Vector3 scale_ = { 1.0f, 1.0f, 1.0f };
    float meshRadius_ = 1.0f;
    float slideDist_ = 0.3f;
    float timer_ = 0.0f;
    bool active_ = false;
};

} // namespace engine::graphics
