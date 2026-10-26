/**
 * @file AwakenGaugeHud.h
 * @brief 覚醒ゲージの表示を担当するHUD要素を定義するファイル
 */
#pragma once
#include "HudLayer.h"
#include "Sprite.h"
#include <string>

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
    std::unique_ptr<engine::graphics::Sprite> marker_; ///< 覚醒できる量を示す縦線
    float gauge_ = 0.0f;
    Vector2 position_ = {}; ///< ゲージ左上（UILayoutから毎フレーム読む）
    bool awakened_ = false;
    float pulse_ = 1.0f;
    float threshold_ = 0.0f; ///< 覚醒できるゲージ量（player_params.json の awakenActivationThreshold）
    bool awakenReady_ = false; ///< 今 R で覚醒できるか
    bool finisherReady_ = false; ///< 今 F でフィニッシャーを出せるか（満タン）
    std::wstring awakenPrompt_; ///< 覚醒の案内（割り当てたキー名に置き換え済み）
    std::wstring finisherPrompt_; ///< フィニッシャーの案内（割り当てたキー名に置き換え済み）
};
}
