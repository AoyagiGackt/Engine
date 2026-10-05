/**
 * @file TrainingWeaponPickups.h
 * @brief トレーニングルームに配置された武器の表示と取得判定を管理するクラスを定義するファイル
 */
#pragma once
#include "Object3d.h"
#include "WeaponManager.h"
#include <array>
#include <memory>
namespace engine::graphics { class ModelCommon; class Model; }
namespace engine::game {
/** @brief 配置武器の見た目と接触を管理し、装備した事実だけをシーンへ返す。 */
class TrainingWeaponPickups {
public:
    void Initialize(engine::graphics::ModelCommon* models);
    bool Update(const Vector3& playerPosition, WeaponManager& weapons, float deltaSeconds);
    void RefreshVisuals();
    void Draw();
private:
    struct Pickup {
        WeaponType type = WeaponType::Sword;
        engine::graphics::Model* model = nullptr;
        std::unique_ptr<engine::graphics::Object3d> object;
        Vector3 position = {};
        bool wasTouching = false;
    };
    engine::graphics::ModelCommon* modelCommon_ = nullptr;
    /** @brief 並べる武器の数（本編で敵から奪える剣・槍・ハンマー・短剣だけ） */
    static constexpr int kPickupCount = 4;
    std::array<Pickup, kPickupCount> pickups_;
    float pulseTime_ = 0.0f;
};
}
