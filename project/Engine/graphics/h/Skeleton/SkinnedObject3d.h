/**
 * @file SkinnedObject3d.h
 * @brief SkinnedObject3dが公開する型とAPIを定義するファイル
 */
#pragma once
#include "Animation.h"
#include "MakeAffine.h"
#include "ModelCommon.h"
#include "ObjectMaterialLayout.h"
#include "Skeleton.h"
#include "SkinCS.h"
#include "SkinCommon.h"
#include "SkinnedModel.h"
#include <string>
#include <wrl/client.h>

#ifdef USE_IMGUI
#include "SkeletonOverlayRenderer.h"
#include <memory>
#endif

namespace engine::graphics {
using engine::game::Animation;

class Camera;
class Object3dCommon;
class ShadowManager;
class OutlineEffect;

// スキニング（ボーンアニメーション）付き 3D オブジェクト
// SkinnedModel + Skeleton + Animation を組み合わせて毎フレーム描画する
class SkinnedObject3d {
public:
    static void SetCommonCamera(Camera* camera);
    static void SetLightViewProjection(const Matrix4x4& lvp);
    static void SetCommonObjectCommon(Object3dCommon* objectCommon);
    static void SetCommonShadowManager(ShadowManager* shadowManager);
    static void SetCommonModelCommon(ModelCommon* modelCommon);

    void Initialize(SkinCommon* skinCommon);

    void SetModel(SkinnedModel* model); // SkinCS の初期化も行う
    // アニメーションを切り替える再生時刻は先頭に巻き戻る
    void SetAnimation(Animation anim)
    {
        animation_ = std::move(anim);
        animTime_ = 0.0f;
    }
    void SetSkeleton(Skeleton skeleton) { skeleton_ = std::move(skeleton); }
    void SetEnvCubemapFilePath(const std::string& path) { envCubemapFilePath_ = path; }

    void SetPosition(const Vector3& pos) { transform_.translate = pos; }
    void SetRotation(const Vector3& rot) { transform_.rotate = rot; }
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetAnimSpeed(float s) { animSpeed_ = s; }
    // スクラブ再生用  アニメーション時刻を直接指定する
    // （SetAnimation() は呼ぶ度に0へ戻すため、クリップ更新直後にこれで再生位置を復元する）
    void SetAnimTime(float t) { animTime_ = t; }

    void SetColor(const Vector4& color)
    {
        if (materialData_) {
            materialData_->color = color;
        }
    }
    void SetEnableLighting(bool enable)
    {
        if (materialData_) {
            materialData_->enableLighting = enable ? 1 : 0;
        }
    }

    // リムライト（縁取り発光enableLighting 有効時のみシェーダーで反映される）
    void SetRimColor(const Vector3& color)
    {
        if (materialData_) {
            materialData_->rimColor = color;
        }
    }
    void SetRimPower(float power)
    {
        if (materialData_) {
            materialData_->rimPower = power;
        }
    }
    void SetRimIntensity(float intensity)
    {
        if (materialData_) {
            materialData_->rimIntensity = intensity;
        }
    }
    void SetEnableRim(bool enable)
    {
        if (materialData_) {
            materialData_->enableRim = enable ? 1 : 0;
        }
    }

    Vector3 GetPosition() const { return transform_.translate; }
    Vector3 GetRotation() const { return transform_.rotate; }
    Vector3 GetScale() const { return transform_.scale; }
    float GetAnimSpeed() const { return animSpeed_; }

    // 武器アタッチ等でジョイント行列を参照するためのゲッター
    // （skeletonSpaceMatrix は Update() 後に最新になる）
    const Skeleton& GetSkeleton() const { return skeleton_; }
    const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }

    void Update();
    void Draw();
    void DiagnosticsDraw();

    /**
     * @brief アウトライン2パス描画（OutlineEffect::BeginOutlinePass() の後に呼ぶ）
     * @note CS スキニング（skinCSReady_）でのみ対応。Draw() より先に呼ばれる想定のため、
     *       スキニング結果を自前で Dispatch し直す
     */
    void DrawOutline(OutlineEffect* effect);

private:
    static const int kMaxJoints = 128;

    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
        Matrix4x4 WorldInverseTranspose; ///< World の逆転置行列（非均一スケール対応法線変換用）
        Matrix4x4 LightVP;
    };

    static Camera* commonCamera_;
    static Matrix4x4 commonLightVP_;
    static Object3dCommon* commonObjectCommon_;
    static ShadowManager* commonShadowManager_;
    static ModelCommon* commonModelCommon_;

    SkinCommon* skinCommon_ = nullptr;
    SkinnedModel* model_ = nullptr;

    SkinCS skinCS_;
    bool skinCSReady_ = false;

    void InitializeSkinCS();

    std::string envCubemapFilePath_;

    Skeleton skeleton_;
    Animation animation_;
    float animTime_ = 0.0f;
    float animSpeed_ = 1.0f;

    Transform transform_ { { 1, 1, 1 }, { 0, 0, 0 }, { 0, 0, 0 } };
    Matrix4x4 worldMatrix_ = MakeIdentity4x4();

#ifdef USE_IMGUI
    std::unique_ptr<SkeletonOverlayRenderer> skeletonDebugRenderer_;
#endif

    Microsoft::WRL::ComPtr<ID3D12Resource> transformCB_;
    TransformationMatrix* transformData_ = nullptr;

    // Object3dPS.hlsl の Material 構造体と一致する共用レイアウト（Object3d と共通）
    Microsoft::WRL::ComPtr<ID3D12Resource> materialCB_;
    ObjectMaterialLayout* materialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> paletteCB_;
    Matrix4x4* paletteData_ = nullptr;
};

} // namespace engine::graphics
