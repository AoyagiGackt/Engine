#include "TrainingWeaponPickups.h"
#include "ModelManager.h"
#include <cmath>
using namespace engine;
using namespace engine::graphics;
namespace engine::game {
namespace {
constexpr float kFirstPickupX = 4.5f;
constexpr float kPickupSpacing = 3.0f;
constexpr float kPickupY = 3.0f;
constexpr float kTouchRadius = 1.0f;
constexpr float kHoverSpeed = 2.5f;
constexpr float kHoverAmplitude = 0.15f;
constexpr float kTilt = 0.2f;
constexpr float kSpinSpeed = 0.8f;
constexpr Vector4 kTouchingColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kIdleColor = { 0.75f, 0.85f, 1.0f, 1.0f };
}
void TrainingWeaponPickups::Initialize(ModelCommon* models)
{
    modelCommon_ = models;

    struct PickupAsset {
        WeaponType type;
        const char* modelPath;
        const char* texturePath;
        float scale;
    };
    static constexpr PickupAsset kPickupAssets[] = {
        { WeaponType::Sword, "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png", 0.28f },
        { WeaponType::Spear, "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png", 0.13f },
        { WeaponType::Hammer, "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png", 0.27f },
        { WeaponType::Dagger, "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png", 0.42f },
    };
    static_assert(std::size(kPickupAssets) == kPickupCount, "本編で手に入る武器だけを並べる");
    for (int i = 0; i < static_cast<int>(pickups_.size()); ++i) {
        const PickupAsset& asset = kPickupAssets[i];
        Pickup& pickup = pickups_[i];
        pickup.type = asset.type;
        pickup.position = { kFirstPickupX + static_cast<float>(i) * kPickupSpacing, kPickupY, 0.0f };
        pickup.model = ModelManager::GetInstance()->GetOrLoad(modelCommon_, asset.modelPath, asset.texturePath);
        pickup.object = std::make_unique<Object3d>();
        pickup.object->Initialize(modelCommon_);
        pickup.object->SetModel(pickup.model);
        pickup.object->SetPosition(pickup.position);
        pickup.object->SetScale({ asset.scale, asset.scale, asset.scale });
        pickup.object->SetEnableLighting(true);
        pickup.object->Update();
    }
}
bool TrainingWeaponPickups::Update(const Vector3& playerPosition, WeaponManager& weapons, float deltaSeconds)
{
    bool equipped = false;

    pulseTime_ += deltaSeconds;
    for (int i = 0; i < static_cast<int>(pickups_.size()); ++i) {
        Pickup& pickup = pickups_[i];
        const float dx = playerPosition.x - pickup.position.x;
        const float dy = playerPosition.y - pickup.position.y;
        const bool touching = dx * dx + dy * dy <= kTouchRadius * kTouchRadius;
        if (touching && !pickup.wasTouching) {
            weapons.EquipForTraining(pickup.type);
            equipped = true;
        }
        pickup.wasTouching = touching;

        const float hover = std::sin(pulseTime_ * kHoverSpeed + static_cast<float>(i)) * kHoverAmplitude;
        pickup.object->SetPosition({ pickup.position.x, pickup.position.y + hover, pickup.position.z });
        pickup.object->SetRotation({ kTilt, pulseTime_ * kSpinSpeed, 0.0f });
        pickup.object->SetColor(touching ? kTouchingColor : kIdleColor);
        pickup.object->Update();
    }

    return equipped;
}
void TrainingWeaponPickups::RefreshVisuals() { for (auto& pickup : pickups_) { pickup.object->Update(); } }
void TrainingWeaponPickups::Draw() { for (auto& pickup : pickups_) { pickup.object->Draw(); } }
}
