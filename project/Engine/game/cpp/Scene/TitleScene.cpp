/**
 * @file TitleScene.cpp
 * @brief タイトル画面のメニュー表示と入力待ち・シーン遷移（TitleScene）の実装
 */
#include "TitleScene.h"
#include "GameConstants.h"
#include "ModelManager.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "SkinnedObject3d.h"
#include "SrvManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <numbers>
#include <random>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
// ステージの実際の広さに近い規模にする（level01基準、幅60前後の想定）
constexpr float kDemoStageMinX = 0.0f;
constexpr float kDemoStageMaxX = 44.0f;
constexpr float kDemoFloorY = -0.6f;
constexpr float kDemoGroundY = 0.4f; // プレイヤー・的の立ち位置Y
constexpr float kDemoCameraY = 4.8f;
constexpr float kDemoStartX = 22.0f;
constexpr float kDemoArenaMinX = 12.0f;
constexpr float kDemoArenaMaxX = 32.0f;
constexpr float kDemoCameraX = 22.0f;
constexpr float kDemoEffectZ = -1.0f;

// 的（BattleTestSceneのDummyと同じスライム）を並べて、1体ずつ順番に近づいて倒しながら進む
constexpr float kDemoDummyX[] = { 14.0f, 18.0f, 26.0f, 30.0f };
constexpr float kDemoApproachStopDistance = 1.6f; // この距離まで近づいたら攻撃を始める
constexpr float kDemoAttackPulseInterval = 0.32f;
constexpr float kDemoHitHeightTolerance = 0.7f;
constexpr float kDemoBackstepDuration = 0.18f;
constexpr float kDummyModelScale = 0.55f;
constexpr float kDummyModelFootOffsetY = -0.65f;
constexpr float kDummyHitFlashDuration = 0.12f;
constexpr float kDummyDefeatSinkDepth = 0.8f; // 倒した的は地面へ沈めて退場させる

// タイトル背景は遊べない舞台なので、当たり判定のない壁や足場を置かず、地面と背景だけで構成する。
constexpr float kDemoCityX[] = { 6.0f, 18.0f, 30.0f, 42.0f };
constexpr float kDemoCityZ = 6.0f;
constexpr float kDemoCityScale = 0.42f;

// 床は舞台の外側まで敷き詰める（カメラ端で画面下に隙間を出さない）
constexpr int kDemoFloorLayers = 2;
constexpr int kDemoFloorOuterMargin = 11;

// メニューの配置
constexpr float kMenuX = 440.0f;
constexpr float kMenuY = 520.0f;
constexpr float kMenuWidth = 400.0f;
constexpr float kMenuItemHeight = 60.0f;

// 初期配置する的ごとの動きのばらつき（インデックスに比例して変える）
constexpr float kDummyAirborneHeight = 2.4f;
constexpr float kDummyBobPhaseStep = 1.73f;
constexpr float kDummyBobSpeedBase = 1.15f;
constexpr float kDummyBobSpeedStep = 0.19f;
constexpr float kDummyMoveSpeedBase = 0.75f;
constexpr float kDummyMoveSpeedStep = 0.13f;
constexpr float kDummyAttackCooldownBase = 0.35f;
constexpr float kDummyAttackCooldownStep = 0.27f;
constexpr int kDemoInitialAttackCount = 3;

// 倒された的の復帰
constexpr float kRespawnDistanceMin = 7.0f;
constexpr float kRespawnDistanceMax = 12.0f;
constexpr float kRespawnDelayMin = 0.9f;
constexpr float kRespawnDelayMax = 2.2f;
constexpr float kRespawnMoveSpeedMin = 0.65f;
constexpr float kRespawnMoveSpeedMax = 1.35f;
constexpr float kRespawnAttackDelayMin = 0.2f;
constexpr float kRespawnAttackDelayMax = 1.1f;
constexpr double kRespawnSideChance = 0.5;
constexpr float kArenaEdgeMargin = 0.5f; // 舞台端から内側へ収める余白

// 周回ごとの再配置
constexpr std::array<float, 4> kLapSpawnLanes = { 8.0f, 17.0f, 28.0f, 39.0f };
constexpr float kLapSpawnJitter = 1.25f;
constexpr float kLapStageEdgeMargin = 2.0f;
constexpr double kLapAirborneChance = 0.3;
constexpr float kLapAirborneHeight = 2.0f;
constexpr float kLapAirborneHeightJitterScale = 0.35f;
constexpr float kLapBobSpeedMin = 0.85f;
constexpr float kLapBobSpeedMax = 1.75f;
constexpr float kLapAttackDelayMin = 0.15f;
constexpr float kLapAttackDelayMax = 1.25f;
constexpr int kLapAttackCountMin = 2;
constexpr int kLapAttackCountMax = 4;
constexpr double kLapSkillFinisherChance = 0.55;

// 的の接近と攻撃
constexpr float kDummyPreferredDistanceBase = 1.05f;
constexpr int kDummyPreferredDistanceVariants = 3;
constexpr float kDummyPreferredDistanceStep = 0.38f;
constexpr float kDummyAttackAnimDuration = 0.34f;
constexpr float kDummyNextAttackMin = 0.75f;
constexpr float kDummyNextAttackMax = 1.65f;
constexpr float kDummyLungeDistance = 0.7f;
constexpr float kNoTargetDistance = 100000.0f; // 最寄りの的を探す時の初期値

// デモのプレイヤーの回避・移動の工夫
constexpr float kIncomingStrikeWindowStart = 0.10f; // 的の攻撃アニメ残り時間がこの範囲なら回避する
constexpr float kIncomingStrikeWindowEnd = 0.22f;
constexpr float kIncomingStrikeRange = 2.4f;
constexpr float kEnemyDodgeCooldown = 0.8f;
constexpr float kMoveTrickDelay = 0.45f; // 移動を始めてからジャンプ/回避を混ぜるまでの時間
constexpr float kJumpToAirborneRange = 4.5f;
constexpr float kDodgeApproachMinDistance = 3.0f;

// 攻撃演出
constexpr float kHitEffectAlpha = 0.9f;
constexpr float kHammerTrailScale = 0.85f;
constexpr float kDefaultTrailScale = 0.5f;
constexpr float kHitTrailLifetime = 0.2f;
constexpr float kSwordSlashAngle = 0.25f;
constexpr float kSwordSkillSlashRadius = 2.6f;
constexpr float kSwordSlashRadius = 1.55f;
constexpr float kSwordCrossOffsetY = 0.25f;
constexpr float kSwordCrossAngle = 0.35f;
constexpr float kSwordSkillCrossRadius = 2.2f;
constexpr float kSwordCrossRadius = 1.2f;
constexpr float kSpearTrailScale = 0.32f;
constexpr float kSpearTrailLifetime = 0.28f;
constexpr float kSpearThrustRadius = 1.0f;
constexpr float kSpearRingSpeed = 3.3f;
constexpr int kSpearRingCount = 12;
constexpr float kSpearRingLifetime = 0.18f;
constexpr float kSpearRingSize = 0.10f;
constexpr int kDaggerCutCount = 3;
constexpr float kDaggerCutSpacing = 0.28f;
constexpr float kDaggerCutRadius = 0.85f;
constexpr float kHammerSkillRingSpeed = 5.2f;
constexpr float kHammerRingSpeed = 3.8f;
constexpr int kHammerSkillRingCount = 24;
constexpr int kHammerRingCount = 16;
constexpr float kHammerRingLifetime = 0.34f;
constexpr float kHammerSkillRingSize = 0.28f;
constexpr float kHammerRingSize = 0.20f;
constexpr float kHammerStarHeight = 0.25f;
constexpr float kShotRingSpeed = 4.5f;
constexpr Vector4 kShotRingColor = { 1.0f, 0.85f, 0.35f, 0.95f };
constexpr int kShotRingCount = 10;
constexpr float kShotRingLifetime = 0.12f;
constexpr float kShotRingSize = 0.08f;

// 的の見た目
constexpr float kDummyBobAmplitude = 0.32f;
constexpr Vector4 kDummyFlashColor = { 1.6f, 1.6f, 1.6f, 1.0f };
constexpr Vector4 kDummyNormalColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr float kDummyFlashShrink = 0.9f;

// 常時出す武器の軌跡
constexpr float kHammerIdleTrailScale = 0.72f;
constexpr float kSpearIdleTrailScale = 0.50f;
constexpr float kDaggerIdleTrailScale = 0.34f;
constexpr float kDefaultIdleTrailScale = 0.46f;
constexpr float kIdleTrailScaleRatio = 0.48f; // 攻撃していない時の軌跡の縮小率
constexpr float kIdleTrailAlpha = 0.42f;
constexpr float kIdleTrailLifetime = 0.20f;

// 武器ごとのデモのコンボ段数
constexpr int kSwordComboLength = 4;
constexpr int kSpearComboLength = 3;
constexpr int kDaggerComboLength = 5;
constexpr int kHammerComboLength = 2;
constexpr int kDefaultComboLength = 3;

int DemoComboLength(WeaponType type)
{
    switch (type) {
    case WeaponType::Sword:
        return kSwordComboLength;
    case WeaponType::Spear:
        return kSpearComboLength;
    case WeaponType::Dagger:
        return kDaggerComboLength;
    case WeaponType::Hammer:
        return kHammerComboLength;
    default:
        return kDefaultComboLength;
    }
}
}

void TitleScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    fontRenderer_.Initialize(spriteCommon_.get());

    menu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    menu_.SetLayout(kMenuX, kMenuY, kMenuWidth, kMenuItemHeight);
    menu_.SetItems({
        { "NEW GAME" },
        { "CONTINUE", SaveDataManager::GetInstance()->HasContinue() },
        { "TRAINING" },
        { "OPTIONS" },
    });

    InitializeDemo();
    floatingTitle_.Initialize(dxCommon_, input_, modelCommon_.get(), shadowManager_.get());
    audio_->PlayTitleBGM();
}






void TitleScene::InitializeDemo()
{
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);
    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, SrvManager::GetInstance());
    Object3d::SetCommonObjectCommon(objectCommon_.get());
    Object3d::SetCommonShadowManager(shadowManager_.get());
    SkinnedObject3d::SetCommonObjectCommon(objectCommon_.get());
    SkinnedObject3d::SetCommonShadowManager(shadowManager_.get());

    camera_ = std::make_unique<Camera>();
    demoCameraX_ = kDemoCameraX;
    camera_->SetTranslate({ demoCameraX_, kDemoCameraY, GameConstants::kCameraDistanceZ });
    Object3d::SetCommonCamera(camera_.get());

    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    player_->SetHorizontalBounds(kDemoArenaMinX, kDemoArenaMaxX);
    player_->SetPosition({ kDemoStartX, kDemoGroundY, 0.0f });

    particleManager_ = ParticleManager::GetInstance();
    SceneShared::CreateParticleGroupsFromJson(particleManager_, "Resources/particles/gameplay.json");

    modelSkydome_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/SkyDome/SkyDome.obj",
        "Resources/SkyDome/skySphere.png");
    skydome_ = std::make_unique<Skydome>();
    skydome_->Initialize(modelCommon_.get(), modelSkydome_);

    groundModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/block/block.png");
    // カメラが端で止まっても画面下に隙間が出ないよう、舞台の外側まで連続した二段の床を敷く。
    for (int y = 0; y < kDemoFloorLayers; ++y) {
        for (int x = -kDemoFloorOuterMargin; x <= static_cast<int>(kDemoStageMaxX - kDemoStageMinX) + kDemoFloorOuterMargin; ++x) {
            auto block = std::make_unique<Object3d>();
            block->Initialize(modelCommon_.get());
            block->SetModel(groundModel_);
            block->SetPosition({ kDemoStageMinX + static_cast<float>(x), kDemoFloorY - static_cast<float>(y), 0.0f });
            block->SetEnableLighting(false);
            block->Update();
            groundBlocks_.push_back(std::move(block));
        }
    }

    cityModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/DowntownCityMegaKit[Standard]/Exports/glTF (Godot)/Building_Small_1.gltf",
        "Resources/DowntownCityMegaKit[Standard]/Textures/T_RedBrick_BaseColor.png");
    for (float x : kDemoCityX) {
        auto city = std::make_unique<Object3d>();
        city->Initialize(modelCommon_.get());
        city->SetModel(cityModel_);
        city->SetPosition({ x, kDemoFloorY, kDemoCityZ });
        city->SetScale({ kDemoCityScale, kDemoCityScale, kDemoCityScale });
        city->Update();
        cityObjects_.push_back(std::move(city));
    }

    dummyModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Slime.obj",
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SlimePalette.png");
    demoDummies_.clear();
    int dummyIndex = 0;
    for (float x : kDemoDummyX) {
        DemoDummy dummy;
        dummy.x = x;
        dummy.airborne = false;
        dummy.baseY = dummy.airborne ? kDemoGroundY + kDummyAirborneHeight : kDemoGroundY + kDummyModelFootOffsetY;
        dummy.currentY = dummy.baseY;
        dummy.bobPhase = static_cast<float>(dummyIndex) * kDummyBobPhaseStep;
        dummy.bobSpeed = kDummyBobSpeedBase + static_cast<float>(dummyIndex) * kDummyBobSpeedStep;
        dummy.moveSpeed = kDummyMoveSpeedBase + static_cast<float>(dummyIndex) * kDummyMoveSpeedStep;
        dummy.attackCooldown = kDummyAttackCooldownBase + static_cast<float>(dummyIndex) * kDummyAttackCooldownStep;
        dummy.object = std::make_unique<Object3d>();
        dummy.object->Initialize(modelCommon_.get());
        dummy.object->SetModel(dummyModel_);
        dummy.object->SetEnableLighting(false);
        dummy.object->SetScale({ kDummyModelScale, kDummyModelScale, kDummyModelScale });
        // このモデルは正面軸が画面奥向きなので、カメラ側を向くよう180度反転する。
        dummy.object->SetRotation({ 0.0f, std::numbers::pi_v<float>, 0.0f });
        dummy.object->SetPosition({ x, dummy.currentY, 0.0f });
        dummy.object->Update();
        demoDummies_.push_back(std::move(dummy));
        ++dummyIndex;
    }
    demoTargetIndex_ = 0;
    demoAttackTimer_ = 0.0f;
    demoTravelTimer_ = 0.0f;
    demoMoveTrickUsed_ = false;
    demoBackstepTimer_ = 0.0f;
    demoAttackCount_ = kDemoInitialAttackCount;
    demoUseSkillFinisher_ = false;

    // デモでは全武器を披露するため、実際の解放状況は退避しておいて終了時に必ず戻す
    // （Reset()せずUnlockAll()すると装備選択には影響しない＝CONTINUE直前でも安全）
    weaponManagerSnapshot_ = WeaponManager::GetInstance()->SaveSnapshot();
    WeaponManager::GetInstance()->UnlockAll();
    demoWeaponIndex_ = 0;
    if (!WeaponManager::GetInstance()->GetList().empty()) {
        WeaponManager::GetInstance()->EquipForTraining(WeaponManager::GetInstance()->GetList()[0].type);
        demoAttackCount_ = DemoComboLength(WeaponManager::GetInstance()->GetCurrent().type);
    }
}

void TitleScene::Update()
{
    fontRenderer_.Reset();

    menu_.Update(input_);
    if (menu_.ConsumeConfirm(input_)) {
        switch (menu_.GetSelectedIndex()) {
        case 0: // NEW GAME
            RunData::GetInstance()->StartNewRun();
            WeaponManager::GetInstance()->Reset();
            SaveDataManager::GetInstance()->ClearContinue();
            SceneFlow::GetInstance()->Transition("TITLE", "new_game", "MAP");
            break;
        case 1: // CONTINUE
            WeaponManager::GetInstance()->RestoreSnapshot(weaponManagerSnapshot_);
            SaveDataManager::GetInstance()->LoadContinue(*RunData::GetInstance());
            SceneFlow::GetInstance()->Transition("TITLE", "continue", "MAP");
            break;
        case 2: // TRAINING
            WeaponManager::GetInstance()->RestoreSnapshot(weaponManagerSnapshot_);
            SceneFlow::GetInstance()->Transition("TITLE", "training", "TRAINING");
            break;
        case 3: // OPTIONS
            SceneFlow::GetInstance()->Transition("TITLE", "options", "OPTIONS");
            break;
        default:
            break;
        }
    }

    UpdateDemo();

    floatingTitle_.Update(!GetStageEditor().IsVisible());
}

void TitleScene::UpdateDemo()
{
    // 倒された敵は戦闘範囲の外から個別に復帰する。ほかの状態は巻き戻さず、
    // タイトル表示中はひと続きの戦闘としてデモを動かし続ける。
    std::uniform_real_distribution<float> respawnDistanceDist(kRespawnDistanceMin, kRespawnDistanceMax);
    std::uniform_real_distribution<float> respawnDelayDist(kRespawnDelayMin, kRespawnDelayMax);
    std::uniform_real_distribution<float> moveSpeedDist(kRespawnMoveSpeedMin, kRespawnMoveSpeedMax);
    std::uniform_real_distribution<float> attackDelayDist(kRespawnAttackDelayMin, kRespawnAttackDelayMax);
    std::uniform_real_distribution<float> phaseDist(0.0f, GameConstants::kTwoPi);
    std::bernoulli_distribution sideDist(kRespawnSideChance);
    for (auto& dummy : demoDummies_) {
        if (!dummy.defeated) {
            continue;
        }
        dummy.respawnTimer -= GameConstants::kFrameDeltaTime;
        if (dummy.respawnTimer > 0.0f) {
            continue;
        }
        const float side = sideDist(demoRng_) ? 1.0f : -1.0f;
        dummy.x = std::clamp(player_->GetPosition().x + side * respawnDistanceDist(demoRng_),
            kDemoArenaMinX + kArenaEdgeMargin, kDemoArenaMaxX - kArenaEdgeMargin);
        dummy.airborne = false;
        dummy.baseY = kDemoGroundY + kDummyModelFootOffsetY;
        dummy.currentY = dummy.baseY;
        dummy.bobPhase = phaseDist(demoRng_);
        dummy.moveSpeed = moveSpeedDist(demoRng_);
        dummy.attackCooldown = attackDelayDist(demoRng_);
        dummy.attackAnimTimer = 0.0f;
        dummy.lungeOffset = 0.0f;
        dummy.hitFlashTimer = 0.0f;
        dummy.defeated = false;
        dummy.object->SetColor(kDummyNormalColor);
        dummy.object->SetScale({ kDummyModelScale, kDummyModelScale, kDummyModelScale });
        dummy.object->SetPosition({ dummy.x, dummy.currentY, 0.0f });
        dummy.object->Update();
    }
    const bool allDefeated = false;
    if (allDefeated) {
        ++demoCycleIndex_;
        auto* wm = WeaponManager::GetInstance();
        const int count = wm->GetCount();
        if (count > 0) {
            std::uniform_int_distribution<int> weaponDist(0, count - 1);
            int nextWeapon = weaponDist(demoRng_);
            if (count > 1 && nextWeapon == demoWeaponIndex_) {
                nextWeapon = (nextWeapon + 1) % count;
            }
            demoWeaponIndex_ = nextWeapon;
            wm->EquipForTraining(wm->GetList()[demoWeaponIndex_].type);
        }
        // 同じ順番を繰り返さないよう、4つのレーンを毎周シャッフルして少しだけ位置を揺らす。
        std::array<float, 4> spawnLanes = kLapSpawnLanes;
        std::shuffle(spawnLanes.begin(), spawnLanes.end(), demoRng_);
        std::uniform_real_distribution<float> jitter(-kLapSpawnJitter, kLapSpawnJitter);
        size_t laneIndex = 0;
        for (auto& dummy : demoDummies_) {
            dummy.x = std::clamp(spawnLanes[laneIndex++] + jitter(demoRng_),
                kDemoStageMinX + kLapStageEdgeMargin, kDemoStageMaxX - kLapStageEdgeMargin);
            std::bernoulli_distribution airborneDist(kLapAirborneChance);
            dummy.airborne = airborneDist(demoRng_);
            dummy.baseY = dummy.airborne ? kDemoGroundY + kLapAirborneHeight + jitter(demoRng_) * kLapAirborneHeightJitterScale
                                         : kDemoGroundY + kDummyModelFootOffsetY;
            dummy.currentY = dummy.baseY;
            std::uniform_real_distribution<float> phaseDist(0.0f, GameConstants::kTwoPi);
            std::uniform_real_distribution<float> speedDist(kLapBobSpeedMin, kLapBobSpeedMax);
            std::uniform_real_distribution<float> moveSpeedDist(kRespawnMoveSpeedMin, kRespawnMoveSpeedMax);
            std::uniform_real_distribution<float> attackDelayDist(kLapAttackDelayMin, kLapAttackDelayMax);
            dummy.bobPhase = phaseDist(demoRng_);
            dummy.bobSpeed = speedDist(demoRng_);
            dummy.moveSpeed = moveSpeedDist(demoRng_);
            dummy.attackCooldown = attackDelayDist(demoRng_);
            dummy.attackAnimTimer = 0.0f;
            dummy.lungeOffset = 0.0f;
            dummy.defeated = false;
            dummy.hitFlashTimer = 0.0f;
            dummy.object->SetColor(kDummyNormalColor);
            dummy.object->SetScale({ kDummyModelScale, kDummyModelScale, kDummyModelScale });
            dummy.object->SetPosition({ dummy.x, dummy.currentY, 0.0f });
            dummy.object->Update();
        }
        demoTargetIndex_ = 0;
        demoAttackTimer_ = 0.0f;
        demoTravelTimer_ = 0.0f;
        demoMoveTrickUsed_ = false;
        demoBackstepTimer_ = 0.0f;
        player_->SetPosition({ kDemoStartX, kDemoGroundY, 0.0f });
        player_->RefreshVisualTransforms();
        demoCameraX_ = kDemoStageMinX + GameConstants::kCameraHalfW;
        std::uniform_int_distribution<int> attackCountDist(kLapAttackCountMin, kLapAttackCountMax);
        std::bernoulli_distribution skillDist(kLapSkillFinisherChance);
        demoAttackCount_ = attackCountDist(demoRng_);
        demoUseSkillFinisher_ = skillDist(demoRng_);
    }
    // 周回ごとに動きを変える（毎回同じ歩いて殴るの繰り返しだと単調なため）
    const bool oddLap = (demoCycleIndex_ % 2) != 0;

    const float playerX = player_->GetPosition().x;
    // 各敵は独立して行動する。それぞれの速度でプレイヤーに近づき、
    // 少しずつ異なる距離で止まって、個別の待機時間で攻撃する。
    for (size_t i = 0; i < demoDummies_.size(); ++i) {
        auto& dummy = demoDummies_[i];
        if (dummy.defeated) {
            continue;
        }
        const float delta = playerX - dummy.x;
        const float distance = std::abs(delta);
        const float preferredDistance = kDummyPreferredDistanceBase
            + static_cast<float>(i % kDummyPreferredDistanceVariants) * kDummyPreferredDistanceStep;
        dummy.attackCooldown = (std::max)(0.0f,
            dummy.attackCooldown - GameConstants::kFrameDeltaTime);
        dummy.attackAnimTimer = (std::max)(0.0f,
            dummy.attackAnimTimer - GameConstants::kFrameDeltaTime);
        if (distance > preferredDistance && dummy.attackAnimTimer <= 0.0f) {
            const float direction = delta >= 0.0f ? 1.0f : -1.0f;
            dummy.x += direction * dummy.moveSpeed * GameConstants::kFrameDeltaTime;
            dummy.x = std::clamp(dummy.x, kDemoStageMinX + kArenaEdgeMargin, kDemoStageMaxX - kArenaEdgeMargin);
        } else if (dummy.attackCooldown <= 0.0f) {
            dummy.attackAnimTimer = kDummyAttackAnimDuration;
            std::uniform_real_distribution<float> nextAttackDist(kDummyNextAttackMin, kDummyNextAttackMax);
            dummy.attackCooldown = nextAttackDist(demoRng_);
        }
        const float attackPhase = 1.0f - dummy.attackAnimTimer / kDummyAttackAnimDuration;
        const float lungeDirection = delta >= 0.0f ? 1.0f : -1.0f;
        dummy.lungeOffset = dummy.attackAnimTimer > 0.0f
            ? std::sin(std::clamp(attackPhase, 0.0f, 1.0f) * GameConstants::kPi) * kDummyLungeDistance * lungeDirection
            : 0.0f;
    }

    // デモのプレイヤーは最も近い生存中の敵に反応する。
    demoTargetIndex_ = -1;
    float closestDistance = kNoTargetDistance;
    for (int i = 0; i < static_cast<int>(demoDummies_.size()); ++i) {
        if (demoDummies_[i].defeated) {
            continue;
        }
        const float distance = std::abs(demoDummies_[i].x - playerX);
        if (distance < closestDistance) {
            closestDistance = distance;
            demoTargetIndex_ = i;
        }
    }
    DemoDummy* target = demoTargetIndex_ >= 0 ? &demoDummies_[demoTargetIndex_] : nullptr;
    if (target != nullptr) {
        player_->FaceTarget({ target->x, target->currentY, 0.0f });
    }
    const bool inRange = target != nullptr && std::abs(target->x - playerX) <= kDemoApproachStopDistance;
    const float targetDeltaX = target != nullptr ? target->x - playerX : 0.0f;
    bool walkingRight = target != nullptr && !inRange && targetDeltaX > 0.0f;
    bool walkingLeft = target != nullptr && !inRange && targetDeltaX < 0.0f;

    bool attackPulse = false;
    bool skillPulse = false;
    bool dodgePulse = false;
    bool jumpPulse = false;
    bool shootPulse = false;
    bool dodgeAwayFromTarget = false;
    demoEnemyDodgeCooldown_ = (std::max)(0.0f,
        demoEnemyDodgeCooldown_ - GameConstants::kFrameDeltaTime);
    if (target != nullptr && demoEnemyDodgeCooldown_ <= 0.0f) {
        for (const auto& enemy : demoDummies_) {
            const bool incomingStrike = !enemy.defeated
                && enemy.attackAnimTimer > kIncomingStrikeWindowStart && enemy.attackAnimTimer < kIncomingStrikeWindowEnd
                && std::abs(enemy.x - playerX) < kIncomingStrikeRange;
            if (incomingStrike) {
                dodgePulse = true;
                dodgeAwayFromTarget = true;
                demoEnemyDodgeCooldown_ = kEnemyDodgeCooldown;
                break;
            }
        }
    }
    demoBackstepTimer_ = (std::max)(demoBackstepTimer_ - GameConstants::kFrameDeltaTime, 0.0f);
    if (demoBackstepTimer_ > 0.0f) {
        walkingRight = false;
        walkingLeft = false;
        // 対象から離れる方向へ短くステップする。
        if (targetDeltaX >= 0.0f) {
            walkingLeft = true;
        } else {
            walkingRight = true;
        }
    }
    if (walkingRight || walkingLeft) {
        demoTravelTimer_ += GameConstants::kFrameDeltaTime;
        // 空中敵へ高度を合わせる時だけジャンプする。地上敵には回避接近を混ぜる。
        if (!demoMoveTrickUsed_ && demoTravelTimer_ >= kMoveTrickDelay) {
            if (target != nullptr && target->airborne && std::abs(targetDeltaX) <= kJumpToAirborneRange) {
                jumpPulse = true;
                demoMoveTrickUsed_ = true;
            } else if (target != nullptr && !target->airborne && std::abs(targetDeltaX) > kDodgeApproachMinDistance) {
                dodgePulse = true;
                demoMoveTrickUsed_ = true;
            }
        }
    }
    // 空中の敵には遠距離攻撃を使い、ジャンプの高さが一致するのを待たない。
    const bool heightAligned = target != nullptr
        && (target->airborne
            || std::abs(player_->GetPosition().y - target->currentY) <= kDemoHitHeightTolerance);
    if (target != nullptr && inRange && heightAligned && demoBackstepTimer_ <= 0.0f) {
        demoAttackTimer_ += GameConstants::kFrameDeltaTime;
        const WeaponType weaponType = WeaponManager::GetInstance()->GetCurrent().type;
        const int attackCount = DemoComboLength(weaponType);
        const int pulseIndex = static_cast<int>(demoAttackTimer_ / kDemoAttackPulseInterval);
        const bool onPulseFrame = std::fmod(demoAttackTimer_, kDemoAttackPulseInterval) < GameConstants::kFrameDeltaTime;
        if (onPulseFrame && pulseIndex < attackCount) {
            // 敵と周回に応じて通常コンボと武器固有技を織り交ぜる。
            if (target->airborne) {
                shootPulse = true;
            } else if ((weaponType == WeaponType::Dagger && pulseIndex == 0)
                || ((weaponType == WeaponType::Sword || weaponType == WeaponType::Hammer)
                    && pulseIndex == attackCount - 1)) {
                skillPulse = true;
            } else {
                attackPulse = true;
            }
            target->hitFlashTimer = kDummyHitFlashDuration;
            if (particleManager_) {
                const auto& weapon = WeaponManager::GetInstance()->GetCurrent();
                const Vector4 color = { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], kHitEffectAlpha };
                const float facing = targetDeltaX >= 0.0f ? 1.0f : -1.0f;
                Vector3 weaponPos = player_->GetActiveWeaponWorldPosition();
                weaponPos.z = kDemoEffectZ;
                // 武器ごとの軌跡で、命中前から装備中の戦闘スタイルを見せる。
                particleManager_->EmitTrail("weapon_trail", weaponPos, color,
                    weapon.type == WeaponType::Hammer ? kHammerTrailScale : kDefaultTrailScale, kHitTrailLifetime);
                particleManager_->EmitHitStar("hit_spark", { target->x, target->currentY, kDemoEffectZ }, color);
                switch (weapon.type) {
                case WeaponType::Sword:
                    // 素早い交差斬りと、幅の広い赤色の締め攻撃。
                    particleManager_->EmitSlash("sword_slash", weaponPos,
                        facing > 0.0f ? kSwordSlashAngle : GameConstants::kPi - kSwordSlashAngle, color,
                        skillPulse ? kSwordSkillSlashRadius : kSwordSlashRadius);
                    particleManager_->EmitSlash("sword_slash", { target->x, target->currentY + kSwordCrossOffsetY, kDemoEffectZ },
                        facing > 0.0f ? -kSwordCrossAngle : GameConstants::kPi + kSwordCrossAngle, color,
                        skillPulse ? kSwordSkillCrossRadius : kSwordCrossRadius);
                    break;
                case WeaponType::Spear:
                    // 細く長い突きの軌跡と、命中点に集中する衝撃リング。
                    particleManager_->EmitTrail("weapon_trail",
                        { (weaponPos.x + target->x) * 0.5f, weaponPos.y, kDemoEffectZ }, color, kSpearTrailScale, kSpearTrailLifetime);
                    particleManager_->EmitSlash("sword_slash", { target->x, target->currentY, kDemoEffectZ },
                        facing > 0.0f ? 0.0f : GameConstants::kPi, color, kSpearThrustRadius);
                    particleManager_->EmitRing("hit_ring", { target->x, target->currentY, kDemoEffectZ },
                        kSpearRingSpeed, color, kSpearRingCount, kSpearRingLifetime, kSpearRingSize);
                    break;
                case WeaponType::Dagger:
                    // 小さな水色の斬線を重ねて、攻撃の速さを表現する。
                    for (int cut = 0; cut < kDaggerCutCount; ++cut) {
                        const float offset = (static_cast<float>(cut) - 1.0f) * kDaggerCutSpacing;
                        particleManager_->EmitSlash("sword_slash",
                            { target->x + offset, target->currentY + offset, kDemoEffectZ },
                            (facing > 0.0f ? 0.0f : GameConstants::kPi) + offset, color, kDaggerCutRadius);
                    }
                    break;
                case WeaponType::Hammer:
                    // 重い攻撃は地面への衝撃を強調する。
                    particleManager_->EmitRing("hit_ring", { target->x, kDemoGroundY, kDemoEffectZ },
                        skillPulse ? kHammerSkillRingSpeed : kHammerRingSpeed, color,
                        skillPulse ? kHammerSkillRingCount : kHammerRingCount, kHammerRingLifetime,
                        skillPulse ? kHammerSkillRingSize : kHammerRingSize);
                    particleManager_->EmitHitStar("hit_spark", { target->x, kDemoGroundY + kHammerStarHeight, kDemoEffectZ }, color);
                    break;
                default:
                    break;
                }
                if (shootPulse) {
                    particleManager_->EmitRing("hit_ring", weaponPos, kShotRingSpeed,
                        kShotRingColor, kShotRingCount, kShotRingLifetime, kShotRingSize);
                }
            }
            // 連打の合間に一歩引いて再接近させ、棒立ちコンボにならないようにする。
            if (pulseIndex + 1 < attackCount && ((pulseIndex + demoTargetIndex_) % 2) != 0) {
                demoBackstepTimer_ = kDemoBackstepDuration;
            }
        }
        if (pulseIndex >= attackCount) {
            // 撃破: 地面へ沈めて退場させ、次の的へ向かう
            target->defeated = true;
            target->respawnTimer = respawnDelayDist(demoRng_);
            target->object->SetPosition({ target->x, target->currentY - kDummyDefeatSinkDepth, 0.0f });
            target->object->Update();
            demoTargetIndex_ = -1;
            demoAttackTimer_ = 0.0f;
            demoTravelTimer_ = 0.0f;
            demoMoveTrickUsed_ = false;
            dodgePulse = oddLap;
            // 敵を1体倒すたびに武器を切り替え、同じコンボを連続して見せない。
            auto* wm = WeaponManager::GetInstance();
            const int weaponCount = wm->GetCount();
            if (weaponCount > 0) {
                demoWeaponIndex_ = (demoWeaponIndex_ + 1) % weaponCount;
                wm->EquipForTraining(wm->GetList()[demoWeaponIndex_].type);
                demoAttackCount_ = DemoComboLength(wm->GetCurrent().type);
            }
            demoUseSkillFinisher_ = true;
        }
    }

    // 実機の入力に一切左右されないよう、参照されうるアクションを毎フレーム明示的に指定する
    for (int i = 0; i < static_cast<int>(Input::Action::Count); ++i) {
        input_->SetActionOverride(static_cast<Input::Action>(i), false);
    }
    input_->SetActionOverride(Input::Action::MoveRight, walkingRight);
    input_->SetActionOverride(Input::Action::MoveLeft, walkingLeft);
    input_->SetActionOverride(Input::Action::Attack, attackPulse);
    input_->SetActionOverride(Input::Action::Shoot, shootPulse);
    input_->SetActionOverride(Input::Action::Skill, skillPulse);
    input_->SetActionOverride(Input::Action::Dodge, dodgePulse);
    input_->SetActionOverride(Input::Action::Jump, jumpPulse);
    // 回避は移動入力が無いと「向いている方向と逆」へ下がる仕様なので、右へ攻め込む演出のつもりが
    // 毎回まったく逆方向（壁側）へ下がってしまっていた。前へ進む入力を足して意図した方向にする
    if (dodgePulse) {
        const bool moveRight = dodgeAwayFromTarget ? targetDeltaX < 0.0f : targetDeltaX >= 0.0f;
        input_->SetActionOverride(moveRight ? Input::Action::MoveRight : Input::Action::MoveLeft, true);
    }

    player_->Update(input_);
    // 攻撃の踏み込みは通常移動のクランプ後に加算されるため、描画前にも舞台内へ収める。
    Vector3& playerPosRef = player_->GetPositionRef();
    playerPosRef.x = std::clamp(playerPosRef.x, kDemoArenaMinX + kArenaEdgeMargin, kDemoArenaMaxX - kArenaEdgeMargin);
    player_->RefreshVisualTransforms();

    for (auto& dummy : demoDummies_) {
        dummy.hitFlashTimer = (std::max)(dummy.hitFlashTimer - GameConstants::kFrameDeltaTime, 0.0f);
        if (dummy.defeated) {
            continue;
        }
        if (dummy.airborne) {
            dummy.bobPhase += GameConstants::kFrameDeltaTime * dummy.bobSpeed;
            dummy.currentY = dummy.baseY + std::sin(dummy.bobPhase) * kDummyBobAmplitude;
        } else {
            dummy.currentY = dummy.baseY;
        }
        const bool flashing = dummy.hitFlashTimer > 0.0f;
        dummy.object->SetColor(flashing ? kDummyFlashColor : kDummyNormalColor);
        const float shrink = flashing ? kDummyFlashShrink : 1.0f;
        dummy.object->SetScale({ kDummyModelScale * shrink, kDummyModelScale * shrink, kDummyModelScale * shrink });
        const float facingYaw = player_->GetPosition().x >= dummy.x
            ? 0.0f
            : GameConstants::kPi;
        dummy.object->SetRotation({ 0.0f, facingYaw, 0.0f });
        dummy.object->SetPosition({ dummy.x + dummy.lungeOffset, dummy.currentY, 0.0f });
        dummy.object->Update();
    }

    // 通常はゲームプレイ画面が継続的な武器の軌跡を出す。タイトルのデモでは
    // プレイヤーを直接描画するため、装備中の武器の軌跡もここで生成する。
    if (particleManager_) {
        const WeaponData& weapon = WeaponManager::GetInstance()->GetCurrent();
        const Vector4 trailColor = { weapon.effectColor[0], weapon.effectColor[1],
            weapon.effectColor[2], weapon.effectColor[3] };
        const float baseScale = weapon.type == WeaponType::Hammer ? kHammerIdleTrailScale
            : weapon.type == WeaponType::Spear                    ? kSpearIdleTrailScale
            : weapon.type == WeaponType::Dagger                   ? kDaggerIdleTrailScale
                                                                   : kDefaultIdleTrailScale;
        Vector3 trailPos = player_->GetActiveWeaponWorldPosition();
        trailPos.z = kDemoEffectZ;
        const float trailScale = player_->IsMeleeAttacking() ? baseScale : baseScale * kIdleTrailScaleRatio;
        const float trailAlpha = player_->IsMeleeAttacking() ? 1.0f : kIdleTrailAlpha;
        particleManager_->EmitTrail("weapon_trail", trailPos,
            { trailColor.x, trailColor.y, trailColor.z, trailAlpha }, trailScale, kIdleTrailLifetime);
    }

    // 闘技場を固定カメラで映し、独立した敵の動きを見やすくする。
    demoCameraX_ = kDemoCameraX;
    camera_->SetTranslate({ demoCameraX_, kDemoCameraY, GameConstants::kCameraDistanceZ });

    skydome_->Update(camera_.get());

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    SkinnedObject3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
}

void TitleScene::Draw()
{
    DrawDemoShadowPass();
    DrawDemoWorld();
    GetStageEditor().DrawObjects();
    floatingTitle_.Draw();

    spriteCommon_->CommonDrawSettings();

    menu_.Draw();
    GetStageEditor().DrawUIText(fontRenderer_);
    fontRenderer_.Draw();
}

void TitleScene::DrawDemoShadowPass()
{
    // TrainingScene::Draw()と同じ最小構成（シャドウマップの状態遷移だけ行い、投影物は描かない）
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    shadowManager_->BeginShadowPass(commandList);
    modelCommon_->BeginShadowPass();
    shadowManager_->EndShadowPass(commandList);
}

void TitleScene::DrawDemoWorld()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon_->GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT viewport = dxCommon_->GetCenteredClientViewport();
    D3D12_RECT scissor = dxCommon_->GetCenteredClientScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(commandList);
    shadowManager_->SetShadowMap(commandList, SrvManager::GetInstance());
    skydome_->Draw();
    for (auto& city : cityObjects_) {
        city->Draw();
    }
    for (auto& block : groundBlocks_) {
        block->Draw();
    }
    for (auto& dummy : demoDummies_) {
        if (!dummy.defeated) {
            dummy.object->Draw();
        }
    }
    player_->Draw();
    if (particleManager_) {
        particleManager_->Update(camera_.get());
        particleManager_->Draw(camera_.get());
    }
}

void TitleScene::Finalize()
{
    // デモ用のグループを残すと、本編が同名のグループを生成する際に停止する。
    // シーン切り替え元でGPUの完了を待ってから呼ばれるので、安全に解放できる。
    if (particleManager_) {
        particleManager_->ClearAllGroups();
        particleManager_ = nullptr;
    }
    // デモ用に全解放していた武器を元に戻し、自動操作用のオーバーライドも解除する
    // （NEW GAME/TRAININGへ抜ける際は既にRestoreSnapshot済みだが、二重に戻しても値は変わらないので無害）
    WeaponManager::GetInstance()->RestoreSnapshot(weaponManagerSnapshot_);
    input_->ClearActionOverrides();
}
