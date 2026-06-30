/**
 * @file ShopScene.h
 * @brief ローグライトのスキル選択ショップシーンを定義するファイル
 */
#pragma once
#include "Audio.h"
#include "BaseScene.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "RunData.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <memory>
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::graphics::ImGuiManager;
using engine::Input;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/**
 * @brief スキル選択ショップシーン
 * @note 未取得スキルからランダムに最大3つを提示し、
 * プレイヤーが1つを選択して RunData に追加する。スキップも可能
 */
class ShopScene : public BaseScene {
public:
    /** @brief シーンの初期化。未取得スキルからランダム3択を構築する */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /** @brief リソースを解放する */
    void Finalize() override;
    /** @brief 入力によるスキル選択を更新し、確定後にマップへ遷移する */
    void Update() override;
    /** @brief スキルカードと選択状態を描画する */
    void Draw() override;
    /** @brief ImGui マネージャーを設定する */
    void SetImGuiManager(ImGuiManager* imgui) override { imguiManager_ = imgui; }

private:
    DirectXCommon* dxCommon_     = nullptr;
    Input*         input_        = nullptr;
    Audio*         audio_        = nullptr;
    ImGuiManager*  imguiManager_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    std::unique_ptr<Sprite>       bgSprite_;
    std::unique_ptr<Sprite>       cardSprite_;

    FontRenderer fontRenderer_;

    // 提示するスキル3種
    RunData::Skill offered_[3] = {
        RunData::Skill::BlinkPlus,
        RunData::Skill::ComboExtend,
        RunData::Skill::FastFire
    };
    int offerCount_ = 0; // 実際に提示できる数

    bool done_      = false;
    float doneTimer_= 0.0f;
    int   chosen_   = -1; // 選択されたインデックス（-1=スキップ）
};

} // namespace engine::game
