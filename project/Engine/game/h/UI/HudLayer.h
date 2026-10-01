/**
 * @file HudLayer.h
 * @brief HUD要素の共通インターフェースと、それらを一括で更新・描画するHudLayerを定義するファイル
 */
#pragma once
#include "Vector2.h"
#include <memory>
#include <vector>

namespace engine { class DirectXCommon; }
namespace engine::graphics { class Camera; class ModelCommon; class SpriteCommon; }
namespace engine::game {
class FontRenderer;
class WeaponManager;

// 初期化時に借りる描画基盤。所有権はシーンから移さない。
struct HudServices {
    engine::graphics::SpriteCommon& sprites;
    engine::graphics::ModelCommon& models;
    engine::DirectXCommon& graphics;
    WeaponManager& weapons;
};
// レベル側にHUDアンカーが置かれていない時の既定表示位置（画面左上基準のピクセル座標）
inline constexpr Vector2 kDefaultHudWeaponAnchor = { 12.0f, 12.0f };
inline constexpr Vector2 kDefaultHudControlsAnchor = { 800.0f, 12.0f };

// そのフレームの表示に必要な値だけを渡す。
struct HudFrame {
    const engine::graphics::Camera& camera;
    float deltaSeconds = 0.0f;
    float awakenGauge = 0.0f;
    bool awakened = false;
    float pulseTime = 0.0f;
    Vector2 weaponAnchor = kDefaultHudWeaponAnchor;
    Vector2 controlsAnchor = kDefaultHudControlsAnchor;
};
enum class HudEvent { WeaponAcquired };

/** @brief 表示要素の共通契約。実装側だけがスプライトや演出状態を操作する。 */
class IHudWidget {
public:
    virtual ~IHudWidget() = default;
    virtual void Initialize(const HudServices& services) = 0;
    virtual void Update(const HudFrame& frame) = 0;
    virtual void QueueText(FontRenderer& font) const = 0;
    virtual void Draw() = 0;
    virtual void Notify(HudEvent) {}
};

/** @brief HUD要素を所有し、具体的な型に依存せず更新・描画する。 */
class HudLayer {
public:
    void Add(std::unique_ptr<IHudWidget> widget) { widgets_.push_back(std::move(widget)); }
    void Initialize(const HudServices& services) { for (auto& w : widgets_) { w->Initialize(services); } }
    void Update(const HudFrame& frame) { for (auto& w : widgets_) { w->Update(frame); } }
    void QueueText(FontRenderer& font) const { for (const auto& w : widgets_) { w->QueueText(font); } }
    void Draw() { for (auto& w : widgets_) { w->Draw(); } }
    void Notify(HudEvent event) { for (auto& w : widgets_) { w->Notify(event); } }
private:
    std::vector<std::unique_ptr<IHudWidget>> widgets_;
};
}
