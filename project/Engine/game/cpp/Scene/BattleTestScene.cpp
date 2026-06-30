#include "BattleTestScene.h"
#include "Collision.h"
#include "GameConstants.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

static constexpr float kWarpRetX   =  3.0f;
static constexpr float kReturnProx =  3.0f;
static constexpr float kDummyMaxHp = 100.0f;

static void WorldToScreen(float wx, float wy, float cx, float cy,
    float& sx, float& sy)
{
    sx = (wx - cx) / GameConstants::kCameraHalfW * 640.0f + 640.0f;
    sy = -(wy - cy) / GameConstants::kCameraHalfH * 360.0f + 360.0f;
}

void BattleTestScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_ = dxCommon;
    input_    = input;
    audio_    = audio;

    srvManager_    = SrvManager::GetInstance();
    weaponManager_ = WeaponManager::GetInstance();
    pm_            = ParticleManager::GetInstance();

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

    auto addBlock = [&](float x, float y, float z) {
        auto b = std::make_unique<Object3d>();
        b->Initialize(modelCommon_.get());
        b->SetModel(modelBlock_.get());
        b->SetEnableLighting(false);
        b->SetPosition({ x, y, z });
        b->Update();
        borderBlocks_.push_back(std::move(b));
    };
    for (int x = 0; x <= 28; ++x) { addBlock(static_cast<float>(x), -0.6f, 0.0f); }
    for (int x = 0; x <= 28; ++x) { addBlock(static_cast<float>(x), 13.0f, 0.0f); }
    for (int y = 0; y <= 12; ++y) { addBlock(2.0f,  static_cast<float>(y), 0.0f); }
    for (int y = 0; y <= 12; ++y) { addBlock(28.0f, static_cast<float>(y), 0.0f); }

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

    pm_->CreateParticleGroup("bt_hit_ring",  "Resources/circle2.png");
    pm_->CreateParticleGroup("bt_hit_spark", "Resources/circle2.png");
    pm_->SetAdditiveBlend("bt_hit_ring",  true);
    pm_->SetAdditiveBlend("bt_hit_spark", true);

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
        WorldToScreen(d.pos.x, d.pos.y, cam.x, cam.y, sx, sy);

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
    fontRenderer_.Reset();

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

    {
        constexpr float kBR = 0.5f;
        const Vector3& pp = player_->GetPosition();
        camera_->SetTranslate({
            std::clamp(pp.x,        2.0f  - kBR + GameConstants::kCameraHalfW,  28.0f + kBR - GameConstants::kCameraHalfW),
            std::clamp(pp.y + 6.0f, -0.6f - kBR + GameConstants::kCameraHalfH,  13.0f + kBR - GameConstants::kCameraHalfH),
            -30.0f
        });
    }

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    for (auto& b : borderBlocks_) { b->Update(); }

    warpPulseTimer_ += GameConstants::kFrameDeltaTime;
    float pulse = 0.6f + 0.4f * std::sin(warpPulseTimer_ * 4.0f);
    for (auto& p : warpPortalBlocks_) {
        p->SetColor({ 1.0f * pulse, 0.5f * pulse, 0.1f * pulse, 0.9f });
        p->Update();
    }

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

    const Vector3& cam = camera_->GetTranslate();
    bool nearReturn = std::abs(pp.x - kWarpRetX) < kReturnProx;

    if (nearReturn && input_->TriggerKey(DIK_RETURN)) {
        SceneManager::GetInstance()->ChangeScene("TRAINING");
    }

    // ---- UI（FontRenderer） ----
    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale;
    constexpr Vector4 kColorHeader = { 1.0f, 0.85f, 0.0f, 1.0f };
    constexpr Vector4 kColorNormal = { 0.85f, 0.85f, 0.85f, 1.0f };
    constexpr Vector4 kColorHint   = { 0.6f, 0.6f, 0.6f, 1.0f };

    float px = 12.0f;
    float py = 12.0f;

    fontRenderer_.DrawStringW(L"テストステージ", px, py, kScale, kColorHeader);
    py += kLineH + 2.0f;

    char buf[80];
    std::snprintf(buf, sizeof(buf), "武器: %s", weapon.name.c_str());
    fontRenderer_.DrawString(buf, px, py, kScale, kColorNormal);
    py += kLineH;

    std::snprintf(buf, sizeof(buf), "DMG:%.0f  RNG:%.1f  %.2fs",
        weapon.damage, weapon.range, weapon.attackInterval);
    fontRenderer_.DrawString(buf, px, py, kScale, kColorNormal);
    py += kLineH + 2.0f;

    fontRenderer_.DrawStringW(L"[L] 格闘  [K] 射撃  [R] 覚醒", px, py, kScale, kColorHint);

    // 戻りポータルのラベル
    if (nearReturn) {
        float sx, sy;
        WorldToScreen(kWarpRetX, 5.0f, cam.x, cam.y, sx, sy);
        constexpr Vector4 kColorReturn = { 1.0f, 0.6f, 0.1f, 1.0f };
        fontRenderer_.DrawString("[ ENTER ] Back", sx - 84.0f, sy - 36.0f, kScale, kColorReturn);
    }

    // ── 覚醒ゲージ UI ────────────────────────────────────────────────
    {
        constexpr float kBarW  = 280.0f;
        constexpr float kBarH  =  14.0f;
        constexpr float kBarX  = 640.0f - kBarW * 0.5f;
        constexpr float kBarY  = 700.0f;
        float gauge = player_->GetAwakenGauge();
        bool  awake = player_->IsAwakened();

        awakenGaugeBg_->SetPosition({ kBarX, kBarY });
        awakenGaugeBg_->SetSize({ kBarW, kBarH });
        awakenGaugeBg_->Update();

        float pulse = awake ? (0.7f + 0.3f * std::sin(warpPulseTimer_ * 8.0f)) : 1.0f;
        Vector4 fgColor = awake
            ? Vector4{ 0.05f * pulse, 0.6f * pulse, 1.0f, 0.95f }
            : Vector4{ 0.10f, 0.45f, 0.95f, 0.85f };
        awakenGaugeFg_->SetColor(fgColor);
        awakenGaugeFg_->SetPosition({ kBarX, kBarY });
        awakenGaugeFg_->SetSize({ kBarW * gauge, kBarH });
        awakenGaugeFg_->Update();

        constexpr Vector4 kLabelColor = { 0.6f, 0.8f, 1.0f, 0.9f };
        fontRenderer_.DrawString("AWAKEN", kBarX, kBarY - 18.0f, kScale, kLabelColor);
        if (awake) {
            fontRenderer_.DrawString("ACTIVE", kBarX + kBarW - 70.0f, kBarY - 18.0f, kScale,
                { pulse, pulse, 1.0f, 1.0f });
        } else if (gauge >= 0.3f) {
            fontRenderer_.DrawString("[R] Activate", kBarX + kBarW * 0.5f - 70.0f,
                kBarY - 18.0f, kScale, { 0.8f, 0.9f, 1.0f, 0.8f });
        }
    }
}

void BattleTestScene::Draw()
{
    shadowManager_->BeginShadowPass(dxCommon_->GetCommandList());
    modelCommon_->BeginShadowPass();
    shadowManager_->EndShadowPass(dxCommon_->GetCommandList());

    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon_->GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT vp = { 0, 0,
        static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight),
        0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, WinApp::kClientWidth, WinApp::kClientHeight };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(cmd);
    shadowManager_->SetShadowMap(cmd, srvManager_);

    for (auto& b : borderBlocks_)     { b->Draw(); }
    for (auto& p : warpPortalBlocks_) { p->Draw(); }
    for (auto& d : dummies_)          { d.object->Draw(); }
    bulletPool_.Draw();
    player_->Draw();

    // パーティクル（PreDraw でコマンドリストがリセットされるため Draw() 内で呼ぶ）
    pm_->Update(camera_.get());
    pm_->Draw(camera_.get());

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
    fontRenderer_.Draw();
}

void BattleTestScene::Finalize()
{
    pm_->ClearAllGroups();
}
