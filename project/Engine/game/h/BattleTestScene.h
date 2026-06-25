#pragma once
#include <array>
#include <memory>
#include <vector>

#include "Audio.h"
#include "BaseScene.h"
#include "BulletPool.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ParticleManager.h"
#include "Player.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"
#include "WeaponManager.h"

class BattleTestScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;
    void SetImGuiManager(ImGuiManager* imgui) override { imguiManager_ = imgui; }

private:
    // 訓練用マネキン（動かない敵）
    struct Dummy {
        std::unique_ptr<Object3d> object;
        Vector3  pos;
        Vector3  homePos;
        float    hp;
        float    maxHp;
        float    hitFlash    = 0.0f; // 被弾時の白フラッシュタイマー
        float    hpDisplay_  = 1.0f; // HP バー表示用（0→1 に回復する演出値）
        float    knockVelX   = 0.0f;
        float    knockVelY   = 0.0f;
        float    returnTimer = 0.0f; // 最後に被弾してからの秒数（超えたら中央へ戻る）

        // HP バー用スプライト
        std::unique_ptr<Sprite> hpBarBg;
        std::unique_ptr<Sprite> hpBarFg;
    };

    void SpawnHitEffect(const Vector3& pos);
    void UpdateHpBars();

    DirectXCommon* dxCommon_     = nullptr;
    Input*         input_        = nullptr;
    Audio*         audio_        = nullptr;
    ImGuiManager*  imguiManager_ = nullptr;
    SrvManager*    srvManager_   = nullptr;
    ParticleManager* pm_         = nullptr;

    std::unique_ptr<SpriteCommon>    spriteCommon_;
    std::unique_ptr<ModelCommon>     modelCommon_;
    std::unique_ptr<Object3dCommon>  objectCommon_;
    std::unique_ptr<ShadowManager>   shadowManager_;
    std::unique_ptr<Camera>          camera_;

    // 境界ブロック
    std::unique_ptr<Model>                  modelBlock_;
    std::vector<std::unique_ptr<Object3d>>  borderBlocks_;

    // ワープポータル（トレーニングルームへ戻る）
    std::vector<std::unique_ptr<Object3d>>  warpPortalBlocks_;

    // プレイヤー
    std::unique_ptr<Player> player_;

    // 訓練マネキン
    std::unique_ptr<Model>   modelDummy_;
    std::vector<Dummy>       dummies_;

    // 武器
    WeaponManager* weaponManager_ = nullptr;
    float attackCooldown_  = 0.0f;

    // 弾丸
    BulletPool bulletPool_;

    // ワープ演出
    float warpPulseTimer_ = 0.0f;

    // 覚醒ゲージ UI
    std::unique_ptr<Sprite> awakenGaugeBg_;
    std::unique_ptr<Sprite> awakenGaugeFg_;

    FontRenderer fontRenderer_;
};
