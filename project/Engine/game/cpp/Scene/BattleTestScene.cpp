#include "BattleTestScene.h"
#include "BorderBlockBuilder.h"
#include "Collision.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "PostEffectRenderTarget.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

static constexpr float kWarpRetX   =  3.0f;
static constexpr float kReturnProx =  3.0f;
static constexpr float kDummyMaxHp = 100.0f;

void BattleTestScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_ = dxCommon;
    input_    = input;
    audio_    = audio;

    srvManager_    = SrvManager::GetInstance();
    weaponManager_ = WeaponManager::GetInstance();
    pm_            = ParticleManager::GetInstance();

    grayscaleEffect_ = GrayscaleEffect::GetInstance();
    imageFilter_     = ImageFilter::GetInstance();
    hsvFilter_       = HsvFilter::GetInstance();

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);

    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, srvManager_);

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 14.5f, 6.0f, -30.0f });
    Object3d::SetCommonCamera(camera_.get());

    modelBlock_ = std::make_unique<Model>();
    modelBlock_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");

    BuildBorderBlocks(modelCommon_.get(), modelBlock_.get(), borderBlocks_);

    for (int i = 0; i < 5; ++i) {
        auto p = std::make_unique<Object3d>();
        p->Initialize(modelCommon_.get());
        p->SetModel(modelBlock_.get());
        p->SetEnableLighting(false);
        p->SetPosition({ kWarpRetX, 0.4f + static_cast<float>(i) * 1.0f, 0.0f });
        p->SetColor({ 1.0f, 0.5f, 0.1f, 0.9f });
        p->Update();
        warpPortalBlocks_.push_back(std::move(p));
    }

    modelDummy_ = std::make_unique<Model>();
    modelDummy_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/monsterBall.png");

    pm_->CreateParticleGroup("bt_hit_ring",    "Resources/circle2.png");
    pm_->CreateParticleGroup("bt_hit_spark",   "Resources/circle2.png");
    pm_->CreateParticleGroup("bt_sword_slash", "Resources/circle2.png");
    pm_->SetAdditiveBlend("bt_hit_ring",    true);
    pm_->SetAdditiveBlend("bt_hit_spark",   true);
    pm_->SetAdditiveBlend("bt_sword_slash", true);

    {
        Dummy d;
        d.pos      = { 15.0f, 0.4f, 0.0f };
        d.homePos  = d.pos;
        d.hp       = kDummyMaxHp;
        d.maxHp    = kDummyMaxHp;
        d.hitFlash = 0.0f;

        d.object = std::make_unique<Object3d>();
        d.object->Initialize(modelCommon_.get());
        d.object->SetModel(modelDummy_.get());
        d.object->SetEnableLighting(false);
        d.object->SetPosition(d.pos);
        d.object->Update();

        d.hpBarBg = std::make_unique<Sprite>();
        d.hpBarBg->Initialize(spriteCommon_.get(), "Resources/white.png");
        d.hpBarBg->SetColor({ 0.2f, 0.2f, 0.2f, 0.8f });

        d.hpBarFg = std::make_unique<Sprite>();
        d.hpBarFg->Initialize(spriteCommon_.get(), "Resources/white.png");
        d.hpBarFg->SetColor({ 0.2f, 0.9f, 0.2f, 0.9f });

        dummies_.push_back(std::move(d));
    }

    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());

    bulletPool_.Initialize(modelCommon_.get(), modelBlock_.get());

    awakenGaugeBg_ = std::make_unique<Sprite>();
    awakenGaugeBg_->Initialize(spriteCommon_.get(), "Resources/white.png");
    awakenGaugeBg_->SetColor({ 0.05f, 0.05f, 0.15f, 0.75f });

    awakenGaugeFg_ = std::make_unique<Sprite>();
    awakenGaugeFg_->Initialize(spriteCommon_.get(), "Resources/white.png");

    fontRenderer_.Initialize(spriteCommon_.get());
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    finisherOverlay_ = SceneShared::CreateFinisherOverlay(spriteCommon_.get());

    glassShatterBgSprite_ = std::make_unique<Sprite>();
    glassShatterBgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    glassShatterBgSprite_->SetPosition({ 0.0f, 0.0f });
    glassShatterBgSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
                                      static_cast<float>(WinApp::kClientHeight) });

    glassShatter_.Initialize(dxCommon_, srvManager_);
    bladeFlash_.Initialize(dxCommon_);
    spaceWarp_.Initialize(dxCommon_, srvManager_);
    dummySlice_.Initialize(dxCommon_);
    finisherShatter_.Initialize(dxCommon_, srvManager_);
    finisherShatter_.SetDuration(0.9f);
    ImGuiControlPanel::RegisterGlassShatterTrigger([this]() { TriggerGlassShatterTest(); });
}

void BattleTestScene::SpawnHitEffect(const Vector3& pos)
{
    pm_->EmitRing("bt_hit_ring", pos, 3.0f, { 1.0f, 0.85f, 0.2f, 1.0f }, 12, 0.25f, 0.18f);
    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> vx(-3.0f, 3.0f);
    std::uniform_real_distribution<float> vy(2.0f, 5.0f);
    for (int i = 0; i < 6; ++i) {
        pm_->EmitGravity("bt_hit_spark", pos,
            { vx(rng), vy(rng), 0.0f },
            { 1.0f, 0.55f, 0.1f, 1.0f }, 0.6f, 0.13f);
    }
}


void BattleTestScene::UpdateHpBars()
{
    const Vector3& cam = camera_->GetTranslate();
    constexpr float kBarW  = 60.0f;
    constexpr float kBarH  =  8.0f;
    constexpr float kBarUp = 70.0f;

    for (auto& d : dummies_) {
        float sx, sy;
        SceneShared::WorldToScreen(d.pos.x, d.pos.y, cam.x, cam.y, sx, sy);

        float ratio = d.hpDisplay_;
        d.hpBarFg->SetColor({ 1.0f - ratio, ratio * 0.85f + 0.15f, 0.0f, 0.9f });

        d.hpBarBg->SetPosition({ sx - kBarW * 0.5f, sy - kBarUp });
        d.hpBarBg->SetSize({ kBarW, kBarH });
        d.hpBarBg->Update();

        d.hpBarFg->SetPosition({ sx - kBarW * 0.5f, sy - kBarUp });
        d.hpBarFg->SetSize({ kBarW * ratio, kBarH });
        d.hpBarFg->Update();
    }
}

void BattleTestScene::Update()
{
    if (glassShatter_.IsActive()) {
        glassShatter_.Update(GameConstants::kFrameDeltaTime);
        return;
    }

    fontRenderer_.Reset();

    SceneShared::UpdateWeaponCycle(input_, weaponManager_, weaponCycleTimer_);
    UpdatePlayerAndCamera();
    UpdateEnvironment();

    bool hitFromCombat  = UpdateCombat();
    bool hitFromFinisher = UpdateFinisherSlash();
    UpdateComboRank(hitFromCombat || hitFromFinisher);
    UpdateDummies();

    dummySlice_.Update(GameConstants::kFrameDeltaTime, camera_.get());
    bladeFlash_.Update(GameConstants::kFrameDeltaTime, camera_.get());

    // 切断演出が飛散に移ったら、隠していたダミーを再表示する
    if (dummySlice_.IsBursting() || !dummySlice_.IsActive()) {
        for (auto& d : dummies_) { d.sliced = false; }
    }

    // プレイヤー位置を画面UVへ投影して空間歪みの中心に設定する
    if (spaceWarp_.IsActive() || finisherActive_) {
        const Vector3&  pp = player_->GetPosition();
        const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
        const float cx = pp.x * vp.m[0][0] + pp.y * vp.m[1][0] + pp.z * vp.m[2][0] + vp.m[3][0];
        const float cy = pp.x * vp.m[0][1] + pp.y * vp.m[1][1] + pp.z * vp.m[2][1] + vp.m[3][1];
        const float cw = pp.x * vp.m[0][3] + pp.y * vp.m[1][3] + pp.z * vp.m[2][3] + vp.m[3][3];
        if (cw > 0.0001f) {
            spaceWarp_.SetCenterUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
        }
    }
    spaceWarp_.Update(GameConstants::kFrameDeltaTime);

    // 解放時の世界割れ
    finisherShatter_.Update(GameConstants::kFrameDeltaTime);
    if (finisherShatter_.IsFinished()) {
        finisherShatter_.Reset();
    }

    SlashMark::GetInstance()->Update(GameConstants::kFrameDeltaTime);

    bool nearReturn = SceneShared::UpdatePortalTransition(input_, player_->GetPosition(), kWarpRetX, kReturnProx, "TRAINING");
    DrawHud(nearReturn);
}

void BattleTestScene::UpdatePlayerAndCamera()
{
    // 乱舞のターゲット：最も近いダミーを選ぶ
    {
        const Vector3& pp = player_->GetPosition();
        Vector3 rampTarget = { pp.x + player_->GetLastDirX() * 6.0f, pp.y, 0.0f };
        float minDist = FLT_MAX;
        for (const auto& d : dummies_) {
            float dist = std::abs(d.pos.x - pp.x);
            if (dist < minDist) { minDist = dist; rampTarget = d.pos; }
        }
        player_->Update(input_, rampTarget);
    }

    SceneShared::UpdateCameraFollow(camera_.get(), player_->GetPosition());
}

void BattleTestScene::UpdateEnvironment()
{
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    for (auto& b : borderBlocks_) { b->Update(); }

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = 0.6f + 0.4f * std::sin(warpPulseTimer_ * 4.0f);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ 1.0f * pulse, 0.5f * pulse, 0.1f * pulse, 0.9f });
        p->Update();
    }
}

bool BattleTestScene::UpdateCombat()
{
    auto* tm = TimeManager::GetInstance();
    attackCooldown_ -= GameConstants::kFrameDeltaTime;
    const WeaponData& weapon  = weaponManager_->GetCurrent();
    const Vector3&    pp      = player_->GetPosition();
    const float       atkMult = player_->IsAwakened() ? 1.5f : 1.0f;

    // ダミーの AABB を計算するラムダ（ダミーは 1×1×1 の正方形）
    auto dummyAABB = [](const Dummy& d) -> AABB {
        return { { d.pos.x - 0.5f, d.pos.y - 0.5f, -0.5f },
                 { d.pos.x + 0.5f, d.pos.y + 0.5f,  0.5f } };
    };

    // コンボランク：実際にダミーへ命中した時だけ立てるフラグ
    bool hitConfirmed = false;

    // 格闘コンボ（L キー）
    if (player_->JustComboHit() && attackCooldown_ <= 0.0f) {
        attackCooldown_ = weapon.attackInterval * (player_->IsAwakened() ? 0.65f : 1.0f);
        AABB meleeRange = {
            { pp.x - weapon.range, pp.y - 1.5f, -0.5f },
            { pp.x + weapon.range, pp.y + 1.5f,  0.5f }
        };
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) { continue; }
            if (Collision::CheckCollision(meleeRange, dummyAABB(d))) {
                hitConfirmed  = true;
                d.hp          = d.maxHp;
                d.hitFlash    = 0.14f;
                d.hpDisplay_  = 0.0f;
                d.returnTimer = 1.5f;
                // コンボステップに応じたノックバック強度（武器ごとの knockbackMult を反映）
                float kbMult = ((player_->GetComboStep() == 3) ? 1.5f :
                               (player_->GetComboStep() == 2) ? 1.1f : 0.8f) * atkMult * weapon.knockbackMult;
                d.knockVelX += player_->GetLastDirX() * 0.22f * kbMult;
                d.knockVelY += 0.08f * kbMult;
                SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
                tm->RequestHitStop(4);
            }
        }
    }

    // 射撃（K キー）— 射程2倍、ダメージ半減
    if (player_->JustFired()) {
        AABB shotRange = {
            { pp.x - weapon.range * 2.0f, pp.y - 1.5f, -0.5f },
            { pp.x + weapon.range * 2.0f, pp.y + 1.5f,  0.5f }
        };
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) { continue; }
            if (Collision::CheckCollision(shotRange, dummyAABB(d))) {
                hitConfirmed  = true;
                d.hp          = d.maxHp;
                d.hitFlash    = 0.10f;
                d.hpDisplay_  = 0.0f;
                d.returnTimer = 1.5f;
                d.knockVelX  += player_->GetLastDirX() * 0.12f * atkMult;
                SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
                tm->RequestHitStop(2);
            }
        }
    }

    // ── 覚醒乱舞ヒット ───────────────────────────────────────────────
    if (player_->JustRampageHit()) {
        const bool isFinisher = player_->JustRampageFinish();
        AABB rushRange = {
            { pp.x - 2.5f, pp.y - 1.5f, -0.5f },
            { pp.x + 2.5f, pp.y + 1.5f,  0.5f }
        };
        for (auto& d : dummies_) {
            if (Collision::CheckCollision(rushRange, dummyAABB(d))) {
                hitConfirmed  = true;
                d.hp          = d.maxHp;
                d.hitFlash    = isFinisher ? 0.20f : 0.08f;
                d.hpDisplay_  = 0.0f;
                d.returnTimer = 1.5f;
                float kb = (isFinisher ? 0.45f : 0.12f) * weapon.knockbackMult;
                d.knockVelX += player_->GetLastDirX() * kb * atkMult;
                d.knockVelY += (isFinisher ? 0.18f : 0.03f) * atkMult * weapon.knockbackMult;
                SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
                tm->RequestHitStop(isFinisher ? 6 : 2);
            }
        }
    }

    // ── フィニッシャースラッシュ：発動の合図（斬撃線の表示は UpdateFinisherSlash に委譲）──
    if (player_->JustFinisherSlash()) {
        finisherActive_    = true;
        finisherLineIdx_   = 0;
        finisherBeatTimer_ = GameConstants::kFinisherChargeDelay;
        tm->RequestHitStop(GameConstants::kHitStopJuggle);
        ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.35f }, 0.10f);
        SpawnHitEffect({ pp.x, pp.y + 0.5f, 0.0f });
        SceneShared::EmitFinisherCharge(pm_, "bt_hit_ring", "bt_hit_spark",
            { pp.x, pp.y + 0.5f, 0.0f });
        spaceWarp_.AddImpulse(0.4f);
    }

    // ── スペースキー スピン連射 ──────────────────────────────────────
    if (player_->JustSpinShot()) {
        constexpr float kBulletSpeed = 0.30f;
        Vector3 firePos = { pp.x, pp.y + 0.4f, 0.0f };

        if (player_->IsUpsideDown()) {
            // 逆さ: 下方向中心に 5 方向ばらまき
            constexpr float kBaseAngle = 270.0f * (3.14159265f / 180.0f); // 真下
            constexpr float kSpread    =  30.0f * (3.14159265f / 180.0f); // 30°間隔
            for (int i = -2; i <= 2; ++i) {
                float angle = kBaseAngle + i * kSpread;
                bulletPool_.Spawn(firePos, { std::cos(angle) * kBulletSpeed,
                                             std::sin(angle) * kBulletSpeed, 0.0f });
            }
            tm->RequestHitStop(3);
            ScreenFlash::GetInstance()->Request({ 1.0f, 0.7f, 0.1f, 0.55f }, 0.10f);
        } else {
            // 通常: 向いている方向に 1 発
            bulletPool_.Spawn(firePos, { player_->GetLastDirX() * kBulletSpeed, 0.0f, 0.0f });
        }
    }

    // ── 弾丸の移動・衝突判定 ────────────────────────────────────────
    bulletPool_.Update();
    for (int bi = 0; bi < BulletPool::kMaxBullets; ++bi) {
        if (!bulletPool_.IsActive(bi)) { continue; }
        const Vector3& bpos = bulletPool_.GetPos(bi);
        const Vector3& bvel = bulletPool_.GetVel(bi);
        AABB bulletAABB = { { bpos.x - 0.12f, bpos.y - 0.12f, -0.5f },
                            { bpos.x + 0.12f, bpos.y + 0.12f,  0.5f } };
        for (auto& d : dummies_) {
            if (d.hp <= 0.0f) { continue; }
            if (Collision::CheckCollision(bulletAABB, dummyAABB(d))) {
                hitConfirmed  = true;
                d.hp          = d.maxHp;
                d.hitFlash    = 0.08f;
                d.hpDisplay_  = 0.0f;
                d.returnTimer = 1.5f;
                float bspd = std::sqrt(bvel.x * bvel.x + bvel.y * bvel.y);
                if (bspd > 0.001f) {
                    d.knockVelX += bvel.x / bspd * 0.09f * weapon.knockbackMult;
                    d.knockVelY += bvel.y / bspd * 0.04f * weapon.knockbackMult;
                }
                SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
                tm->RequestHitStop(2);
                bulletPool_.Kill(bi);
                break;
            }
        }
    }

    return hitConfirmed;
}

bool BattleTestScene::UpdateFinisherSlash()
{
    if (!finisherActive_) { return false; }

    finisherBeatTimer_ -= GameConstants::kFrameDeltaTime;
    if (finisherBeatTimer_ > 0.0f) { return false; }

    auto*          tm = TimeManager::GetInstance();
    const Vector3& pp = player_->GetPosition();

    if (finisherLineIdx_ < GameConstants::kFinisherSlashLines) {
        // 画面全体を埋め尽くすようにランダムな位置を高速で斬り刻む
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
        std::uniform_real_distribution<float> offXDist(-7.5f, 7.5f);
        std::uniform_real_distribution<float> offYDist(-4.0f, 4.0f);
        std::uniform_real_distribution<float> lenDist(3.0f, 7.0f);
        std::uniform_real_distribution<float> thickDist(3.0f, 7.0f);
        const float   ang    = angleDist(rng);
        const Vector2 dir    = { std::cos(ang), std::sin(ang) };
        const Vector2 center = { pp.x + offXDist(rng), pp.y + offYDist(rng) };
        const float   len    = lenDist(rng);

        SlashMarkParams sm;
        sm.start = { center.x - dir.x * len, center.y - dir.y * len };
        sm.end   = { center.x + dir.x * len, center.y + dir.y * len };
        sm.color     = { 0.75f, 0.95f, 1.0f, 1.0f };
        sm.thickness = thickDist(rng);
        // 解放の瞬間まで全ての斬撃線を画面に残す
        sm.duration  = (GameConstants::kFinisherSlashLines - 1 - finisherLineIdx_) * GameConstants::kFinisherLineInterval
                     + GameConstants::kFinisherImpactDelay + 0.25f;
        SlashMark::GetInstance()->Spawn(sm);

        SceneShared::EmitFinisherSlashLine(pm_, "bt_sword_slash", "bt_hit_spark",
            { center.x, center.y, 0.0f }, ang, len);

        // 空間にガラス質の刃を明滅させ、歪みを脈動させる
        bladeFlash_.Emit({ center.x, center.y, 0.0f }, 3, 4.0f, 1.2f, 2.8f);
        spaceWarp_.AddImpulse(0.12f);

        tm->RequestHitStop(GameConstants::kHitStopFinisherBeat);

        // 斬撃線が出るたびに実際にヒットさせ、マネキンを浮かせ続ける
        for (auto& d : dummies_) {
            d.hp          = d.maxHp;
            d.hitFlash    = 0.10f;
            d.hpDisplay_  = 0.0f;
            d.returnTimer = 1.5f;
            d.knockVelX  += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * 0.06f;
            d.knockVelY  += 0.06f;
            SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
        }

        finisherLineIdx_++;
        finisherBeatTimer_ = (finisherLineIdx_ < GameConstants::kFinisherSlashLines)
            ? GameConstants::kFinisherLineInterval
            : GameConstants::kFinisherImpactDelay;
        return true;
    }

    // 解放：溜めた斬撃が一斉に炸裂し、距離を問わず全マネキンに命中
    finisherActive_ = false;
    for (auto& d : dummies_) {
        d.hp          = d.maxHp;
        d.hitFlash    = 0.22f;
        d.hpDisplay_  = 0.0f;
        d.returnTimer = 1.5f;
        d.knockVelX  += ((d.pos.x >= pp.x) ? 1.0f : -1.0f) * 0.5f;
        d.knockVelY  += 0.20f;
        SpawnHitEffect({ d.pos.x, d.pos.y + 0.5f, 0.0f });
    }

    // 解放の瞬間、太く短い閃光の斬撃線を重ねる
    static std::mt19937 rngRelease{ std::random_device{}() };
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    for (int i = 0; i < 8; ++i) {
        const float   ang = angleDist(rngRelease);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        SlashMarkParams sm;
        sm.start = { pp.x - dir.x * GameConstants::kFinisherSlashRadius,
                     pp.y - dir.y * GameConstants::kFinisherSlashRadius };
        sm.end   = { pp.x + dir.x * GameConstants::kFinisherSlashRadius,
                     pp.y + dir.y * GameConstants::kFinisherSlashRadius };
        sm.color     = { 1.0f, 1.0f, 1.0f, 1.0f };
        sm.thickness = 9.0f;
        sm.duration  = 0.15f;
        SlashMark::GetInstance()->Spawn(sm);
    }

    tm->RequestHitStop(GameConstants::kHitStopFinisherSlash);
    ScreenFlash::GetInstance()->Request({ 0.75f, 0.95f, 1.0f, 0.65f }, GameConstants::kShakeFinisherSlashDur);
    SceneShared::EmitFinisherRelease(pm_, "bt_hit_ring", "bt_hit_spark",
        { pp.x, pp.y + 0.5f, 0.0f });

    // 解放の瞬間：刃の一斉放出と空間歪みの最大化、最も近いダミーを切断破片に差し替える
    bladeFlash_.Emit({ pp.x, pp.y + 0.5f, 0.0f }, 30, GameConstants::kFinisherSlashRadius, 2.0f, 5.0f);
    spaceWarp_.AddImpulse(1.0f);
    {
        Dummy* nearest = nullptr;
        float  minDist = FLT_MAX;
        for (auto& d : dummies_) {
            float dist = std::abs(d.pos.x - pp.x);
            if (dist < minDist) { minDist = dist; nearest = &d; }
        }
        if (nearest != nullptr) {
            static std::mt19937 rngSlice{ std::random_device{}() };
            dummySlice_.Start(modelDummy_.get(), nearest->pos, { 1.0f, 1.0f, 1.0f }, rngSlice());
            nearest->sliced = true;
        }
    }

    // 「暗転+斬撃線ごと凍った画面」をプレイヤー位置から砕き、素の世界を見せる
    {
        const Matrix4x4 vp = Multiply(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
        const float cx = pp.x * vp.m[0][0] + pp.y * vp.m[1][0] + pp.z * vp.m[2][0] + vp.m[3][0];
        const float cy = pp.x * vp.m[0][1] + pp.y * vp.m[1][1] + pp.z * vp.m[2][1] + vp.m[3][1];
        const float cw = pp.x * vp.m[0][3] + pp.y * vp.m[1][3] + pp.z * vp.m[2][3] + vp.m[3][3];
        if (cw > 0.0001f) {
            finisherShatter_.SetImpactUV(cx / cw * 0.5f + 0.5f, 0.5f - cy / cw * 0.5f);
        }
    }
    finisherShatter_.Reset();
    finisherShatter_.Start();
    return true;
}

void BattleTestScene::UpdateComboRank(bool hitConfirmed)
{
    // コンボランク追跡（実際にダミーへ命中した時のみ加算）
    if (hitConfirmed) {
        trComboCount_++;
        trComboTimer_ = 1.2f;
        trRankAlpha_  = 1.0f;
        if (trComboCount_ > trMaxCombo_) { trMaxCombo_ = trComboCount_; }
    }
    trComboTimer_ -= GameConstants::kFrameDeltaTime;
    if (trComboTimer_ <= 0.0f) {
        trComboTimer_ = 0.0f;
        // コンボ切れ → フェードアウト後にリセット
        trRankAlpha_ -= GameConstants::kFrameDeltaTime * 2.0f;
        if (trRankAlpha_ <= 0.0f) { trRankAlpha_ = 0.0f; trComboCount_ = 0; }
    }
}

void BattleTestScene::UpdateDummies()
{
    for (auto& d : dummies_) {
        // ノックバック物理
        d.knockVelY -= 0.012f;
        d.pos.x     += d.knockVelX;
        d.pos.y     += d.knockVelY;

        if (d.pos.y <= 0.4f) { d.pos.y = 0.4f; d.knockVelY = 0.0f; }
        d.pos.x = std::clamp(d.pos.x, 3.0f, 27.0f);
        if (d.pos.x <= 3.01f || d.pos.x >= 26.99f) { d.knockVelX = 0.0f; }
        d.knockVelX *= 0.84f;
        d.knockVelY *= 0.88f;

        // HP バー表示値を回復（被弾後 0.8 秒で満タンに戻る）
        d.hpDisplay_ = (std::min)(d.hpDisplay_ + GameConstants::kFrameDeltaTime / 0.8f, 1.0f);

        // 帰還タイマー（被弾から 1.5 秒後に中央へ戻る）
        d.returnTimer -= GameConstants::kFrameDeltaTime;
        if (d.returnTimer <= 0.0f) {
            d.returnTimer = 0.0f;
            d.pos.x += (d.homePos.x - d.pos.x) * 0.05f;
            d.pos.y += (d.homePos.y - d.pos.y) * 0.05f;
        }

        d.object->SetPosition(d.pos);

        d.hitFlash -= GameConstants::kFrameDeltaTime;
        if (d.hitFlash > 0) { d.object->SetColor({ 1.5f, 1.5f, 1.5f, 1.0f }); }
        else                 { d.object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f }); }
        d.object->Update();
    }

    UpdateHpBars();
}

void BattleTestScene::DrawHud(bool nearReturnPortal)
{
    DrawWeaponHud(nearReturnPortal);
    SceneShared::DrawControlsHud(fontRenderer_, L": Back (portal)");
    DrawComboRankHud();
    SceneShared::DrawAwakenGaugeHud(fontRenderer_, awakenGaugeBg_.get(), awakenGaugeFg_.get(),
        player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_);
}

void BattleTestScene::DrawWeaponHud(bool nearReturnPortal)
{
    constexpr float kScale = 1.5f;
    constexpr Vector4 kColorHint = { 0.6f, 0.6f, 0.6f, 1.0f };

    float py = SceneShared::DrawWeaponListHud(fontRenderer_, weaponManager_, L"テストステージ");
    fontRenderer_.DrawStringW(L"[L] 格闘  [K] 射撃  [R] 覚醒", 12.0f, py, kScale, kColorHint);

    // 戻りポータルのラベル
    if (nearReturnPortal) {
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        SceneShared::WorldToScreen(kWarpRetX, 5.0f, cam.x, cam.y, sx, sy);
        constexpr Vector4 kColorReturn = { 1.0f, 0.6f, 0.1f, 1.0f };
        fontRenderer_.DrawString("[ ENTER ] Back", sx - 84.0f, sy - 36.0f, kScale, kColorReturn);
    }
}

void BattleTestScene::DrawComboRankHud()
{
    // ── コンボランク（画面中央） ──────────────────────────────────────
    if (trComboCount_ <= 0 && trRankAlpha_ <= 0.0f) { return; }

    struct RankDef { const char* label; Vector4 color; };
    static constexpr RankDef kRanks[] = {
        { "D",   { 0.55f, 0.55f, 0.55f, 1.0f } },
        { "C",   { 0.85f, 0.85f, 0.85f, 1.0f } },
        { "B",   { 0.30f, 0.72f, 1.00f, 1.0f } },
        { "A",   { 0.20f, 1.00f, 0.40f, 1.0f } },
        { "S",   { 1.00f, 0.90f, 0.10f, 1.0f } },
        { "SS",  { 1.00f, 0.55f, 0.10f, 1.0f } },
        { "SSS", { 1.00f, 0.30f, 0.30f, 1.0f } },
    };
    int ri = (trComboCount_ >= 25) ? 6 :
             (trComboCount_ >= 18) ? 5 :
             (trComboCount_ >= 12) ? 4 :
             (trComboCount_ >=  8) ? 3 :
             (trComboCount_ >=  5) ? 2 :
             (trComboCount_ >=  3) ? 1 : 0;
    const char* lbl = kRanks[ri].label;
    Vector4 rc = kRanks[ri].color;
    rc.w *= trRankAlpha_;

    constexpr float kRS = 5.0f;  // ランク文字スケール
    constexpr float kHS = 2.0f;  // ヒット数スケール
    int   lblLen = static_cast<int>(std::strlen(lbl));
    float rankW  = FontRenderer::kCharW * kRS * static_cast<float>(lblLen);
    fontRenderer_.DrawString(lbl, 640.0f - rankW * 0.5f, 158.0f, kRS, rc);

    char hitBuf[24];
    std::snprintf(hitBuf, sizeof(hitBuf), "x%d HIT", trComboCount_);
    float hw = FontRenderer::kCharW * kHS * static_cast<float>(std::strlen(hitBuf));
    fontRenderer_.DrawString(hitBuf, 640.0f - hw * 0.5f, 240.0f, kHS,
        { 1.0f, 1.0f, 1.0f, trRankAlpha_ });

    if (trMaxCombo_ > 0) {
        char bestBuf[24];
        std::snprintf(bestBuf, sizeof(bestBuf), "BEST:%d", trMaxCombo_);
        constexpr float kBS = 1.3f;
        float bw = FontRenderer::kCharW * kBS * static_cast<float>(std::strlen(bestBuf));
        fontRenderer_.DrawString(bestBuf, 640.0f - bw * 0.5f, 270.0f, kBS,
            { 0.7f, 0.7f, 0.7f, trRankAlpha_ * 0.8f });
    }
}

void BattleTestScene::Draw()
{
    // ---- ガラス割れ演出中（かつキャプチャ済み）は通常描画をスキップ ----
    if (glassShatter_.IsActive() && !glassShatter_.NeedCapture()) {
        spriteCommon_->CommonDrawSettings();
        glassShatterBgSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
        glassShatterBgSprite_->Update();
        glassShatterBgSprite_->Draw();
        glassShatter_.Apply();
        return;
    }

    shadowManager_->BeginShadowPass(dxCommon_->GetCommandList());
    modelCommon_->BeginShadowPass();
    shadowManager_->EndShadowPass(dxCommon_->GetCommandList());

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    SetupMainRenderTarget();

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(cmd);
    shadowManager_->SetShadowMap(cmd, srvManager_);

    for (auto& b : borderBlocks_)     { b->Draw(); }
    for (auto& p : warpPortalBlocks_) { p->Draw(); }
    for (auto& d : dummies_)          { if (!d.sliced) { d.object->Draw(); } }
    bulletPool_.Draw();
    player_->Draw();
    dummySlice_.Draw();

    // パーティクル（PreDraw でコマンドリストがリセットされるため Draw() 内で呼ぶ）
    pm_->Update(camera_.get());
    pm_->Draw(camera_.get());

    bladeFlash_.Draw();

    // 空間歪み（バックバッファ直描き時のみ。UIより先に画面をキャプチャして歪ませる）
    if (spaceWarp_.IsActive()
        && GetActiveRTVHandle().ptr == dxCommon_->GetCurrentBackBufferHandle().ptr) {
        spaceWarp_.CaptureAndApply();
        SetupMainRenderTarget(); // 歪み描画で変わったレンダーターゲット設定を戻す
    }

    // HP バー + テキスト UI（2D スプライト）
    spriteCommon_->CommonDrawSettings();
    shadowManager_->SetShadowMap(cmd, srvManager_);
    for (auto& d : dummies_) {
        if (d.hp > 0.0f) {
            d.hpBarBg->Draw();
            d.hpBarFg->Draw();
        }
    }
    awakenGaugeBg_->Draw();
    if (player_->GetAwakenGauge() > 0.0f) { awakenGaugeFg_->Draw(); }

    // 大技中と解放フレーム（凍結画面のキャプチャ前）だけ暗転を重ねる。
    // 解放後の暗さは砕け散る凍結画面が持ち去るので、素の世界には重ねない
    const bool captureFrame = finisherShatter_.IsActive() && finisherShatter_.NeedCapture();
    if (finisherActive_ || captureFrame) {
        finisherOverlay_->SetColor({ 0.0f, 0.0f, 0.05f, GameConstants::kFinisherOverlayAlpha });
        finisherOverlay_->Update();
        finisherOverlay_->Draw();
    }
    SlashMark::GetInstance()->Draw();

    // ---- 解放時の世界割れ（暗転+斬撃線ごと凍った画面を砕き、下から素の世界が現れる）----
    if (finisherShatter_.IsActive()
        && GetActiveRTVHandle().ptr == dxCommon_->GetCurrentBackBufferHandle().ptr) {
        if (finisherShatter_.NeedCapture()) {
            finisherShatter_.CaptureFrame();
        }
        finisherShatter_.Apply();

        // Apply が変えたレンダーターゲットとルートシグネチャを後続のスプライト描画用に戻す
        SetupMainRenderTarget();
        spriteCommon_->CommonDrawSettings();
    }

    fontRenderer_.Draw();

    // ---- ガラス割れエフェクト（テスト再生時のみ）----
    if (glassShatter_.IsActive()) {
        if (glassShatter_.NeedCapture()) {
            glassShatter_.CaptureFrame();
        }
        glassShatter_.Apply();
    }
}

void BattleTestScene::Finalize()
{
    ImGuiControlPanel::RegisterGlassShatterTrigger(nullptr);
    pm_->ClearAllGroups();
    glassShatter_.Finalize();
    finisherShatter_.Finalize();
    spaceWarp_.Finalize();
    bladeFlash_.Clear();
    SlashMark::GetInstance()->Clear();
}

void BattleTestScene::TriggerGlassShatterTest()
{
    if (glassShatter_.IsActive()) { return; }
    glassShatter_.Start();
}

D3D12_CPU_DESCRIPTOR_HANDLE BattleTestScene::GetActiveRTVHandle() const
{
    return GetActiveSceneRTVHandle(dxCommon_, imageFilter_, grayscaleEffect_, hsvFilter_);
}

void BattleTestScene::SetupMainRenderTarget()
{
    SetupSceneRenderTarget(dxCommon_, GetActiveRTVHandle());
}
