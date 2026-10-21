/**
 * @file HudLayer.h
 * @brief HUD要素の共通インターフェースと、それらを一括で更新・描画するHudLayerを定義するファイル
 */
#pragma once
#include "Vector2.h"
#include <memory>
#include <vector>

namespace engine {
    class DirectXCommon;
}
namespace engine::graphics {
    class Camera;
    class ModelCommon;
    class SpriteCommon;
}
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
// 武器一覧パネルの既定表示位置（画面左上基準のピクセル座標。各シーンのUILayoutで上書きできる）
inline constexpr Vector2 kDefaultHudWeaponAnchor = { 12.0f, 12.0f };

// そのフレームの表示に必要な値だけを渡す。
struct HudFrame {
    const engine::graphics::Camera& camera;
    float deltaSeconds = 0.0f;
    float awakenGauge = 0.0f;
    bool awakened = false;
    float pulseTime = 0.0f;
};
enum class HudEvent { WeaponAcquired };

/** @brief 表示要素の共通契約。実装側だけがスプライトや演出状態を操作する。 */
class IHudWidget {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IHudWidget() = default;
    /** @brief HUD要素の描画資源を用意する */
    virtual void Initialize(const HudServices& services) = 0;
    /** @brief HUD要素の表示をこのフレームの状態に合わせて更新する */
    virtual void Update(const HudFrame& frame) = 0;
    /** @brief HUD要素の文字を描画予約する */
    virtual void QueueText(FontRenderer& font) const = 0;
    /** @brief HUD要素を描画する */
    virtual void Draw() = 0;
    /** @brief HUDへの知らせを受け取る（既定では何もしない） */
    virtual void Notify(HudEvent) {}
};

/** @brief HUD要素を所有し、具体的な型に依存せず更新・描画する。 */
class HudLayer {
public:
    /** @brief HUD要素を追加する */
    void Add(std::unique_ptr<IHudWidget> widget) {
        widgets_.push_back(std::move(widget));
    }
    /** @brief すべてのHUD要素を初期化する */
    void Initialize(const HudServices& services) {
        for (auto& w : widgets_)
        {
            w->Initialize(services);
        }
    }
    /** @brief すべてのHUD要素を更新する */
    void Update(const HudFrame& frame) {
        for (auto& w : widgets_)
        {
            w->Update(frame);
        }
    }
    /** @brief すべてのHUD要素の文字を描画予約する */
    void QueueText(FontRenderer& font) const {
        for (const auto& w : widgets_)
        {
            w->QueueText(font);
        }
    }
    /** @brief すべてのHUD要素を描画する */
    void Draw() {
        for (auto& w : widgets_)
        {
            w->Draw();
        }
    }
    /** @brief すべてのHUD要素へ知らせを渡す */
    void Notify(HudEvent event) {
        for (auto& w : widgets_)
        {
            w->Notify(event);
        }
    }
private:
    std::vector<std::unique_ptr<IHudWidget>> widgets_;
};
}
