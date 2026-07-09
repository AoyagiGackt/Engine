/**
 * @file AnimationEditorScene.h
 * @brief スキンメッシュのボーンへ直接キーフレームを打てる簡易アニメーションエディタ
 * @note タイトル画面「ANIM EDIT」から入る。作成したクリップは Resources/Animations/Custom/ に
 *       独自JSON（Animation.h の Animation 構造体そのまま）で保存し、LoadAnimationJson() で読み戻せる。
 *       武器・銃・リグは CharacterVisuals.h の定義を Player.cpp と共用する。
 *       EditMode::Reference で本編クリップの再生比較・ポーズ取り込み、EditMode::ComboTest で
 *       実際の MeleeCombo/GunCombo 入力（L/K）による攻撃モーション確認ができる。
 */
#pragma once
#include <memory>
#include <string>

#include "Animation.h"
#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
#include "CharacterVisuals.h"
#include "DirectXCommon.h"
#include "GunCombo.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "MeleeCombo.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ShadowManager.h"
#include "SkinCommon.h"
#include "SkinnedModel.h"
#include "SkinnedObject3d.h"
#include "SrvManager.h"
#include "Weapon.h"

namespace engine::game {
using engine::Audio;
using engine::graphics::Camera;
using engine::DirectXCommon;
using engine::graphics::ImGuiManager;
using engine::Input;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Object3dCommon;
using engine::graphics::ShadowManager;
using engine::graphics::SkinCommon;
using engine::graphics::SkinnedModel;
using engine::graphics::SkinnedObject3d;
using engine::graphics::SrvManager;

/// @brief ボーンにキーフレームを直接打って独自クリップを作れる簡易アニメーションエディタ
class AnimationEditorScene : public BaseScene {
public:
    /// @brief 編集モード（Reference/ComboTest 中はポーズ編集キー打ちを行わない）
    enum class EditMode { PoseEdit, Reference, ComboTest };

    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;
    void SetImGuiManager(ImGuiManager* imgui) override { imguiManager_ = imgui; }

private:
    /// @brief 編集対象リグを切り替える（データは CharacterVisuals.h の kNormalRigVisual/kAwakenedRigVisual を共用）
    void LoadRig(int presetIndex);

    void RenderMainPanel();
    void RenderJointListPanel();
    void RenderPosePanel();
    void RenderTimelinePanel();
    void RenderReferencePanel();
    void RenderComboTestPanel();
    void DrawJointGizmos();

    /// @brief 選択中ジョイントの現在Quaternionから編集用Euler角(度)を作り直す
    void SyncEulerFromSelectedJoint();
    /// @brief クリップ内容が変わった直後の共通処理（SetAnimation()し直してスクラブ位置を復元）
    void ApplyClipToPreview();

    /// @brief 手持ち武器/銃の切り替え（-1で非表示）。データは CharacterVisuals.h の kHeldWeaponVisuals/kGunVisuals を共用
    void LoadHeldWeapon(int presetIndex);
    void LoadGun(int presetIndex);
    /// @brief 矢印キーでプレビューモデルを自由に歩かせる（キーフレームには残らない、確認用）
    void UpdateFreeMovement();

    /// @brief 編集モードを切り替える（各モード固有の再生状態をリセットする）
    void SetMode(EditMode mode);

    /// @brief 現在のリグの内蔵クリップ（Idle/Run/Slash等）をリファレンスとして読み込む。
    ///        インデックスは.cpp内 kReferenceAnims の並び順
    void LoadReferenceAnim(int index);

    /// @brief 現在のリグの現在のスケルトンポーズを、指定時刻のキーフレームとして clip_ 全ボーンへ焼き込む
    ///        （リファレンス再生中のポーズを取り込んで新規クリップの下地にする用途）
    void BakePoseToClip(float time);

    /// @brief 実際のコンボ入力（L=近接/K=射撃）で meleeCombo_/gunCombo_ を駆動し、
    ///        本編と同じ攻撃モーション・武器スイングを再生する（Edit Pose とは排他）
    void UpdateComboTest();
    /// @brief コンボテスト用の待機/斬撃/パンチモーションを読み直してキャッシュする（毎フレーム再読込を避ける）
    void RefreshComboAnims();

    DirectXCommon* dxCommon_     = nullptr;
    Input*         input_        = nullptr;
    Audio*         audio_        = nullptr;
    ImGuiManager*  imguiManager_ = nullptr;
    SrvManager*    srvManager_   = nullptr;

    std::unique_ptr<ModelCommon>    modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager>  shadowManager_;
    std::unique_ptr<Camera>         camera_;
    std::unique_ptr<SkinCommon>     skinCommon_;

    // プレビュー対象のリグ
    int                              currentRigIndex_ = 0;
    std::unique_ptr<SkinnedModel>    skinnedModel_;
    std::unique_ptr<SkinnedObject3d> previewObject_;
    Vector3                          previewPos_ = { 0.0f, 0.0f, 0.0f }; // 矢印キーで動かせる（キーフレーム対象外）

    // 手持ち武器/銃（現在のリグの手ボーンに追従。データは CharacterVisuals.h の
    // kHeldWeaponVisuals/kGunVisuals を Player.cpp と共用する）
    int                       heldWeaponIndex_ = -1; // -1 = 非表示、それ以外は kHeldWeaponVisuals のIndex
    std::unique_ptr<Model>    heldWeaponModel_;
    std::unique_ptr<Object3d> heldWeaponObject_;
    int                       gunIndex_ = -1; // -1 = 非表示、それ以外は kGunVisuals のIndex
    std::unique_ptr<Model>    gunModel_;
    std::unique_ptr<Object3d> gunObject_;

    // カメラ操作用（SceneEditor同様、DragFloat3で直接編集する）
    // 原点に立つキャラクター（身長 約1〜1.2）を正面から見下ろす程度で捉える位置
    Vector3 cameraPos_ = { 0.0f, 1.3f, -3.2f };
    Vector3 cameraRot_ = { 0.23f, 0.0f, 0.0f };

    // 編集中のクリップ
    Animation clip_;
    float     scrubTime_    = 0.0f;
    float     clipDuration_ = 1.0f;
    bool      isPlaying_    = false;

    // 現在の編集モード（PoseEdit以外はタイムライン/Poseパネルでの打鍵を止める）
    EditMode mode_ = EditMode::PoseEdit;

    // リファレンス再生（現在のリグに元から入っているクリップを再生して見比べる／ポーズを取り込む）
    int       referenceAnimIndex_ = 0; // RigVisualDef の idle/run/jump/... のうちどれか（.cpp内テーブルの並び順）
    Animation referenceClip_;
    float     referenceTime_      = 0.0f;
    bool      referencePlaying_   = false;

    // コンボテスト（実際の L=近接/K=射撃 入力で本編と同じ MeleeCombo/GunCombo を駆動する）
    WeaponType            comboWeaponType_ = WeaponType::Sword;
    GunType               comboGunType_    = GunType::Pistol;
    MeleeComboController  meleeCombo_;
    GunComboController    gunCombo_;
    float                 comboAttackAnimTimer_ = 0.0f; // Player::attackAnimTimer_ 相当
    Animation             comboIdleAnim_;                // 攻撃していない間に再生する待機モーション
    Animation             comboSlashAnim_;
    Animation             comboPunchAnim_;

    // 選択ジョイント
    std::string selectedJoint_;
    Vector3     editEulerDeg_ = { 0.0f, 0.0f, 0.0f }; // 選択中ジョイントの回転編集キャッシュ(度)

    // 保存/読込
    char clipNameBuf_[64] = "new_clip";
    std::string statusMessage_;
};

} // namespace engine::game
