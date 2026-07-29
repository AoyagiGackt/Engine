/**
 * @file GamePlaySceneInitializer.cpp
 * @brief メインステージのゲーム実体と進行ギミックを構築する
 */
#include "GamePlaySceneInitializer.h"

#include "AudioBridge.h"
#include "EnemyRegistry.h"
#include "GamePlayScene.h"
#include "LevelLoader.h"
#include "PlayerBridge.h"
#include "RunData.h"

using namespace engine;
using namespace engine::graphics;

namespace engine::game {

void GamePlaySceneInitializer::InitializeStageActors(GamePlayScene& scene)
{
    const LevelData levelData = LevelLoader::Load("Resources/Levels/level01.json");

    // プレイヤーを生成し、ラン中に取得した強化を開始状態へ反映する
    scene.player_ = std::make_unique<Player>();
    scene.player_->Initialize(scene.modelCommon_.get());
    scene.player_->SetPosition(levelData.playerSpawn);
    PlayerBridge::GetInstance()->SetPlayer(scene.player_.get());
    AudioBridge::GetInstance()->SetAudio(scene.audio_);
    auto* runData = RunData::GetInstance();
    if (runData->IsRunActive()) {
        Player::SkillMods mods;
        mods.blinkDistMult = runData->HasSkill(RunData::Skill::BlinkPlus) ? 1.5f : 1.0f;
        mods.comboMaxBonus = runData->HasSkill(RunData::Skill::ComboExtend) ? 1 : 0;
        mods.fireIntervalMult = runData->HasSkill(RunData::Skill::FastFire) ? 0.5f : 1.0f;
        mods.gaugeChargeMult = runData->HasSkill(RunData::Skill::AwakenBoost) ? 1.5f : 1.0f;
        mods.speedMult = runData->HasSkill(RunData::Skill::SpeedUp) ? 1.2f : 1.0f;
        mods.jumpMult = runData->HasSkill(RunData::Skill::HighJump) ? 1.25f : 1.0f;
        mods.juggleMaxBonus = runData->HasSkill(RunData::Skill::JuggleExtend) ? 4 : 0;
        scene.player_->ApplySkillMods(mods);
    }

    // 主敵を生成し、選択中のランノードに応じて耐久値を調整する
    scene.enemy_ = std::make_unique<EnemyEntity>();
    scene.enemy_->Initialize(scene.modelCommon_.get(), levelData.enemySpawn, WeaponType::Hammer);
    scene.enemy_->SetId("enemy");
    EnemyRegistry::GetInstance()->Register(scene.enemy_->GetId(), scene.enemy_.get());
    if (runData->IsRunActive()) {
        int maxHp = 20;
        if (runData->GetCurrentNode() == RunData::NodeType::Elite) {
            maxHp = 35;
        } else if (runData->GetCurrentNode() == RunData::NodeType::Boss) {
            maxHp = 60;
        }
        scene.enemy_->SetMaxHp(maxHp);
    }
    scene.enemy_->SetColor({ 0.9f, 0.65f, 0.15f, 1.0f });

    // 武器種ごとの敵を配置し、撃破順に取得できる進行対象として登録する
    struct WeaponEnemySpawn {
        Vector3 position;
        WeaponType type;
        Vector4 color;
    };
    constexpr WeaponEnemySpawn kWeaponEnemySpawns[] = {
        { { 12.0f, 0.4f, 0.0f }, WeaponType::Sword, { 1.0f, 0.3f, 0.15f, 1.0f } },
        { { 21.0f, 0.4f, 0.0f }, WeaponType::Spear, { 0.25f, 0.75f, 1.0f, 1.0f } },
        { { 30.0f, 0.4f, 0.0f }, WeaponType::Dagger, { 0.15f, 0.85f, 1.0f, 1.0f } },
    };
    for (const auto& spawn : kWeaponEnemySpawns) {
        GamePlayScene::WeaponEnemyEntry entry;
        entry.enemy = std::make_unique<EnemyEntity>();
        entry.enemy->Initialize(scene.modelCommon_.get(), spawn.position, spawn.type);
        entry.enemy->SetMaxHp(5);
        entry.enemy->SetColor(spawn.color);
        entry.weaponType = spawn.type;
        scene.weaponEnemies_.push_back(std::move(entry));
    }

    // 武器固有技で解除する二つの進行障壁を共通モデルから生成する
    scene.gimmickBlockModel_ = std::make_unique<Model>();
    scene.gimmickBlockModel_->Initialize(scene.modelCommon_.get(),
        "Resources/block/block.obj", "Resources/white.png");
    const auto createGate = [&scene](const Vector3& position, const Vector4& color) {
        auto gate = std::make_unique<Object3d>();
        gate->Initialize(scene.modelCommon_.get());
        gate->SetModel(scene.gimmickBlockModel_.get());
        gate->SetPosition(position);
        gate->SetScale({ 1.0f, 10.0f, 1.0f });
        gate->SetColor(color);
        gate->SetEnableLighting(false);
        gate->Update();
        return gate;
    };
    scene.swordGate_ = createGate({ 17.5f, 4.5f, 0.0f }, { 0.90f, 0.08f, 0.03f, 1.0f });
    scene.spearGate_ = createGate({ 25.5f, 4.5f, 0.0f }, { 0.04f, 0.35f, 0.95f, 1.0f });

    // 寄り道の収集物をデータ列から生成し、表示位置と回収状態をまとめて所有する
    scene.energyCoreModel_ = std::make_unique<Model>();
    scene.energyCoreModel_->Initialize(scene.modelCommon_.get(),
        "Resources/block/block.obj", "Resources/Effects/circle2.png");
    constexpr Vector3 kEnergyCorePositions[] = {
        { 9.5f, 2.3f, 0.0f }, { 15.5f, 4.1f, 0.0f }, { 21.0f, 6.1f, 0.0f }
    };
    for (const Vector3& position : kEnergyCorePositions) {
        GamePlayScene::EnergyCoreEntry entry;
        entry.position = position;
        entry.object = std::make_unique<Object3d>();
        entry.object->Initialize(scene.modelCommon_.get());
        entry.object->SetModel(scene.energyCoreModel_.get());
        entry.object->SetPosition(position);
        entry.object->SetScale({ 0.35f, 0.35f, 0.35f });
        entry.object->SetEnableLighting(false);
        entry.object->Update();
        scene.energyCores_.push_back(std::move(entry));
    }
}

} // namespace engine::game
