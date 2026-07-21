/**
 * @file SkinCommon.h
 * @brief SkinCommonの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
#include "BlendMode.h"
#include "DirectXCommon.h"
#include <wrl/client.h>
namespace engine::graphics {

// スキニング（ボーンアニメーション）専用の PSO / Root Signature を管理するクラス
// ModelCommon と同じスロット 0-5 を保ちつつ、スロット 6 (VS b1) にスキニングパレットを追加
/**
 * @brief SkinCommon に関する型を提供する
 * @details SkinCommon が扱うデータと操作の責務をまとめる
 */
class SkinCommon {
public:
    /**
     * @brief Initialize に対応する処理を開始する
     * @param dxCommon 処理に使用する値
     * @return なし
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /**
     * @brief CommonDrawSettings に対応する処理を実行する
     * @param blendMode 処理に使用する値
     * @return なし
     */
    void CommonDrawSettings(BlendMode blendMode = BlendMode::Alpha);

    engine::DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStates_[static_cast<size_t>(BlendMode::Count)];
};

} // namespace engine::graphics
