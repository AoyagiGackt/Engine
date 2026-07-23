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
 * @brief スキニング専用のルートシグネチャと、ブレンドモードごとの PSO 一式を保持・管理するクラス
 */
class SkinCommon {
public:
    /**
     * @brief ルートシグネチャと SkinnedVS/Object3dPS を使った PSO 一式（各ブレンドモード分）を構築する
     * @param dxCommon デバイス取得・シェーダーコンパイルに使う DirectX 基盤
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /**
     * @brief 指定ブレンドモードのルートシグネチャ・PSO・プリミティブトポロジをコマンドリストにセットする
     * @param blendMode 使用するブレンドモード（既定はアルファブレンド）
     */
    void CommonDrawSettings(BlendMode blendMode = BlendMode::Alpha);

    engine::DirectXCommon* GetDxCommon() const { return dxCommon_; }

private:
    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStates_[static_cast<size_t>(BlendMode::Count)];
};

} // namespace engine::graphics
