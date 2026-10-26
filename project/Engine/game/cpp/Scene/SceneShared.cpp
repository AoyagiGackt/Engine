/**
 * @file SceneShared.cpp
 * @brief BattleTestScene/TrainingScene/GamePlayScene間で共通の武器切替・カメラ追従・HUD描画処理（SceneShared名前空間）の実装
 */
#include "SceneShared.h"
#include "GameRules.h"
#include "PlayerBridge.h"
#include "AudioBridge.h"
#include "HitEffectLibrary.h"
#include "CombatTuning.h"
#include "TextBufferCapacity.h"
#include "Audio.h"
#include "BulletPool.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "GameSettings.h"
#include "Input.h"
#include "JsonHelper.h"
#include "Object3d.h"
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
#include <vector>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine::graphics;

namespace engine::game::sceneShared {

/** @brief 訓練・戦闘テスト共通のプレイヤーと弾丸の準備を同じ順序で行う */
void InitializePlayerAndBullets(std::unique_ptr<Player>& player, BulletPool& bullets,
    ModelCommon* modelCommon, Model* bulletModel, engine::Audio* audio) {
    player = std::make_unique<Player>();
    player->Initialize(modelCommon);
    // 生成済みのプレイヤーを登録してから、同じモデル基盤で弾丸を準備する。
    PlayerBridge::GetInstance()->SetPlayer(player.get());
    AudioBridge::GetInstance()->SetAudio(audio);
    bullets.Initialize(modelCommon, bulletModel);
}

namespace {
    // フィニッシャー演出共通の色（青白い剣閃）
    constexpr Vector4 kFinisherGlowColor = { 0.60f, 0.85f, 1.00f, 0.95f };
    constexpr Vector4 kFinisherSparkColor = { 0.80f, 0.95f, 1.00f, 1.00f };
    constexpr Vector4 kFinisherOverlayTint = { 0.0f, 0.0f, 0.05f, 0.0f }; // アルファはHitEffectLibrary::GetInstance()->SceneParticles().finisherOverlayAlpha

    constexpr float kDefaultIconScale = 0.2f; // weapon_icons.jsonにscaleがない時の既定値
    constexpr int kWeaponSlotCount = WeaponManager::kSlotCount;
    constexpr float kWeaponCycleCooldown = 0.15f; // 武器切り替えの連打を抑える間隔（秒）

    // HUDの行送り
    constexpr float kHudSectionGap = 2.0f; // 見出しの下に空ける余白
    constexpr float kHudHintGap = 4.0f; // 操作ヒントの前に空ける余白
    constexpr const char* kSharedHudLayoutName = "hud"; // 武器一覧・操作説明の文字の見た目（全シーン共通）

    // 逆さ状態のスピン連射
    constexpr int kUpsideDownHitStopFrames = 3;
    constexpr Vector4 kUpsideDownFlashColor = { 1.0f, 0.7f, 0.1f, 0.55f };
    constexpr float kUpsideDownFlashSeconds = 0.10f;
    constexpr float kRangeHalfDepth = 0.5f; // 攻撃判定の奥行きの半分（2Dの面に沿った薄い箱にする）

    /** @brief 向いている方向へ frontRange、背後へ backRange 伸ばした、高さ ±halfHeight の判定の箱を作る */
    engine::AABB MakeFacingBox(const Vector3& playerPos, float dirX, float frontRange, float backRange, float halfHeight) {
        const float left = (dirX >= 0.0f) ? backRange : frontRange;
        const float right = (dirX >= 0.0f) ? frontRange : backRange;
        return { { playerPos.x - left, playerPos.y - halfHeight, -kRangeHalfDepth },
            { playerPos.x + right, playerPos.y + halfHeight, kRangeHalfDepth } };
    }

    /** @brief 締めの演出の配置に使う乱数生成器を返す */
    std::mt19937& FinisherRng() {
        static std::mt19937 sRng { std::random_device { }() };
        return sRng;
    }
} // namespace

/** @brief 武器スロットUIの3Dアイコン素材一覧をJSONから読み込む */
std::vector<WeaponIconAsset> LoadWeaponIconAssets(const std::string& jsonPath) {
    std::vector<WeaponIconAsset> assets;
    nlohmann::json j = engine::JsonHelper::Load(jsonPath);
    if (j.is_object() && j.contains("icons") && j["icons"].is_array()) {
        for (const auto& entry : j["icons"]) {
            WeaponIconAsset asset;
            asset.type = ParseWeaponTypeName(entry.value("type", std::string("Sword")));
            asset.modelPath = entry.value("model", std::string());
            asset.texturePath = entry.value("texture", std::string());
            asset.scale = entry.value("scale", kDefaultIconScale);
            asset.baseYaw = entry.value("baseYawDeg", 0.0f) * gameConstants::kDegToRad;
            assets.push_back(std::move(asset));
        }
    }
    if (assets.empty()) {
        // Resources/Config/weapon_icons.json が無い場合の後方互換の既定値（目視調整済み）
        assets = {
            { WeaponType::kSword, "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png", 0.18f, 0.0f },
            { WeaponType::kDagger, "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png", 0.31f, 0.0f },
            { WeaponType::kHammer, "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png", 0.18f, gameConstants::kPi },
            { WeaponType::kSpear, "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png", 0.08f, 0.0f },
            { WeaponType::kGreatsword, "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png", 0.12f, 0.0f },
            { WeaponType::kScythe, "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png", 0.14f, 0.0f },
            { WeaponType::kAxe, "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png", 0.13f, 0.0f },
        };
    }
    return assets;
}

/** @brief 大技（フィニッシャースラッシュ）演出中の画面暗転オーバーレイスプライトを生成する */
std::unique_ptr<Sprite> CreateFinisherOverlay(SpriteCommon* spriteCommon) {
    auto overlay = std::make_unique<Sprite>();
    overlay->Initialize(spriteCommon, "Resources/white.png");
    overlay->SetPosition({ 0.0f, 0.0f });
    overlay->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });
    overlay->SetColor({ kFinisherOverlayTint.x, kFinisherOverlayTint.y, kFinisherOverlayTint.z,
        HitEffectLibrary::GetInstance()->SceneParticles().finisherOverlayAlpha });
    return overlay;
}

/** @brief 有効なポストエフェクトに応じたオフスクリーンRTV（未使用時はバックバッファ）を返す */
D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle(engine::DirectXCommon* dxCommon,
    const std::initializer_list<IPostEffectSource*>& effects) {
    return GetActiveSceneRTVHandle(dxCommon, effects);
}

/** @brief メイン描画先（GetActiveRTVHandle）とビューポート/シザーを設定する */
void SetupMainRenderTarget(engine::DirectXCommon* dxCommon,
    const std::initializer_list<IPostEffectSource*>& effects) {
    SetupSceneRenderTarget(dxCommon, GetActiveSceneRTVHandle(dxCommon, effects));
}

/** @brief JSONファイル（[{name, texture, additive}, ...]）に従ってパーティクルグループを一括登録する */
void CreateParticleGroupsFromJson(ParticleManager* pm, const std::string& jsonPath) {
    for (const auto& group : JsonHelper::Load(jsonPath)) {
        std::string name = group.value("name", "");
        if (name.empty()) {
            continue;
        }
        pm->CreateParticleGroup(name, group.value("texture", ""));
        pm->SetAdditiveBlend(name, group.value("additive", false));
    }
}

/** @brief 武器切替入力（Q/E、数字キー）を処理するweaponCycleTimer は呼び出し側が保持するクールダウン */
void UpdateWeaponCycle(Input* input, WeaponManager* weaponManager,
    float& weaponCycleTimer, bool cycleAllUnlocked) {
    // 武器切り替え（Q/E、数字キー 1〜スロット数）
    weaponCycleTimer -= gameConstants::kFrameDeltaTime;
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

/** @brief スペースキーのスピン連射弾を発射する（逆さ時は下方向中心に5方向ばらまき、通常時は向いている方向へ1発）JustSpinShot()でなければ何もしない */
void UpdateSpinShotFire(Player* player, BulletPool& bulletPool) {
    if (!player->JustSpinShot()) {
        return;
    }

    constexpr float kBulletSpeed = 0.30f;
    const Vector3& pp = player->GetPosition();
    Vector3 firePos = { pp.x, pp.y, 0.0f };

    if (player->IsUpsideDown()) {
        // 逆さ: 下方向中心に 5 方向ばらまき
        constexpr float kBaseAngle = 270.0f * gameConstants::kDegToRad; // 真下
        constexpr float kSpread = 30.0f * gameConstants::kDegToRad; // 30°間隔
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

/** @brief 向いている方向に厚く、背後は控えめな攻撃判定AABBを作る（近接・射撃共通） */
engine::AABB MakeDirectionalRange(const Vector3& playerPos, float dirX, float frontRange, float backRange) {
    // 縦方向の許容量。大きすぎると足場の上下に離れた（＝見た目では当たっていない）敵まで
    // 判定に巻き込んでしまう（高低差のあるステージで顕在化する）
    constexpr float kVerticalHalfHeight = 0.8f;
    return MakeFacingBox(playerPos, dirX, frontRange, backRange, kVerticalHalfHeight);
}

/** @brief 高さの異なる敵へ誤命中しない射撃用の横長判定を生成する */
engine::AABB MakeDirectionalShotRange(const Vector3& playerPos, float dirX, float frontRange, float backRange) {
    constexpr float kShotHalfHeight = 0.3f;
    return MakeFacingBox(playerPos, dirX, frontRange, backRange, kShotHalfHeight);
}

/** @brief ワールド座標をスクリーン座標に変換する（ギズモのピッキング等、外部から使う用） */
void WorldToScreen(float worldX, float worldY, float camX, float camY, float& outX, float& outY) {
    outX = (worldX - camX) / gameConstants::kCameraHalfW * gameConstants::kScreenCenterX + gameConstants::kScreenCenterX;
    outY = -(worldY - camY) / gameConstants::kCameraHalfH * gameConstants::kScreenCenterY + gameConstants::kScreenCenterY;
}

/** @brief プレイヤーにカメラを追従させる（ステージ境界クランプ込み） */
void UpdateCameraFollow(Camera* camera, const Vector3& playerPos, const std::vector<AABB>& stageSolids, const Vector3* lockTarget) {
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
    const float cameraMinX = stageLeft + gameConstants::kCameraHalfW;
    const float cameraMaxX = stageRight - gameConstants::kCameraHalfW;
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
    const float cameraMinY = stageBottom + gameConstants::kCameraHalfH;
    const float cameraMaxY = stageTop - gameConstants::kCameraHalfH;
    const float cameraTargetY = playerPos.y + gameConstants::kCameraFollowOffsetY;
    const float cameraY = cameraMinY <= cameraMaxY
        ? std::clamp(cameraTargetY, cameraMinY, cameraMaxY)
        : (stageBottom + stageTop) * 0.5f;
    camera->SetTranslate({ cameraX, cameraY, gameConstants::kCameraDistanceZ });
}

/** @brief 近接判定 + ENTER キーでのシーン遷移を行うポータル処理近接中なら true を返す */
bool UpdatePortalTransition(Input* input, const Vector3& playerPos,
    float portalX, float proximity, const char* sceneName, const char* outcome,
    const char* fallbackScene, Audio* audio) {
    bool isNear = std::abs(playerPos.x - portalX) < proximity;
    if (isNear && input->TriggerAction(Input::Action::kInteract)) {
        if (audio) {
            audio->PlayMenuSelect();
        }
        SceneFlow::GetInstance()->Transition(sceneName, outcome, fallbackScene);
    }
    return isNear;
}

/** @brief BGMまたはSEの音量を0〜1の範囲で増減し、Audioへ反映して設定ファイルへ保存する */
void AdjustVolume(Audio* audio, bool bgm, float delta) {
    GameSettings settings = GameSettingsManager::GetInstance()->Get();
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
    GameSettingsManager::GetInstance()->Apply(settings);
    GameSettingsManager::GetInstance()->Save();
}

/** @brief オプション欄に出す音量の表示（< 100% > の形） */
std::string VolumeLabel(float volume) {
    constexpr float kPercentScale = 100.0f;
    constexpr float kRoundingOffset = 0.5f;
    char buf[textBufferCapacity::kCodepoint];
    std::snprintf(buf, sizeof(buf), "< %3d%% >", static_cast<int>(volume * kPercentScale + kRoundingOffset));
    return buf;
}

/** @brief オートコンボのON/OFFを切り替えて保存する */
void ToggleAutoCombo(Audio* audio) {
    GameSettings settings = GameSettingsManager::GetInstance()->Get();
    settings.autoCombo = !settings.autoCombo;
    audio->PlayMenuChoice();
    GameSettingsManager::GetInstance()->Apply(settings);
    GameSettingsManager::GetInstance()->Save();
}

/** @brief オプション欄に出すオートコンボの状態表示（< ON > / < OFF >） */
const char* AutoComboLabel() {
    return GameSettingsManager::GetInstance()->Get().autoCombo ? "< ON >" : "< OFF >";
}

/** @brief 武器一覧HUD（ヘッダー・リスト・Q/E切替ヒント）を描画し、次に描画すべきY座標を返す */
float DrawWeaponListHud(FontRenderer& fontRenderer, WeaponManager* weaponManager, const wchar_t* headerText, const Vector2& anchor,
    const std::vector<float>* powerRatios) {
    constexpr float kDefaultScale = 1.15f;
    constexpr Vector4 kWeakenedColor = { 1.0f, 0.4f, 0.3f, 1.0f }; // 疲労で威力が落ちている武器
    constexpr float kPercent = 100.0f;
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

    // 文字は少しずらした影を先に描いてから本体を重ねる
    auto drawShadowedW = [&](const std::wstring& text, float x, float y, const Vector4& color) {
        fontRenderer.DrawStringW(text, x + kShadowOffset, y + kShadowOffset, scale, kShadow);
        fontRenderer.DrawStringW(text, x, y, scale, color);
    };
    auto drawShadowed = [&](const std::string& text, float x, float y, const Vector4& color) {
        fontRenderer.DrawString(text, x + kShadowOffset, y + kShadowOffset, scale, kShadow);
        fontRenderer.DrawString(text, x, y, scale, color);
    };

    // 見出しと区切り
    drawShadowedW(headerText, px, py, headerColor);
    py += lineHeight + kHudSectionGap;
    drawShadowedW(L"-- 武器選択 --", px, py, textColor);
    py += lineHeight + kHudSectionGap;

    // スロットごとに、武器名と威力（威力倍率が渡されていなければダメージと射程）を1行で出す
    // 武器名は日本語のスタイル名で出す。日本語と英数字で文字幅が違うので、
    // スロット番号・武器名・数値 の3列に分け、武器名の列幅は一番長い名前に合わせる
    constexpr int kColumnGapChars = 2; // 列と列の間に空ける英数字の文字数
    const float columnGap = FontRenderer::kCharW * scale * kColumnGapChars;
    const auto& weaponList = weaponManager->GetList();
    float nameColumnWidth = 0.0f;
    for (const WeaponData& weapon : weaponList) {
        nameColumnWidth = (std::max)(nameColumnWidth, FontRenderer::MeasureStringW(WeaponManager::GetDisplayName(weapon), scale).x);
    }
    for (int slot = 0; slot < kWeaponSlotCount; ++slot) {
        const int weaponIndex = weaponManager->GetSlotWeaponIndex(slot);
        const bool occupied = weaponIndex >= 0;
        const bool selected = occupied && slot == weaponManager->GetSelectedSlot();
        char head[textBufferCapacity::kStatus];
        std::snprintf(head, sizeof(head), "%s SLOT %d", selected ? ">" : " ", slot + 1);
        const float nameX = px + static_cast<float>(std::strlen(head)) * FontRenderer::kCharW * scale + columnGap;
        if (!occupied) {
            drawShadowed(head, px, py, emptyColor);
            drawShadowed("EMPTY", nameX, py, emptyColor);
            py += lineHeight;
            continue;
        }
        const auto& weapon = weaponList[weaponIndex];
        char stats[textBufferCapacity::kStatus];
        bool weakened = false;
        if (powerRatios != nullptr && weaponIndex < static_cast<int>(powerRatios->size())) {
            const float power = (*powerRatios)[weaponIndex];
            weakened = power < 1.0f;
            std::snprintf(stats, sizeof(stats), "PWR %3d%%", static_cast<int>(std::round(power * kPercent)));
        } else {
            std::snprintf(stats, sizeof(stats), "DMG %.0f  RNG %.1f", weapon.damage, weapon.range);
        }
        // 疲労で威力が落ちた武器は赤、選択中は黄
        const Vector4& color = weakened ? kWeakenedColor : selected ? selectedColor : textColor;
        drawShadowed(head, px, py, color);
        drawShadowedW(WeaponManager::GetDisplayName(weapon), nameX, py, color);
        drawShadowed(stats, nameX + nameColumnWidth + columnGap, py, color);
        py += lineHeight;
    }

    // 選択中の銃（近接スタイルとは独立に G キーで循環）
    py += kHudSectionGap;
    const RangedWeaponData& gun = weaponManager->GetRanged();
    std::wstring gunLine = L"銃: " + gun.nameJp;
    drawShadowedW(gunLine, px, py, selectedColor);
    py += lineHeight;

    // 切り替え操作の案内（キー/ボタン名は入力機器に合わせて置き換える）
    py += kHudHintGap;
    const Input* hintInput = Input::GetCurrent();
    const std::wstring slotHint = GameRules::GetInstance()->Get().gunSwitchEnabled ? L"{Slot}  武器切替    {GunSwitch}  銃切替" : L"{Slot}  武器切替";
    drawShadowedW(hintInput ? hintInput->ExpandPrompts(slotHint) : slotHint, px, py, hintColor);
    py += lineHeight;
    return py;
}

/** @brief 右側の操作説明パネルを描画する */
void DrawControlsHud(FontRenderer& fontRenderer, const Vector2& anchor, const wchar_t* portalActionLabel) {
    // ── 操作説明（右パネル） ─────────────────────────────────────────
    // 明るいブロックの上に乗ると薄い色の文字が背景に埋もれるため、色自体を変えるだけでなく
    // 影を1枚後ろに敷いて、背景が明るくても暗くても文字の輪郭が必ず見えるようにする
    constexpr float kDefaultScale = 1.05f;
    constexpr Vector4 kDefaultHeaderColor = { 1.0f, 0.78f, 0.15f, 1.0f }; // 見出し: アンバー
    constexpr Vector4 kDefaultTextColor = { 0.95f, 0.92f, 0.80f, 1.0f }; // 本文: 暖色寄りのクリーム
    UILayout& layout = UILayout::Get(kSharedHudLayoutName);
    float x = anchor.x;
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

    const std::wstring header = L"-- 操作説明 --";
    std::vector<std::wstring> rows;

    // {操作名} は最後に使った機器（キーボード/パッド）のボタン名へ置き換わる
    const Input* input = Input::GetCurrent();
    auto row = [&](const wchar_t* key, const wchar_t* desc) {
        const std::wstring keyText = input ? input->ExpandPrompts(key) : std::wstring(key);
        rows.push_back(keyText + L"  " + desc);
    };
    row(L"{Move}", L": 移動");
    row(L"{Jump}", L": ジャンプ");
    row(L"{Dodge}", L": 回避（無敵）");
    row(L"{Attack}", L": コンボ (x3)");
    row(L"{Shoot}", L": 銃コンボ");
    if (GameRules::GetInstance()->Get().gunSwitchEnabled) {
        row(L"{GunSwitch}", L": 銃切替");
    }
    row(L"{Skill}", L": 武器固有技");
    row(L"{Slot}", L": 武器スロット選択");
    row(L"{Interact}", portalActionLabel);
    row(L"{Awaken}", L": 覚醒発動");
    row(L"{Finisher}", L": フィニッシャー");
    row(L"{Pause}", L": ポーズ・音量設定");

    // 入力機器ごとの表記と影を含めて右端に余白を確保する。
    constexpr float kRightMargin = 16.0f;
    float maxWidth = FontRenderer::MeasureStringW(header, scale).x;
    for (const auto& text : rows) {
        maxWidth = (std::max)(maxWidth, FontRenderer::MeasureStringW(text, scale).x);
    }
    x = (std::min)(x, gameConstants::kScreenWidth - kRightMargin - maxWidth - kShadowOffset);
    drawShadowed(header, iy, headerColor);
    iy += lineHeight + kHudSectionGap;
    for (const auto& text : rows) {
        drawShadowed(text, iy, textColor);
        iy += lineHeight;
    }
}



/** @brief フィニッシャー発動時の溜めエフェクト（周囲から中心へ収束する光粒＋小リング）を放出する */
void EmitFinisherCharge(ParticleManager* pm,
    const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos) {
    auto& rng = FinisherRng();
    std::uniform_real_distribution<float> angleDist(0.0f, gameConstants::kTwoPi);
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
        const float speed = r / CombatTune().finisherChargeDelay;
        Vector3 vel = { -std::cos(ang) * speed, -std::sin(ang) * speed, 0.0f };
        pm->EmitWithColor(sparkGroup, spawn, vel, kFinisherGlowColor,
            CombatTune().finisherChargeDelay, scaleDist(rng), true);
    }

    pm->EmitRing(ringGroup, pos, kChargeRingSpeed, kFinisherGlowColor, kChargeRingCount, kChargeRingLifetime, kChargeRingSize);
}

/** @brief 締めの斬線を画面に出す */
void EmitFinisherSlashLine(ParticleManager* pm,
    const std::string& slashGroup, const std::string& sparkGroup,
    const Vector3& center, float angle, float halfLength) {
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

/** @brief 解放の瞬間の炸裂エフェクト（多重リング＋放射火花＋上昇する余韻）を放出する */
void EmitFinisherRelease(ParticleManager* pm,
    const std::string& ringGroup, const std::string& sparkGroup, const Vector3& pos) {
    // 溜めの演出の大きさ・数・寿命
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

/** @brief 画面上の2点を結ぶ斬撃の跡をワールド座標に出す */
void SpawnSlashMarkWorld(const Vector2& start, const Vector2& end, float camX, float camY,
    const Vector4& color, float thickness, float duration) {
    SlashMarkParams sm;
    WorldToScreen(start.x, start.y, camX, camY, sm.start.x, sm.start.y);
    WorldToScreen(end.x, end.y, camX, camY, sm.end.x, sm.end.y);
    sm.color = color;
    sm.thickness = thickness;
    sm.duration = duration;
    SlashMark::GetInstance()->Spawn(sm);
}

/** @brief 白い画像からHPバーの下地と前景の2枚を作る */
void CreateHpBarSprites(SpriteCommon* spriteCommon, std::unique_ptr<Sprite>& background,
    std::unique_ptr<Sprite>& foreground, const Vector4& backgroundColor) {
    // 下地は指定色で塗り、前景は白のまま作る（幅と色は UpdateWorldHpBar 側で毎フレーム決める）
    background = std::make_unique<Sprite>();
    background->Initialize(spriteCommon, "Resources/white.png");
    background->SetColor(backgroundColor);
    foreground = std::make_unique<Sprite>();
    foreground->Initialize(spriteCommon, "Resources/white.png");
}

/** @brief 戻りポータルの目印になるブロックを、指定位置から上へ等間隔に積む */
void CreateWarpPortalBlocks(ModelCommon* modelCommon, Model* blockModel, const Vector3& base, int count, float spacing,
    const Vector4& color, std::vector<std::unique_ptr<Object3d>>& outBlocks) {
    for (int i = 0; i < count; ++i) {
        // ライティングを切った単色のブロックを、下から順に間隔をあけて置く
        auto block = std::make_unique<Object3d>();
        block->Initialize(modelCommon);
        block->SetModel(blockModel);
        block->SetEnableLighting(false);
        block->SetPosition({ base.x, base.y + static_cast<float>(i) * spacing, base.z });
        block->SetColor(color);
        block->Update();
        outBlocks.push_back(std::move(block));
    }
}

/** @brief 少しずらした影を先に描いてから本体を重ね、明るい背景でも読める文字にする */
void DrawShadowedTextW(FontRenderer& fontRenderer, const std::wstring& text, float x, float y, const ShadowedTextStyle& style) {
    fontRenderer.DrawStringW(text, x + style.shadowOffset, y + style.shadowOffset, style.scale, style.shadowColor);
    fontRenderer.DrawStringW(text, x, y, style.scale, style.color);
}

/** @brief 武器一覧の下に出す操作ヒントの見た目を UI レイアウトから読む（書かれていない項目は defaults のまま） */
ShadowedTextStyle ReadWeaponHintStyle(UILayout& layout, const ShadowedTextStyle& defaults) {
    return {
        layout.Float("weapon_list.hint_scale", defaults.scale),
        layout.Color("weapon_list.hint_color", defaults.color),
        layout.Color("weapon_list.shadow_color", defaults.shadowColor),
        layout.Float("weapon_list.shadow_offset", defaults.shadowOffset),
    };
}

/** @brief HP比率を前景の幅と色へ反映し、背景と同じ画面位置に配置する */
void UpdateWorldHpBar(Sprite* background, Sprite* foreground, const Vector3& position,
    const Vector3& cameraPosition, float ratio, const HpBarStyle& style) {
    float screenX, screenY;
    WorldToScreen(position.x, position.y, cameraPosition.x, cameraPosition.y, screenX, screenY);
    const Vector2 screenPosition = { screenX - style.size.x * 0.5f, screenY - style.raise };
    foreground->SetColor({ 1.0f - ratio, ratio * style.greenScale + style.greenBase, 0.0f, style.alpha });
    if (style.backgroundColor) {
        background->SetColor(*style.backgroundColor);
    }
    // 背景は全幅、前景は現在HPの割合だけを描く
    background->SetPosition(screenPosition);
    background->SetSize(style.size);
    background->Update();
    foreground->SetPosition(screenPosition);
    foreground->SetSize({ style.size.x * ratio, style.size.y });
    foreground->Update();
}

/** @brief マウスの位置を画面の論理座標で返す（黒帯を除いた描画領域に合わせて換算する） */
Vector2 MouseScreenPosition(const engine::Input* input, const engine::DirectXCommon* dxCommon) {
    const POINT clientMouse = input->GetMouseClientPosition();
    const D3D12_VIEWPORT client = dxCommon->GetCenteredClientViewport();
    return {
        (clientMouse.x - client.TopLeftX) * gameConstants::kScreenWidth / client.Width,
        (clientMouse.y - client.TopLeftY) * gameConstants::kScreenHeight / client.Height
    };
}

/** @brief ゲーム画面のクリックを受けてよいか（デバッグUIがマウスを使っている間は false） */
bool IsMouseFreeForGame() {
#ifdef USE_IMGUI
    return !ImGui::GetIO().WantCaptureMouse;
#else
    return true;
#endif
}

} // namespace engine::game::SceneShared
