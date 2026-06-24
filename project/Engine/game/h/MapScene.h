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
#include <vector>

class MapScene : public BaseScene {
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
    std::unique_ptr<Sprite>       bgSprite_;   // 黒背景
    std::unique_ptr<Sprite>       nodeSprite_; // ノードボックス（都度色変え）

    FontRenderer fontRenderer_;

    // マップ定義: floors_[floor][col] = NodeType
    std::vector<std::vector<RunData::NodeType>> floors_;

    // 選択状態
    int   selectedCol_    = 0;
    bool  waitingResult_  = false; // RESTノード後の待機
    float waitTimer_      = 0.0f;
    int   restHealAmount_ = 0;
};
