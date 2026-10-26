/**
 * @file SkinnedObject3d.h
 * @brief アニメーションするスキンメッシュのオブジェクト（SkinnedObject3d）の宣言
 */
#pragma once
#include "PipelineHelper.h"
#include "Animation.h"
#include "MakeAffine.h"
#include "ModelCommon.h"
#include "ObjectMaterialLayout.h"
#include "PoseLayer.h"
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
/**
 * @brief GPU スキニングによるボーンアニメーション付き 3D オブジェクトの描画クラス
 * @details SkinnedModel（頂点・ボーン情報）と Skeleton（ジョイント階層）と Animation（キーフレーム）を
 *          毎フレーム合成し、CS でスキニングした頂点、またはフォールバックの VS 内ボーン計算で描画する
 */
class SkinnedObject3d {
public:
    /**
     * @brief 全インスタンス共通で参照するカメラを設定する
     * @param camera Update()/Draw() で view/projection 行列取得に使うカメラ
     */
    static void SetCommonCamera(Camera* camera);
    /**
     * @brief 全インスタンス共通のライト視点ビュープロジェクション行列を設定する（シャドウ判定用）
     * @param lvp シャドウマップ生成時に使ったライト視点の VP 行列
     */
    static void SetLightViewProjection(const Matrix4x4& lvp);
    /**
     * @brief 全インスタンス共通の Object3dCommon（ライト・共有描画設定）を設定する
     * @param objectCommon Draw() でライト設定の取得に使う Object3dCommon インスタンス
     */
    static void SetCommonObjectCommon(Object3dCommon* objectCommon);
    /**
     * @brief 全インスタンス共通の ShadowManager を設定する
     * @param shadowManager Draw() でシャドウマップ SRV のバインドに使う ShadowManager インスタンス
     */
    static void SetCommonShadowManager(ShadowManager* shadowManager);
    /**
     * @brief 全インスタンス共通の ModelCommon（CS スキニング済みパスの PSO）を設定する
     * @param modelCommon Draw() で CS スキニング結果を描画する際に使う ModelCommon インスタンス
     */
    static void SetCommonModelCommon(ModelCommon* modelCommon);

    /**
     * @brief スキンモデルの描画共通設定を作り、全インスタンス共通の ModelCommon とカメラ（通常モデルと同じもの）を設定する
     * @param modelCommon 通常モデルの描画共通設定（デバイスの取得と CS スキニング後の描画に使う）
     * @return 作った SkinCommon（呼び出し側が持ち、SkinnedObject3d::Initialize に渡す）
     */
    static std::unique_ptr<SkinCommon> CreateSharedCommon(ModelCommon* modelCommon);

    /**
     * @brief 変換行列・マテリアル・スキニングパレットの定数バッファを確保し既定値で初期化する
     * @param skinCommon 描画に使う PSO・ルートシグネチャを保持する SkinCommon インスタンス
     */
    void Initialize(SkinCommon* skinCommon);

    void SetModel(SkinnedModel* model); // SkinCS の初期化も行う
    // アニメーションを切り替える再生時刻は先頭に巻き戻る
    void SetAnimation(Animation anim) {
        animation_ = std::move(anim);
        animTime_ = 0.0f;
    }
    /** @brief スケルトンを差し替える（値で受け取り std::move で持つ） */
    void SetSkeleton(Skeleton skeleton) {
        skeleton_ = std::move(skeleton);
        appliedLayerRotations_.clear(); // 添字が別のスケルトンを指さないよう巻き戻し情報を捨てる
    }

    /**
     * @brief ベースアニメの上に重ねるポーズレイヤーを設定する（nullptrで解除）
     * @note レイヤーの寿命は呼び出し側が管理する（このオブジェクトより長く生存させること）
     */
    void SetPoseLayer(const IPoseLayer* layer) {
        poseLayer_ = layer;
    }
    /** @brief 環境マップのキューブマップのファイルパスを設定する */
    void SetEnvCubemapFilePath(const std::string& path) {
        envCubemapFilePath_ = path;
    }

    /** @brief 位置を設定する */
    void SetPosition(const Vector3& pos) {
        transform_.translate = pos;
    }
    /** @brief 回転を設定する */
    void SetRotation(const Vector3& rot) {
        transform_.rotate = rot;
    }
    /** @brief 拡大率を設定する */
    void SetScale(const Vector3& scale) {
        transform_.scale = scale;
    }
    /** @brief アニメーションの再生速度を設定する */
    void SetAnimSpeed(float s) {
        animSpeed_ = s;
    }
    // スクラブ再生用  アニメーション時刻を直接指定する
    // （SetAnimation() は呼ぶ度に0へ戻すため、クリップ更新直後にこれで再生位置を復元する）
    void SetAnimTime(float t) {
        animTime_ = t;
    }

    /** @brief 色を設定する */
    void SetColor(const Vector4& color) {
        pipelineHelper::SetMaterialValue(materialData_, &ObjectMaterialLayout::color, color);
    }
    /** @brief ライティングを使うかを設定する */
    void SetEnableLighting(bool enable) {
        pipelineHelper::SetMaterialValue(materialData_, &ObjectMaterialLayout::enableLighting, enable ? 1 : 0);
    }

    // リムライト（縁取り発光enableLighting 有効時のみシェーダーで反映される）
    void SetRimColor(const Vector3& color) {
        pipelineHelper::SetMaterialValue(materialData_, &ObjectMaterialLayout::rimColor, color);
    }
    /** @brief リムライトの鋭さを設定する */
    void SetRimPower(float power) {
        pipelineHelper::SetMaterialValue(materialData_, &ObjectMaterialLayout::rimPower, power);
    }
    /** @brief リムライトの強さを設定する */
    void SetRimIntensity(float intensity) {
        pipelineHelper::SetMaterialValue(materialData_, &ObjectMaterialLayout::rimIntensity, intensity);
    }
    /** @brief リムライトを使うかを設定する */
    void SetEnableRim(bool enable) {
        pipelineHelper::SetMaterialValue(materialData_, &ObjectMaterialLayout::enableRim, enable ? 1 : 0);
    }

    /** @brief 位置を返す */
    Vector3 GetPosition() const {
        return transform_.translate;
    }
    /** @brief 回転を返す */
    Vector3 GetRotation() const {
        return transform_.rotate;
    }
    /** @brief 拡大率を返す */
    Vector3 GetScale() const {
        return transform_.scale;
    }
    /** @brief アニメーションの再生速度を返す */
    float GetAnimSpeed() const {
        return animSpeed_;
    }

    // 武器アタッチ等でジョイント行列を参照するためのゲッター
    // （skeletonSpaceMatrix は Update() 後に最新になる）
    const Skeleton& GetSkeleton() const {
        return skeleton_;
    }
    /** @brief ワールド行列を返す */
    const Matrix4x4& GetWorldMatrix() const {
        return worldMatrix_;
    }

    /**
     * @brief アニメーションを適用してスケルトンとスキニングパレット、ワールド/WVP行列を更新する
     */
    void Update();
    /**
     * @brief スキニング済み頂点を描画する（CS スキニング可なら CS 結果を、不可なら VS 内ボーン計算にフォールバックする）
     */
    void Draw();
    /**
     * @brief ImGui 有効時にスケルトンのジョイント・ボーンをワールド空間へデバッグ描画する
     */
    void DiagnosticsDraw();

    /**
     * @brief アウトライン2パス描画（OutlineEffect::BeginOutlinePass() の後に呼ぶ）
     * @note CS スキニング（skinCSReady_）でのみ対応。Draw() より先に呼ばれる想定のため、
     *       スキニング結果を自前で Dispatch し直す
     */
    void DrawOutline(OutlineEffect* effect);

private:
    /** @brief 計算済みスキン頂点を三角形として描画する */
    void DrawSkinnedVertices(ID3D12GraphicsCommandList* cmd);

    /** @brief どちらの描画パスでも同じスロット（0: マテリアル、1: 変換行列、3: ライト、4: 影、5: 環境マップ）を設定する */
    void BindSharedSlots(ID3D12GraphicsCommandList* cmd) const;
    static const int kMaxJoints = 128;

    /**
     * @brief VS に渡す座標変換用定数バッファのレイアウト（SkinnedVS.hlsl / Object3dVS.hlsl と一致させる）
     */
    struct TransformationMatrix {
        Matrix4x4 wvp;
        Matrix4x4 world;
        Matrix4x4 worldInverseTranspose; ///< World の逆転置行列（非均一スケール対応法線変換用）
        Matrix4x4 lightVP;
    };

    static Camera* sCommonCamera;
    static Matrix4x4 sCommonLightVP;
    static Object3dCommon* sCommonObjectCommon;
    static ShadowManager* sCommonShadowManager;
    static ModelCommon* sCommonModelCommon;

    SkinCommon* skinCommon_ = nullptr;
    SkinnedModel* model_ = nullptr;

    SkinCS skinCS_;
    bool skinCSReady_ = false;

    /**
     * @brief model_ と skinCommon_ が両方設定済みなら SkinCS を初期化する（未設定時は何もしない）
     */
    void InitializeSkinCS();

    std::string envCubemapFilePath_;

    Skeleton skeleton_;
    Animation animation_;
    float animTime_ = 0.0f;
    float animSpeed_ = 1.0f;

    /** @brief レイヤーを掛ける前のボーン回転 */
    struct SavedJointRotation {
        int32_t jointIndex;
        Quaternion rotate;
    };

    const IPoseLayer* poseLayer_ = nullptr;
    /// 前回の評価でレイヤーを掛ける前の回転（次の評価の頭で戻し、差分が積み重ならないようにする）
    std::vector<SavedJointRotation> appliedLayerRotations_;
    /// レイヤーから集めた差分回転の作業領域（毎フレーム確保し直さないよう保持する）
    std::vector<PoseLayerRotation> layerScratch_;

    /** @brief 前回レイヤーで書き換えたボーンの回転を元に戻す */
    void RestoreLayerRotations();
    /** @brief レイヤーの差分回転をボーンへ掛け、戻すための元の回転を記録する */
    void ApplyPoseLayer();

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
