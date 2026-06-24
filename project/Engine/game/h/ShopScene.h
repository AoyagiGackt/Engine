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

class ShopScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;
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
