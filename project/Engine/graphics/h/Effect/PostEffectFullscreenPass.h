/**
 * @file PostEffectFullscreenPass.h
 * @brief 全画面ポストエフェクトの共通の窓口と資源（PostEffectFullscreenPass）の宣言
 */
#pragma once
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief 有効状態・描画先・シーン入力と合成処理を提供するポストエフェクトの共通インターフェース
 * @note GetActiveSceneRTVHandle() がこのインターフェース経由で各エフェクトを走査することで、
 * 新しいポストエフェクトを追加してもその関数自体を変更しなくて済むようにする（Strategyパターン）
 */
class IPostEffectSource {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IPostEffectSource() = default;
    /** @brief このポストエフェクトを使うかを返す */
    virtual bool IsEnabled() const = 0;
    /** @brief シーンの描画をこのエフェクトの入力へ開始する */
    virtual void BeginScene() = 0;
    /** @brief シーン描画の入力を確定する */
    virtual void EndScene() = 0;
    /** @brief 入力画像へエフェクトを適用してバックバッファへ合成する */
    virtual void Apply(SrvManager* srvManager) = 0;
    /** @brief シーンを描き込むRTVのハンドルを返す */
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRTVHandle() const = 0;
};

// シーンをオフスクリーンに描画し、フルスクリーンPSで加工してバックバッファへ合成する処理の共通基盤
// GrayscaleEffect/HsvFilterのように定数バッファ1つ+SRV1枚のフルスクリーンパスはこれを継承する
/**
 * @brief オフスクリーンRTVへのシーン描画からバックバッファへのフルスクリーン合成までを担う基底クラス
 */
class PostEffectFullscreenPass : public IPostEffectSource {
public:
    /**
     * @brief オフスクリーンRTV（＋共有深度バッファ）をレンダーターゲットに設定しクリアする
     * @note 3D描画の前に呼ぶ、2回目以降の呼び出しはリソースをRTV状態へ遷移させる
     */
    void BeginScene() override;
    /**
     * @brief オフスクリーンテクスチャをピクセルシェーダーリソース状態へ遷移させる
     * @note 3D描画の後、Apply() の前に呼ぶ
     */
    void EndScene() override;
    /**
     * @brief バックバッファをレンダーターゲットに戻し、オフスクリーン結果をフルスクリーンPSで合成する
     * @param srvManager SRVディスクリプタヒープ管理
     */
    void Apply(SrvManager* srvManager) override;

    /** @brief 有効にするかを設定する */
    void SetEnabled(bool v) {
        enabled_ = v;
    }
    /** @brief 有効かを返す */
    bool IsEnabled() const override {
        return enabled_;
    }

    /** @brief シーンを描き込むRTVのハンドルを返す */
    D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRTVHandle() const override {
        return rtvHandle_;
    }

protected:
    /** @brief 全画面パスの描画資源が未確保の状態を作る */
    PostEffectFullscreenPass() = default;
    /** @brief 所有する資源はメンバのデストラクタで解放する */
    ~PostEffectFullscreenPass() = default;
    PostEffectFullscreenPass(const PostEffectFullscreenPass&) = delete;
    PostEffectFullscreenPass& operator=(const PostEffectFullscreenPass&) = delete;

    // オフスクリーンRTV/SRV・定数バッファ(256byte固定)・ルートシグネチャ/PSOを生成する
    // 戻り値はマップ済み定数バッファの先頭アドレス（派生クラスが自分のParams構造体にキャストして使う）
    void* InitializeCommon(engine::DirectXCommon* dxCommon, SrvManager* srvManager, const wchar_t* psShaderPath);
    void FinalizeCommon();

    engine::DirectXCommon* dxCommon_ = nullptr;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneTexture_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_ = { };
    uint32_t srvIndex_ = UINT32_MAX;
    bool isFirstFrame_ = true;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cbResource_;
    void* cbData_ = nullptr;

    bool enabled_ = false;
};

} // namespace engine::graphics
