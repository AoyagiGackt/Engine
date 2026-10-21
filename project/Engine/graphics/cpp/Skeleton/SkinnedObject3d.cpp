/**
 * @file SkinnedObject3d.cpp
 * @brief アニメーションするスキンメッシュのオブジェクト（SkinnedObject3d）の実装
 */
#include "SkinnedObject3d.h"
#include "PipelineHelper.h"
#include "GameConstants.h"
#include "ResourceAlignment.h"
#include "Camera.h"
#include "LightManager.h"
#include "Logger.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "OutlineEffect.h"
#include "ShadowManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace engine::graphics {
namespace {

// アニメーション更新のフレームレート
constexpr float kAnimationFramesPerSecond = GameConstants::kTargetFps;

}
using namespace engine::game;

using namespace Microsoft::WRL;

Camera* SkinnedObject3d::commonCamera_ = nullptr;
Matrix4x4 SkinnedObject3d::commonLightVP_ = MakeIdentity4x4();
Object3dCommon* SkinnedObject3d::commonObjectCommon_ = nullptr;
ShadowManager* SkinnedObject3d::commonShadowManager_ = nullptr;
ModelCommon* SkinnedObject3d::commonModelCommon_ = nullptr;

/** @brief 全インスタンス共通で参照するカメラを設定する */
void SkinnedObject3d::SetCommonCamera(Camera* camera) {
    commonCamera_ = camera;
}
/** @brief 全インスタンス共通のライト視点ビュープロジェクション行列を設定する（シャドウ判定用） */
void SkinnedObject3d::SetLightViewProjection(const Matrix4x4& lvp) {
    commonLightVP_ = lvp;
}
/** @brief 全インスタンス共通の Object3dCommon（ライト・共有描画設定）を設定する */
void SkinnedObject3d::SetCommonObjectCommon(Object3dCommon* oc) {
    commonObjectCommon_ = oc;
}
/** @brief 全インスタンス共通の ShadowManager を設定する */
void SkinnedObject3d::SetCommonShadowManager(ShadowManager* sm) {
    commonShadowManager_ = sm;
}
/** @brief 全インスタンス共通の ModelCommon（CS スキニング済みパスの PSO）を設定する */
void SkinnedObject3d::SetCommonModelCommon(ModelCommon* mc) {
    commonModelCommon_ = mc;
}

/** @brief スキンモデルの描画共通設定を作り、全インスタンス共通の ModelCommon とカメラ（通常モデルと同じもの）を設定する */
std::unique_ptr<SkinCommon> SkinnedObject3d::CreateSharedCommon(ModelCommon* modelCommon)
{
    auto skinCommon = std::make_unique<SkinCommon>();
    skinCommon->Initialize(modelCommon->GetDxCommon());
    SetCommonModelCommon(modelCommon);
    SetCommonCamera(Object3d::GetCommonCamera());
    return skinCommon;
}

/** @brief 描画するモデルを設定する */
void SkinnedObject3d::SetModel(SkinnedModel* model)
{
    model_ = model;
    InitializeSkinCS();
}

/** @brief model_ と skinCommon_ が両方設定済みなら SkinCS を初期化する（未設定時は何もしない） */
void SkinnedObject3d::InitializeSkinCS()
{
    if (!model_ || !skinCommon_)
    {
        return;
    }

    skinCS_.Initialize(skinCommon_->GetDxCommon(),
        model_->GetVertexResource(),
        model_->GetVertexCount());
    skinCSReady_ = true;
}

/** @brief 変換行列・マテリアル・スキニングパレットの定数バッファを確保し既定値で初期化する */
void SkinnedObject3d::Initialize(SkinCommon* skinCommon)
{
    skinCommon_ = skinCommon;
    ID3D12Device* device = skinCommon_->GetDxCommon()->GetDevice();

    // CBV は 256 バイトアライン必須なので切り上げる
    transformCB_ = PipelineHelper::CreateUploadBuffer(device, AlignConstantBufferSize(sizeof(TransformationMatrix)));
    transformCB_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));

    materialCB_ = PipelineHelper::CreateUploadBuffer(device, sizeof(ObjectMaterialLayout));
    materialCB_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    *materialData_ = ObjectMaterialLayout { }; // 既定値（ライティング有効・リム無効など）
    materialData_->uvTransform = MakeIdentity4x4();

    // パレット: 256 バイト境界に合わせる（128 * 64 = 8192 はすでに倍数）
    paletteCB_ = PipelineHelper::CreateUploadBuffer(device, sizeof(Matrix4x4) * kMaxJoints);
    paletteCB_->Map(0, nullptr, reinterpret_cast<void**>(&paletteData_));
    for (int i = 0; i < kMaxJoints; ++i)
    {
        paletteData_[i] = MakeIdentity4x4();
    }

#ifdef USE_IMGUI
    skeletonDebugRenderer_ = std::make_unique<SkeletonOverlayRenderer>();
    skeletonDebugRenderer_->Initialize(skinCommon_->GetDxCommon());
#endif
}

/** @brief 前回レイヤーで書き換えたボーンの回転を元に戻す */
void SkinnedObject3d::RestoreLayerRotations()
{
    // 同じ骨に複数回掛けた場合でも最初の回転へ戻るよう、記録と逆順に戻す
    for (auto it = appliedLayerRotations_.rbegin(); it != appliedLayerRotations_.rend(); ++it)
    {
        if (it->jointIndex >= 0 && it->jointIndex < static_cast<int32_t>(skeleton_.GetJointCount()))
        {
            skeleton_.SetJointRotation(it->jointIndex, it->rotate);
        }
    }
    appliedLayerRotations_.clear();
}

/** @brief レイヤーの差分回転をボーンへ掛け、戻すための元の回転を記録する */
void SkinnedObject3d::ApplyPoseLayer()
{
    if (poseLayer_ == nullptr)
    {
        return;
    }
    layerScratch_.clear();
    poseLayer_->Collect(skeleton_, layerScratch_);

    for (const PoseLayerRotation& layer : layerScratch_)
    {
        if (layer.jointIndex < 0 || layer.jointIndex >= static_cast<int32_t>(skeleton_.GetJointCount()))
        {
            continue;
        }
        const Joint& joint = skeleton_.GetJoint(layer.jointIndex);
        appliedLayerRotations_.push_back({ layer.jointIndex, joint.transform.rotate });
        // 差分はボーンのローカル軸で先に掛ける（ベースの姿勢を保ったまま、その骨だけを捻る）
        skeleton_.SetJointRotation(layer.jointIndex, Normalize(Multiply(joint.transform.rotate, layer.delta)));
    }
}

/** @brief アニメーションを適用してスケルトンとスキニングパレット、ワールド/WVP行列を更新する */
void SkinnedObject3d::Update()
{
    if (!model_)
    {
        return;
    }

    // 前回レイヤーで書き換えた回転を戻してからベースを評価する
    // （ベースアニメにキーの無いボーンは上書きされないため、戻さないと差分が毎回積み重なる）
    RestoreLayerRotations();

    // アニメーション時刻を進める
    if (animation_.duration > 0.0f)
    {
        animTime_ = std::fmod(animTime_ + animSpeed_ / kAnimationFramesPerSecond, animation_.duration);
    }

    // スケルトン各ジョイントにアニメーションを適用
    for (size_t jointIndex = 0; jointIndex < skeleton_.GetJointCount(); ++jointIndex)
    {
        const Joint& joint = skeleton_.GetJoint(jointIndex);
        auto it = animation_.nodeAnimations.find(joint.name);
        if (it != animation_.nodeAnimations.end())
        {
            const NodeAnimation& na = it->second;
            QuaternionTransform transform = joint.transform;
            if (!na.translate.keyframes.empty())
            {
                transform.translate = CalculateValue(na.translate, animTime_);
            }

            if (!na.rotate.keyframes.empty())
            {
                transform.rotate = CalculateValue(na.rotate, animTime_);
            }

            if (!na.scale.keyframes.empty())
            {
                transform.scale = CalculateValue(na.scale, animTime_);
            }
            skeleton_.SetJointTransform(joint.index, transform);
        }
    }

    ApplyPoseLayer();
    skeleton_.Update();

    // スキニングパレット = inverseBindMatrix * skeletonSpaceMatrix
    const auto& boneNames = model_->GetBoneNames();
    const auto& ibms = model_->GetInverseBindMatrices();
    static std::unordered_set<std::string> warnedBones;
    for (uint32_t i = 0; i < static_cast<uint32_t>(boneNames.size()) && i < kMaxJoints; ++i)
    {
        const auto jointIndex = skeleton_.FindJointIndex(boneNames[i]);
        if (jointIndex)
        {
            paletteData_[i] = Multiply(ibms[i], skeleton_.GetJoint(*jointIndex).skeletonSpaceMatrix);
        }
        else
        {
            // ボーン名がスケルトンに存在しない場合、パレットは単位行列のまま（初期化済み）
            if (warnedBones.insert(boneNames[i]).second)
            {
                Logger::LogWarning("Bone '" + boneNames[i] + "' not found in skeleton. Palette index " + std::to_string(i) + " will remain identity.");
            }
        }
    }

    // ワールド行列 / WVP を計算
    worldMatrix_ = MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);
    Matrix4x4 world = worldMatrix_;
    Matrix4x4 vp = MakeIdentity4x4();
    if (commonCamera_)
    {
        vp = Multiply(commonCamera_->GetViewMatrix(), commonCamera_->GetProjectionMatrix());
        materialData_->cameraWorldPos = commonCamera_->GetTranslate();
    }
    transformData_->WVP = Multiply(world, vp);
    transformData_->World = world;
    transformData_->WorldInverseTranspose = Transpose(Inverse(world));
    transformData_->LightVP = commonLightVP_;

    materialData_->shadingType = LightManager::GetInstance()->GetLightingMode();
}

/** @brief ImGui 有効時にスケルトンのジョイント・ボーンをワールド空間へデバッグ描画する */
void SkinnedObject3d::DiagnosticsDraw()
{
#ifdef USE_IMGUI
    if (skeletonDebugRenderer_)
    {
        skeletonDebugRenderer_->Draw(skeleton_, worldMatrix_, commonCamera_);
    }
#endif
}

/** @brief どちらの描画パスでも同じスロット（0: マテリアル、1: 変換行列、3: ライト、4: 影、5: 環境マップ）を設定する */
void SkinnedObject3d::BindSharedSlots(ID3D12GraphicsCommandList* cmd) const
{
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::ModelRootSlot::kMaterial, materialCB_->GetGPUVirtualAddress());
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::ModelRootSlot::kTransform, transformCB_->GetGPUVirtualAddress());
    PipelineHelper::BindCommonLighting(cmd, commonObjectCommon_, commonShadowManager_);
    // 環境マップが未設定なら通常のテクスチャで代用する
    const std::string& envTexture = envCubemapFilePath_.empty() ? model_->GetTextureFilePath() : envCubemapFilePath_;
    cmd->SetGraphicsRootDescriptorTable(PipelineHelper::ModelRootSlot::kEnvironment, TextureManager::GetInstance()->GetSrvHandleGPU(envTexture));
}

/** @brief スキニング済み頂点を描画する（CS スキニング可なら CS 結果を、不可なら VS 内ボーン計算にフォールバックする） */
void SkinnedObject3d::Draw()
{
    if (!model_)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmd = skinCommon_->GetDxCommon()->GetCommandList();

    if (skinCSReady_ && commonModelCommon_)
    {
        // CS スキニング済みパス:
        // Draw() 内で Dispatch することでコマンドリストが確実に開いている状態で実行される
        skinCS_.Dispatch(cmd, paletteCB_->GetGPUVirtualAddress());

        // 計算済み頂点バッファ (position/texcoord/normal のみ) を使い、
        // ModelCommon の標準 PSO (Object3dVS + Object3dPS) で描画する
        commonModelCommon_->CommonDrawSettings();

        // スロット 0・1・3・4・5（マテリアル・変換行列・ライト・影・環境マップ）
        BindSharedSlots(cmd);
        // スロット 2 (t0): テクスチャ
        const D3D12_GPU_DESCRIPTOR_HANDLE texHandle = TextureManager::GetInstance()->GetSrvHandleGPU(model_->GetTextureFilePath());
        cmd->SetGraphicsRootDescriptorTable(PipelineHelper::ModelRootSlot::kTexture, texHandle);

        // CS が書き込んだ頂点バッファをセット (ボーンデータなし: pos/uv/normal のみ)
        DrawSkinnedVertices(cmd);
    }
    else
    {
        // フォールバック: SkinCommon の PSO (SkinnedVS) でボーン計算を VS 内で行う従来パス
        BindSharedSlots(cmd);

        // スロット 6 (VS, b1): スキニングパレット
        cmd->SetGraphicsRootConstantBufferView(PipelineHelper::SkinnedRootSlot::kPalette, paletteCB_->GetGPUVirtualAddress());
        // スロット 7 (PS, b2): ポイントライト
        if (commonObjectCommon_)
        {
            commonObjectCommon_->SetPointLights(cmd, PipelineHelper::SkinnedRootSlot::kPointLights);
        }
        // スロット 8 (PS, t3): 法線マップ（スキンドメッシュ未使用のためテクスチャで代替）
        {
            D3D12_GPU_DESCRIPTOR_HANDLE texHandle = TextureManager::GetInstance()->GetSrvHandleGPU(model_->GetTextureFilePath());
            cmd->SetGraphicsRootDescriptorTable(PipelineHelper::SkinnedRootSlot::kNormalMap, texHandle);
        }
        model_->Draw(cmd);
    }
}

/** @brief アウトライン2パス描画（OutlineEffect::BeginOutlinePass() の後に呼ぶ） */
void SkinnedObject3d::DrawOutline(OutlineEffect* effect)
{
    // フォールバックパス（VS 内ボーン計算）は頂点レイアウトがボーンウェイト付きで
    // OutlineVS の入力（POSITION/TEXCOORD/NORMAL のみ）と一致しないため未対応
    if (!model_ || !effect || !skinCSReady_)
    {
        return;
    }

    ID3D12GraphicsCommandList* cmd = skinCommon_->GetDxCommon()->GetCommandList();

    // アウトラインパスは Draw() より先に呼ばれる想定なので、
    // 今フレーム分のスキニング結果をここで計算しておく（Draw() 側でも再計算するが軽量なので許容）
    // ※ Dispatch はコンピュート用 PSO にコマンドリストを切り替えるため、
    //    このあと BeginOutlinePass() でグラフィックス用ルートシグネチャ/PSO に戻す必要がある
    //    （通常の Draw() は直後に CommonDrawSettings() で戻しているが、こちらは戻していなかった）
    skinCS_.Dispatch(cmd, paletteCB_->GetGPUVirtualAddress());
    effect->BeginOutlinePass();

    // slot 1 (VS b1): 座標変換行列（OutlineEffect のルートシグネチャに合わせる）
    cmd->SetGraphicsRootConstantBufferView(PipelineHelper::ModelRootSlot::kTransform, transformCB_->GetGPUVirtualAddress());

    DrawSkinnedVertices(cmd);
}

/** @brief 計算済みスキン頂点を三角形として描画する */
void SkinnedObject3d::DrawSkinnedVertices(ID3D12GraphicsCommandList* cmd)
{
    // 通常描画と輪郭描画で同じ頂点範囲を使う
    const auto& vertexBuffer = skinCS_.GetOutputVBV();
    cmd->IASetVertexBuffers(0, 1, &vertexBuffer);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(model_->GetVertexCount(), 1, 0, 0);
}

} // namespace engine::graphics
