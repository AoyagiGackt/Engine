/**
 * @file TextureManager.h
 * @brief テクスチャの読み込み、GPUへの転送、およびSRV（シェーダーリソースビュー）の管理を行うファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "DirectXTex.h"
#include <d3d12.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <wrl.h>
namespace engine::graphics {

/**
 * @brief 複数枚のテクスチャをまとめて読み込む際に使う（TextureManager::LoadTexturesParallel）
 * @note シーン開始時などにテクスチャを何十枚もまとめて読む場面で、
 * デコード＋ミップ生成（CPU依存でGPUに触れない部分）だけをワーカースレッドで並列化するための一時データ
 */
struct DecodedTexture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource; ///< VRAM上に確保したテクスチャリソース（COMMON状態）
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer; ///< ピクセルデータを書き込み済みのアップロードバッファ
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints; ///< サブリソースごとのコピー元レイアウト
    DirectX::TexMetadata metadata { };
    UINT subresourceCount = 0;
};

/**
 * @brief テクスチャを管理するシングルトンクラス
 * @note DirectXTexライブラリを使用して画像を読み込み、SrvManagerと連携して
 * 適切なディスクリプタを割り当てます一度読み込んだパスの画像は内部でキャッシュされます
 */
class TextureManager {
public:
    /**
     * @brief TextureManagerの唯一のインスタンスを取得する
     * @return TextureManager* シングルトンインスタンスへのポインタ
     */
    static TextureManager* GetInstance();

    /**
     * @brief マネージャーの初期化
     * @param dxCommon DirectX基盤のポインタ（デバイス取得などに使用）
     */
    void Initialize(engine::DirectXCommon* dxCommon);

    /**
     * @brief マネージャーの終了処理保持している全テクスチャリソースを解放する
     */
    void Finalize();

    /**
     * @brief 画像ファイルを読み込み、GPUリソースを作成する
     * @param filePath 読み込む画像のパス（例: "Resources/uvChecker.png"）
     * @note すでに読み込み済みのパスが指定された場合は、新たにロードせず既存のデータを参照します
     */
    void LoadTexture(const std::string& filePath);

    /**
     * @brief 複数のテクスチャをまとめて読み込む
     * @param filePaths 読み込む画像のパス一覧（読み込み済みのものは自動でスキップされる）
     * @note デコード＋ミップ生成をワーカースレッドで並列に行うため、LoadTexture() を
     * 枚数分ループするより待ち時間を短縮できる（シーン開始時の一括ロード向け）
     * GPUコマンドの記録・SRV確保は共有状態を触るため呼び出しスレッドで順番に行う
     * LoadTexture() 同様、呼び出し後は FlushUploads() を呼ぶこと
     */
    void LoadTexturesParallel(const std::vector<std::string>& filePaths);

    // RGBA8 生ピクセルデータからテクスチャを作成する（フォント等のコード生成テクスチャ用）
    void LoadFromRawRGBA8(const std::string& name,
        const uint8_t* rgbaData,
        uint32_t width, uint32_t height);

    // 指定キーのテクスチャが登録済みかどうかを返す
    bool HasTexture(const std::string& name) const { return textureDatas_.contains(name); }

    /**
     * @brief 保留中のテクスチャ転送を一括実行し、GPUとの同期を1回だけ行う
     * @note LoadTexture() を複数回呼んだ後、描画開始前に必ず呼び出すこと
     * 内部でコピーキューへの一括Executeと状態遷移（COMMON→PIXEL_SHADER_RESOURCE）を行う
     */
    void FlushUploads();

    /**
     * @brief 読み込み済みテクスチャのファイル更新日時をチェックし、変更があれば再読み込みする
     * @note 毎フレーム呼ぶ想定（開発中のアセット反復用）。SRVインデックスは変えずに中身だけ差し替える
     */
    void CheckHotReload();

    /**
     * @brief 指定したファイルパスに対応するSRVインデックスを取得する
     * @param filePath 取得したいテクスチャのファイルパス
     * @return uint32_t SrvManagerで管理されているSRVのインデックス
     */
    uint32_t GetTextureIndexByFilePath(const std::string& filePath);

    /**
     * @brief 指定したテクスチャのGPUハンドルを取得する
     * @param filePath 取得したいテクスチャのファイルパス
     * @return D3D12_GPU_DESCRIPTOR_HANDLE 描画コマンドに渡すためのGPUハンドル
     * @note 事前に LoadTexture() で読み込まれている必要あり
     */
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& filePath);

    /**
     * @brief 指定したテクスチャのメタデータ（幅、高さ、形式など）を取得する
     * @param filePath 取得したいテクスチャのファイルパス
     * @return const DirectX::TexMetadata& DirectXTexで定義されたメタデータ構造体
     */
    const DirectX::TexMetadata& GetMetaData(const std::string& filePath);

private:
    /**
     * @brief 読み込み済みのテクスチャ1つ分のデータを保持する構造体
     */
    struct TextureData {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource; ///< GPU上のテクスチャリソース
        uint32_t srvIndex; ///< デスクリプタヒープ上のインデックス
        DirectX::TexMetadata metadata; ///< テクスチャのメタデータ（幅、高さ、形式等）
        /** @brief 最終更新日時（ファイルからの読み込みでない場合は既定値のまま＝ホットリロード対象外） */
        std::filesystem::file_time_type lastWriteTime { };
    };

    /**
     * @brief 画像ファイルを読み込み、GPUリソースを作成してコピーコマンドを積む（SRVはまだ作らない）
     * @note LoadTexture()（新規SRV確保）とCheckHotReload()（既存SRVの差し替え）の両方から使う共通処理
     */
    Microsoft::WRL::ComPtr<ID3D12Resource> LoadAndQueueUpload(
        const std::string& filePath, DirectX::TexMetadata& outMetadata);

    /**
     * @brief 画像ファイルをデコードしGPUリソース／アップロードバッファを作成する（コピーコマンドはまだ記録しない）
     * @note GPUに触れるのは ID3D12Device 経由のリソース作成のみで、これはスレッドセーフなためワーカースレッドから呼べる
     * WICを使うためスレッド内でCOMを初期化する
     */
    DecodedTexture DecodeTexture(const std::string& filePath);

    /**
     * @brief DecodeTexture() の結果からコピーコマンドを記録し、保留リストに積む
     * @note copyCmdList_ 等の共有状態を触るため、呼び出しスレッドで直列に実行すること
     */
    void QueueUpload(const DecodedTexture& decoded);

    /** @brief DirectX基盤のポインタ */
    engine::DirectXCommon* dxCommon_ = nullptr;

    /** @brief 読み込み済みテクスチャの管理用マップ（キー ファイルパス） */
    std::map<std::string, TextureData> textureDatas_;


    // コピーキュー関連（初期化時に作成し再利用する）


    /** @brief テクスチャ転送専用のコピーコマンドキュー */
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> copyQueue_;
    /** @brief コピーキュー用コマンドアロケータ（再利用） */
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copyAllocator_;
    /** @brief コピーキュー用コマンドリスト（Open 状態を維持し LoadTexture で記録） */
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyCmdList_;
    /** @brief コピーキュー完了待機用フェンス（再利用） */
    Microsoft::WRL::ComPtr<ID3D12Fence> copyFence_;
    UINT64 copyFenceValue_ = 0;
    HANDLE copyFenceEvent_ = nullptr;


    // バリア遷移用（COMMON → PIXEL_SHADER_RESOURCE、グラフィックスキュー）

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> transAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> transCmdList_;


    // バッチ転送の保留リスト

    /** @brief GPU がコピーを終えるまで保持するアップロードバッファ */
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> pendingUploadBuffers_;
    /** @brief COMMON → PIXEL_SHADER_RESOURCE 遷移待ちのリソース */
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> pendingResources_;
    /** @brief FlushUploads() が必要かを示すフラグ */
    bool hasPendingCopies_ = false;
};

} // namespace engine::graphics
