/**
 * @file WeaponTrailEffect.h
 * @brief 武器の根元と刃先の軌跡を帯ポリゴンとして加算描画するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <cstdint>
#include <deque>
#include <wrl/client.h>

namespace engine::graphics {

class Camera;

/** @brief 軌跡の見た目（色・寿命・補間の細かさ） */
struct WeaponTrailStyle {
    Vector4 tipColor = { 1.0f, 1.0f, 1.0f, 0.9f }; ///< 刃先側の色（アルファは最大不透明度）
    Vector4 baseColor = { 0.4f, 0.7f, 1.0f, 0.0f }; ///< 根元側の色（アルファ0で根元へ向かって透ける）
    float lifetime = 0.16f; ///< 1サンプルが消えるまでの秒数
    int subdivisions = 6; ///< サンプル間をCatmull-Rom補間で何分割するか（1で補間なし）
};

/**
 * @brief 武器軌跡の帯描画
 * @note 毎フレーム刃の根元/先端をAddSample()で積み、サンプル間をCatmull-Rom曲線で分割して帯を作る。
 *       60fpsで速い振りをサンプルすると折れ線になるため、補間でフレーム間の弧を復元する。
 *       BreakStroke()で次のサンプルと前のサンプルを繋がなくする（段の切れ目用）。
 */
class WeaponTrailEffect {
public:
    void Initialize(engine::DirectXCommon* dxCommon);

    /**
     * @brief 今フレームの刃の位置を積む
     * @param base 刃の根元（ワールド座標）
     * @param tip  刃の先端（ワールド座標）
     */
    void AddSample(const Vector3& base, const Vector3& tip);

    /** @brief 振りの区切り 次に積むサンプルを新しい帯の始点にする */
    void BreakStroke() { ++strokeId_; }

    /**
     * @brief 寿命を進めて頂点バッファを組み直す
     * @param dt     経過秒数（ヒットストップ中に0を渡すと軌跡もその場で止まる）
     * @param camera ビュープロジェクション行列の取得元
     */
    void Update(float dt, Camera* camera);

    /** @brief 帯の描画コマンドを積む（3D描画パス内で呼ぶ） */
    void Draw();

    /** @brief 全サンプルを消す */
    void Clear() { samples_.clear(); vertexCount_ = 0; }

    void SetStyle(const WeaponTrailStyle& style) { style_ = style; }
    const WeaponTrailStyle& GetStyle() const { return style_; }

    /**
     * @brief 最新のサンプルから指定秒数さかのぼった間に刃先が動いた量を返す
     * @param window さかのぼる秒数
     * @param outMotion 刃先の移動量（最新 - 過去）
     * @return 同じ振りの中に比較できるサンプルがあればtrue
     */
    bool GetRecentTipMotion(float window, Vector3& outMotion) const;

    uint32_t GetVertexCount() const { return vertexCount_; }
    int GetSampleCount() const { return static_cast<int>(samples_.size()); }

private:
    static constexpr int kMaxSamples = 64;
    static constexpr int kMaxSubdivisions = 12;
    static constexpr int kVertsPerQuad = 6;
    static constexpr uint32_t kMaxVertices = kMaxSamples * kMaxSubdivisions * kVertsPerQuad;

    /** @brief 頂点（bladeflashシェーダーの入力レイアウトと一致させること） */
    struct TrailVertex {
        Vector4 position;
        Vector4 color;
    };

    /** @brief cbuffer のメモリレイアウト（HLSL の SceneParams と一致させること） */
    struct SceneCB {
        Matrix4x4 viewProj;
    };

    /** @brief 1フレーム分の刃の位置 */
    struct Sample {
        Vector3 base;
        Vector3 tip;
        float age = 0.0f;
        uint32_t stroke = 0;
    };

    void CreatePipeline();
    /** @brief サンプル列から帯の三角形を組み立てる */
    void RebuildVertices();
    /** @brief 補間位置1点ぶんの頂点色を作る */
    Vector4 MakeColor(const Vector4& color, float age) const;

    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_ { };
    TrailVertex* vertexData_ = nullptr;
    uint32_t vertexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneCB_;
    SceneCB* sceneCBData_ = nullptr;

    std::deque<Sample> samples_; ///< 古い順（末尾が最新）
    uint32_t strokeId_ = 0;
    WeaponTrailStyle style_;
};

} // namespace engine::graphics
