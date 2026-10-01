/**
 * @file ModelManager.h
 * @brief 3Dモデルデータの読み込み・保持を一元管理するファイル
 */
#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "SkinnedModel.h"
#include <map>
#include <memory>
#include <string>
namespace engine::graphics {
/**
 * @brief 3Dモデルを管理するシングルトンクラス
 * @note 同じファイルパスのモデルが複数回要求された場合でも、
 * 一度だけメモリに読み込んで共有する仕組み（Flyweightパターン）になっており、
 * メモリ使用量とロード時間を最適化します
 */
class ModelManager {
public:
    /**
     * @brief ModelManagerの唯一のインスタンスを取得する
     * @return ModelManager* シングルトンインスタンスへのポインタ
     */
    static ModelManager* GetInstance();

    /**
     * @brief マネージャーの初期化モデル生成に必要な共通設定を登録する
     * @param modelCommon モデルの共通描画設定を持つオブジェクトのポインタ
     */
    void Initialize(ModelCommon* modelCommon);

    /**
     * @brief 指定したファイルパスのモデルを読み込み、メモリに保持する
     * @param filePath 読み込むOBJファイルのパス（例: "Resources/model.obj"）
     * @param textureFilePath モデルに適用するテクスチャのパス（例: "Resources/texture.png"）
     * @note すでに同じ filePath のモデルが読み込まれている場合は、新規ロードをスキップします
     */
    void LoadModel(const std::string& filePath, const std::string& textureFilePath);

    /**
     * @brief 読み込み済みのモデルを検索して取得する
     * @param filePath 取得したいモデルのファイルパス
     * @return Model* 見つかったモデルのポインタ（未ロードの場合は nullptr を返す）
     */
    Model* FindModel(const std::string& filePath);

    /**
     * @brief モデル+テクスチャの組み合わせで読み込み済みなら共有し、無ければ読み込んで登録する
     * @param modelCommon モデル生成に使う共通描画設定
     * @param modelFilePath 読み込むモデルファイルのパス（OBJ/glTF）
     * @param textureFilePath 貼り付けるテクスチャのパス
     * @note 同じ敵/配置物を複数体・複数レベルで使い回す際に、頂点解析とGPUバッファ生成を1回だけで済ませる
     */
    Model* GetOrLoad(ModelCommon* modelCommon, const std::string& modelFilePath, const std::string& textureFilePath);

    /**
     * @brief GetOrLoad()のSkinnedModel版（アニメーション付きモデルの静的メッシュ・ボーン情報を共有する）
     * @note アニメーション再生位置やスケルトンの現在姿勢はSkinnedObject3d側が個体ごとに持つため、
     * ここで共有するSkinnedModelは不変のメッシュ・逆バインド行列だけで安全に使い回せる
     */
    SkinnedModel* GetOrLoadSkinned(engine::DirectXCommon* dxCommon,
        const std::string& gltfFilePath, const std::string& textureFilePath);

    /**
     * @brief マネージャーの終了処理保持しているすべてのモデルデータを破棄する
     */
    void Finalize();

private:
    // シングルトンパターンのためのコンストラクタ・コピー禁止設定
    ModelManager() = default;
    ~ModelManager() = default;
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    /** @brief モデル生成時に渡す共通描画設定のポインタ */
    ModelCommon* modelCommon_ = nullptr;

    /** * @brief 読み込み済みモデルのリスト（連想配列）
     * @note キーにファイルパス（std::string）、値にモデルの実体（std::unique_ptr<Model>）を保持
     */
    std::map<std::string, std::unique_ptr<Model>> models_;

    /** @brief GetOrLoad()用のキャッシュ（キー モデルパス+'|'+テクスチャパス） */
    std::map<std::string, std::unique_ptr<Model>> sharedModels_;
    /** @brief GetOrLoadSkinned()用のキャッシュ（キー モデルパス+'|'+テクスチャパス） */
    std::map<std::string, std::unique_ptr<SkinnedModel>> sharedSkinnedModels_;
};

} // namespace engine::graphics
