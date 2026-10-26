/**
 * @file MaterialManager.cpp
 * @brief マテリアルの管理（MaterialManager）の実装
 */
#include "MaterialManager.h"

namespace engine::graphics {
/** @brief 既定のマテリアルを用意して初期化する */
MaterialManager::MaterialManager()
    : currentMaterialIndex_(0) {
    InitMaterials();
}

/** @brief 現在描画に使用するマテリアルのインデックスを設定する */
void MaterialManager::SetCurrentMaterialIndex(size_t index) {
    currentMaterialIndex_ = index;
}
/** @brief 現在選択されているマテリアルのインデックス番号を取得する */
size_t MaterialManager::GetCurrentMaterialIndex() const {
    return currentMaterialIndex_;
}
/** @brief 現在選択されているマテリアルのデータを値として取得する */
Material MaterialManager::GetCurrentMaterial() const {
    return materials_[currentMaterialIndex_];
}

/** @brief 初期化時にいくつかのデフォルトマテリアル（赤、緑、青など）を生成してリストに登録する */
void MaterialManager::InitMaterials() {
    // 赤、緑、青
    materials_.push_back({ { 1, 0, 0, 1 }, 1, { 0, 0, 0 }, MakeIdentity4x4() });
    materials_.push_back({ { 0, 1, 0, 1 }, 1, { 0, 0, 0 }, MakeIdentity4x4() });
    materials_.push_back({ { 0, 0, 1, 1 }, 1, { 0, 0, 0 }, MakeIdentity4x4() });
    materials_.push_back({ { 1, 1, 1, 1 }, 1, { 0, 0, 0 }, MakeIdentity4x4() });
}

/** @brief MaterialManagerの唯一のインスタンスを取得する */
MaterialManager* MaterialManager::GetInstance() {
    static MaterialManager sInstance;
    return &sInstance;
}

/** @brief マネージャーの終了処理を行う（必要に応じてリソースを解放する） */
void MaterialManager::Finalize() {
}
} // namespace engine::graphics
