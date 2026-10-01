#include "WeaponSlotHud.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "ModelManager.h"
#include "SceneShared.h"
#include "SpriteCommon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
using namespace engine;
using namespace engine::graphics;
namespace engine::game {
namespace {
// 枠の配置（画面左下から横に並べる）
constexpr float kSlotsLeft = 24.0f;
constexpr float kSlotsTop = 630.0f;
constexpr float kSlotPitch = 66.0f; // 隣の枠までの間隔
constexpr float kGunFrameGap = 24.0f; // 武器枠の列と銃枠の間の余白
constexpr float kIconInset = 6.0f; // 枠の内側に色アイコンを置く余白
constexpr float kGunIconSize = 36.0f;

// 枠とアイコンの色
constexpr Vector4 kSlotFrameColor = { 0.08f, 0.08f, 0.13f, 0.85f };
constexpr Vector4 kGunFrameColor = { 0.08f, 0.08f, 0.1f, 0.85f };
constexpr Vector4 kEmptyIconColor = { 0.2f, 0.2f, 0.2f, 0.35f };
constexpr Vector4 kGunIconColor = { 0.6f, 0.85f, 1.0f, 0.9f };
constexpr Vector4 kHiddenColor = { 0.0f, 0.0f, 0.0f, 0.0f };
constexpr float kFrameAlpha = 0.85f;
constexpr float kUnlockedIconAlpha = 0.95f;

// 選択中の枠の明滅と入手時のフラッシュ
constexpr float kPulseBase = 0.7f;
constexpr float kPulseAmplitude = 0.3f;
constexpr float kPulseSpeed = 6.0f;
constexpr float kFrameBrightnessBase = 0.08f;
constexpr float kFrameActivePulseGain = 0.35f;
constexpr float kFrameFlashGain = 0.5f;
constexpr float kFrameActiveBlueBoost = 0.2f;
constexpr float kFrameIdleBlueBoost = 0.05f;

// 3Dモデルアイコン（カメラ手前のワールド座標に置く）
constexpr float kIconDepth = 6.0f; // カメラからの距離
constexpr float kDepthScale = kIconDepth / -GameConstants::kCameraDistanceZ; // 画面端までの見かけの広さをこの距離に合わせる比率
constexpr float kIconTilt = 0.3f;
constexpr float kIconSwingSpeed = 0.8f;
constexpr float kIconSwingAmplitude = 0.35f;
constexpr float kModelActiveBrightness = 0.9f;
constexpr float kModelActivePulseGain = 0.1f;
constexpr float kModelIdleBrightness = 0.6f;
constexpr float kModelFlashGain = 0.4f;

// 色アイコン（モデルがない武器）
constexpr float kColorActiveBase = 0.7f;
constexpr float kColorActivePulseGain = 0.3f;
constexpr float kColorIdle = 0.5f;
constexpr float kColorFlashGain = 0.5f;
constexpr float kLockedBrightness = 0.2f;
constexpr float kLockedFlashGain = 0.6f;
constexpr float kLockedAlpha = 0.35f;
constexpr float kLockedAlphaFlashGain = 0.3f;
constexpr float kGunIconSpinSpeed = 0.6f;

// 文字
constexpr Vector2 kLockedMarkOffset = { 22.0f, 16.0f };
constexpr float kLockedMarkScale = 1.6f;
constexpr Vector4 kLockedMarkColor = { 0.6f, 0.6f, 0.6f, 0.9f };
constexpr Vector2 kSlotNumberOffset = { 4.0f, 3.0f };
constexpr float kSlotNumberScale = 0.9f;
constexpr Vector4 kSlotNumberColor = { 0.95f, 0.9f, 0.75f, 1.0f };
constexpr Vector2 kGunLabelOffset = { 10.0f, 60.0f };
constexpr float kGunLabelScale = 1.0f;
}

void WeaponSlotHud::Initialize(const HudServices& services)
{
    sprites_ = &services.sprites;
    models_ = &services.models;
    graphics_ = &services.graphics;
    weapons_ = &services.weapons;
    InitializeFrames();
    const auto assets = SceneShared::LoadWeaponIconAssets("Resources/Config/weapon_icons.json");
    const auto& list = weapons_->GetList();
    modelIcons_.resize(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        for (const auto& asset : assets) {
            if (asset.type != list[i].type) { continue; }
            auto& icon = modelIcons_[i];
            icon.scale = asset.scale;
            icon.baseYaw = asset.baseYaw;
            icon.object = std::make_unique<Object3d>();
            icon.object->Initialize(models_);
            icon.object->SetModel(ModelManager::GetInstance()->GetOrLoad(models_, asset.modelPath, asset.texturePath));
            icon.object->SetEnableLighting(true);
            break;
        }
    }
}
void WeaponSlotHud::InitializeFrames()
{
    auto create = [&](Vector2 position, Vector2 size) {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(sprites_, "Resources/white.png");
        sprite->SetPosition(position);
        sprite->SetSize(size);
        return sprite;
    };
    const float iconSize = kSlotSize - kIconInset * 2.0f;
    for (int i = 0; i < kSlotCount; ++i) {
        auto& slot = slots_[i];
        slot.position = { kSlotsLeft + i * kSlotPitch, kSlotsTop };
        slot.frame = create(slot.position, { kSlotSize, kSlotSize });
        slot.frame->SetColor(kSlotFrameColor);
        slot.frame->Update();
        slot.icon = create({ slot.position.x + kIconInset, slot.position.y + kIconInset }, { iconSize, iconSize });
        slot.icon->SetColor(kEmptyIconColor);
        slot.icon->Update();
    }
    gunPosition_ = { kSlotsLeft + kSlotCount * kSlotPitch + kGunFrameGap, kSlotsTop };
    gunFrame_ = create(gunPosition_, { kSlotSize, kSlotSize });
    gunFrame_->SetColor(kGunFrameColor);
    gunFrame_->Update();
    gunIcon_ = create({ gunPosition_.x + kSlotSize * 0.5f, gunPosition_.y + kSlotSize * 0.5f }, { kGunIconSize, kGunIconSize });
    gunIcon_->SetAnchorPoint({ 0.5f, 0.5f });
    gunIcon_->SetColor(kGunIconColor);
    gunIcon_->Update();
}
bool WeaponSlotHud::HasModelIcon(int index) const
{
    return index >= 0 && index < static_cast<int>(modelIcons_.size())
        && weapons_->IsUnlocked(index) && modelIcons_[index].object;
}
void WeaponSlotHud::Notify(HudEvent event)
{
    if (event == HudEvent::WeaponAcquired) { flashRemaining_ = kFlashDuration; }
}
void WeaponSlotHud::Update(const HudFrame& frame)
{
    pulseTime_ += frame.deltaSeconds;
    flashRemaining_ = (std::max)(0.0f, flashRemaining_ - frame.deltaSeconds);
    const float flash = flashRemaining_ / kFlashDuration;
    const float pulse = kPulseBase + kPulseAmplitude * std::sin(pulseTime_ * kPulseSpeed);
    const auto& list = weapons_->GetList();
    const auto& cam = frame.camera.GetTranslate();
    for (int i = 0; i < kSlotCount; ++i) {
        auto& slot = slots_[i];
        const bool active = i == weapons_->GetSelectedSlot();
        const float brightness = kFrameBrightnessBase + (active ? pulse * kFrameActivePulseGain : 0.0f) + flash * kFrameFlashGain;
        slot.frame->SetColor({ brightness, brightness, brightness + (active ? kFrameActiveBlueBoost : kFrameIdleBlueBoost), kFrameAlpha });
        slot.frame->Update();
        const int index = weapons_->GetSlotWeaponIndex(i);
        const bool unlocked = index >= 0 && index < static_cast<int>(list.size()) && weapons_->IsUnlocked(index);
        if (HasModelIcon(index)) {
            slot.icon->SetColor(kHiddenColor);
            auto& icon = modelIcons_[index];
            const float sx = slot.position.x + kSlotSize * 0.5f;
            const float sy = slot.position.y + kSlotSize * 0.5f;
            icon.object->SetPosition({
                cam.x + (sx - GameConstants::kScreenCenterX) / GameConstants::kScreenCenterX * GameConstants::kCameraHalfW * kDepthScale,
                cam.y - (sy - GameConstants::kScreenCenterY) / GameConstants::kScreenCenterY * GameConstants::kCameraHalfH * kDepthScale,
                cam.z + kIconDepth });
            icon.object->SetRotation({ kIconTilt, icon.baseYaw + std::sin(pulseTime_ * kIconSwingSpeed) * kIconSwingAmplitude, 0.0f });
            const float scale = icon.scale * kDepthScale;
            icon.object->SetScale({ scale, scale, scale });
            const float b = (active ? (kModelActiveBrightness + pulse * kModelActivePulseGain) : kModelIdleBrightness) + flash * kModelFlashGain;
            icon.object->SetColor({ b, b, b, 1.0f });
            icon.object->Update();
        } else if (unlocked) {
            const float m = (active ? (kColorActiveBase + pulse * kColorActivePulseGain) : kColorIdle) + flash * kColorFlashGain;
            const float* c = list[index].styleColor;
            slot.icon->SetColor({ c[0] * m, c[1] * m, c[2] * m, kUnlockedIconAlpha });
        } else {
            const float b = kLockedBrightness + flash * kLockedFlashGain;
            slot.icon->SetColor({ b, b, b, kLockedAlpha + flash * kLockedAlphaFlashGain });
        }
        slot.icon->Update();
    }
    gunIcon_->SetRotation(pulseTime_ * kGunIconSpinSpeed);
    gunIcon_->Update();
}
void WeaponSlotHud::QueueText(FontRenderer& font) const
{
    for (int i = 0; i < kSlotCount; ++i) {
        const int index = weapons_->GetSlotWeaponIndex(i);
        if (index < 0 || !weapons_->IsUnlocked(index)) {
            font.DrawStringW(L"?", slots_[i].position.x + kLockedMarkOffset.x, slots_[i].position.y + kLockedMarkOffset.y,
                kLockedMarkScale, kLockedMarkColor);
        }
        if (showNumbers_) {
            font.DrawString(std::to_string(i + 1), slots_[i].position.x + kSlotNumberOffset.x, slots_[i].position.y + kSlotNumberOffset.y,
                kSlotNumberScale, kSlotNumberColor);
        }
    }
    font.DrawString("GUN", gunPosition_.x + kGunLabelOffset.x, gunPosition_.y + kGunLabelOffset.y, kGunLabelScale, kGunIconColor);
}
void WeaponSlotHud::Draw()
{
    for (auto& slot : slots_) { slot.frame->Draw(); }
    gunFrame_->Draw();
    models_->CommonDrawSettings();
    Object3d::RebindCommonLighting(graphics_->GetCommandList());
    for (int i = 0; i < kSlotCount; ++i) {
        const int index = weapons_->GetSlotWeaponIndex(i);
        if (HasModelIcon(index)) { modelIcons_[index].object->Draw(); }
    }
    sprites_->CommonDrawSettings();
    for (auto& slot : slots_) { slot.icon->Draw(); }
    gunIcon_->Draw();
}
}
