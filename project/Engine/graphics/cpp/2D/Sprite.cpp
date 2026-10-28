/**
 * @file Sprite.cpp
 * @brief スプライト（Sprite）の頂点・定数バッファの用意と描画の実装
 */
#include "Sprite.h"
#include "FrameUploadAllocator.h"
#include "PipelineHelper.h"
#include "EngineConstants.h"
#include "Logger.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <SrvManager.h>

namespace engine::graphics {
namespace {

// スプライトの頂点数と投影範囲
constexpr unsigned int kSpriteVertexCount = 6;
constexpr float kSpriteFarClip = 100.0f;

}

using namespace Microsoft::WRL;

/** @brief スプライトの初期化とバッファの生成 */
void Sprite::Initialize(SpriteCommon* spriteCommon, const std::string& textureFilePath) {
    spriteCommon_ = spriteCommon;
    textureFilePath_ = textureFilePath;

    // テクスチャ読み込み (TextureManagerに任せる)
    TextureManager::GetInstance()->LoadTexture(textureFilePath);

    // 初期サイズをテクスチャサイズに合わせる
    const auto& metadata = TextureManager::GetInstance()->GetMetaData(textureFilePath);
    size_ = { static_cast<float>(metadata.width), static_cast<float>(metadata.height) };
    textureSize_ = size_; // 切り出しサイズも全体に設定


    // 頂点バッファ作成
    vertexResource_ = spriteCommon_->GetDxCommon()->CreateBufferResource(sizeof(VertexDataSprite) * kSpriteVertexCount);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexDataSprite) * kSpriteVertexCount;
    vertexBufferView_.StrideInBytes = sizeof(VertexDataSprite);

    VertexDataSprite* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    // 左下 (0, 1)
    vertexData[0].position = { 0.0f, 1.0f, 0.5f, 1.0f };
    vertexData[0].texcoord = { 0.0f, 1.0f };
    vertexData[0].normal = { 0.0f, 0.0f, -1.0f };
    vertexData[0].tangent = { 1.0f, 0.0f, 0.0f };
    // 左上 (0, 0)
    vertexData[1].position = { 0.0f, 0.0f, 0.5f, 1.0f };
    vertexData[1].texcoord = { 0.0f, 0.0f };
    vertexData[1].normal = { 0.0f, 0.0f, -1.0f };
    vertexData[1].tangent = { 1.0f, 0.0f, 0.0f };
    // 右下 (1, 1)
    vertexData[2].position = { 1.0f, 1.0f, 0.5f, 1.0f };
    vertexData[2].texcoord = { 1.0f, 1.0f };
    vertexData[2].normal = { 0.0f, 0.0f, -1.0f };
    vertexData[2].tangent = { 1.0f, 0.0f, 0.0f };
    // 左上
    vertexData[3] = vertexData[1];
    // 右上 (1, 0)
    vertexData[4].position = { 1.0f, 0.0f, 0.5f, 1.0f };
    vertexData[4].texcoord = { 1.0f, 0.0f };
    vertexData[4].normal = { 0.0f, 0.0f, -1.0f };
    vertexData[4].tangent = { 1.0f, 0.0f, 0.0f };
    // 右下
    vertexData[5] = vertexData[2];

    // マテリアル作成
    material_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    material_.enableLighting = false;
    material_.shadingType = 0;
    material_.useCubemap = 0;
    material_.useTexture = 1;
    material_.uvTransform = MakeIdentity4x4();

    // WVP作成
    transformationMatrix_ = { MakeIdentity4x4(), MakeIdentity4x4(), MakeIdentity4x4(), MakeIdentity4x4() };
}

/** @brief 描画するテクスチャを別の画像に変更する */
void Sprite::SetTexture(const std::string& textureFilePath) {
    textureFilePath_ = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath);
    // UV切り出しサイズを新しいテクスチャの全体サイズにリセット
    const auto& metadata = TextureManager::GetInstance()->GetMetaData(textureFilePath);
    textureSize_ = { static_cast<float>(metadata.width), static_cast<float>(metadata.height) };
}

/** @brief テクスチャサイズと実際の描画サイズを調整・反映する内部処理 */
void Sprite::AdjustTextureSize() {
    // テクスチャのメタデータを取得
    const auto& metadata = TextureManager::GetInstance()->GetMetaData(textureFilePath_);

    // UV座標空間での開始位置とサイズを計算
    Vector2 uvPos = { textureLeftTop_.x / metadata.width, textureLeftTop_.y / metadata.height };
    Vector2 uvSize = { textureSize_.x / metadata.width, textureSize_.y / metadata.height };

    // 反転処理
    if (isFlipX_) {
        uvPos.x += uvSize.x;
        uvSize.x = -uvSize.x;
    }

    if (isFlipY_) {
        uvPos.y += uvSize.y;
        uvSize.y = -uvSize.y;
    }

    // UV Transform行列の作成
    Matrix4x4 uvMatrix = MakeIdentity4x4();
    uvMatrix = Multiply(uvMatrix, MakeScaleMatrix({ uvSize.x, uvSize.y, 1.0f }));
    uvMatrix = Multiply(uvMatrix, MakeTranslateMatrix({ uvPos.x, uvPos.y, 0.0f }));

    material_.uvTransform = uvMatrix;
}

/** @brief 毎フレームの更新処理座標やサイズから行列を再計算する */
void Sprite::Update() {
    AdjustTextureSize();

    // ワールド行列の計算
    Matrix4x4 anchorMatrix = MakeTranslateMatrix({ -anchorPoint_.x, -anchorPoint_.y, 0.0f });

    // スケーリング
    Matrix4x4 scaleMatrix = MakeScaleMatrix({ size_.x, size_.y, 1.0f });

    // 回転 (Z軸回転)
    Matrix4x4 rotateMatrix = MakeRotateZMatrix(rotation_);

    // 平行移動 (指定座標へ)
    Matrix4x4 translateMatrix = MakeTranslateMatrix({ position_.x, position_.y, 0.0f });

    // 行列の合成  Anchor -> Scale -> Rotate -> Translate
    Matrix4x4 worldMatrix = MakeIdentity4x4();
    worldMatrix = Multiply(worldMatrix, anchorMatrix);
    worldMatrix = Multiply(worldMatrix, scaleMatrix);
    worldMatrix = Multiply(worldMatrix, rotateMatrix);
    worldMatrix = Multiply(worldMatrix, translateMatrix);

    // ビュー・プロジェクション
    Matrix4x4 viewMatrix = MakeIdentity4x4();
    Matrix4x4 projectionMatrix = MakeOrthographicMatrix(
        0.0f, 0.0f,
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight),
        0.0f, kSpriteFarClip);

    Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    transformationMatrix_.wvp = worldViewProjectionMatrix;
    transformationMatrix_.world = worldMatrix;
}

/** @brief スプライトを描画するコマンドを積む */
void Sprite::Draw() {
    ID3D12GraphicsCommandList* commandList = spriteCommon_->GetCommandList();

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // マテリアル (色・UV)
    commandList->SetGraphicsRootConstantBufferView(pipelineHelper::spriteRootSlot::kMaterial, spriteCommon_->GetDxCommon()->GetFrameUploadAllocator().Allocate(&material_, sizeof(material_)));
    // 座標変換
    commandList->SetGraphicsRootConstantBufferView(pipelineHelper::spriteRootSlot::kTransform, spriteCommon_->GetDxCommon()->GetFrameUploadAllocator().Allocate(&transformationMatrix_, sizeof(transformationMatrix_)));

    // テクスチャ (TextureManagerからハンドルを取得してセット)
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_);
    commandList->SetGraphicsRootDescriptorTable(pipelineHelper::spriteRootSlot::kTexture, textureSrvHandle);

    if (useExternalTexture_) {
        // 動画など、外部で作成したテクスチャを使う場合
        commandList->SetGraphicsRootDescriptorTable(pipelineHelper::spriteRootSlot::kTexture, SrvManager::GetInstance()->GetGPUDescriptorHandle(textureIndex_));
    } else {
        // 通常のテクスチャを使う場合
        commandList->SetGraphicsRootDescriptorTable(pipelineHelper::spriteRootSlot::kTexture, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_));
    }

    commandList->DrawInstanced(kSpriteVertexCount, 1, 0, 0);
}

} // namespace engine::graphics
