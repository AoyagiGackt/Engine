/**
 * @file Object3dCommon.h
 * @brief 3Dオブジェクト共通のライティング設定を管理するファイル
 *
 * - DirectionalLight の動的更新（時刻に応じたライト方向・色・アンビエント）
 * - PointLight の追加・管理（最大 kMaxPointLights 個）
 * - シャドウマップは ShadowManager クラスが担当
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <wrl/client.h>
#include <cstdint>

/**
 * @brief 3Dオブジェクト全体で共有されるライティング設定を管理するクラス
 */
class Object3dCommon {
public:
    // =============================================
    // ポイントライト定義
    // =============================================

    /// シェーダー側の PointLight[8] と合わせること
    static constexpr UINT kMaxPointLights = 8;

    /**
     * @brief ポイントライト 1 個分のデータ
     * HLSL の PointLight 構造体と完全一致させること（48 バイト）
     */
    struct PointLight {
        Vector3 position;   ///< ライトのワールド座標
        float   radius;     ///< 減衰距離（radius を超えると完全に暗くなる）
        Vector4 color;      ///< ライトの色（RGB のみ使用、A は未使用）
        float   intensity;  ///< 明るさの倍率
        float   pad[3];     ///< HLSL 16 バイトアライン用パディング
    };
    static_assert(sizeof(PointLight) == 48, "PointLight must be 48 bytes for HLSL alignment");

    // =============================================
    // 初期化・ライティング更新
    // =============================================

    /** @brief 初期化（ライト定数バッファの作成） */
    void Initialize(DirectXCommon* dxCommon);

    /**
     * @brief 平行光源をスロット 3 に、ポイントライト群をスロット 6 にセットする
     * @note CommonDrawSettings() の後、描画前に毎フレーム呼ぶ
     * @param commandList 描画コマンドリスト
     */
    void SetDefaultLight(ID3D12GraphicsCommandList* commandList);

    /**
     * @brief ポイントライトを指定スロットにセットする
     * @param commandList 描画コマンドリスト
     * @param rootParamSlot ルートシグネチャのスロット番号
     *        ModelCommon パイプライン → 6
     *        SkinCommon  パイプライン → 7
     */
    void SetPointLights(ID3D12GraphicsCommandList* commandList, UINT rootParamSlot);

    /**
     * @brief 時刻に合わせてライト方向・色・アンビエントを更新する
     * @param timeRatio ゲーム内時刻の進行率（0.0=18:00 〜 1.0=翌6:00）
     */
    void UpdateLight(float timeRatio);

    /** @brief 現在のライト方向を取得（ShadowManager に渡す用） */
    Vector3 GetLightDirection() const;

    // =============================================
    // ポイントライト管理
    // =============================================

    /**
     * @brief 全ポイントライトをクリアする
     * @note フレーム先頭で呼び、その後 AddPointLight で必要分を追加するのが基本パターン
     */
    void ClearPointLights() { pointLightCount_ = 0; }

    /**
     * @brief ポイントライトを追加する
     * @param light 追加するポイントライトデータ
     * @note 上限（kMaxPointLights = 8）を超えると無視されます
     */
    void AddPointLight(const PointLight& light) {
        if (pointLightCount_ < kMaxPointLights && pointLightData_) {
            pointLightData_->lights[pointLightCount_++] = light;
        }
    }

    /** @brief 現在登録されているポイントライト数を返す */
    UINT GetPointLightCount() const { return pointLightCount_; }

private:
    DirectXCommon* dxCommon_ = nullptr;

    // --- 平行光源 ---
    struct DirectionalLight {
        Vector4 color;
        Vector3 direction;
        float   intensity;
        Vector3 ambientColor;
        float   ambientIntensity;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> lightResource_;
    DirectionalLight* lightData_ = nullptr; ///< Map 済みポインタ

    // --- ポイントライト群 ---
    struct PointLightBuffer {
        uint32_t   count;
        float      pad[3];
        PointLight lights[kMaxPointLights];
    };
    static_assert(sizeof(PointLightBuffer) == 16 + 48 * 8,
                  "PointLightBuffer size mismatch with HLSL");

    Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
    PointLightBuffer* pointLightData_  = nullptr; ///< Map 済みポインタ
    UINT              pointLightCount_ = 0;        ///< AddPointLight で増える現在のライト数
};
