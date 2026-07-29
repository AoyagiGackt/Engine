/**
 * @file SceneShared.cpp
 * @brief BattleTestScene/TrainingScene/GamePlayScene間で共通の武器切替・カメラ追従・HUD描画処理（SceneShared名前空間）の実装
 */
#include "SceneShared.h"
#include "BulletPool.h"
#include "Camera.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "ParticleManager.h"
#include "Player.h"
#include "PostEffectRenderTarget.h"
#include "ScreenFlash.h"
#include "SceneManager.h"
#include "SlashMark.h"
#include "Sprite.h"
#include "TimeManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
using namespace engine;
using namespace engine::graphics;

namespace engine::game::SceneShared {

namespace {
    // フィニッシャー演出共通の色（青白い剣閃）
    constexpr Vector4 kFinisherGlowColor = { 0.60f, 0.85f, 1.00f, 0.95f };
    constexpr Vector4 kFinisherSparkColor = { 0.80f, 0.95f, 1.00f, 1.00f };

    std::mt19937& FinisherRng()
    {
        static std::mt19937 rng { std::random_device { }() };
        return rng;
    }
} // namespace

void InitializeWeaponSlotHud(SpriteCommon* spriteCommon, WeaponManager* weaponManager,
    WeaponSlotUI* slots, Vector2* slotPos, int slotCount,
    float slotSize, float slotGap, float marginX, float baseY, bool checkUnlockedForInitialColor,
    std::unique_ptr<Sprite>& gunFrame, std::unique_ptr<Sprite>& gunIcon, Vector2& gunPos)
{
    const auto& list = weaponManager->GetList();
    for (int i = 0; i < slotCount; ++i) {
        const float x = marginX + static_cast<float>(i) * (slotSize + slotGap);
        slotPos[i] = { x, baseY };

        slots[i].frame = std::make_unique<Sprite>();
        slots[i].frame->Initialize(spriteCommon, "Resources/white.png");
        slots[i].frame->SetPosition({ x, baseY });
        slots[i].frame->SetSize({ slotSize, slotSize });

        slots[i].icon = std::make_unique<Sprite>();
        slots[i].icon->Initialize(spriteCommon, "Resources/white.png");
        slots[i].icon->SetPosition({ x + 6.0f, baseY + 6.0f });
        slots[i].icon->SetSize({ slotSize - 12.0f, slotSize - 12.0f });

        const bool showStyleColor = i < static_cast<int>(list.size())
            && (!checkUnlockedForInitialColor || weaponManager->IsUnlocked(i));
        if (showStyleColor) {
            const float* c = list[i].styleColor;
            slots[i].icon->SetColor({ c[0], c[1], c[2], 0.9f });
        } else if (checkUnlockedForInitialColor) {
            slots[i].icon->SetColor({ 0.2f, 0.2f, 0.2f, 0.35f }); // 未解放スロット
        }
    }

    const float gunX = marginX + static_cast<float>(slotCount) * (slotSize + slotGap) + 24.0f;
    gunPos = { gunX, baseY };

    gunFrame = std::make_unique<Sprite>();
    gunFrame->Initialize(spriteCommon, "Resources/white.png");
    gunFrame->SetPosition({ gunX, baseY });
    gunFrame->SetSize({ slotSize, slotSize });

    gunIcon = std::make_unique<Sprite>();
    gunIcon->Initialize(spriteCommon, "Resources/white.png");
    gunIcon->SetAnchorPoint({ 0.5f, 0.5f });
    gunIcon->SetPosition({ gunX + slotSize * 0.5f, baseY + slotSize * 0.5f });
    gunIcon->SetSize({ slotSize - 20.0f, slotSize - 20.0f });
    gunIcon->SetColor({ 0.6f, 0.85f, 1.0f, 0.9f });
}

void UpdateWeaponSlotHud(WeaponManager* weaponManager, WeaponSlotUI* slots, int slotCount,
    float pulseTimer, float flash, Sprite* gunIcon, float gunIconAngle)
{
    const int activeIndex = weaponManager->GetSelectedSlot();
    const float pulse = 0.7f + 0.3f * std::sin(pulseTimer * 6.0f);

    const auto& list = weaponManager->GetList();
    for (int i = 0; i < slotCount; ++i) {
        const bool active = (i == activeIndex);
        const float frameB = 0.08f + (active ? pulse * 0.35f : 0.0f) + flash * 0.5f;
        slots[i].frame->SetColor({ frameB, frameB, frameB + (active ? 0.2f : 0.05f), 0.85f });
        slots[i].frame->Update();

        const int weaponIndex = weaponManager->GetSlotWeaponIndex(i);
        const bool unlocked = weaponIndex >= 0 && weaponIndex < static_cast<int>(list.size())
            && weaponManager->IsUnlocked(weaponIndex);
        if (unlocked) {
            const float iconMul = (active ? (0.7f + pulse * 0.3f) : 0.5f) + flash * 0.5f;
            const float* c = list[weaponIndex].styleColor;
            slots[i].icon->SetColor({ c[0] * iconMul, c[1] * iconMul, c[2] * iconMul, 0.95f });
        } else {
            const float lockFlash = 0.2f + flash * 0.6f; // 新規解放の瞬間はロック中のスロットも一緒に光らせる
            slots[i].icon->SetColor({ lockFlash, lockFlash, lockFlash, 0.35f + flash * 0.3f });
        }
        slots[i].icon->Update();
    }

    gunIcon->SetRotation(gunIconAngle);
    gunIcon->Update();
}

void DrawWeaponSlotFrames(const WeaponSlotUI* slots, int slotCount, Sprite* gunFrame)
{
    for (int i = 0; i < slotCount; ++i) {
        slots[i].frame->Draw();
    }
    gunFrame->Draw();
}

void DrawWeaponSlotIconsAndLabels(const WeaponSlotUI* slots, int slotCount, const Vector2* slotPos,
    Sprite* gunIcon, const Vector2& gunPos, WeaponManager* weaponManager, FontRenderer& fontRenderer,
    float slotSize)
{
    for (int i = 0; i < slotCount; ++i) {
        slots[i].icon->Draw();
    }
    gunIcon->Draw();

    const auto& list = weaponManager->GetList();
    for (int i = 0; i < slotCount && i < static_cast<int>(list.size()); ++i) {
        if (weaponManager->IsUnlocked(i)) {
            continue;
        }
        fontRenderer.DrawStringW(L"?",
            slotPos[i].x + slotSize * 0.5f - 6.0f,
            slotPos[i].y + slotSize * 0.5f - 12.0f, 1.6f,
            { 0.6f, 0.6f, 0.6f, 0.9f });
    }
    fontRenderer.DrawString("GUN", gunPos.x + 10.0f, gunPos.y + slotSize + 4.0f, 1.0f,
        { 0.6f, 0.85f, 1.0f, 0.9f });
}

std::unique_ptr<Sprite> CreateFinisherOverlay(SpriteCommon* spriteCommon)
{
    auto overlay = std::make_unique<Sprite>();
    overlay->Initialize(spriteCommon, "Resources/white.png");
    overlay->SetPosition({ 0.0f, 0.0f });
    overlay->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });
    overlay->SetColor({ 0.0f, 0.0f, 0.05f, GameConstants::kFinisherOverlayAlpha });
    return overlay;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle(engine::DirectXCommon* dxCommon,
    std::initializer_list<IPostEffectSource*> effects)
{
    return GetActiveSceneRTVHandle(dxCommon, effects);
}

void SetupMainRenderTarget(engine::DirectXCommon* dxCommon,
    std::initializer_list<IPostEffectSource*> effects)
{
    SetupSceneRenderTarget(dxCommon, GetActiveSceneRTVHandle(dxCommon, effects));
}

void CreateParticleGroupsFromJson(ParticleManager* pm, const std::string& jsonPath)
{
    for (const auto& group : JsonHelper::Load(jsonPath)) {
        std::string name = group.value("name", "");
        if (name.empty()) {
            continue;
        }
        pm->CreateParticleGroup(name, group.value("texture", ""));
        pm->SetAdditiveBlend(name, group.value("additive", false));
    }
}

void UpdateWeaponCycle(Input* input, WeaponManager* weaponManager,
    float& weaponCycleTimer, bool cycleAllUnlocked)
{
    // 武器切り替え（Q/E、数字キー 1〜4）
    weaponCycleTimer -= GameConstants::kFrameDeltaTime;
    if (weaponCycleTimer <= 0.0f) {
        if (input->TriggerKey(DIK_Q)) {
            if (cycleAllUnlocked) {
                weaponManager->SelectPrevUnlockedInCurrentSlot();
            } else {
                weaponManager->SelectPrev();
            }
            weaponCycleTimer = 0.15f;
        }
        if (input->TriggerKey(DIK_E)) {
            if (cycleAllUnlocked) {
                weaponManager->SelectNextUnlockedInCurrentSlot();
            } else {
                weaponManager->SelectNext();
            }
            weaponCycleTimer = 0.15f;
        }
        for (int i = 0; i < 4; ++i) {
            if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) {
                weaponManager->SelectSlot(i);
                weaponCycleTimer = 0.15f;
            }
        }
    }
}

void UpdateSpinShotFire(Player* player, BulletPool& bulletPool)
{
    if (!player->JustSpinShot()) {
        return;
    }

    constexpr float kBulletSpeed = 0.30f;
    const Vector3& pp = player->GetPosition();
    Vector3 firePos = { pp.x, pp.y, 0.0f };

    if (player->IsUpsideDown()) {
        // 逆さ: 下方向中心に 5 方向ばらまき
        constexpr float kBaseAngle = 270.0f * (3.14159265f / 180.0f); // 真下
        constexpr float kSpread = 30.0f * (3.14159265f / 180.0f); // 30°間隔
        for (int i = -2; i <= 2; ++i) {
            float angle = kBaseAngle + i * kSpread;
            bulletPool.Spawn(firePos, { std::cos(angle) * kBulletSpeed, std::sin(angle) * kBulletSpeed, 0.0f });
        }
        TimeManager::GetInstance()->RequestHitStop(3);
        ScreenFlash::GetInstance()->Request({ 1.0f, 0.7f, 0.1f, 0.55f }, 0.10f);
    } else {
        // 通常: 向いている方向に 1 発
        bulletPool.Spawn(firePos, { player->GetLastDirX() * kBulletSpeed, 0.0f, 0.0f });
    }
}

engine::AABB MakeDirectionalRange(const Vector3& playerPos, float dirX, float frontRange, float backRange)
{
    const float left = (dirX >= 0.0f) ? backRange : frontRange;
    const float right = (dirX >= 0.0f) ? frontRange : backRange;
    return { { playerPos.x - left, playerPos.y - 1.5f, -0.5f },
        { playerPos.x + right, playerPos.y + 1.5f, 0.5f } };
}

engine::AABB MakeDirectionalShotRange(const Vector3& playerPos, float dirX, float frontRange, float backRange)
{
    constexpr float kShotHalfHeight = 0.3f;
    const float left = (dirX >= 0.0f) ? backRange : frontRange;
    const float right = (dirX >= 0.0f) ? frontRange : backRange;
    return { { playerPos.x - left, playerPos.y - kShotHalfHeight, -0.5f },
        { playerPos.x + right, playerPos.y + kShotHalfHeight, 0.5f } };
}

void WorldToScreen(float worldX, float worldY, float camX, float camY, float& outX, float& outY)
{
    outX = (worldX - camX) / GameConstants::kCameraHalfW * GameConstants::kScreenCenterX + GameConstants::kScreenCenterX;
    outY = -(worldY - camY) / GameConstants::kCameraHalfH * GameConstants::kScreenCenterY + GameConstants::kScreenCenterY;
}

void UpdateCameraFollow(Camera* camera, const Vector3& playerPos, const std::vector<AABB>& stageSolids, const Vector3* lockTarget)
{
    // ロックオン中はカメラをほんの少しだけ対象側へ寄せる（気付きにくいという声への対策、
    // 派手に振るとロック対象がプレイヤーの目の前にいる時に画角が窮屈になるので控えめにする）
    constexpr float kLockOnCameraShiftRatio = 0.15f;
    // stageSolidsが空の場合(配置ブロックが1つも無いシーン)に使う既定ステージ範囲
    // 通常のトレーニング/バトルテストステージの境界ブロック配置(BorderBlockBuilder参照)に合わせた値
    constexpr float kBlockRadius = 0.5f;
    constexpr float kDefaultStageLeft = 2.0f;
    constexpr float kDefaultStageRight = 36.0f;
    constexpr float kDefaultStageBottom = -1.0f;
    constexpr float kDefaultStageTop = 12.0f;
    float stageLeft = kDefaultStageLeft - kBlockRadius;
    float stageRight = kDefaultStageRight + kBlockRadius;
    float stageBottom = kDefaultStageBottom - kBlockRadius;
    float stageTop = kDefaultStageTop + kBlockRadius;
    if (!stageSolids.empty()) {
        stageLeft = stageSolids.front().min.x;
        stageRight = stageSolids.front().max.x;
        stageBottom = stageSolids.front().min.y;
        stageTop = stageSolids.front().max.y;
        for (const AABB& solid : stageSolids) {
            stageLeft = (std::min)(stageLeft, solid.min.x);
            stageRight = (std::max)(stageRight, solid.max.x);
            stageBottom = (std::min)(stageBottom, solid.min.y);
            stageTop = (std::max)(stageTop, solid.max.y);
        }
    }
    const float cameraMinX = stageLeft + GameConstants::kCameraHalfW;
    const float cameraMaxX = stageRight - GameConstants::kCameraHalfW;
    float cameraX = cameraMinX <= cameraMaxX
        ? std::clamp(playerPos.x, cameraMinX, cameraMaxX)
        : (stageLeft + stageRight) * 0.5f;
    if (lockTarget != nullptr) {
        cameraX += (lockTarget->x - playerPos.x) * kLockOnCameraShiftRatio;
        if (cameraMinX <= cameraMaxX) {
            cameraX = std::clamp(cameraX, cameraMinX, cameraMaxX);
        }
    }

    // Xと同様にYも組んだブロックの範囲内へクランプし、ジャンプ等でブロックの外（未構築の空間）が
    // 画面に映り込まないようにする
    const float cameraMinY = stageBottom + GameConstants::kCameraHalfH;
    const float cameraMaxY = stageTop - GameConstants::kCameraHalfH;
    const float cameraTargetY = playerPos.y + GameConstants::kCameraFollowOffsetY;
    const float cameraY = cameraMinY <= cameraMaxY
        ? std::clamp(cameraTargetY, cameraMinY, cameraMaxY)
        : (stageBottom + stageTop) * 0.5f;
    camera->SetTranslate({ cameraX, cameraY, GameConstants::kCameraDistanceZ });
}

bool UpdatePortalTransition(Input* input, const Vector3& playerPos,
    float portalX, float proximity, const char* targetSceneName)
{
    bool isNear = std::abs(playerPos.x - portalX) < proximity;
    if (isNear && input->TriggerKey(DIK_RETURN)) {
        SceneManager::GetInstance()->ChangeSceneWithLoading(targetSceneName);
    }
    return isNear;
}

float DrawWeaponListHud(FontRenderer& fontRenderer, WeaponManager* weaponManager, const wchar_t* headerText)
{
    constexpr float kScale = 1.15f;
    constexpr float kLineH = FontRenderer::kCharH * kScale;
    // 操作説明パネルと同じく、明るいブロックの上でも埋もれないよう暖色系＋影付きにする
    constexpr Vector4 kColorHeader = { 1.0f, 0.78f, 0.15f, 1.0f }; // アンバー
    constexpr Vector4 kColorNormal = { 0.95f, 0.92f, 0.80f, 1.0f }; // クリーム
    constexpr Vector4 kColorSel = { 1.0f, 0.95f, 0.35f, 1.0f }; // 選択中は明るい黄
    constexpr Vector4 kColorLocked = { 0.55f, 0.50f, 0.40f, 0.85f };
    constexpr Vector4 kColorHint = { 0.80f, 0.76f, 0.65f, 1.0f };
    constexpr Vector4 kShadow = { 0.05f, 0.04f, 0.02f, 0.9f };
    constexpr float kShadowOffset = 1.6f;

    float px = 12.0f;
    float py = 12.0f;

    auto drawShadowedW = [&](const std::wstring& text, float x, float y, const Vector4& color) {
        fontRenderer.DrawStringW(text, x + kShadowOffset, y + kShadowOffset, kScale, kShadow);
        fontRenderer.DrawStringW(text, x, y, kScale, color);
    };
    auto drawShadowed = [&](const std::string& text, float x, float y, const Vector4& color) {
        fontRenderer.DrawString(text, x + kShadowOffset, y + kShadowOffset, kScale, kShadow);
        fontRenderer.DrawString(text, x, y, kScale, color);
    };

    drawShadowedW(headerText, px, py, kColorHeader);
    py += kLineH + 2.0f;
    drawShadowedW(L"-- 武器選択 --", px, py, kColorNormal);
    py += kLineH + 2.0f;

    const auto& weaponList = weaponManager->GetList();
    for (int slot = 0; slot < 4; ++slot) {
        const int weaponIndex = weaponManager->GetSlotWeaponIndex(slot);
        const bool occupied = weaponIndex >= 0;
        const bool selected = occupied && slot == weaponManager->GetSelectedSlot();
        char buf[80];
        if (occupied) {
            const auto& weapon = weaponList[weaponIndex];
            std::snprintf(buf, sizeof(buf), "%s SLOT %d  %-8s  DMG %.0f  RNG %.1f",
                selected ? ">" : " ", slot + 1, weapon.name.c_str(), weapon.damage, weapon.range);
        } else {
            std::snprintf(buf, sizeof(buf), "  SLOT %d  EMPTY", slot + 1);
        }
        drawShadowed(buf, px, py,
            selected ? kColorSel : occupied ? kColorNormal
                                            : kColorLocked);
        py += kLineH;
    }

    // 選択中の銃（近接スタイルとは独立に G キーで循環）
    py += 2.0f;
    const RangedWeaponData& gun = weaponManager->GetRanged();
    std::wstring gunLine = L"銃[G]: " + gun.nameJp;
    drawShadowedW(gunLine, px, py, kColorSel);
    py += kLineH;

    py += 4.0f;
    drawShadowedW(L"Q E または 1から4  武器切替    G  銃切替", px, py, kColorHint);
    py += kLineH;
    return py;
}

void DrawAwakenGaugeHud(FontRenderer& fontRenderer, Sprite* bgSprite, Sprite* fgSprite,
    float gauge, bool awakened, float pulseTimer)
{
    constexpr float kScale = 1.5f;
    constexpr float kBarW = 280.0f;
    constexpr float kBarH = 14.0f;
    constexpr float kBarX = 640.0f - kBarW * 0.5f;
    constexpr float kBarY = 700.0f;

    bgSprite->SetPosition({ kBarX, kBarY });
    bgSprite->SetSize({ kBarW, kBarH });
    bgSprite->Update();

    float pulse = awakened ? (0.7f + 0.3f * std::sin(pulseTimer * 8.0f)) : 1.0f;
    Vector4 fgColor = awakened
        ? Vector4 { 0.05f * pulse, 0.6f * pulse, 1.0f, 0.95f }
        : Vector4 { 0.10f, 0.45f, 0.95f, 0.85f };
    fgSprite->SetColor(fgColor);
    fgSprite->SetPosition({ kBarX, kBarY });
    fgSprite->SetSize({ kBarW * gauge, kBarH });
    fgSprite->Update();

    constexpr Vector4 kLabelColor = { 0.6f, 0.8f, 1.0f, 0.9f };
    fontRenderer.DrawString("AWAKEN", kBarX, kBarY - 18.0f, kScale, kLabelColor);
    if (awakened) {
        fontRenderer.DrawString("ACTIVE", kBarX + kBarW - 70.0f, kBarY - 18.0f, kScale,
            { pulse, pulse, 1.0f, 1.0f });
    } else if (gauge >= 0.3f) {
        fontRenderer.DrawString("[R] Activate", kBarX + kBarW * 0.5f - 70.0f,
            kBarY - 18.0f, kScale, { 0.8f, 0.9f, 1.0f, 0.8f });
    }
}

void EmitFinisherCharge(ParticleManager* pm,
    const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos)
{
    auto& rng = FinisherRng();
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    std::uniform_real_distribution<float> radiusDist(2.2f, 3.4f);
    std::uniform_real_distribution<float> scaleDist(0.10f, 0.20f);

    // 周囲から中心へ吸い込まれる光粒溜め時間内に到達する速度を逆算する
    constexpr int kMoteCount = 20;
    for (int i = 0; i < kMoteCount; ++i) {
        const float ang = angleDist(rng);
        const float r = radiusDist(rng);
        Vector3 spawn = { pos.x + std::cos(ang) * r, pos.y + std::sin(ang) * r, 0.0f };
        const float speed = r / GameConstants::kFinisherChargeDelay;
        Vector3 vel = { -std::cos(ang) * speed, -std::sin(ang) * speed, 0.0f };
        pm->EmitWithColor(sparkGroup, spawn, vel, kFinisherGlowColor,
            GameConstants::kFinisherChargeDelay, scaleDist(rng), true);
    }

    pm->EmitRing(ringGroup, pos, 2.0f, kFinisherGlowColor, 10, 0.30f, 0.22f);
}

void EmitFinisherSlashLine(ParticleManager* pm,
    const std::string& slashGroup, const std::string& sparkGroup,
    const Vector3& center, float angle, float halfLength)
{
    auto& rng = FinisherRng();
    std::uniform_real_distribution<float> tDist(-halfLength, halfLength);
    std::uniform_real_distribution<float> driftDist(-0.8f, 0.8f);
    std::uniform_real_distribution<float> scaleDist(0.08f, 0.16f);

    if (!slashGroup.empty()) {
        pm->EmitSlash(slashGroup, center, angle, { 0.60f, 0.85f, 1.0f, 0.9f }, halfLength);
    }

    // 斬線に沿って散る煌めき
    const Vector2 dir = { std::cos(angle), std::sin(angle) };
    constexpr int kGlintCount = 4;
    for (int i = 0; i < kGlintCount; ++i) {
        const float t = tDist(rng);
        Vector3 spawn = { center.x + dir.x * t, center.y + dir.y * t, 0.0f };
        Vector3 vel = { driftDist(rng), driftDist(rng) + 0.5f, 0.0f };
        pm->EmitWithColor(sparkGroup, spawn, vel, kFinisherSparkColor, 0.25f, scaleDist(rng), true);
    }

    // 交点の閃光（細い針状の光条）
    pm->EmitHitStar(sparkGroup, center, kFinisherSparkColor);
}

void EmitFinisherRelease(ParticleManager* pm,
    const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos)
{
    auto& rng = FinisherRng();

    // 速度差のある二重リングで衝撃波の広がりを作る
    pm->EmitRing(ringGroup, pos, 9.0f, { 0.70f, 0.90f, 1.0f, 1.0f }, 28, 0.55f, 0.40f);
    pm->EmitRing(ringGroup, pos, 4.5f, kFinisherGlowColor, 18, 0.45f, 0.26f);

    // 放射状に飛び散る火花
    std::uniform_real_distribution<float> vxDist(-7.0f, 7.0f);
    std::uniform_real_distribution<float> vyDist(4.0f, 11.0f);
    for (int i = 0; i < 24; ++i) {
        pm->EmitGravity(sparkGroup, pos,
            { vxDist(rng), vyDist(rng), 0.0f },
            kFinisherSparkColor, 1.1f, 0.22f);
    }

    // ゆっくり立ち昇る余韻の光粒
    std::uniform_real_distribution<float> offXDist(-1.5f, 1.5f);
    std::uniform_real_distribution<float> riseDist(1.2f, 2.8f);
    std::uniform_real_distribution<float> scaleDist(0.10f, 0.22f);
    for (int i = 0; i < 12; ++i) {
        Vector3 spawn = { pos.x + offXDist(rng), pos.y + offXDist(rng) * 0.5f, 0.0f };
        pm->EmitWithColor(sparkGroup, spawn, { 0.0f, riseDist(rng), 0.0f },
            kFinisherGlowColor, 0.9f, scaleDist(rng), true);
    }

    // 中心の大きな光条
    pm->EmitHitStar(sparkGroup, pos, { 1.0f, 1.0f, 1.0f, 1.0f });
    pm->EmitHitStar(sparkGroup, pos, kFinisherSparkColor);
}

void SpawnSlashMarkWorld(const Vector2& start, const Vector2& end, float camX, float camY,
    const Vector4& color, float thickness, float duration)
{
    SlashMarkParams sm;
    WorldToScreen(start.x, start.y, camX, camY, sm.start.x, sm.start.y);
    WorldToScreen(end.x, end.y, camX, camY, sm.end.x, sm.end.y);
    sm.color = color;
    sm.thickness = thickness;
    sm.duration = duration;
    SlashMark::GetInstance()->Spawn(sm);
}

} // namespace engine::game::SceneShared
