/**
 * @file WeaponSlotHud.h
 * @brief 武器枠と所持武器アイコンの表示を担当するHUD要素を定義するファイル
 */
#pragma once
#include "HudLayer.h"
#include "Object3d.h"
#include "Sprite.h"
#include "WeaponManager.h"
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
    static constexpr int kSlotCount = WeaponManager::kSlotCount;
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
    /** @brief UILayoutから枠の位置・間隔を読み、枠とアイコンを並べ直す（エディタで動かした結果を毎フレーム反映する） */
    void ApplyLayout();
    bool HasModelIcon(int weaponIndex) const;

    /** @brief 1枠ぶんのアイコンを描き分けるのに使う、そのフレームの状態 */
    struct SlotIconState {
        int weaponIndex = -1;
        bool active = false; ///< 選択中の枠か
        bool unlocked = false; ///< 武器を手に入れているか
        float pulse = 0.0f; ///< 選択中の明滅
        float flash = 0.0f; ///< 武器を手に入れた直後の光り（0〜1）
        Vector3 cameraPosition {};
    };
    /** @brief 武器の3Dモデルを枠の位置に浮かべる */
    void ShowModelIcon(Slot& slot, const SlotIconState& state);
    /** @brief 武器の色で塗ったアイコンを出す */
    void ShowColorIcon(Slot& slot, const SlotIconState& state);
    /** @brief まだ手に入れていない枠を暗く出す */
    void ShowLockedIcon(Slot& slot, const SlotIconState& state);
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
