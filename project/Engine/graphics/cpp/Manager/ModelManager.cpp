/**
 * @file ModelManager.cpp
 * @brief 読み込んだモデルの管理（ModelManager）の実装
 */
#include "ModelManager.h"
#include <utility>

namespace engine::graphics {
namespace {
/** @brief 登録済み実体を再利用し、未登録なら生成してキャッシュへ所有権を移す */
template<class Cache, class Create>
auto FindOrLoadSharedModel(Cache& cache, const std::string& key, const Create& create) {
    const auto found = cache.find(key);
    if (found != cache.end()) {
        return found->second.get();
    }
    auto model = create();
    auto* result = model.get();
    cache[key] = std::move(model);
    return result;
}
}
/** @brief ModelManagerの唯一のインスタンスを取得する */
ModelManager* ModelManager::GetInstance() {
    static ModelManager sInstance;
    return &sInstance;
}

/** @brief マネージャーの初期化モデル生成に必要な共通設定を登録する */
void ModelManager::Initialize(ModelCommon* modelCommon) {
    modelCommon_ = modelCommon;
}

/** @brief 指定したファイルパスのモデルを読み込み、メモリに保持する */
void ModelManager::LoadModel(const std::string& filePath, const std::string& textureFilePath) {
    if (models_.contains(filePath)) {
        return;
    }

    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(modelCommon_, filePath, textureFilePath);

    models_[filePath] = std::move(model);
}

/** @brief 読み込み済みのモデルを検索して取得する */
Model* ModelManager::FindModel(const std::string& filePath) {
    if (models_.contains(filePath)) {
        return models_[filePath].get();
    }

    return nullptr;
}

/** @brief モデル+テクスチャの組み合わせで読み込み済みなら共有し、無ければ読み込んで登録する */
Model* ModelManager::GetOrLoad(ModelCommon* modelCommon, const std::string& modelFilePath, const std::string& textureFilePath) {
    const std::string key = modelFilePath + '|' + textureFilePath;
    return FindOrLoadSharedModel(sharedModels_, key, [&] {
        auto model = std::make_unique<Model>();
        model->Initialize(modelCommon, modelFilePath, textureFilePath);
        return model;
    });
}

/** @brief GetOrLoad()のSkinnedModel版（アニメーション付きモデルの静的メッシュ・ボーン情報を共有する） */
SkinnedModel* ModelManager::GetOrLoadSkinned(engine::DirectXCommon* dxCommon,
    const std::string& gltfFilePath, const std::string& textureFilePath) {
    const std::string key = gltfFilePath + '|' + textureFilePath;
    return FindOrLoadSharedModel(sharedSkinnedModels_, key, [&] {
        auto model = std::make_unique<SkinnedModel>();
        model->Initialize(dxCommon, gltfFilePath, textureFilePath);
        return model;
    });
}

/** @brief マネージャーの終了処理保持しているすべてのモデルデータを破棄する */
void ModelManager::Finalize() {
    models_.clear();
    sharedModels_.clear();
    sharedSkinnedModels_.clear();
}
} // namespace engine::graphics
