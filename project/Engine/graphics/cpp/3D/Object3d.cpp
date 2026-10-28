/**
 * @file Object3d.cpp
 * @brief 3Dオブジェクト（Object3d）の行列・マテリアルの更新と描画の実装
 */
#include "Object3d.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "Camera.h"
#include "LightManager.h"
#include "LightingMode.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Object3dCommon.h"
#include "OutlineEffect.h"
#include "ShadowManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <cmath>

namespace engine::graphics {
namespace {

// マテリアルの初期値
constexpr engine::graphics::ObjectMaterialLayout kDefaultMaterial {};

/** @brief 変換行列だけを指定スロットへ束縛し、材質を伴わないパスでモデル形状を描画する */
void DrawGeometryWithTransform(Model* model, ModelCommon* common, D3D12_GPU_VIRTUAL_ADDRESS transform, UINT slot) {
    if (!model) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = common->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(slot, transform);
    model->DrawGeometryOnly(common);
}

}

using namespace Microsoft::WRL;

Camera* Object3d::sCommonCamera = nullptr;
Matrix4x4 Object3d::sCommonLightVP = MakeIdentity4x4();
Object3dCommon* Object3d::sCommonObjectCommon = nullptr;
ShadowManager* Object3d::sCommonShadowManager = nullptr;

/** @brief ライト・シャドウマップのルート引数を再バインドする */
void Object3d::RebindCommonLighting(ID3D12GraphicsCommandList* cmd) {
    // OutlineEffect 等でルートシグネチャを切り替えた後、CommonDrawSettings() で
    // 通常のルートシグネチャに戻しただけではライト/シャドウマップのスロットは未初期化のまま
    // （ルートシグネチャの切り替えは全スロットの束縛を破棄するため）。ここで再バインドする
    pipelineHelper::BindCommonLighting(cmd, sCommonObjectCommon, sCommonShadowManager);
}

/** @brief 全てのObject3dで共通して使用するデフォルトカメラを設定する */
void Object3d::SetCommonCamera(Camera* camera) {
    sCommonCamera = camera;
}

/** @brief 全 Object3d で共通して使うライト空間行列を設定する（毎フレーム GamePlayScene から呼ぶ） */
void Object3d::SetLightViewProjection(const Matrix4x4& lvp) {
    sCommonLightVP = lvp;
}

/** @brief オブジェクトを初期化し、変換行列とマテリアルを既定値にする */
void Object3d::Initialize(ModelCommon* modelCommon) {
    modelCommon_ = modelCommon;
    // 更新はCPU側で行い、描画の直前にGPU用領域へ退避する。
    transformationMatrix_ = { MakeIdentity4x4(), MakeIdentity4x4(), MakeIdentity4x4(), MakeIdentity4x4() };

    material_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    material_.enableLighting = true;
    material_.shadingType = 1; // HalfLambert
    material_.useCubemap = 0;
    material_.useTexture = 1; // デフォルトはテクスチャあり
    material_.uvTransform = MakeIdentity4x4();
    material_.specularColor = { 1.0f, 1.0f, 1.0f }; // 白いハイライト
    material_.shininess = kDefaultMaterial.shininess; // ほどよい光沢
    material_.cameraWorldPos = { 0.0f, 0.0f, 0.0f };
    material_.envMapIntensity = 0.0f;
    material_.rimColor = { 1.0f, 1.0f, 1.0f };
    material_.rimPower = kDefaultMaterial.rimPower;
    material_.rimIntensity = 0.0f;
    material_.enableRim = 0;
    material_.useNormalMap = 0;
    material_.metallic = 0.0f;
    material_.roughness = 0.5f;
}

/** @brief 毎フレームの更新処理トランスフォームからワールド行列とWVP行列を計算する */
void Object3d::Update() {
    Matrix4x4 worldMatrix = useLocalMatrix_
        ? localMatrix_
        : MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);

    Matrix4x4 viewMatrix;
    Matrix4x4 projectionMatrix;

    // カメラがあればカメラから行列をもらう
    Camera* camera = camera_ ? camera_ : sCommonCamera;

    if (camera) {
        viewMatrix = camera->GetViewMatrix();
        projectionMatrix = camera->GetProjectionMatrix();
        // カメラのワールド座標をマテリアルに書き込む（スペキュラ計算に使用）
        material_.cameraWorldPos = camera->GetTranslate();
    }

    // WVP行列の合成
    Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    // 定数バッファへ転送
    transformationMatrix_.wvp = worldViewProjectionMatrix;
    transformationMatrix_.world = worldMatrix;
    transformationMatrix_.worldInverseTranspose = Transpose(Inverse(worldMatrix));
    transformationMatrix_.lightVP = sCommonLightVP;

    // 毎フレームライティングモードをマテリアルに反映させる（ロック済みオブジェクトは除外）
    if (!lockShadingType_) {
        material_.shadingType = LightManager::GetInstance()->GetLightingMode();
    }

    collider_.aabb.min = { transform_.translate.x - 0.5f, transform_.translate.y - 0.5f, transform_.translate.z - 0.5f };
    collider_.aabb.max = { transform_.translate.x + 0.5f, transform_.translate.y + 0.5f, transform_.translate.z + 0.5f };
}

/** @brief 現在の変換行列を、このフレームの描画用領域へコピーしてアドレスを返す */
D3D12_GPU_VIRTUAL_ADDRESS Object3d::UploadTransform() {
    return modelCommon_->GetDxCommon()->GetFrameUploadAllocator().Allocate(&transformationMatrix_, sizeof(transformationMatrix_));
}

/** @brief 現在のマテリアルを、このフレームの描画用領域へコピーしてアドレスを返す */
D3D12_GPU_VIRTUAL_ADDRESS Object3d::UploadMaterial() {
    return modelCommon_->GetDxCommon()->GetFrameUploadAllocator().Allocate(&material_, sizeof(material_));
}

/** @brief シャドウパス用の描画（テクスチャなし・深度のみ） */
void Object3d::DrawShadow() {
    DrawGeometryWithTransform(model_, modelCommon_, UploadTransform(), pipelineHelper::shadowRootSlot::kTransform);
}

/** @brief WVP行列のみをバインドしてジオメトリだけを描画する（マテリアル/テクスチャなし） */
void Object3d::DrawForNormalCapture() {
    // NormalCapture RS  slot0 = VS b0 (TransformationMatrix) - DrawShadow と同じスロット配置
    DrawGeometryWithTransform(model_, modelCommon_, UploadTransform(), pipelineHelper::shadowRootSlot::kTransform);
}

/** @brief アウトラインを描く */
void Object3d::DrawOutline(OutlineEffect* effect) {
    if (!model_ || !effect) {
        return;
    }
    DrawGeometryWithTransform(model_, modelCommon_, UploadTransform(), pipelineHelper::outlineRootSlot::kTransform);
}

/** @brief ファイルパスを指定して、ModelManagerからモデルを検索してセットする */
void Object3d::SetModel(const std::string& filePath) {
    // マネージャーからモデルを検索してセット
    model_ = ModelManager::GetInstance()->FindModel(filePath);
}

/** @brief 法線マップテクスチャを読み込んで設定し、useNormalMapフラグを有効にする */
void Object3d::SetNormalMap(const std::string& filePath) {
    normalMapFilePath_ = filePath;
    TextureManager::GetInstance()->LoadTexture(filePath);
    material_.useNormalMap = 1;
}

/** @brief オブジェクトを描画するコマンドを積む */
void Object3d::Draw() {
    if (!model_) {
        return;
    }

    // ModelCommonからコマンドリストを取得
    ID3D12GraphicsCommandList* commandList = modelCommon_->GetDxCommon()->GetCommandList();

    // Spriteやエフェクトがルートシグネチャを切り替えた後でも、通常3D描画が
    // 使用する平行光源・ポイントライト・シャドウマップを描画直前に保証する
    RebindCommonLighting(commandList);

    // マテリアルと座標変換を設定（描画ごとに別の領域へ写すので、同じフレームで値を変えて何度描いてもよい）
    commandList->SetGraphicsRootConstantBufferView(pipelineHelper::modelRootSlot::kMaterial, UploadMaterial());
    commandList->SetGraphicsRootConstantBufferView(pipelineHelper::modelRootSlot::kTransform, UploadTransform());

    // スロット7: 法線マップ（未設定時は diffuse をダミーとして流用）
    const std::string& normalPath = normalMapFilePath_.empty() ? model_->GetTextureFilePath() : normalMapFilePath_;
    commandList->SetGraphicsRootDescriptorTable(pipelineHelper::modelRootSlot::kNormalMap, TextureManager::GetInstance()->GetSrvHandleGPU(normalPath));

    model_->Draw(modelCommon_);
}

} // namespace engine::graphics
