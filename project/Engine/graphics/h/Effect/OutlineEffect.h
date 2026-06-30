/**
 * @file OutlineEffect.h
 * @brief 2パスアウトラインエフェクト
 *
 * 【2パス手法の概要】
 *   Pass1（通常描画）: Object3d::Draw() で普通に描く
 *   Pass2（このクラス）: 法線方向に膨らんだメッシュを前面カリングで描き、
 *                       外側にはみ出た面だけがアウトライン色で見える
 *
 * 【使い方】
 *   // 初期化（一度だけ）
 *   OutlineEffect::GetInstance()->Initialize(dxCommon_);
 *
 *   // 描画時（アウトラインを出したいオブジェクトごとに）
 *   auto* outline = OutlineEffect::GetInstance();
 *   outline->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f }); // 黒縁
 *   outline->SetWidth(0.02f);                        // 幅
 *
 *   outline->BeginOutlinePass();          // アウトライン用 PSO に切り替え
 *   playerObj_.DrawOutline(outline);      // 法線押し出し + 前面カリング描画
 *   modelCommon_->CommonDrawSettings();   // 通常描画 PSO に戻す
 *   playerObj_.Draw();                    // 通常描画
 *
 * 【注意】
 *   BeginOutlinePass() は ModelCommon の PSO を上書きするので、
 *   アウトライン描画後は必ず CommonDrawSettings() で元に戻すこと。
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <wrl/client.h>
namespace engine::graphics {

class OutlineEffect {
public:
    /// @brief シングルトンインスタンスを取得する
    static OutlineEffect* GetInstance()
    {
        static OutlineEffect instance;
        return &instance;
    }

    // ---- 初期化 / 破棄 ----
    /**
     * @brief 初期化。アウトライン用ルートシグネチャ・PSO・定数バッファを生成する
     * @param dxCommon DirectX 共通クラスのポインタ
     */
    void Initialize(engine::DirectXCommon* dxCommon);

    /// @brief GPU リソースを解放する
    void Finalize();

    // ---- 描画パイプライン ----
    /**
     * @brief アウトライン描画パスを開始する
     *        内部でアウトライン用 PSO と root signature をコマンドリストにセットする
     * @note この後 Object3d::DrawOutline(this) を呼ぶこと
     */
    void BeginOutlinePass();

    // ---- パラメータ ----
    /**
     * @brief アウトラインの色を設定する
     * @param color RGBA（アルファで半透明アウトラインも可能）
     */
    void SetColor(const Vector4& color) { if (cbData_) cbData_->color = color; }

    /**
     * @brief アウトラインの太さを設定する
     * @param width クリップ空間単位（推奨 0.01〜0.05）。大きすぎると正面も塗りつぶされる
     */
    void SetWidth(float width) { if (cbData_) cbData_->width = width; }

    /// @brief 現在のアウトライン色を取得する
    Vector4 GetColor() const { return cbData_ ? cbData_->color : Vector4{0, 0, 0, 1}; }

    /// @brief 現在のアウトライン幅を取得する
    float   GetWidth() const { return cbData_ ? cbData_->width : 0.f; }

private:
    OutlineEffect() = default;
    ~OutlineEffect() = default;
    OutlineEffect(const OutlineEffect&) = delete;
    OutlineEffect& operator=(const OutlineEffect&) = delete;

    engine::DirectXCommon* dxCommon_ = nullptr;

    // --- アウトライン専用 PSO ---
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_; ///< アウトライン用ルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_; ///< 前面カリング・法線押し出し用 PSO

    // --- 定数バッファ（シェーダー b0 に対応、VS と PS が共用）---
    // VS では width を使って頂点を押し出し、PS では color を使ってピクセルを塗る
    struct OutlineParams {
        Vector4 color = { 0.0f, 0.0f, 0.0f, 1.0f }; ///< アウトライン色（デフォルト黒）
        float   width = 0.02f;                        ///< 押し出し幅（デフォルト 0.02）
        float   pad[3] = {};                          ///< 16 バイトアライン用パディング
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> cbResource_; ///< 定数バッファ GPU リソース
    OutlineParams*                         cbData_ = nullptr; ///< CPU からの書き込みポインタ（マップ済み）
};

} // namespace engine::graphics
