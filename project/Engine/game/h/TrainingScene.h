#pragma once
#include <memory>
#include <vector>

#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Player.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "WeaponManager.h"

class TrainingScene : public BaseScene {
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
    SrvManager*    srvManager_   = nullptr;

    std::unique_ptr<SpriteCommon>    spriteCommon_;
    std::unique_ptr<ModelCommon>     modelCommon_;
    std::unique_ptr<Object3dCommon>  objectCommon_;
    std::unique_ptr<ShadowManager>   shadowManager_;
    std::unique_ptr<Camera>          camera_;

    // 境界ブロック
    std::unique_ptr<Model>                  modelBlock_;
    std::vector<std::unique_ptr<Object3d>>  borderBlocks_;

    // ワープポータル（テストステージへ）
    std::vector<std::unique_ptr<Object3d>>  warpPortalBlocks_;

    // プレイヤー
    std::unique_ptr<Player> player_;

    // 武器選択
    WeaponManager* weaponManager_ = nullptr;
    float weaponCycleTimer_ = 0.0f;

    // ワープ演出タイマー（近づいたら点滅）
    float warpPulseTimer_ = 0.0f;

    // コンボランク
    int   trComboCount_ = 0;
    int   trMaxCombo_   = 0;
    float trComboTimer_ = 0.0f;
    float trRankAlpha_  = 0.0f;

    // 覚醒ゲージ UI
    std::unique_ptr<Sprite> awakenGaugeBg_;
    std::unique_ptr<Sprite> awakenGaugeFg_;

    FontRenderer fontRenderer_;
};
