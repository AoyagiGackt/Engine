/**
 * @file ModelManager.cpp
 * @brief ModelManagerの描画資源とGPU処理の管理に関する具体的な処理を実装するファイル
 */
#include "ModelManager.h"
using namespace engine;
using namespace engine::graphics;

ModelManager* ModelManager::GetInstance()
{
    static ModelManager instance;
    return &instance;
}

void ModelManager::Initialize(ModelCommon* modelCommon)
{
    modelCommon_ = modelCommon;
}

void ModelManager::LoadModel(const std::string& filePath, const std::string& textureFilePath)
{
    if (models_.contains(filePath)) {
        return;
    }

    std::unique_ptr<Model> model = std::make_unique<Model>();
    model->Initialize(modelCommon_, filePath, textureFilePath);

    models_[filePath] = std::move(model);
}

Model* ModelManager::FindModel(const std::string& filePath)
{
    if (models_.contains(filePath)) {
        return models_[filePath].get();
    }

    return nullptr;
}

Model* ModelManager::GetOrLoad(ModelCommon* modelCommon, const std::string& modelFilePath, const std::string& textureFilePath)
{
    const std::string key = modelFilePath + '|' + textureFilePath;
    auto it = sharedModels_.find(key);
    if (it != sharedModels_.end()) {
        return it->second.get();
    }

    auto model = std::make_unique<Model>();
    model->Initialize(modelCommon, modelFilePath, textureFilePath);
    Model* ptr = model.get();
    sharedModels_[key] = std::move(model);
    return ptr;
}

SkinnedModel* ModelManager::GetOrLoadSkinned(engine::DirectXCommon* dxCommon,
    const std::string& gltfFilePath, const std::string& textureFilePath)
{
    const std::string key = gltfFilePath + '|' + textureFilePath;
    auto it = sharedSkinnedModels_.find(key);
    if (it != sharedSkinnedModels_.end()) {
        return it->second.get();
    }

    auto model = std::make_unique<SkinnedModel>();
    model->Initialize(dxCommon, gltfFilePath, textureFilePath);
    SkinnedModel* ptr = model.get();
    sharedSkinnedModels_[key] = std::move(model);
    return ptr;
}

void ModelManager::Finalize()
{
    models_.clear();
    sharedModels_.clear();
    sharedSkinnedModels_.clear();
}