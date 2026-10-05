/**
 * @file AwakenGaugeHud.h
 * @brief 覚醒ゲージの表示を担当するHUD要素を定義するファイル
 */
#pragma once
#include "HudLayer.h"
#include "Sprite.h"

namespace engine::game {
/** @brief 覚醒ゲージの資源・表示値・ラベルを外へ露出させず管理する。 */
class AwakenGaugeHud final : public IHudWidget {
public:
    void Initialize(const HudServices& services) override;
    void Update(const HudFrame& frame) override;
    void QueueText(FontRenderer& font) const override;
    void Draw() override;
private:
    std::unique_ptr<engine::graphics::Sprite> background_;
    std::unique_ptr<engine::graphics::Sprite> foreground_;
    float gauge_ = 0.0f;
    Vector2 position_ = {}; ///< ゲージ左上（UILayoutから毎フレーム読む）
    bool awakened_ = false;
    float pulse_ = 1.0f;
};
}
