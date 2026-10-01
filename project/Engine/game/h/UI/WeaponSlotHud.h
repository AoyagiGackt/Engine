/**
 * @file WeaponSlotHud.h
 * @brief 武器枠と所持武器アイコンの表示を担当するHUD要素を定義するファイル
 */
#pragma once
#include "HudLayer.h"
#include "Object3d.h"
#include "Sprite.h"
#include <array>

namespace engine::game {
/** @brief 武器枠・実物アイコン・選択演出をまとめて管理する。 */
class WeaponSlotHud final : public IHudWidget {
public:
    explicit WeaponSlotHud(bool showNumbers = false) : showNumbers_(showNumbers) {}
    void Initialize(const HudServices& services) override;
    void Update(const HudFrame& frame) override;
    void QueueText(FontRenderer& font) const override;
    void Draw() override;
    void Notify(HudEvent event) override;
private:
    static constexpr int kSlotCount = 4;
    static constexpr float kSlotSize = 56.0f;
    static constexpr float kFlashDuration = 0.35f;
    struct Slot {
        std::unique_ptr<engine::graphics::Sprite> frame;
        std::unique_ptr<engine::graphics::Sprite> icon;
        Vector2 position = {};
    };
    struct ModelIcon {
        std::unique_ptr<engine::graphics::Object3d> object;
        float scale = 0.2f;
        float baseYaw = 0.0f;
    };
    void InitializeFrames();
    bool HasModelIcon(int weaponIndex) const;
    engine::graphics::SpriteCommon* sprites_ = nullptr;
    engine::graphics::ModelCommon* models_ = nullptr;
    engine::DirectXCommon* graphics_ = nullptr;
    WeaponManager* weapons_ = nullptr;
    std::array<Slot, kSlotCount> slots_;
    std::vector<ModelIcon> modelIcons_;
    std::unique_ptr<engine::graphics::Sprite> gunFrame_;
    std::unique_ptr<engine::graphics::Sprite> gunIcon_;
    Vector2 gunPosition_ = {};
    float pulseTime_ = 0.0f;
    float flashRemaining_ = 0.0f;
    bool showNumbers_ = false;
};
}
