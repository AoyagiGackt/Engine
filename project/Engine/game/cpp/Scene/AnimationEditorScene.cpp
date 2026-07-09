#include "AnimationEditorScene.h"
#include "DebugDraw.h"
#include "GameConstants.h"
#include "SceneManager.h"
#include "Skeleton.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {

// 編集対象にできるリグのプリセット。データは CharacterVisuals.h の kNormalRigVisual/kAwakenedRigVisual を
// Player.cpp と共用する（アセットパス・ボーン名・内蔵アニメ名を二重管理しない）
struct RigPreset {
    const char*         label;
    const RigVisualDef* def;
};
constexpr RigPreset kRigPresets[] = {
    { "Alien（通常）",  &kNormalRigVisual },
    { "Mike（覚醒）",   &kAwakenedRigVisual },
};
constexpr int kRigPresetCount = static_cast<int>(sizeof(kRigPresets) / sizeof(kRigPresets[0]));

const char* WeaponTypeLabel(WeaponType type)
{
    switch (type) {
    case WeaponType::Sword:      return "ソード";
    case WeaponType::Dagger:     return "ダガー";
    case WeaponType::Hammer:     return "ハンマー";
    case WeaponType::Spear:      return "スピア";
    case WeaponType::Greatsword: return "グレートソード";
    case WeaponType::Scythe:     return "大鎌";
    case WeaponType::Axe:        return "アックス";
    case WeaponType::Ball:       return "ボール";
    }
    return "?";
}

const char* GunTypeLabel(GunType type)
{
    switch (type) {
    case GunType::Pistol:  return "ピストル";
    case GunType::Magnum:  return "マグナム";
    case GunType::SMG:     return "サブマシンガン";
    case GunType::Shotgun: return "ショットガン";
    case GunType::Railgun: return "レールガン";
    }
    return "?";
}

// リグ内蔵クリップ（RigVisualDef のメンバー）をリファレンス再生の選択肢として並べたテーブル
struct ReferenceAnimEntry {
    const char*               label;
    const char* RigVisualDef::*nameField;
};
constexpr ReferenceAnimEntry kReferenceAnims[] = {
    { "待機",         &RigVisualDef::idle },
    { "走り",         &RigVisualDef::run },
    { "ジャンプ",     &RigVisualDef::jump },
    { "走行ジャンプ", &RigVisualDef::runningJump },
    { "遊泳",         &RigVisualDef::swim },
    { "武器構え待機", &RigVisualDef::idleHold },
    { "武器構え走り", &RigVisualDef::runHold },
    { "斬撃",         &RigVisualDef::slash },
    { "パンチ",       &RigVisualDef::punch },
};
constexpr int kReferenceAnimCount = static_cast<int>(sizeof(kReferenceAnims) / sizeof(kReferenceAnims[0]));

constexpr float kFreeMoveSpeed = 2.0f; // 矢印キー移動速度（unit/秒）

// キーフレームを指定時刻に挿入/上書きする（時刻順を維持する）
template <typename T>
void UpsertKeyframe(AnimationCurve<T>& curve, float time, const T& value)
{
    constexpr float kEps = 1e-4f;
    for (auto& kf : curve.keyframes) {
        if (std::abs(kf.time - time) < kEps) { kf.value = value; return; }
    }
    curve.keyframes.push_back({ time, value });
    std::sort(curve.keyframes.begin(), curve.keyframes.end(),
        [](const auto& a, const auto& b) { return a.time < b.time; });
}

// 指定時刻のキーフレームを削除する（あれば true）
template <typename T>
bool RemoveKeyframeAt(AnimationCurve<T>& curve, float time)
{
    constexpr float kEps = 1e-4f;
    auto it = std::find_if(curve.keyframes.begin(), curve.keyframes.end(),
        [&](const auto& kf) { return std::abs(kf.time - time) < kEps; });
    if (it == curve.keyframes.end()) { return false; }
    curve.keyframes.erase(it);
    return true;
}

} // namespace

void AnimationEditorScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_   = dxCommon;
    input_      = input;
    audio_      = audio;
    srvManager_ = SrvManager::GetInstance();

    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, srvManager_);

    Object3d::SetCommonObjectCommon(objectCommon_.get());
    Object3d::SetCommonShadowManager(shadowManager_.get());

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate(cameraPos_);
    camera_->SetRotate(cameraRot_);
    Object3d::SetCommonCamera(camera_.get());

    skinCommon_ = std::make_unique<SkinCommon>();
    skinCommon_->Initialize(dxCommon_);
    SkinnedObject3d::SetCommonModelCommon(modelCommon_.get());
    SkinnedObject3d::SetCommonCamera(camera_.get());

    LoadRig(0);
}

void AnimationEditorScene::LoadRig(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= kRigPresetCount) { return; }
    currentRigIndex_ = presetIndex;
    const RigVisualDef& def = *kRigPresets[presetIndex].def;

    std::string modelPath = std::string(def.dir) + "/" + def.file;
    skinnedModel_ = std::make_unique<SkinnedModel>();
    skinnedModel_->Initialize(dxCommon_, modelPath, def.texture);

    previewObject_ = std::make_unique<SkinnedObject3d>();
    previewObject_->Initialize(skinCommon_.get());
    previewObject_->SetModel(skinnedModel_.get());
    previewObject_->SetSkeleton(Skeleton::Create(LoadNodeHierarchyFromFile(def.dir, def.file)));
    previewObject_->SetPosition(previewPos_);
    previewObject_->SetScale({ def.scale, def.scale, def.scale });
    previewObject_->SetEnableLighting(true);

    selectedJoint_.clear();
    clip_          = Animation{};
    clipDuration_  = 1.0f;
    clip_.duration = clipDuration_;
    scrubTime_     = 0.0f;
    meleeCombo_.Reset();
    gunCombo_.Reset();
    comboAttackAnimTimer_ = 0.0f;
    SetMode(EditMode::PoseEdit);
}

void AnimationEditorScene::LoadHeldWeapon(int presetIndex)
{
    heldWeaponIndex_ = presetIndex;
    if (presetIndex < 0 || presetIndex >= kHeldWeaponVisualCount) {
        heldWeaponObject_.reset();
        heldWeaponModel_.reset();
        return;
    }
    const HeldWeaponVisual& preset = kHeldWeaponVisuals[presetIndex];
    heldWeaponModel_ = std::make_unique<Model>();
    heldWeaponModel_->Initialize(modelCommon_.get(), preset.modelPath, preset.texturePath);
    heldWeaponObject_ = std::make_unique<Object3d>();
    heldWeaponObject_->Initialize(modelCommon_.get());
    heldWeaponObject_->SetModel(heldWeaponModel_.get());
    heldWeaponObject_->SetEnableLighting(true);
}

void AnimationEditorScene::LoadGun(int presetIndex)
{
    gunIndex_ = presetIndex;
    if (presetIndex < 0 || presetIndex >= kGunVisualCount) {
        gunObject_.reset();
        gunModel_.reset();
        return;
    }
    const GunVisual& preset = kGunVisuals[presetIndex];
    gunModel_ = std::make_unique<Model>();
    gunModel_->Initialize(modelCommon_.get(), preset.modelPath, preset.texturePath);
    gunObject_ = std::make_unique<Object3d>();
    gunObject_->Initialize(modelCommon_.get());
    gunObject_->SetModel(gunModel_.get());
    gunObject_->SetEnableLighting(true);
}

void AnimationEditorScene::UpdateFreeMovement()
{
    if (!previewObject_) { return; }
    float dt = GameConstants::kFrameDeltaTime;
    if (input_->PushKey(DIK_LEFT))  { previewPos_.x -= kFreeMoveSpeed * dt; }
    if (input_->PushKey(DIK_RIGHT)) { previewPos_.x += kFreeMoveSpeed * dt; }
    if (input_->PushKey(DIK_UP))    { previewPos_.z += kFreeMoveSpeed * dt; }
    if (input_->PushKey(DIK_DOWN))  { previewPos_.z -= kFreeMoveSpeed * dt; }
    previewObject_->SetPosition(previewPos_);
}

void AnimationEditorScene::ApplyClipToPreview()
{
    if (!previewObject_) { return; }
    clip_.duration = clipDuration_;
    previewObject_->SetAnimSpeed(0.0f);
    previewObject_->SetAnimation(clip_);      // animTime_ が0へ戻る
    previewObject_->SetAnimTime(scrubTime_);  // すぐにスクラブ位置へ復元する
}

void AnimationEditorScene::LoadReferenceAnim(int index)
{
    if (index < 0 || index >= kReferenceAnimCount || !previewObject_) { return; }
    referenceAnimIndex_ = index;
    const RigVisualDef& def = *kRigPresets[currentRigIndex_].def;
    const char* animName = def.*(kReferenceAnims[index].nameField);
    referenceClip_ = LoadAnimationFile(def.dir, def.file, animName);
    referenceTime_ = 0.0f;
    previewObject_->SetAnimSpeed(0.0f);
    previewObject_->SetAnimation(referenceClip_);
    previewObject_->SetAnimTime(referenceTime_);
}

void AnimationEditorScene::RefreshComboAnims()
{
    if (!previewObject_) { return; }
    const RigVisualDef& def = *kRigPresets[currentRigIndex_].def;
    comboIdleAnim_  = LoadAnimationFile(def.dir, def.file, UsesHoldPose(comboWeaponType_) ? def.idleHold : def.idle);
    comboSlashAnim_ = LoadAnimationFile(def.dir, def.file, def.slash);
    comboPunchAnim_ = LoadAnimationFile(def.dir, def.file, def.punch);
}

void AnimationEditorScene::SetMode(EditMode mode)
{
    mode_ = mode;
    meleeCombo_.Reset();
    gunCombo_.Reset();
    comboAttackAnimTimer_ = 0.0f;
    switch (mode_) {
    case EditMode::PoseEdit:
        ApplyClipToPreview();
        break;
    case EditMode::Reference:
        LoadReferenceAnim(referenceAnimIndex_);
        break;
    case EditMode::ComboTest:
        RefreshComboAnims();
        if (previewObject_) {
            previewObject_->SetAnimSpeed(1.0f);
            previewObject_->SetAnimation(comboIdleAnim_);
        }
        break;
    }
}

void AnimationEditorScene::BakePoseToClip(float time)
{
    if (!previewObject_) { return; }
    const Skeleton& skel = previewObject_->GetSkeleton();
    for (const auto& joint : skel.joints) {
        NodeAnimation& na = clip_.nodeAnimations[joint.name];
        UpsertKeyframe(na.translate, time, joint.transform.translate);
        UpsertKeyframe(na.rotate,    time, joint.transform.rotate);
        UpsertKeyframe(na.scale,     time, joint.transform.scale);
    }
    statusMessage_ = "ポーズを編集中クリップへ焼き込みました";
}

void AnimationEditorScene::UpdateComboTest()
{
    if (!previewObject_) { return; }
    float dt = GameConstants::kFrameDeltaTime;

    if (input_->TriggerKey(DIK_L)) {
        bool launcherInput = input_->PushKey(DIK_S) || input_->PushKey(DIK_DOWN);
        meleeCombo_.TryAttack(comboWeaponType_, launcherInput, /*airborne*/false);
    }
    meleeCombo_.Update(dt);
    if (meleeCombo_.JustStartedStep()) {
        const MeleeAttackDef* atk = meleeCombo_.GetActive();
        previewObject_->SetAnimation(atk->slashAnim ? comboSlashAnim_ : comboPunchAnim_);
        previewObject_->SetAnimSpeed(atk->animSpeed);
        comboAttackAnimTimer_ = atk->duration / atk->animSpeed;
    }

    if (input_->TriggerKey(DIK_K)) {
        gunCombo_.TryShoot(comboGunType_);
    }
    gunCombo_.Update(dt);

    if (comboAttackAnimTimer_ > 0.0f) {
        comboAttackAnimTimer_ -= dt;
        if (comboAttackAnimTimer_ <= 0.0f) {
            previewObject_->SetAnimSpeed(1.0f);
            previewObject_->SetAnimation(comboIdleAnim_);
        }
    }

    previewObject_->Update();
}

void AnimationEditorScene::SyncEulerFromSelectedJoint()
{
    if (selectedJoint_.empty() || !previewObject_) { return; }
    const Skeleton& skel = previewObject_->GetSkeleton();
    auto it = skel.jointMap.find(selectedJoint_);
    if (it == skel.jointMap.end()) { return; }

    // Quaternion→Euler(XYZ, ラジアン)の近似変換編集UI用途でジンバルロックは許容する
    const Quaternion& q = skel.joints[it->second].transform.rotate;
    float sinp = std::clamp(2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f);
    float eulerX = std::atan2(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    float eulerY = std::asin(sinp);
    float eulerZ = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z));

    editEulerDeg_ = { eulerX / GameConstants::kDegToRad,
                       eulerY / GameConstants::kDegToRad,
                       eulerZ / GameConstants::kDegToRad };
}

void AnimationEditorScene::Update()
{
    if (input_->TriggerKey(DIK_BACK)) {
        SceneManager::GetInstance()->ChangeScene("TITLE", 0.4f, 0.4f);
        return;
    }

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());

    RenderMainPanel();
    RenderJointListPanel();
    switch (mode_) {
    case EditMode::PoseEdit:
        RenderPosePanel();
        RenderTimelinePanel();
        break;
    case EditMode::Reference:
        RenderReferencePanel();
        break;
    case EditMode::ComboTest:
        RenderComboTestPanel();
        break;
    }

    UpdateFreeMovement();

    if (previewObject_) {
        if (mode_ == EditMode::ComboTest) {
            UpdateComboTest();
        } else if (mode_ == EditMode::Reference) {
            previewObject_->SetAnimTime(referenceTime_);
            previewObject_->Update();
        } else {
            previewObject_->SetAnimTime(scrubTime_);
            previewObject_->Update();
        }

        const RigVisualDef& def = *kRigPresets[currentRigIndex_].def;
        const Skeleton&  skel  = previewObject_->GetSkeleton();
        const Matrix4x4& world = previewObject_->GetWorldMatrix();
        if (heldWeaponObject_ && heldWeaponIndex_ >= 0) {
            const HeldWeaponVisual& wp  = kHeldWeaponVisuals[heldWeaponIndex_];
            Vector3 rot = wp.gripRotate;
            if (mode_ == EditMode::ComboTest) { rot = rot + meleeCombo_.GetSwingOffset(); }
            AttachToBone(heldWeaponObject_.get(), skel, world, def.meleeBone, wp.gripScale, rot, wp.gripTranslate);
        }
        if (gunObject_ && gunIndex_ >= 0) {
            const GunVisual& gp = kGunVisuals[gunIndex_];
            Vector3 rot = gp.gripRotate;
            if (mode_ == EditMode::ComboTest) { rot = rot + gunCombo_.GetPoseOffset(); }
            AttachToBone(gunObject_.get(), skel, world, def.gunBone, gp.gripScale, rot, gp.gripTranslate);
        }
    }

    DrawJointGizmos();
}

void AnimationEditorScene::RenderMainPanel()
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    if (ImGui::Begin("アニメーションエディタ")) {
        ImGui::TextDisabled("Backspace: タイトルへ  /  矢印キー: モデルを移動");
        ImGui::Separator();

        if (ImGui::BeginCombo("リグ", kRigPresets[currentRigIndex_].label)) {
            for (int i = 0; i < kRigPresetCount; ++i) {
                bool selected = (i == currentRigIndex_);
                if (ImGui::Selectable(kRigPresets[i].label, selected)) { LoadRig(i); }
            }
            ImGui::EndCombo();
        }

        const char* weaponLabel = (heldWeaponIndex_ >= 0) ? WeaponTypeLabel(kHeldWeaponVisuals[heldWeaponIndex_].type) : "(なし)";
        if (ImGui::BeginCombo("武器", weaponLabel)) {
            bool noneSelected = (heldWeaponIndex_ < 0);
            if (ImGui::Selectable("(なし)", noneSelected)) { LoadHeldWeapon(-1); }
            for (int i = 0; i < kHeldWeaponVisualCount; ++i) {
                bool selected = (i == heldWeaponIndex_);
                if (ImGui::Selectable(WeaponTypeLabel(kHeldWeaponVisuals[i].type), selected)) { LoadHeldWeapon(i); }
            }
            ImGui::EndCombo();
        }

        const char* gunLabel = (gunIndex_ >= 0) ? GunTypeLabel(kGunVisuals[gunIndex_].type) : "(なし)";
        if (ImGui::BeginCombo("銃", gunLabel)) {
            bool noneSelected = (gunIndex_ < 0);
            if (ImGui::Selectable("(なし)", noneSelected)) { LoadGun(-1); }
            for (int i = 0; i < kGunVisualCount; ++i) {
                bool selected = (i == gunIndex_);
                if (ImGui::Selectable(GunTypeLabel(kGunVisuals[i].type), selected)) { LoadGun(i); }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        ImGui::Text("モード");
        static const char* kModeLabels[] = { "ポーズ編集", "リファレンス再生", "コンボテスト" };
        int modeIdx = static_cast<int>(mode_);
        if (ImGui::Combo("##Mode", &modeIdx, kModeLabels, 3)) { SetMode(static_cast<EditMode>(modeIdx)); }

        ImGui::Separator();
        ImGui::Text("カメラ");
        if (ImGui::DragFloat3("位置", &cameraPos_.x, 0.05f)) { camera_->SetTranslate(cameraPos_); }
        if (ImGui::DragFloat3("回転", &cameraRot_.x, 0.01f)) { camera_->SetRotate(cameraRot_); }

        ImGui::Separator();
        ImGui::Text("クリップ");
        ImGui::InputText("名前", clipNameBuf_, sizeof(clipNameBuf_));
        if (ImGui::Button("保存")) {
            std::string path = std::string("Resources/Animations/Custom/") + clipNameBuf_ + ".json";
            SaveAnimationJson(path, clip_);
            statusMessage_ = "保存しました: " + path;
        }
        ImGui::SameLine();
        if (ImGui::Button("読込")) {
            std::string path = std::string("Resources/Animations/Custom/") + clipNameBuf_ + ".json";
            clip_          = LoadAnimationJson(path);
            clipDuration_  = (clip_.duration > 0.0f) ? clip_.duration : 1.0f;
            clip_.duration = clipDuration_;
            scrubTime_     = 0.0f;
            SetMode(EditMode::PoseEdit);
            SyncEulerFromSelectedJoint();
            statusMessage_ = "読み込みました: " + path;
        }
        ImGui::SameLine();
        if (ImGui::Button("新規")) {
            clip_          = Animation{};
            clipDuration_  = 1.0f;
            clip_.duration = clipDuration_;
            scrubTime_     = 0.0f;
            SetMode(EditMode::PoseEdit);
            statusMessage_ = "新規クリップ";
        }
        if (!statusMessage_.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", statusMessage_.c_str());
        }
    }
    ImGui::End();
#endif
}

void AnimationEditorScene::RenderJointListPanel()
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(260, 400), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(10, 320), ImGuiCond_Once);
    if (ImGui::Begin("ジョイント一覧")) {
        if (!previewObject_) {
            ImGui::TextDisabled("リグが読み込まれていません。");
        } else {
            const Skeleton& skel = previewObject_->GetSkeleton();
            for (const auto& joint : skel.joints) {
                bool selected = (joint.name == selectedJoint_);
                if (ImGui::Selectable(joint.name.c_str(), selected)) {
                    selectedJoint_ = joint.name;
                    SyncEulerFromSelectedJoint();
                }
            }
        }
    }
    ImGui::End();
#endif
}

void AnimationEditorScene::RenderPosePanel()
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(320, 260), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(970, 10), ImGuiCond_Once);
    if (ImGui::Begin("ポーズ")) {
        if (selectedJoint_.empty() || !previewObject_) {
            ImGui::TextDisabled("ジョイント一覧からボーンを選んでください。");
        } else {
            const Skeleton& skel = previewObject_->GetSkeleton();
            auto it = skel.jointMap.find(selectedJoint_);
            if (it == skel.jointMap.end()) {
                ImGui::TextDisabled("現在のリグにこのボーンはありません。");
            } else {
                ImGui::Text("ボーン: %s", selectedJoint_.c_str());
                ImGui::Separator();

                const Joint& joint = skel.joints[it->second];
                Vector3 pos   = joint.transform.translate;
                Vector3 scale = joint.transform.scale;

                bool changed = false;
                changed |= ImGui::DragFloat3("位置",         &pos.x, 0.01f);
                changed |= ImGui::DragFloat3("回転(度)",     &editEulerDeg_.x, 0.5f);
                changed |= ImGui::DragFloat3("スケール",     &scale.x, 0.01f, 0.01f, 10.0f);

                if (changed) {
                    NodeAnimation& na = clip_.nodeAnimations[selectedJoint_];
                    Vector3 eulerRad = { editEulerDeg_.x * GameConstants::kDegToRad,
                                          editEulerDeg_.y * GameConstants::kDegToRad,
                                          editEulerDeg_.z * GameConstants::kDegToRad };
                    UpsertKeyframe(na.translate, scrubTime_, pos);
                    UpsertKeyframe(na.rotate,    scrubTime_, MakeRotateXYZQuaternion(eulerRad));
                    UpsertKeyframe(na.scale,     scrubTime_, scale);
                    ApplyClipToPreview();
                }

                ImGui::Separator();
                if (ImGui::Button("キーフレーム追加（現在のポーズを保持）")) {
                    NodeAnimation& na2 = clip_.nodeAnimations[selectedJoint_];
                    UpsertKeyframe(na2.translate, scrubTime_, joint.transform.translate);
                    UpsertKeyframe(na2.rotate,    scrubTime_, joint.transform.rotate);
                    UpsertKeyframe(na2.scale,     scrubTime_, joint.transform.scale);
                    ApplyClipToPreview();
                }
                ImGui::SameLine();
                if (ImGui::Button("この時刻のキーフレームを削除")) {
                    auto found = clip_.nodeAnimations.find(selectedJoint_);
                    if (found != clip_.nodeAnimations.end()) {
                        RemoveKeyframeAt(found->second.translate, scrubTime_);
                        RemoveKeyframeAt(found->second.rotate,    scrubTime_);
                        RemoveKeyframeAt(found->second.scale,     scrubTime_);
                        ApplyClipToPreview();
                    }
                }
            }
        }
    }
    ImGui::End();
#endif
}

void AnimationEditorScene::RenderTimelinePanel()
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(700, 150), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(280, 600), ImGuiCond_Once);
    if (ImGui::Begin("タイムライン")) {
        if (ImGui::DragFloat("長さ(秒)", &clipDuration_, 0.05f, 0.1f, 30.0f)) {
            clip_.duration = clipDuration_;
            if (scrubTime_ > clipDuration_) { scrubTime_ = clipDuration_; }
        }
        bool timeChanged = ImGui::SliderFloat("時刻", &scrubTime_, 0.0f, clipDuration_);
        if (ImGui::Button("◀ 1フレーム")) {
            scrubTime_ = (std::max)(0.0f, scrubTime_ - GameConstants::kFrameDeltaTime);
            timeChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("1フレーム ▶")) {
            scrubTime_ = (std::min)(clipDuration_, scrubTime_ + GameConstants::kFrameDeltaTime);
            timeChanged = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("再生", &isPlaying_);
        if (isPlaying_) {
            scrubTime_ += GameConstants::kFrameDeltaTime;
            if (scrubTime_ > clipDuration_) { scrubTime_ = 0.0f; }
            timeChanged = true;
        }
        if (timeChanged) { SyncEulerFromSelectedJoint(); }

        // 選択中ジョイントのキーフレーム時刻一覧（クリックでジャンプ）
        if (!selectedJoint_.empty()) {
            auto found = clip_.nodeAnimations.find(selectedJoint_);
            if (found != clip_.nodeAnimations.end()) {
                ImGui::Text("キーフレーム:");
                std::vector<float> times;
                for (const auto& kf : found->second.translate.keyframes) { times.push_back(kf.time); }
                for (const auto& kf : found->second.rotate.keyframes)    { times.push_back(kf.time); }
                std::sort(times.begin(), times.end());
                times.erase(std::unique(times.begin(), times.end(),
                    [](float a, float b) { return std::abs(a - b) < 1e-4f; }), times.end());
                for (float t : times) {
                    ImGui::SameLine();
                    char label[32];
                    std::snprintf(label, sizeof(label), "%.2f", t);
                    if (ImGui::SmallButton(label)) { scrubTime_ = t; SyncEulerFromSelectedJoint(); }
                }
            }
        }
    }
    ImGui::End();
#endif
}

void AnimationEditorScene::RenderReferencePanel()
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(360, 180), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(280, 600), ImGuiCond_Once);
    if (ImGui::Begin("リファレンス再生")) {
        ImGui::TextDisabled("既存クリップを再生して見比べたり、ポーズを取り込んだりできます。");
        if (ImGui::BeginCombo("クリップ", kReferenceAnims[referenceAnimIndex_].label)) {
            for (int i = 0; i < kReferenceAnimCount; ++i) {
                bool selected = (i == referenceAnimIndex_);
                if (ImGui::Selectable(kReferenceAnims[i].label, selected)) { LoadReferenceAnim(i); }
            }
            ImGui::EndCombo();
        }

        float duration = referenceClip_.duration > 0.0f ? referenceClip_.duration : 1.0f;
        ImGui::SliderFloat("時刻", &referenceTime_, 0.0f, duration);
        if (ImGui::Button("◀ 1フレーム")) {
            referenceTime_ = (std::max)(0.0f, referenceTime_ - GameConstants::kFrameDeltaTime);
        }
        ImGui::SameLine();
        if (ImGui::Button("1フレーム ▶")) {
            referenceTime_ = (std::min)(duration, referenceTime_ + GameConstants::kFrameDeltaTime);
        }
        ImGui::SameLine();
        ImGui::Checkbox("再生", &referencePlaying_);
        if (referencePlaying_) {
            referenceTime_ += GameConstants::kFrameDeltaTime;
            if (referenceTime_ > duration) { referenceTime_ = 0.0f; }
        }

        ImGui::Separator();
        if (ImGui::Button("このポーズを編集中クリップへ焼き込む")) {
            BakePoseToClip(scrubTime_);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(ポーズ編集の時刻 %.2f に全ボーン分書き込みます)", scrubTime_);
        if (!statusMessage_.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", statusMessage_.c_str());
        }
    }
    ImGui::End();
#endif
}

void AnimationEditorScene::RenderComboTestPanel()
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(360, 200), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(280, 600), ImGuiCond_Once);
    if (ImGui::Begin("コンボテスト")) {
        ImGui::TextDisabled("L: 近接攻撃（S/↓ 押しながらで打ち上げ）   K: 射撃");
        ImGui::Separator();

        const char* weaponLabel = WeaponTypeLabel(comboWeaponType_);
        if (ImGui::BeginCombo("近接武器", weaponLabel)) {
            for (const auto& wv : kHeldWeaponVisuals) {
                bool selected = (wv.type == comboWeaponType_);
                if (ImGui::Selectable(WeaponTypeLabel(wv.type), selected) && comboWeaponType_ != wv.type) {
                    comboWeaponType_ = wv.type;
                    meleeCombo_.Reset();
                    RefreshComboAnims();
                    if (comboAttackAnimTimer_ <= 0.0f && previewObject_) {
                        previewObject_->SetAnimSpeed(1.0f);
                        previewObject_->SetAnimation(comboIdleAnim_);
                    }
                }
            }
            ImGui::EndCombo();
        }

        const char* gunLabel = GunTypeLabel(comboGunType_);
        if (ImGui::BeginCombo("銃", gunLabel)) {
            for (const auto& gv : kGunVisuals) {
                bool selected = (gv.type == comboGunType_);
                if (ImGui::Selectable(GunTypeLabel(gv.type), selected) && comboGunType_ != gv.type) {
                    comboGunType_ = gv.type;
                    gunCombo_.Reset();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        if (const MeleeAttackDef* atk = meleeCombo_.GetActive()) {
            ImGui::Text("近接: %s (%d段目)", atk->id, meleeCombo_.GetStep());
        } else {
            ImGui::TextDisabled("近接: 待機中");
        }
        if (const GunShotDef* shot = gunCombo_.GetActive()) {
            ImGui::Text("射撃: %s (%d段目)", shot->id, gunCombo_.GetStep());
        } else {
            ImGui::TextDisabled("射撃: 待機中");
        }
    }
    ImGui::End();
#endif
}

void AnimationEditorScene::DrawJointGizmos()
{
#ifdef USE_IMGUI
    if (!previewObject_) { return; }
    DebugDraw::SetCamera(camera_->GetViewProjectionMatrix(),
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight));

    const Skeleton&  skel  = previewObject_->GetSkeleton();
    const Matrix4x4& world = previewObject_->GetWorldMatrix();
    for (const auto& joint : skel.joints) {
        Matrix4x4 jointWorld = Multiply(joint.skeletonSpaceMatrix, world);
        Vector3   p          = { jointWorld.m[3][0], jointWorld.m[3][1], jointWorld.m[3][2] };
        bool selected = (joint.name == selectedJoint_);
        DebugDraw::DrawSphere({ p, selected ? 0.09f : 0.035f },
            selected ? DebugDraw::kColorYellow : DebugDraw::kColorCyan, selected ? 16 : 8);
    }
#endif
}

void AnimationEditorScene::Draw()
{
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();

    // ---- シャドウパス ----
    shadowManager_->BeginShadowPass(cmd);
    modelCommon_->BeginShadowPass();
    shadowManager_->EndShadowPass(cmd);

    // ---- メイン3D描画 ----
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon_->GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = { 0, 0,
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight),
        0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(cmd);
    shadowManager_->SetShadowMap(cmd, srvManager_);

    if (heldWeaponObject_) { heldWeaponObject_->Draw(); }
    if (gunObject_)        { gunObject_->Draw(); }
    if (previewObject_)    { previewObject_->Draw(); }
}

void AnimationEditorScene::Finalize()
{
}
