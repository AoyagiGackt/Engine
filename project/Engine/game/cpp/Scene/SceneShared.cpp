/**
 * @file SceneShared.cpp
 * @brief BattleTestScene/TrainingScene/GamePlayScene間で共通の武器切替・カメラ追従・HUD描画処理（SceneShared名前空間）の実装
 */
#include "SceneShared.h"
#include "Audio.h"
#include "BulletPool.h"
#include "Camera.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "GameSettings.h"
#include "Input.h"
#include "JsonHelper.h"
#include "ParticleManager.h"
#include "Player.h"
#include "PostEffectRenderTarget.h"
#include "ScreenFlash.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "SlashMark.h"
#include "Sprite.h"
#include "TimeManager.h"
#include "UILayout.h"
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
    constexpr Vector4 kFinisherOverlayTint = { 0.0f, 0.0f, 0.05f, 0.0f }; // アルファはGameConstants::kFinisherOverlayAlpha

    constexpr float kDefaultIconScale = 0.2f; // weapon_icons.jsonにscaleがない時の既定値
    constexpr int kWeaponSlotCount = 4;
    constexpr float kWeaponCycleCooldown = 0.15f; // 武器切り替えの連打を抑える間隔（秒）

    // HUDの行送り
    constexpr float kHudSectionGap = 2.0f; // 見出しの下に空ける余白
    constexpr float kHudHintGap = 4.0f; // 操作ヒントの前に空ける余白
    constexpr const char* kSharedHudLayoutName = "hud"; // 武器一覧・操作説明の文字の見た目（全シーン共通）

    // 逆さ状態のスピン連射
    constexpr int kUpsideDownHitStopFrames = 3;
    constexpr Vector4 kUpsideDownFlashColor = { 1.0f, 0.7f, 0.1f, 0.55f };
    constexpr float kUpsideDownFlashSeconds = 0.10f;

    std::mt19937& FinisherRng()
    {
        static std::mt19937 rng { std::random_device { }() };
        return rng;
    }

    WeaponType ParseIconWeaponType(const std::string& type)
    {
        if (type == "Dagger")
            return WeaponType::Dagger;
        if (type == "Hammer")
            return WeaponType::Hammer;
        if (type == "Spear")
            return WeaponType::Spear;
        if (type == "Greatsword")
            return WeaponType::Greatsword;
        if (type == "Scythe")
            return WeaponType::Scythe;
        if (type == "Axe")
            return WeaponType::Axe;
        return WeaponType::Sword;
    }
} // namespace

std::vector<WeaponIconAsset> LoadWeaponIconAssets(const std::string& jsonPath)
{
    std::vector<WeaponIconAsset> assets;
    nlohmann::json j = engine::JsonHelper::Load(jsonPath);
    if (j.is_object() && j.contains("icons") && j["icons"].is_array()) {
        for (const auto& entry : j["icons"]) {
            WeaponIconAsset asset;
            asset.type = ParseIconWeaponType(entry.value("type", std::string("Sword")));
            asset.modelPath = entry.value("model", std::string());
            asset.texturePath = entry.value("texture", std::string());
            asset.scale = entry.value("scale", kDefaultIconScale);
            asset.baseYaw = entry.value("baseYawDeg", 0.0f) * GameConstants::kDegToRad;
            assets.push_back(std::move(asset));
        }
    }
    if (assets.empty()) {
        // Resources/Config/weapon_icons.json が無い場合の後方互換の既定値（目視調整済み）
        assets = {
            { WeaponType::Sword, "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png", 0.18f, 0.0f },
            { WeaponType::Dagger, "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png", 0.31f, 0.0f },
            { WeaponType::Hammer, "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png", 0.18f, GameConstants::kPi },
            { WeaponType::Spear, "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png", 0.08f, 0.0f },
            { WeaponType::Greatsword, "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png", 0.12f, 0.0f },
            { WeaponType::Scythe, "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png", 0.14f, 0.0f },
            { WeaponType::Axe, "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png", 0.13f, 0.0f },
        };
    }
    return assets;
}









std::unique_ptr<Sprite> CreateFinisherOverlay(SpriteCommon* spriteCommon)
{
    auto overlay = std::make_unique<Sprite>();
    overlay->Initialize(spriteCommon, "Resources/white.png");
    overlay->SetPosition({ 0.0f, 0.0f });
    overlay->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });
    overlay->SetColor({ kFinisherOverlayTint.x, kFinisherOverlayTint.y, kFinisherOverlayTint.z,
        GameConstants::kFinisherOverlayAlpha });
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
            weaponCycleTimer = kWeaponCycleCooldown;
        }
        if (input->TriggerKey(DIK_E)) {
            if (cycleAllUnlocked) {
                weaponManager->SelectNextUnlockedInCurrentSlot();
            } else {
                weaponManager->SelectNext();
            }
            weaponCycleTimer = kWeaponCycleCooldown;
        }
        for (int i = 0; i < kWeaponSlotCount; ++i) {
            if (input->TriggerKey(static_cast<uint8_t>(DIK_1 + i))) {
                weaponManager->SelectSlot(i);
                weaponCycleTimer = kWeaponCycleCooldown;
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
        constexpr float kBaseAngle = 270.0f * GameConstants::kDegToRad; // 真下
        constexpr float kSpread = 30.0f * GameConstants::kDegToRad; // 30°間隔
        constexpr int kSideShots = 2; // 中央の左右に撃つ本数
        for (int i = -kSideShots; i <= kSideShots; ++i) {
            float angle = kBaseAngle + i * kSpread;
            bulletPool.Spawn(firePos, { std::cos(angle) * kBulletSpeed, std::sin(angle) * kBulletSpeed, 0.0f });
        }
        TimeManager::GetInstance()->RequestHitStop(kUpsideDownHitStopFrames);
        ScreenFlash::GetInstance()->Request(kUpsideDownFlashColor, kUpsideDownFlashSeconds);
    } else {
        // 通常: 向いている方向に 1 発
        bulletPool.Spawn(firePos, { player->GetLastDirX() * kBulletSpeed, 0.0f, 0.0f });
    }
}

engine::AABB MakeDirectionalRange(const Vector3& playerPos, float dirX, float frontRange, float backRange)
{
    // 縦方向の許容量。大きすぎると足場の上下に離れた（＝見た目では当たっていない）敵まで
    // 判定に巻き込んでしまう（高低差のあるステージで顕在化する）
    constexpr float kVerticalHalfHeight = 0.8f;
    const float left = (dirX >= 0.0f) ? backRange : frontRange;
    const float right = (dirX >= 0.0f) ? frontRange : backRange;
    return { { playerPos.x - left, playerPos.y - kVerticalHalfHeight, -0.5f },
        { playerPos.x + right, playerPos.y + kVerticalHalfHeight, 0.5f } };
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
    float portalX, float proximity, const char* sceneName, const char* outcome,
    const char* fallbackScene, Audio* audio)
{
    bool isNear = std::abs(playerPos.x - portalX) < proximity;
    if (isNear && input->TriggerKey(DIK_RETURN)) {
        if (audio) {
            audio->PlayMenuSelect();
        }
        SceneFlow::GetInstance()->Transition(sceneName, outcome, fallbackScene);
    }
    return isNear;
}

void AdjustVolume(Audio* audio, bool bgm, float delta)
{
    GameSettings& settings = GameSettingsManager::GetInstance()->Get();
    float& volume = bgm ? settings.bgmVolume : settings.seVolume;
    const float previousVolume = volume;
    volume = std::clamp(volume + delta, 0.0f, 1.0f);
    if (volume == previousVolume) {
        return;
    }
    if (bgm) {
        audio->SetBGMVolume(volume);
    } else {
        audio->SetSEVolume(volume);
    }
    audio->PlayMenuChoice();
    GameSettingsManager::GetInstance()->Save();
}

float DrawWeaponListHud(FontRenderer& fontRenderer, WeaponManager* weaponManager, const wchar_t* headerText, const Vector2& anchor)
{
    constexpr float kDefaultScale = 1.15f;
    // 操作説明パネルと同じく、明るいブロックの上でも埋もれないよう暖色系＋影付きにする
    constexpr Vector4 kDefaultHeaderColor = { 1.0f, 0.78f, 0.15f, 1.0f }; // アンバー
    constexpr Vector4 kDefaultNormalColor = { 0.95f, 0.92f, 0.80f, 1.0f }; // クリーム
    constexpr Vector4 kDefaultSelectedColor = { 1.0f, 0.95f, 0.35f, 1.0f }; // 選択中は明るい黄
    constexpr Vector4 kDefaultLockedColor = { 0.55f, 0.50f, 0.40f, 0.85f };
    constexpr Vector4 kDefaultHintColor = { 0.80f, 0.76f, 0.65f, 1.0f };
    constexpr Vector4 kShadow = { 0.05f, 0.04f, 0.02f, 0.9f };
    constexpr float kShadowOffset = 1.6f;

    // 文字の大きさと色は全シーン共通のHUDレイアウト（Resources/Config/UI/hud.json）で調整する
    UILayout& layout = UILayout::Get(kSharedHudLayoutName);
    const float scale = layout.Float("weapon_list.scale", kDefaultScale);
    const float lineHeight = FontRenderer::kCharH * scale;
    const Vector4 headerColor = layout.Color("weapon_list.header_color", kDefaultHeaderColor);
    const Vector4 textColor = layout.Color("weapon_list.text_color", kDefaultNormalColor);
    const Vector4 selectedColor = layout.Color("weapon_list.selected_color", kDefaultSelectedColor);
    const Vector4 emptyColor = layout.Color("weapon_list.empty_color", kDefaultLockedColor);
    const Vector4 hintColor = layout.Color("weapon_list.hint_color", kDefaultHintColor);

    const float px = anchor.x;
    float py = anchor.y;

    auto drawShadowedW = [&](const std::wstring& text, float x, float y, const Vector4& color) {
        fontRenderer.DrawStringW(text, x + kShadowOffset, y + kShadowOffset, scale, kShadow);
        fontRenderer.DrawStringW(text, x, y, scale, color);
    };
    auto drawShadowed = [&](const std::string& text, float x, float y, const Vector4& color) {
        fontRenderer.DrawString(text, x + kShadowOffset, y + kShadowOffset, scale, kShadow);
        fontRenderer.DrawString(text, x, y, scale, color);
    };

    drawShadowedW(headerText, px, py, headerColor);
    py += lineHeight + kHudSectionGap;
    drawShadowedW(L"-- 武器選択 --", px, py, textColor);
    py += lineHeight + kHudSectionGap;

    const auto& weaponList = weaponManager->GetList();
    for (int slot = 0; slot < kWeaponSlotCount; ++slot) {
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
            selected ? selectedColor : occupied ? textColor
                                            : emptyColor);
        py += lineHeight;
    }

    // 選択中の銃（近接スタイルとは独立に G キーで循環）
    py += kHudSectionGap;
    const RangedWeaponData& gun = weaponManager->GetRanged();
    std::wstring gunLine = L"銃[G]: " + gun.nameJp;
    drawShadowedW(gunLine, px, py, selectedColor);
    py += lineHeight;

    py += kHudHintGap;
    drawShadowedW(L"Q E または 1から4  武器切替    G  銃切替", px, py, hintColor);
    py += lineHeight;
    return py;
}

void DrawControlsHud(FontRenderer& fontRenderer, const Vector2& anchor, const wchar_t* portalActionLabel)
{
    // ── 操作説明（右パネル） ─────────────────────────────────────────
    // 明るいブロックの上に乗ると薄い色の文字が背景に埋もれるため、色自体を変えるだけでなく
    // 影を1枚後ろに敷いて、背景が明るくても暗くても文字の輪郭が必ず見えるようにする
    constexpr float kDefaultScale = 1.05f;
    constexpr Vector4 kDefaultHeaderColor = { 1.0f, 0.78f, 0.15f, 1.0f }; // 見出し: アンバー
    constexpr Vector4 kDefaultTextColor = { 0.95f, 0.92f, 0.80f, 1.0f }; // 本文: 暖色寄りのクリーム
    UILayout& layout = UILayout::Get(kSharedHudLayoutName);
    const float x = anchor.x;
    const float scale = layout.Float("controls.scale", kDefaultScale);
    const float lineHeight = FontRenderer::kCharH * scale + kHudSectionGap;
    const Vector4 headerColor = layout.Color("controls.header_color", kDefaultHeaderColor);
    const Vector4 textColor = layout.Color("controls.text_color", kDefaultTextColor);
    constexpr Vector4 kShadow = { 0.05f, 0.04f, 0.02f, 0.9f };
    constexpr float kShadowOffset = 1.6f;
    float iy = anchor.y;

    auto drawShadowed = [&](const std::wstring& text, float y, const Vector4& color) {
        fontRenderer.DrawStringW(text, x + kShadowOffset, y + kShadowOffset, scale, kShadow);
        fontRenderer.DrawStringW(text, x, y, scale, color);
    };

    drawShadowed(L"-- 操作説明 --", iy, headerColor);
    iy += lineHeight + kHudSectionGap;

    auto row = [&](const char* key, const wchar_t* desc) {
        std::wstring line(key, key + std::strlen(key));
        line += desc;
        drawShadowed(line, iy, textColor);
        iy += lineHeight;
    };
    row("A / D  ", L": 移動");
    row("W      ", L": ジャンプ");
    row("I      ", L": 回避 (全身無敵、コンボから割り込み可)");
    row("L      ", L": コンボ (x3)");
    row("K      ", L": 銃コンボ");
    row("G      ", L": 銃切替");
    row("SPACE  ", L": 武器固有技");
    row("Q / E  ", L": 武器切替");
    row("1 - 4  ", L": スロット直接選択");
    row("ENTER  ", portalActionLabel);
    row("R      ", L": 覚醒発動");
    row("F      ", L": フィニッシャー");
}



void EmitFinisherCharge(ParticleManager* pm,
    const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos)
{
    auto& rng = FinisherRng();
    std::uniform_real_distribution<float> angleDist(0.0f, GameConstants::kTwoPi);
    constexpr float kMoteRadiusMin = 2.2f;
    constexpr float kMoteRadiusMax = 3.4f;
    constexpr float kMoteScaleMin = 0.10f;
    constexpr float kMoteScaleMax = 0.20f;
    constexpr float kChargeRingSpeed = 2.0f;
    constexpr int kChargeRingCount = 10;
    constexpr float kChargeRingLifetime = 0.30f;
    constexpr float kChargeRingSize = 0.22f;
    std::uniform_real_distribution<float> radiusDist(kMoteRadiusMin, kMoteRadiusMax);
    std::uniform_real_distribution<float> scaleDist(kMoteScaleMin, kMoteScaleMax);

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

    pm->EmitRing(ringGroup, pos, kChargeRingSpeed, kFinisherGlowColor, kChargeRingCount, kChargeRingLifetime, kChargeRingSize);
}

void EmitFinisherSlashLine(ParticleManager* pm,
    const std::string& slashGroup, const std::string& sparkGroup,
    const Vector3& center, float angle, float halfLength)
{
    auto& rng = FinisherRng();
    std::uniform_real_distribution<float> tDist(-halfLength, halfLength);
    constexpr float kGlintDrift = 0.8f;
    constexpr float kGlintRise = 0.5f; // 煌めきをわずかに上へ流す速度
    constexpr float kGlintScaleMin = 0.08f;
    constexpr float kGlintScaleMax = 0.16f;
    constexpr float kGlintLifetime = 0.25f;
    constexpr Vector4 kSlashLineColor = { 0.60f, 0.85f, 1.0f, 0.9f };
    std::uniform_real_distribution<float> driftDist(-kGlintDrift, kGlintDrift);
    std::uniform_real_distribution<float> scaleDist(kGlintScaleMin, kGlintScaleMax);

    if (!slashGroup.empty()) {
        pm->EmitSlash(slashGroup, center, angle, kSlashLineColor, halfLength);
    }

    // 斬線に沿って散る煌めき
    const Vector2 dir = { std::cos(angle), std::sin(angle) };
    constexpr int kGlintCount = 4;
    for (int i = 0; i < kGlintCount; ++i) {
        const float t = tDist(rng);
        Vector3 spawn = { center.x + dir.x * t, center.y + dir.y * t, 0.0f };
        Vector3 vel = { driftDist(rng), driftDist(rng) + kGlintRise, 0.0f };
        pm->EmitWithColor(sparkGroup, spawn, vel, kFinisherSparkColor, kGlintLifetime, scaleDist(rng), true);
    }

    // 交点の閃光（細い針状の光条）
    pm->EmitHitStar(sparkGroup, center, kFinisherSparkColor);
}

void EmitFinisherRelease(ParticleManager* pm,
    const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos)
{
    constexpr float kOuterRingSpeed = 9.0f;
    constexpr Vector4 kOuterRingColor = { 0.70f, 0.90f, 1.0f, 1.0f };
    constexpr int kOuterRingCount = 28;
    constexpr float kOuterRingLifetime = 0.55f;
    constexpr float kOuterRingSize = 0.40f;
    constexpr float kInnerRingSpeed = 4.5f;
    constexpr int kInnerRingCount = 18;
    constexpr float kInnerRingLifetime = 0.45f;
    constexpr float kInnerRingSize = 0.26f;
    constexpr float kSparkSpreadX = 7.0f;
    constexpr float kSparkRiseMin = 4.0f;
    constexpr float kSparkRiseMax = 11.0f;
    constexpr int kSparkCount = 24;
    constexpr float kSparkLifetime = 1.1f;
    constexpr float kSparkSize = 0.22f;
    constexpr float kEmberSpread = 1.5f;
    constexpr float kEmberRiseMin = 1.2f;
    constexpr float kEmberRiseMax = 2.8f;
    constexpr float kEmberScaleMin = 0.10f;
    constexpr float kEmberScaleMax = 0.22f;
    constexpr int kEmberCount = 12;
    constexpr float kEmberLifetime = 0.9f;
    constexpr Vector4 kCenterStarColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    auto& rng = FinisherRng();

    // 速度差のある二重リングで衝撃波の広がりを作る
    pm->EmitRing(ringGroup, pos, kOuterRingSpeed, kOuterRingColor, kOuterRingCount, kOuterRingLifetime, kOuterRingSize);
    pm->EmitRing(ringGroup, pos, kInnerRingSpeed, kFinisherGlowColor, kInnerRingCount, kInnerRingLifetime, kInnerRingSize);

    // 放射状に飛び散る火花
    std::uniform_real_distribution<float> vxDist(-kSparkSpreadX, kSparkSpreadX);
    std::uniform_real_distribution<float> vyDist(kSparkRiseMin, kSparkRiseMax);
    for (int i = 0; i < kSparkCount; ++i) {
        pm->EmitGravity(sparkGroup, pos,
            { vxDist(rng), vyDist(rng), 0.0f },
            kFinisherSparkColor, kSparkLifetime, kSparkSize);
    }

    // ゆっくり立ち昇る余韻の光粒
    std::uniform_real_distribution<float> offXDist(-kEmberSpread, kEmberSpread);
    std::uniform_real_distribution<float> riseDist(kEmberRiseMin, kEmberRiseMax);
    std::uniform_real_distribution<float> scaleDist(kEmberScaleMin, kEmberScaleMax);
    for (int i = 0; i < kEmberCount; ++i) {
        Vector3 spawn = { pos.x + offXDist(rng), pos.y + offXDist(rng) * 0.5f, 0.0f };
        pm->EmitWithColor(sparkGroup, spawn, { 0.0f, riseDist(rng), 0.0f },
            kFinisherGlowColor, kEmberLifetime, scaleDist(rng), true);
    }

    // 中心の大きな光条
    pm->EmitHitStar(sparkGroup, pos, kCenterStarColor);
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
