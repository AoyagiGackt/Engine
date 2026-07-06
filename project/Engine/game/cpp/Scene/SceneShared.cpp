#include "SceneShared.h"
#include "Camera.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace engine;
using namespace engine::graphics;

namespace engine::game::SceneShared {

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

void UpdateWeaponCycle(Input* input, WeaponManager* weaponManager, float& weaponCycleTimer)
{
    // 武器切り替え（Q/E、数字キー 1〜4）
    weaponCycleTimer -= GameConstants::kFrameDeltaTime;
    if (weaponCycleTimer <= 0.0f) {
        if (input->TriggerKey(DIK_Q)) { weaponManager->SelectPrev(); weaponCycleTimer = 0.15f; }
        if (input->TriggerKey(DIK_E)) { weaponManager->SelectNext(); weaponCycleTimer = 0.15f; }
        for (int i = 0; i < weaponManager->GetCount(); ++i) {
            if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) {
                weaponManager->SelectIndex(i);
                weaponCycleTimer = 0.15f;
            }
        }
    }
}

void WorldToScreen(float worldX, float worldY, float camX, float camY, float& outX, float& outY)
{
    outX = (worldX - camX) / GameConstants::kCameraHalfW * 640.0f + 640.0f;
    outY = -(worldY - camY) / GameConstants::kCameraHalfH * 360.0f + 360.0f;
}

void UpdateCameraFollow(Camera* camera, const Vector3& playerPos)
{
    constexpr float kBlockRadius = 0.5f;
    camera->SetTranslate({
        std::clamp(playerPos.x,          2.0f  - kBlockRadius + GameConstants::kCameraHalfW,  28.0f + kBlockRadius - GameConstants::kCameraHalfW),
        std::clamp(playerPos.y + 6.0f, -0.6f - kBlockRadius + GameConstants::kCameraHalfH,  13.0f + kBlockRadius - GameConstants::kCameraHalfH),
        -30.0f
    });
}

bool UpdatePortalTransition(Input* input, const Vector3& playerPos,
    float portalX, float proximity, const char* targetSceneName)
{
    bool isNear = std::abs(playerPos.x - portalX) < proximity;
    if (isNear && input->TriggerKey(DIK_RETURN)) {
        SceneManager::GetInstance()->ChangeScene(targetSceneName);
    }
    return isNear;
}

float DrawWeaponListHud(FontRenderer& fontRenderer, WeaponManager* weaponManager, const wchar_t* headerText)
{
    constexpr float kScale = 1.5f;
    constexpr float kLineH = FontRenderer::kCharH * kScale;
    constexpr Vector4 kColorHeader = { 1.0f, 0.85f, 0.0f, 1.0f };
    constexpr Vector4 kColorNormal = { 0.85f, 0.85f, 0.85f, 1.0f };
    constexpr Vector4 kColorSel    = { 1.0f, 1.0f, 0.2f, 1.0f };
    constexpr Vector4 kColorHint   = { 0.6f, 0.6f, 0.6f, 1.0f };

    float px = 12.0f;
    float py = 12.0f;

    fontRenderer.DrawStringW(headerText, px, py, kScale, kColorHeader);
    py += kLineH + 2.0f;
    fontRenderer.DrawStringW(L"-- 武器選択 --", px, py, kScale, kColorNormal);
    py += kLineH + 2.0f;

    const auto& weaponList = weaponManager->GetList();
    for (int i = 0; i < static_cast<int>(weaponList.size()); ++i) {
        bool sel = (i == weaponManager->GetIndex());
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s %d.%-8s DMG:%.0f  RNG:%.1f",
            sel ? ">" : " ", i + 1, weaponList[i].name.c_str(), weaponList[i].damage, weaponList[i].range);
        fontRenderer.DrawString(buf, px, py, kScale, sel ? kColorSel : kColorNormal);
        py += kLineH;
    }

    py += 4.0f;
    fontRenderer.DrawString("Q/E  1-4 : Switch", px, py, kScale, kColorHint);
    py += kLineH;
    return py;
}

void DrawControlsHud(FontRenderer& fontRenderer, const wchar_t* portalActionLabel)
{
    // ── 操作説明（右パネル） ─────────────────────────────────────────
    constexpr float kIx     = 870.0f;
    constexpr float kIS     = 1.3f;
    constexpr float kILineH = FontRenderer::kCharH * kIS + 2.0f;
    constexpr Vector4 kCH   = { 1.0f, 0.85f, 0.0f, 1.0f };
    constexpr Vector4 kCD   = { 0.72f, 0.72f, 0.72f, 1.0f };
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
    row("K      ", L": 射撃");
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
    constexpr float kBarW  = 280.0f;
    constexpr float kBarH  =  14.0f;
    constexpr float kBarX  = 640.0f - kBarW * 0.5f;
    constexpr float kBarY  = 700.0f;

    bgSprite->SetPosition({ kBarX, kBarY });
    bgSprite->SetSize({ kBarW, kBarH });
    bgSprite->Update();

    float pulse = awakened ? (0.7f + 0.3f * std::sin(pulseTimer * 8.0f)) : 1.0f;
    Vector4 fgColor = awakened
        ? Vector4{ 0.05f * pulse, 0.6f * pulse, 1.0f, 0.95f }
        : Vector4{ 0.10f, 0.45f, 0.95f, 0.85f };
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

} // namespace engine::game::SceneShared
