/**
 * @file SrvManager.h
 * @brief SRV（シェーダーリソースビュー）用のデスクリプタヒープとそのインデックスを一括管理するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include <utility>
#include <vector>
#include <wrl/client.h>
namespace engine::graphics {

/**
 * @brief SRV（テクスチャなどのリソース）のデスクリプタを管理するシングルトンクラス
 * @note 全てのテクスチャやインスタンシング用のリソースは、このマネージャーを通じて
 * GPUが参照するためのインデックス（SRV）を割り当てられます
 */
class SrvManager {
public:
    /**
     * @brief SrvManagerの唯一のインスタンスを取得する
     * @return SrvManager* シングルトンインスタンスへのポインタ
     */
    static SrvManager* GetInstance();

    /**
     * @brief マネージャーの初期化デスクリプタヒープの生成とサイズ設定を行う
     * @param dxCommon DirectX基盤のポインタ（デバイス取得などに使用）
     */
    void Initialize(engine::DirectXCommon* dxCommon);

    /**
     * @brief 描画前準備コマンドリストにデスクリプタヒープをセットする
     * @note 毎フレームの描画処理の最初（PreDrawなど）で一度だけ呼び出すこと
     */
    void PreDraw();

    /**
     * @brief 未使用のデスクリプタインデックスを1つ確保する
     * @return uint32_t 確保した場所のインデックス番号
     * @note Free()済みのインデックスがあればそれを再利用し、無ければ新規に確保する
     * 確保できる最大数（kMaxSRVCount）を超えるとアサートが発生します!
     */
    uint32_t Allocate();

    /**
     * @brief 使い終わったデスクリプタインデックスを解放し、以後のAllocate()で再利用可能にする
     * @param srvIndex 解放するインデックス（Allocate()で確保したもの）
     * @note GPUが当該フレームの描画コマンドを実行し終えるまでは再利用させないよう、
     * 数フレーム分の遅延を挟んでからフリーリストに戻す
     */
    void Free(uint32_t srvIndex);

    /**
     * @brief 指定したインデックスの場所に、テクスチャ2D用のSRVを生成する
     * @param srvIndex Allocate()で確保したインデックス
     * @param pResource SRVを紐づけるGPUリソース（テクスチャバッファ）
     * @param Format テクスチャのフォーマット（DXGI_FORMAT）
     * @param MipLevels ミップマップのレベル数
     */
    void CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels);

    /**
     * @brief 指定したインデックスの場所に、キューブマップ用のSRVを生成する
     * @param srvIndex Allocate()で確保したインデックス
     * @param pResource SRVを紐づけるGPUリソース（キューブマップテクスチャ）
     * @param Format テクスチャのフォーマット（DXGI_FORMAT）
     * @param MipLevels ミップマップのレベル数
     */
    void CreateSRVforTextureCube(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels);

    /**
     * @brief 深度テクスチャ（R24G8_TYPELESS）用のSRVを生成する
     * @param srvIndex Allocate()で確保したインデックス
     * @param pResource 深度ステンシルリソース（TYPELESS形式で作成済みのもの）
     */
    void CreateSRVforDepthTexture(uint32_t srvIndex, ID3D12Resource* pResource);

    /**
     * @brief マネージャーの終了処理デスクリプタヒープを解放する
     */
    void Finalize();

    /**
     * @brief 管理しているSRV用デスクリプタヒープ本体を取得する
     * @return ID3D12DescriptorHeap* デスクリプタヒープのポインタ
     */
    ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return descriptorHeap_.Get(); }

    /**
     * @brief 指定したインデックスに対応するCPU側のハンドルを取得する
     * @param index インデックス番号
     * @return D3D12_CPU_DESCRIPTOR_HANDLE CPU用デスクリプタハンドル
     */
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);

    /**
     * @brief 指定したインデックスに対応するGPU側のハンドルを取得する
     * @param index インデックス番号
     * @return D3D12_GPU_DESCRIPTOR_HANDLE GPU用デスクリプタハンドル（描画コマンドで使用）
     */
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

private:
    SrvManager() = default;
    ~SrvManager() = default;
    SrvManager(const SrvManager&) = delete;
    SrvManager& operator=(const SrvManager&) = delete;

    /** @brief DirectX基盤のポインタ */
    engine::DirectXCommon* dxCommon_ = nullptr;

    /** @brief 最大SRV確保数（テクスチャ/パーティクル/インスタンシング等が増えても余裕を持たせる） */
    static const uint32_t kMaxSRVCount = 4096;

    /** @brief デスクリプタヒープの実体 */
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;

    /** @brief デスクリプタ1つあたりのメモリサイズ（ハードウェアによって異なる） */
    uint32_t descriptorSize_ = 0;

    /** @brief 現在どこまでインデックスを使用しているかのカウント */
    uint32_t useIndex_ = 0;

    /** @brief 遅延解放の安全マージン（このフレーム数だけ経過してから再利用を許可する） */
    static constexpr uint64_t kFreeDelayFrames = 3;

    /** @brief 即座に再利用可能なインデックスのリスト */
    std::vector<uint32_t> freeList_;

    /** @brief Free()されたが、まだ再利用不可なインデックス（解放時のフレーム番号付き） */
    std::vector<std::pair<uint64_t, uint32_t>> pendingFree_;

    /** @brief PreDraw()の呼び出し回数（フレームカウンタ代わり） */
    uint64_t frameCount_ = 0;
};

} // namespace engine::graphics
