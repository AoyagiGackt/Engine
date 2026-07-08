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
    camera_->SetTranslate({ 14.5f, 6.0f, -24.0f });
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

    SceneShared::CreateParticleGroupsFromJson(pm_, "Resources/particles/battletest.json");

    knight_ = std::make_unique<KnightEnemy>();
    knight_->Initialize(modelCommon_.get(), { 20.0f, 0.4f, 0.0f });

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

    InitializeWeaponSlotHud();

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
    UpdateTargetLock();
    UpdatePlayerAndCamera();
    UpdateEnvironment();

    bool hitFromCombat  = UpdateCombat();
    bool hitFromFinisher = UpdateFinisherSlash();
    UpdateComboRank(hitFromCombat || hitFromFinisher);
    UpdateDummies();
    UpdateKnightEnemy();
    UpdateWeaponSlotHud();

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
    // 乱舞のターゲット：ロック中ならその対象、そうでなければ最も近いダミー
    {
        const Vector3& pp = player_->GetPosition();
        Vector3 rampTarget = { pp.x + player_->GetLastDirX() * 6.0f, pp.y, 0.0f };

        if (lockedKind_ == LockTargetKind::Knight && knight_) {
            rampTarget = knight_->GetPosition();
        } else if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ < dummies_.size()) {
            rampTarget = dummies_[lockedDummyIndex_].pos;
        } else {
            float minDist = FLT_MAX;
            for (const auto& d : dummies_) {
                float dist = std::abs(d.pos.x - pp.x);
                if (dist < minDist) { minDist = dist; rampTarget = d.pos; }
            }
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
        for (size_t di = 0; di < dummies_.size(); ++di) {
            auto& d = dummies_[di];
            if (d.hp <= 0.0f) { continue; }
            // ロック中の対象は射程に関係なく確実にヒットさせる（「ロックしたのに攻撃が届かない」を無くす）
            bool isLocked = (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ == di);
            bool hit = isLocked || Collision::CheckCollision(meleeRange, dummyAABB(d));
            if (hit) {
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
        // カメラ視界全体にランダムな位置を高速で斬り刻む
        const Vector3& cam = camera_->GetTranslate();
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
        std::uniform_real_distribution<float> offXDist(-GameConstants::kCameraHalfW, GameConstants::kCameraHalfW);
        std::uniform_real_distribution<float> offYDist(-GameConstants::kCameraHalfH, GameConstants::kCameraHalfH);
        std::uniform_real_distribution<float> lenDist(4.0f, 9.0f);
        std::uniform_real_distribution<float> thickDist(3.0f, 7.0f);
        const float   ang    = angleDist(rng);
        const Vector2 dir    = { std::cos(ang), std::sin(ang) };
        const Vector2 center = { cam.x + offXDist(rng), cam.y + offYDist(rng) };
        const float   len    = lenDist(rng);

        // 解放の瞬間まで全ての斬撃線を画面に残す
        const float duration = (GameConstants::kFinisherSlashLines - 1 - finisherLineIdx_) * GameConstants::kFinisherLineInterval
                             + GameConstants::kFinisherImpactDelay + 0.25f;
        SceneShared::SpawnSlashMarkWorld(
            { center.x - dir.x * len, center.y - dir.y * len },
            { center.x + dir.x * len, center.y + dir.y * len },
            cam.x, cam.y, { 0.75f, 0.95f, 1.0f, 1.0f }, thickDist(rng), duration);

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

    // 溜めた斬撃線を一斉に白く光らせてから消し、太く短い閃光の斬撃線を重ねる
    SlashMark::GetInstance()->FlashAll({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.22f);
    static std::mt19937 rngRelease{ std::random_device{}() };
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    const Vector3& cam = camera_->GetTranslate();
    for (int i = 0; i < 8; ++i) {
        const float   ang = angleDist(rngRelease);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        SceneShared::SpawnSlashMarkWorld(
            { pp.x - dir.x * GameConstants::kFinisherSlashRadius,
              pp.y - dir.y * GameConstants::kFinisherSlashRadius },
            { pp.x + dir.x * GameConstants::kFinisherSlashRadius,
              pp.y + dir.y * GameConstants::kFinisherSlashRadius },
            cam.x, cam.y, { 1.0f, 1.0f, 1.0f, 1.0f }, 9.0f, 0.15f);
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

void BattleTestScene::UpdateTargetLock()
{
    // ロック中の対象が無効になっていたら（撃破された等）自動解除
    if (lockedKind_ == LockTargetKind::Knight && (!knight_ || !knight_->IsAlive())) {
        lockedKind_ = LockTargetKind::None;
    }
    if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ >= dummies_.size()) {
        lockedKind_ = LockTargetKind::None;
    }

    if (!input_->TriggerKey(DIK_LSHIFT)) { return; }

    // 候補: 生存中のダミー → ナイト（生存時のみ）→ 末尾は「ロック解除」として巡回する
    struct Candidate { LockTargetKind kind; size_t index; };
    std::vector<Candidate> candidates;
    for (size_t i = 0; i < dummies_.size(); ++i) { candidates.push_back({ LockTargetKind::Dummy, i }); }
    if (knight_ && knight_->IsAlive()) { candidates.push_back({ LockTargetKind::Knight, 0 }); }
    if (candidates.empty()) { lockedKind_ = LockTargetKind::None; return; }

    int curIdx = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        bool sameKind = (candidates[i].kind == lockedKind_);
        bool sameSlot = (lockedKind_ != LockTargetKind::Dummy) || (candidates[i].index == lockedDummyIndex_);
        if (sameKind && sameSlot) { curIdx = static_cast<int>(i); break; }
    }

    int nextIdx = curIdx + 1; // 未ロック(-1)からは先頭へ、最後まで進んだら「解除」に戻る
    if (nextIdx >= static_cast<int>(candidates.size())) {
        lockedKind_ = LockTargetKind::None;
    } else {
        lockedKind_       = candidates[nextIdx].kind;
        lockedDummyIndex_ = candidates[nextIdx].index;
    }
}

void BattleTestScene::UpdateKnightEnemy()
{
    if (!knight_) { return; }

    auto*             tm     = TimeManager::GetInstance();
    const WeaponData& weapon = weaponManager_->GetCurrent();
    const Vector3&    pp     = player_->GetPosition();

    knight_->Update(pm_, pp);

    // ── プレイヤーの攻撃判定（Dummy 用と同じ AABB をナイトにも適用） ──
    if (knight_->IsAlive()) {
        if (player_->JustComboHit()) {
            AABB meleeRange = {
                { pp.x - weapon.range, pp.y - 1.5f, -0.5f },
                { pp.x + weapon.range, pp.y + 1.5f,  0.5f }
            };
            // ロック中は射程に関係なく確実にヒットさせる
            bool isLocked = (lockedKind_ == LockTargetKind::Knight);
            bool hit = isLocked || Collision::CheckCollision(meleeRange, knight_->GetAABB());
            if (hit) {
                knight_->TakeDamage(1);
                SpawnHitEffect({ knight_->GetPosition().x, knight_->GetPosition().y + 0.7f, 0.0f });
                tm->RequestHitStop(4);
            }
        }
        if (player_->JustRampageHit()) {
            AABB rushRange = {
                { pp.x - 2.5f, pp.y - 1.5f, -0.5f },
                { pp.x + 2.5f, pp.y + 1.5f,  0.5f }
            };
            if (Collision::CheckCollision(rushRange, knight_->GetAABB())) {
                knight_->TakeDamage(1);
                SpawnHitEffect({ knight_->GetPosition().x, knight_->GetPosition().y + 0.7f, 0.0f });
                tm->RequestHitStop(player_->JustRampageFinish() ? 6 : 2);
            }
        }
    }

    // ── 撃破後: 近づいて J キーで刺し、武器を奪う ──────────────────
    if (knight_->IsAwaitingSteal()) {
        float dx = knight_->GetPosition().x - pp.x;
        float dy = knight_->GetPosition().y - pp.y;
        constexpr float kStealRange = 2.0f;
        if ((dx * dx + dy * dy) <= kStealRange * kStealRange && input_->TriggerKey(DIK_J)) {
            if (knight_->TryBeginAbsorb()) {
                player_->PlayStealStab();
                tm->RequestHitStop(8);
                ScreenFlash::GetInstance()->Request({ 0.6f, 0.75f, 0.9f, 0.4f }, 0.10f);
            }
        }
    }

    if (knight_->JustAbsorbed()) {
        // ナイトの得物(Sword.obj)に合わせてソードスタイルを解放（重複時は false=強化素材扱い、今は未実装）
        weaponManager_->Unlock(WeaponType::Sword);
        pm_->EmitRing("bt_hit_ring", { pp.x, pp.y + 0.5f, 0.0f }, 0.10f,
            { 0.5f, 0.85f, 1.0f, 1.0f }, 20, 0.35f, 0.4f);
        slotFlashTimer_ = kSlotFlashDuration;
    }
}

void BattleTestScene::InitializeWeaponSlotHud()
{
    constexpr float kSlotSize = 56.0f;
    constexpr float kSlotGap  = 10.0f;
    constexpr float kMarginX  = 24.0f;
    constexpr float kMarginY  = 90.0f; // 画面下端からの距離
    const float baseY = static_cast<float>(WinApp::kClientHeight) - kMarginY;

    const auto& list = weaponManager_->GetList();
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        float x = kMarginX + static_cast<float>(i) * (kSlotSize + kSlotGap);
        weaponSlotPos_[i] = { x, baseY };

        auto frame = std::make_unique<Sprite>();
        frame->Initialize(spriteCommon_.get(), "Resources/white.png");
        frame->SetPosition({ x, baseY });
        frame->SetSize({ kSlotSize, kSlotSize });
        frame->SetColor({ 0.08f, 0.08f, 0.1f, 0.85f });

        auto icon = std::make_unique<Sprite>();
        icon->Initialize(spriteCommon_.get(), "Resources/white.png");
        icon->SetPosition({ x + 6.0f, baseY + 6.0f });
        icon->SetSize({ kSlotSize - 12.0f, kSlotSize - 12.0f });
        if (i < static_cast<int>(list.size()) && weaponManager_->IsUnlocked(i)) {
            const float* c = list[i].styleColor;
            icon->SetColor({ c[0], c[1], c[2], 0.9f });
        } else {
            icon->SetColor({ 0.2f, 0.2f, 0.2f, 0.35f }); // 未解放スロット
        }

        weaponSlots_[i].frame = std::move(frame);
        weaponSlots_[i].icon  = std::move(icon);
    }

    // 各スタイルに対応する実物3Dモデル（色塗り四角の代わりに表示）
    // ダミーの物理武器がまだ無いスタイルはここに追加すれば自動でモデル表示に切り替わる
    // scale はモデル実寸の高さ差を吸収し、見た目のアイコンサイズ(目標高さ約0.8)を揃えるための倍率
    struct IconAsset { WeaponType type; const char* modelPath; const char* texturePath; float scale; };
    static constexpr IconAsset kIconAssets[] = {
        { WeaponType::Sword,  "Resources/Knight/OBJ/Sword.obj",                     "Resources/Knight/OBJ/SwordPalette.png",                   0.18f }, // 実寸高さ約4.35
        { WeaponType::Dagger, "Resources/MedievalWeaponsPack/OBJ/Dagger.obj",       "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png",     0.31f }, // 実寸高さ約2.60
        { WeaponType::Hammer, "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png", 0.18f }, // 実寸高さ約4.33
        { WeaponType::Spear,  "Resources/MedievalWeaponsPack/OBJ/Spear.obj",        "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png",      0.08f }, // 実寸高さ約9.72
    };

    for (int i = 0; i < kWeaponSlotCount && i < static_cast<int>(list.size()); ++i) {
        for (const auto& asset : kIconAssets) {
            if (list[i].type != asset.type) { continue; }
            auto& icon3d = weaponIcons3D_[i];
            icon3d.slotIndex = i;
            icon3d.scale     = asset.scale;
            icon3d.model = std::make_unique<Model>();
            icon3d.model->Initialize(modelCommon_.get(), asset.modelPath, asset.texturePath);
            icon3d.object = std::make_unique<Object3d>();
            icon3d.object->Initialize(modelCommon_.get());
            icon3d.object->SetModel(icon3d.model.get());
            icon3d.object->SetEnableLighting(true);
            break;
        }
    }

    // 常時装備の拳銃（4スロットの右に少し離して配置、スタイルスロットとは別枠だと分かるように）
    float gunX = kMarginX + static_cast<float>(kWeaponSlotCount) * (kSlotSize + kSlotGap) + 24.0f;
    gunPos_ = { gunX, baseY };

    gunFrame_ = std::make_unique<Sprite>();
    gunFrame_->Initialize(spriteCommon_.get(), "Resources/white.png");
    gunFrame_->SetPosition({ gunX, baseY });
    gunFrame_->SetSize({ kSlotSize, kSlotSize });
    gunFrame_->SetColor({ 0.08f, 0.08f, 0.1f, 0.85f });

    gunIcon_ = std::make_unique<Sprite>();
    gunIcon_->Initialize(spriteCommon_.get(), "Resources/white.png");
    gunIcon_->SetAnchorPoint({ 0.5f, 0.5f });
    gunIcon_->SetPosition({ gunX + kSlotSize * 0.5f, baseY + kSlotSize * 0.5f });
    gunIcon_->SetSize({ kSlotSize - 20.0f, kSlotSize - 20.0f });
    gunIcon_->SetColor({ 0.6f, 0.85f, 1.0f, 0.9f });
}

void BattleTestScene::UpdateWeaponSlotHud()
{
    slotPulseTimer_ += GameConstants::kFrameDeltaTime;
    gunIconAngle_    += GameConstants::kFrameDeltaTime * 0.6f; // 常時装備の印として、ゆっくり回り続ける
    if (slotFlashTimer_ > 0.0f) {
        slotFlashTimer_ = (std::max)(0.0f, slotFlashTimer_ - GameConstants::kFrameDeltaTime);
    }

    const int   activeIndex = weaponManager_->GetIndex();
    const float pulse       = 0.7f + 0.3f * std::sin(slotPulseTimer_ * 6.0f);
    const float flash       = slotFlashTimer_ / kSlotFlashDuration;

    const auto& list = weaponManager_->GetList();
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        bool  active  = (i == activeIndex);
        float frameB  = 0.08f + (active ? pulse * 0.35f : 0.0f) + flash * 0.5f;
        weaponSlots_[i].frame->SetColor({ frameB, frameB, frameB + (active ? 0.2f : 0.05f), 0.85f });
        weaponSlots_[i].frame->Update();

        bool unlocked   = (i < static_cast<int>(list.size()) && weaponManager_->IsUnlocked(i));
        bool show3DIcon = unlocked && (weaponIcons3D_[i].slotIndex == i) && weaponIcons3D_[i].object;
        if (unlocked) {
            float iconMul = (active ? (0.7f + pulse * 0.3f) : 0.5f) + flash * 0.5f;
            const float* c = list[i].styleColor;
            // 3Dモデルで表示するスロットは、下地の色四角を隠して実物モデルだけ見せる
            weaponSlots_[i].icon->SetColor({ c[0] * iconMul, c[1] * iconMul, c[2] * iconMul, show3DIcon ? 0.0f : 0.95f });
        } else {
            float lockFlash = 0.2f + flash * 0.6f; // 新規解放の瞬間はロック中のスロットも一緒に光らせる
            weaponSlots_[i].icon->SetColor({ lockFlash, lockFlash, lockFlash, 0.35f + flash * 0.3f });
        }
        weaponSlots_[i].icon->Update();
    }

    gunFrame_->Update();
    gunIcon_->SetRotation(gunIconAngle_);
    gunIcon_->Update();

    // ── 各スロットの3Dアイコン（画面左下に固定表示、ゆっくり回転） ────────
    // カメラは回転しないので、スロットの画面位置をワールド座標へ逆算して張り付ける
    // 元の逆算(Z=0基準)だと地面の境界ブロック(Y=-0.6付近)に埋もれて隠れてしまうため、
    // カメラのすぐ手前(奥行き6)に置き直す。奥行きが変わった分、オフセットとスケールを
    // WorldToScreen の基準距離(24)に対する比率で縮小して同じ画面位置・見た目サイズを保つ
    constexpr float kSlotSize          = 56.0f;
    constexpr float kIconDepth         = 6.0f;         // カメラからの距離
    constexpr float kIconDepthScale    = kIconDepth / 24.0f; // WorldToScreen基準距離(24)との比
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        auto& icon3d = weaponIcons3D_[i];
        if (icon3d.slotIndex != i || !icon3d.object) { continue; }
        if (!weaponManager_->IsUnlocked(i)) { continue; }

        float sx = weaponSlotPos_[i].x + kSlotSize * 0.5f;
        float sy = weaponSlotPos_[i].y + kSlotSize * 0.5f;
        const Vector3& cam = camera_->GetTranslate();
        Vector3 iconPos = {
            cam.x + (sx - 640.0f) / 640.0f * GameConstants::kCameraHalfW * kIconDepthScale,
            cam.y - (sy - 360.0f) / 360.0f * GameConstants::kCameraHalfH * kIconDepthScale,
            cam.z + kIconDepth
        };
        float iconScale = icon3d.scale * kIconDepthScale;
        icon3d.angle += GameConstants::kFrameDeltaTime * 1.2f;
        icon3d.object->SetPosition(iconPos);
        icon3d.object->SetRotation({ 0.3f, icon3d.angle, 0.0f });
        icon3d.object->SetScale({ iconScale, iconScale, iconScale });
        bool  active = (i == activeIndex);
        float b      = (active ? (0.9f + pulse * 0.1f) : 0.6f) + flash * 0.4f;
        icon3d.object->SetColor({ b, b, b, 1.0f });
        icon3d.object->Update();
    }
}

void BattleTestScene::DrawWeaponSlotHud()
{
    for (auto& slot : weaponSlots_) {
        slot.frame->Draw();
        slot.icon->Draw();
    }
    gunFrame_->Draw();
    gunIcon_->Draw();

    // スタイル名の文字ラベルは廃止（枠の中身＝実物の武器モデル/色で見分ける）。
    // 未解放のスロットだけ「?」を出し、中身が武器モデルで隠れないよう控えめな位置にする
    constexpr float kSlotSize = 56.0f;
    const auto& list = weaponManager_->GetList();
    for (int i = 0; i < kWeaponSlotCount && i < static_cast<int>(list.size()); ++i) {
        if (weaponManager_->IsUnlocked(i)) { continue; }
        fontRenderer_.DrawStringW(L"?",
            weaponSlotPos_[i].x + kSlotSize * 0.5f - 6.0f,
            weaponSlotPos_[i].y + kSlotSize * 0.5f - 12.0f, 1.6f,
            { 0.6f, 0.6f, 0.6f, 0.9f });
    }
    fontRenderer_.DrawString("GUN", gunPos_.x + 10.0f, gunPos_.y + kSlotSize + 4.0f, 1.0f,
        { 0.6f, 0.85f, 1.0f, 0.9f });
}

void BattleTestScene::DrawHud(bool nearReturnPortal)
{
    DrawWeaponHud(nearReturnPortal);
    SceneShared::DrawControlsHud(fontRenderer_, L": Back (portal)");
    DrawComboRankHud();
    SceneShared::DrawAwakenGaugeHud(fontRenderer_, awakenGaugeBg_.get(), awakenGaugeFg_.get(),
        player_->GetAwakenGauge(), player_->IsAwakened(), warpPulseTimer_);

    // ── ロックオン中の対象にマーカーを出す ────────────────────────
    if (lockedKind_ != LockTargetKind::None) {
        Vector3 tpos{};
        bool valid = true;
        if (lockedKind_ == LockTargetKind::Knight && knight_) {
            tpos = knight_->GetPosition();
        } else if (lockedKind_ == LockTargetKind::Dummy && lockedDummyIndex_ < dummies_.size()) {
            tpos = dummies_[lockedDummyIndex_].pos;
        } else {
            valid = false;
        }
        if (valid) {
            const Vector3& cam = camera_->GetTranslate();
            float sx, sy;
            SceneShared::WorldToScreen(tpos.x, tpos.y + 1.6f, cam.x, cam.y, sx, sy);
            fontRenderer_.DrawString("v LOCK v", sx - 46.0f, sy, 1.3f, { 1.0f, 0.35f, 0.2f, 1.0f });
        }
    }

    // ── 撃破したナイトの頭上に武器奪取のプロンプトを出す ──────────────
    if (knight_ && knight_->IsAwaitingSteal()) {
        const Vector3& pp = player_->GetPosition();
        const Vector3& kp = knight_->GetPosition();
        float dx = kp.x - pp.x;
        float dy = kp.y - pp.y;
        constexpr float kStealRange = 2.0f;
        if ((dx * dx + dy * dy) <= kStealRange * kStealRange) {
            const Vector3& cam = camera_->GetTranslate();
            float sx, sy;
            SceneShared::WorldToScreen(kp.x, kp.y + 2.2f, cam.x, cam.y, sx, sy);
            fontRenderer_.DrawString("[ J ] Steal Weapon", sx - 90.0f, sy, 1.5f,
                { 0.6f, 0.85f, 1.0f, 1.0f });
        }
    }
}

void BattleTestScene::DrawWeaponHud(bool nearReturnPortal)
{
    constexpr float kScale = 1.5f;
    constexpr Vector4 kColorHint = { 0.6f, 0.6f, 0.6f, 1.0f };

    float py = SceneShared::DrawWeaponListHud(fontRenderer_, weaponManager_, L"テストステージ");
    fontRenderer_.DrawStringW(L"[L] 格闘  [K] 射撃  [R] 覚醒  [Shift] ロックオン切替", 12.0f, py, kScale, kColorHint);

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
    if (knight_) { knight_->Draw(); }
    player_->Draw();
    // 武器スロットの3Dアイコン（左下固定表示）
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        auto& icon3d = weaponIcons3D_[i];
        if (icon3d.slotIndex == i && icon3d.object && weaponManager_->IsUnlocked(i)) {
            icon3d.object->Draw();
        }
    }
    dummySlice_.Draw();

    // パーティクル（PreDraw でコマンドリストがリセットされるため Draw() 内で呼ぶ）
    pm_->Update(camera_.get());
    pm_->Draw(camera_.get());

    bladeFlash_.Draw();

    // 空間歪み（バックバッファ直描き時のみUIより先に画面をキャプチャして歪ませる）
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
    DrawWeaponSlotHud();

    // 大技中と解放フレーム（凍結画面のキャプチャ前）だけ暗転を重ねる
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
    return SceneShared::GetActiveRTVHandle(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}

void BattleTestScene::SetupMainRenderTarget()
{
    SceneShared::SetupMainRenderTarget(dxCommon_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
}
