/**
 * @file MapScene.h
 * @brief ローグライトのフロア選択マップシーンを定義するファイル
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
#include <vector>
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::graphics::ImGuiManager;
using engine::Input;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;

/**
 * @brief スレイザスパイア式のフロア選択マップシーン
 * @note 4フロア×最大3列のノード（FIGHT/ELITE/SHOP/REST/BOSS）を表示し、
 * プレイヤーが次に挑むノードを選択する。選択後は対応するシーンへ遷移する
 */
class MapScene : public BaseScene {
public:
    /** @brief シーンの初期化。スプライト・フォント・マップ定義を構築する */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /** @brief リソースを解放する */
    void Finalize() override;
    /** @brief 入力によるノード選択と遷移判定を更新する */
    void Update() override;
    /** @brief マップノードと現在選択状態を描画する */
    void Draw() override;
    /** @brief ImGui マネージャーを設定する */
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

} // namespace engine::game
