/**
 * @file TrainingHud.h
 * @brief トレーニングの説明・ナビゲーション表示を担当するHUD要素を定義するファイル
 */
#pragma once
#include "HudLayer.h"
#include "Sprite.h"
namespace engine::game {
/** @brief トレーニングの説明・ナビゲーション・背景パネルを管理する。 */
class TrainingHud final : public IHudWidget {
public:
    void Initialize(const HudServices& services) override;
    void Update(const HudFrame& frame) override;
    void QueueText(FontRenderer& font) const override;
    void Draw() override;
private:
    WeaponManager* weapons_ = nullptr;
    Vector2 weaponAnchor_ = {};
    Vector2 controlsAnchor_ = {};
    std::unique_ptr<engine::graphics::Sprite> navigation_;
    std::unique_ptr<engine::graphics::Sprite> accent_;
    std::unique_ptr<engine::graphics::Sprite> weaponPanel_;
    std::unique_ptr<engine::graphics::Sprite> controlsPanel_;
};
}
