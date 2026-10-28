/**
 * @file FrameUploadAllocator.h
 * @brief 描画ごとの定数データを、フレーム別のアップロード領域へ書き込むクラスのファイル
 */
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstddef>
#include <vector>
namespace engine {
class DirectXCommon;

/**
 * @brief 描画ごとの定数データを、フレーム別のアップロード領域へ書き込むクラス
 * @note GPU が前のフレームで読んでいる領域を上書きしないよう、バックバッファの数だけ領域を持つ。
 *       描画のたびに別の場所へ書くので、同じオブジェクトを値を変えて何度描いても、それぞれの値が残る
 */
class FrameUploadAllocator {
public:
    /**
     * @brief 使うデバイスとフレーム数を設定する（ページは最初の確保時に作る）
     * @param dxCommon バッファの生成に使う DirectX 基盤
     * @param frameCount 同時に処理中になりうるフレーム数
     */
    void Initialize(DirectXCommon* dxCommon, UINT frameCount);

    /**
     * @brief これから記録するフレームの領域を、先頭から使い直す
     * @param frameIndex バックバッファの番号
     * @note GPU がこの番号の前回分を読み終えたことを確かめてから呼ぶこと
     */
    void BeginFrame(UINT frameIndex);

    /**
     * @brief データを今のフレームの領域へコピーし、その GPU アドレスを返す
     * @param data コピーするデータ
     * @param size データのバイト数
     * @return 定数バッファビューに渡せる GPU 仮想アドレス（256 バイト境界）
     */
    D3D12_GPU_VIRTUAL_ADDRESS Allocate(const void* data, size_t size);

private:
    /** @brief 1ページの大きさ（足りなければページを追加する） */
    static constexpr size_t kPageBytes = static_cast<size_t>(1) << 20;

    /** @brief Map したままのアップロードバッファ1枚と、その使用量 */
    struct Page {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource; ///< アップロードバッファ
        std::byte* data = nullptr; ///< Map 済みの書き込み先
        size_t capacity = 0; ///< バッファの大きさ
        size_t used = 0; ///< このフレームで使ったバイト数
    };

    /** @brief 指定サイズが入るページを今のフレームの中から探し、無ければ追加する */
    Page& FindOrAddPage(size_t alignedSize);

    /** @brief バッファの生成に使う DirectX 基盤 */
    DirectXCommon* dxCommon_ = nullptr;
    /** @brief フレームごとのページ一覧（ページはフレーム途中で解放しない） */
    std::vector<std::vector<Page>> frames_;
    /** @brief 今記録しているフレームの番号 */
    UINT frameIndex_ = 0;
};
} // namespace engine
