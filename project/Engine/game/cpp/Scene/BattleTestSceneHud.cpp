/**
 * @file BattleTestSceneHud.cpp
 * @brief BattleTestSceneの武器スロットHUD（アイコン切替演出・描画）を実装するファイル
 * @note BattleTestScene.cppからの分割ファイルクラス自体はBattleTestSceneのまま、定義の置き場所だけを分けている
 */
#include "BattleTestScene.h"
#include "AudioBridge.h"
#include "BattleTestSceneRenderer.h"
#include "Collision.h"
#include "DiagnosticsDraw.h"
#include "GameConstants.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "JsonHelper.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {

/** @brief 武器スロットUIの3Dアイコン1個分の素材（モデル・テクスチャ・表示調整値） */
struct IconAsset {
    WeaponType type;
    std::string modelPath;
    std::string texturePath;
    float scale; ///< モデル実寸の高さ差を吸収し、見た目のアイコンサイズ(目標高さ約0.8)を揃えるための倍率
    float baseYaw; ///< モデルの正面がカメラを向くよう回す基準角度（ラジアン）。目視で調整した値
};

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

/**
 * @brief 武器スロットUIの3Dアイコン素材一覧をJSONから読み込む
 * @note ダミーの物理武器がまだ無いスタイルはResources/weapon_icons.jsonに1行追記すれば自動でモデル表示に切り替わる
 * @param jsonPath アイコン素材定義のJSONパス
 * @return 読み込んだ素材一覧。ファイルが無い・読み込めない場合は既存互換の既定値を返す
 */
std::vector<IconAsset> LoadWeaponIconAssets(const std::string& jsonPath)
{
    std::vector<IconAsset> assets;
    nlohmann::json j = engine::JsonHelper::Load(jsonPath);
    if (j.is_object() && j.contains("icons") && j["icons"].is_array()) {
        for (const auto& entry : j["icons"]) {
            IconAsset asset;
            asset.type = ParseIconWeaponType(entry.value("type", std::string("Sword")));
            asset.modelPath = entry.value("model", std::string());
            asset.texturePath = entry.value("texture", std::string());
            asset.scale = entry.value("scale", 0.2f);
            asset.baseYaw = entry.value("baseYawDeg", 0.0f) * GameConstants::kDegToRad;
            assets.push_back(std::move(asset));
        }
    }
    if (assets.empty()) {
        // Resources/weapon_icons.json が無い場合の後方互換の既定値（目視調整済み）
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

} // namespace

void BattleTestScene::InitializeWeaponSlotHud()
{
    constexpr float kSlotSize = 56.0f;
    constexpr float kSlotGap = 10.0f;
    constexpr float kMarginX = 24.0f;
    constexpr float kMarginY = 90.0f; // 画面下端からの距離
    const float baseY = static_cast<float>(WinApp::kClientHeight) - kMarginY;

    SceneShared::InitializeWeaponSlotHud(spriteCommon_.get(), weaponManager_,
        weaponSlots_.data(), weaponSlotPos_.data(), kWeaponSlotCount,
        kSlotSize, kSlotGap, kMarginX, baseY, /*checkUnlockedForInitialColor=*/true,
        gunFrame_, gunIcon_, gunPos_);
    gunFrame_->SetColor({ 0.08f, 0.08f, 0.1f, 0.85f }); // Update()で色を更新しないため初期化時に決め打ちする

    // 各スタイルに対応する実物3Dモデル（色塗り四角の代わりに表示）
    const std::vector<IconAsset> kIconAssets = LoadWeaponIconAssets("Resources/weapon_icons.json");

    const auto& list = weaponManager_->GetList();
    for (int i = 0; i < kWeaponSlotCount && i < static_cast<int>(list.size()); ++i) {
        for (const auto& asset : kIconAssets) {
            if (list[i].type != asset.type) {
                continue;
            }
            auto& icon3d = weaponIcons3D_[i];
            icon3d.slotIndex = i;
            icon3d.scale = asset.scale;
            icon3d.baseYaw = asset.baseYaw;
            icon3d.model = std::make_unique<Model>();
            icon3d.model->Initialize(modelCommon_.get(), asset.modelPath, asset.texturePath);
            icon3d.object = std::make_unique<Object3d>();
            icon3d.object->Initialize(modelCommon_.get());
            icon3d.object->SetModel(icon3d.model.get());
            icon3d.object->SetEnableLighting(true);
            break;
        }
    }
}

void BattleTestScene::UpdateWeaponSlotHud()
{
    slotPulseTimer_ += GameConstants::kFrameDeltaTime;
    gunIconAngle_ += GameConstants::kFrameDeltaTime * 0.6f; // 常時装備の印として、ゆっくり回り続ける
    if (slotFlashTimer_ > 0.0f) {
        slotFlashTimer_ = (std::max)(0.0f, slotFlashTimer_ - GameConstants::kFrameDeltaTime);
    }

    const int activeIndex = weaponManager_->GetSelectedSlot();
    const float pulse = 0.7f + 0.3f * std::sin(slotPulseTimer_ * 6.0f);
    const float flash = slotFlashTimer_ / kSlotFlashDuration;

    SceneShared::UpdateWeaponSlotHud(weaponManager_, weaponSlots_.data(), kWeaponSlotCount,
        slotPulseTimer_, flash, gunIcon_.get(), gunIconAngle_);
    gunFrame_->Update(); // 色はInitialize時のまま変えないので再設定不要

    // 3Dモデルで表示中のスロットは、下地の色四角を隠して実物モデルだけ見せる
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        const int weaponIndex = weaponManager_->GetSlotWeaponIndex(i);
        const bool unlocked = weaponIndex >= 0 && weaponIndex < static_cast<int>(weaponManager_->GetList().size())
            && weaponManager_->IsUnlocked(weaponIndex);
        const bool show3DIcon = unlocked && weaponIndex == i
            && (weaponIcons3D_[i].slotIndex == i) && weaponIcons3D_[i].object;
        if (show3DIcon) {
            Vector4 c = weaponSlots_[i].icon->GetColor();
            weaponSlots_[i].icon->SetColor({ c.x, c.y, c.z, 0.0f });
            weaponSlots_[i].icon->Update();
        }
    }

    // ── 各スロットの3Dアイコン（画面左下に固定表示、ゆっくり回転） ────────
    // カメラは回転しないので、スロットの画面位置をワールド座標へ逆算して張り付ける
    // 元の逆算(Z=0基準)だと地面の境界ブロック(Y=-0.6付近)に埋もれて隠れてしまうため、
    // カメラのすぐ手前(奥行き6)に置き直す。奥行きが変わった分、オフセットとスケールを
    // WorldToScreen の基準距離(24)に対する比率で縮小して同じ画面位置・見た目サイズを保つ
    constexpr float kSlotSize = 56.0f;
    constexpr float kIconDepth = 6.0f; // カメラからの距離
    constexpr float kIconDepthScale = kIconDepth / 24.0f; // WorldToScreen基準距離(24)との比
    for (int i = 0; i < kWeaponSlotCount; ++i) {
        auto& icon3d = weaponIcons3D_[i];
        if (icon3d.slotIndex != i || !icon3d.object) {
            continue;
        }
        if (weaponManager_->GetSlotWeaponIndex(i) != i || !weaponManager_->IsUnlocked(i)) {
            continue;
        }

        float sx = weaponSlotPos_[i].x + kSlotSize * 0.5f;
        float sy = weaponSlotPos_[i].y + kSlotSize * 0.5f;
        const Vector3& cam = camera_->GetTranslate();
        Vector3 iconPos = {
            cam.x + (sx - GameConstants::kScreenCenterX) / GameConstants::kScreenCenterX * GameConstants::kCameraHalfW * kIconDepthScale,
            cam.y - (sy - GameConstants::kScreenCenterY) / GameConstants::kScreenCenterY * GameConstants::kCameraHalfH * kIconDepthScale,
            cam.z + kIconDepth
        };
        float iconScale = icon3d.scale * kIconDepthScale;
        // フルスピンだと必ず背面がカメラを向く瞬間が来て武器が判別できなくなるため、
        // 正面(baseYaw)を中心に小さく揺らすだけにする
        icon3d.wobbleTime += GameConstants::kFrameDeltaTime;
        float yaw = icon3d.baseYaw + std::sin(icon3d.wobbleTime * 0.8f) * 0.35f;
        icon3d.object->SetPosition(iconPos);
        icon3d.object->SetRotation({ 0.3f, yaw, 0.0f });
        icon3d.object->SetScale({ iconScale, iconScale, iconScale });
        bool active = (i == activeIndex);
        float b = (active ? (0.9f + pulse * 0.1f) : 0.6f) + flash * 0.4f;
        icon3d.object->SetColor({ b, b, b, 1.0f });
        icon3d.object->Update();
    }
}

void BattleTestScene::DrawWeaponSlotHud()
{
    constexpr float kSlotSize = 56.0f;

    // 背景の枠を先に描く（3Dモデルがこの手前に来るようにする）
    SceneShared::DrawWeaponSlotFrames(weaponSlots_.data(), kWeaponSlotCount, gunFrame_.get());

    // 枠の中身 実物3Dモデルのスロットは、いったん3D描画パイプラインに切り替えて
    // 枠より手前に描画する（前は3Dワールドと同じパスで描いていたため、後から描かれる
    // 2Dの枠に覆いかぶさられて背面に隠れてしまっていた）
    {
        ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
        modelCommon_->CommonDrawSettings();
        Object3d::RebindCommonLighting(cmd);
        for (int i = 0; i < kWeaponSlotCount; ++i) {
            auto& icon3d = weaponIcons3D_[i];
            if (weaponManager_->GetSlotWeaponIndex(i) == i
                && icon3d.slotIndex == i && icon3d.object && weaponManager_->IsUnlocked(i)) {
                icon3d.object->Draw();
            }
        }
        spriteCommon_->CommonDrawSettings(); // 以降のスプライト描画のため2Dへ戻す
    }

    // 色四角のアイコン（3Dモデル未対応のスタイル用。3Dモデル表示中のスロットはアルファ0で透明）
    // スタイル名の文字ラベルは廃止（枠の中身＝実物の武器モデル/色で見分ける）。
    // 未解放のスロットだけ?を出し、中身が武器モデルで隠れないよう控えめな位置にする
    SceneShared::DrawWeaponSlotIconsAndLabels(weaponSlots_.data(), kWeaponSlotCount, weaponSlotPos_.data(),
        gunIcon_.get(), gunPos_, weaponManager_, fontRenderer_, kSlotSize);
}
