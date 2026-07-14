/**
 * @file BladeFlashEffect.h
 * @brief ガラス片のような鋭利な刃型ポリゴンを空間に明滅させる加算パーティクルを描画するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <random>
#include <vector>
#include <wrl/client.h>

namespace engine::graphics {

class Camera;

/**
 * @brief 刃型フラッシュパーティクル
 * @note 中心が白熱し先端へ透ける細長い菱形ポリゴンを一瞬だけ表示する
 *       大技の「空間に無数の斬閃が走る」演出用テクスチャ不要の加算描画
 */
class BladeFlashEffect {
public:
    void Initialize(engine::DirectXCommon* dxCommon);

    /**
     * @brief 指定位置の周囲に刃を発生させる
     * @param center 発生中心（ワールド座標）
     * @param count  発生数
     * @param radius 発生範囲の半径
     * @param minLen 刃の最短全長
     * @param maxLen 刃の最長全長
     */
    void Emit(const Vector3& center, int count, float radius, float minLen, float maxLen);

    /** @brief 寿命更新と頂点バッファの再構築毎フレーム呼ぶ */
    void Update(float dt, Camera* camera);

    /** @brief 全刃の描画コマンドを積む3D描画パス内で呼ぶ */
    void Draw();

    /** @brief 全刃を即座に消す（シーン切り替え時などに呼ぶ） */
    void Clear() { blades_.clear(); }

    bool IsActive() const { return !blades_.empty(); }

private:
    static constexpr int kMaxBlades = 256;
    static constexpr int kVertsPerBlade = 12; // 中心から4枚の三角形の扇

    /** @brief 頂点（HLSLの入力レイアウトと一致させること） */
    struct BladeVertex {
        Vector4 position;
        Vector4 color;
    };

    /** @brief cbuffer のメモリレイアウト（HLSL の SceneParams と一致させること） */
    struct SceneCB {
        Matrix4x4 viewProj;
    };

    /** @brief 刃1本分の状態 */
    struct Blade {
        Vector3 pos = { };
        Vector3 drift = { }; ///< 刃の軸方向への微小移動
        float angle = 0.0f;
        float halfLen = 1.0f;
        float halfWidth = 0.06f;
        float age = 0.0f;
        float life = 0.2f;
        Vector4 coreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 edgeColor = { 0.35f, 0.7f, 1.0f, 1.0f };
    };

    void CreatePipeline();

    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_ { };
    BladeVertex* vertexData_ = nullptr;
    uint32_t vertexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> sceneCB_;
    SceneCB* sceneCBData_ = nullptr;

    std::vector<Blade> blades_;
    std::mt19937 rng_ { std::random_device { }() };
};

} // namespace engine::graphics
