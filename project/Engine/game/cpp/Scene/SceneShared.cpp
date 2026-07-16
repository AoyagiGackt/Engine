#include "SceneShared.h"
#include "Camera.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "ParticleManager.h"
#include "PostEffectRenderTarget.h"
#include "SceneManager.h"
#include "SlashMark.h"
#include "Sprite.h"
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

void UpdateWeaponCycle(Input* input, WeaponManager* weaponManager, float& weaponCycleTimer)
{
    // 武器切り替え（Q/E、数字キー 1〜4）
    weaponCycleTimer -= GameConstants::kFrameDeltaTime;
    if (weaponCycleTimer <= 0.0f) {
        if (input->TriggerKey(DIK_Q)) {
            weaponManager->SelectPrev();
            weaponCycleTimer = 0.15f;
        }
        if (input->TriggerKey(DIK_E)) {
            weaponManager->SelectNext();
            weaponCycleTimer = 0.15f;
        }
        for (int i = 0; i < weaponManager->GetCount(); ++i) {
            if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) {
                weaponManager->SelectIndex(i);
                weaponCycleTimer = 0.15f;
            }
        }
    }
}

engine::AABB MakeDirectionalRange(const Vector3& playerPos, float dirX, float frontRange, float backRange)
{
    const float left = (dirX >= 0.0f) ? backRange : frontRange;
    const float right = (dirX >= 0.0f) ? frontRange : backRange;
    return { { playerPos.x - left, playerPos.y - 1.5f, -0.5f },
        { playerPos.x + right, playerPos.y + 1.5f, 0.5f } };
}

void WorldToScreen(float worldX, float worldY, float camX, float camY, float& outX, float& outY)
{
    outX = (worldX - camX) / GameConstants::kCameraHalfW * 640.0f + 640.0f;
    outY = -(worldY - camY) / GameConstants::kCameraHalfH * 360.0f + 360.0f;
}

void UpdateCameraFollow(Camera* camera, const Vector3& playerPos)
{
    constexpr float kBlockRadius = 0.5f;
    camera->SetTranslate({ std::clamp(playerPos.x, 2.0f - kBlockRadius + GameConstants::kCameraHalfW, 28.0f + kBlockRadius - GameConstants::kCameraHalfW),
        std::clamp(playerPos.y + 3.0f, -0.6f - kBlockRadius + GameConstants::kCameraHalfH, 13.0f + kBlockRadius - GameConstants::kCameraHalfH),
        -24.0f });
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
    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale;
    constexpr Vector4 kColorHeader = { 1.0f, 0.85f, 0.0f, 1.0f };
    constexpr Vector4 kColorNormal = { 0.85f, 0.85f, 0.85f, 1.0f };
    constexpr Vector4 kColorSel = { 1.0f, 1.0f, 0.2f, 1.0f };
    constexpr Vector4 kColorLocked = { 0.45f, 0.45f, 0.45f, 0.8f };
    constexpr Vector4 kColorHint = { 0.6f, 0.6f, 0.6f, 1.0f };

    float px = 12.0f;
    float py = 12.0f;

    fontRenderer.DrawStringW(headerText, px, py, kScale, kColorHeader);
    py += kLineH + 2.0f;
    fontRenderer.DrawStringW(L"-- 武器選択 --", px, py, kScale, kColorNormal);
    py += kLineH + 2.0f;

    const auto& weaponList = weaponManager->GetList();
    for (int i = 0; i < static_cast<int>(weaponList.size()); ++i) {
        bool unlocked = weaponManager->IsUnlocked(i);
        bool sel = unlocked && (i == weaponManager->GetIndex());
        char buf[64];
        if (unlocked) {
            std::snprintf(buf, sizeof(buf), "%s %d.%-8s DMG:%.0f  RNG:%.1f",
                sel ? ">" : " ", i + 1, weaponList[i].name.c_str(), weaponList[i].damage, weaponList[i].range);
        } else {
            std::snprintf(buf, sizeof(buf), "  %d.???      [LOCKED]", i + 1);
        }
        fontRenderer.DrawString(buf, px, py, kScale, sel ? kColorSel : unlocked ? kColorNormal
                                                                                : kColorLocked);
        py += kLineH;
    }

    // 選択中の銃（近接スタイルとは独立に G キーで循環）
    py += 2.0f;
    const RangedWeaponData& gun = weaponManager->GetRanged();
    std::wstring gunLine = L"銃[G]: " + gun.nameJp;
    fontRenderer.DrawStringW(gunLine, px, py, kScale, kColorSel);
    py += kLineH;

    py += 4.0f;
    fontRenderer.DrawString("Q/E 1-4 : Melee  G : Gun", px, py, kScale, kColorHint);
    py += kLineH;
    return py;
}

void DrawControlsHud(FontRenderer& fontRenderer, const wchar_t* portalActionLabel)
{
    // ── 操作説明（右パネル） ─────────────────────────────────────────
    constexpr float kIx = 870.0f;
    constexpr float kIS = 1.3f;
    constexpr float kILineH = FontRenderer::kCharH * kIS + 2.0f;
    constexpr Vector4 kCH = { 1.0f, 0.85f, 0.0f, 1.0f };
    constexpr Vector4 kCD = { 0.72f, 0.72f, 0.72f, 1.0f };
    float iy = 12.0f;

    fontRenderer.DrawStringW(L"-- 操作説明 --", kIx, iy, kIS, kCH);
    iy += kILineH + 2.0f;

    auto row = [&](const char* key, const wchar_t* desc) {
        std::wstring line(key, key + std::strlen(key));
        line += desc;
        fontRenderer.DrawStringW(line, kIx, iy, kIS, kCD);
        iy += kILineH;
    };
    row("A / D  ", L": 移動");
    row("W      ", L": ジャンプ");
    row("L      ", L": コンボ (x3)");
    row("K      ", L": 銃コンボ");
    row("G      ", L": 銃切替");
    row("SPACE  ", L": スピン連射");
    row("(Air)  ", L": スピン+散弾");
    row("Q / E  ", L": 武器切替");
    row("1-4", L": Weapon Select");
    row("ENTER", portalActionLabel);
    row("R", L": Awaken (30%+)");
    row("F", L": Finisher (gauge MAX)");
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
