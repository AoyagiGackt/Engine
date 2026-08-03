/**
 * @file GamePlaySceneInitializer.cpp
 * @brief メインステージのゲーム実体と進行ギミックを構築する
 */
#include "GamePlaySceneInitializer.h"

#include "AudioBridge.h"
#include "GamePlayScene.h"
#include "LevelLoader.h"
#include "Logger.h"
#include "PlayerBridge.h"
#include "RunData.h"
#include "StageEditor.h"

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

    // 主敵・武器持ち雑魚敵はStageEditorに配置されたenemy_basic実体を使う（OnEditorLevelLoaded()参照）。
    // Initialize()の時点ではまだレベルJSONが未読み込みのためここでは生成しない。

    // 寄り道の収集物をデータ列から生成し、表示位置と回収状態をまとめて所有する
    scene.energyCoreModel_ = std::make_unique<Model>();
    scene.energyCoreModel_->Initialize(scene.modelCommon_.get(),
        "Resources/block/block.obj", "Resources/Effects/circle2.png");
    constexpr Vector3 kEnergyCorePositions[] = {
        { 7.5f, 2.3f, 0.0f }, { 18.5f, 4.1f, 0.0f }, { 28.5f, 5.5f, 0.0f }
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

void GamePlayScene::OnEditorLevelLoaded()
{
    // 見た目の色分けは元のハードコード値を踏襲する（武器種別ごとに雑魚の見分けがつくように）
    auto colorForWeapon = [](WeaponType type) -> Vector4 {
        switch (type) {
        case WeaponType::Spear:
            return { 0.25f, 0.75f, 1.0f, 1.0f };
        case WeaponType::Dagger:
            return { 0.15f, 0.85f, 1.0f, 1.0f };
        case WeaponType::Sword:
        default:
            return { 1.0f, 0.3f, 0.15f, 1.0f };
        }
    };

    enemy_ = nullptr;
    weaponEnemies_.clear();

    auto* runData = RunData::GetInstance();
    for (const CombatEnemyRef& ref : GetStageEditor().GetCombatEnemies()) {
        if (ref.isStageBoss) {
            if (enemy_) {
                Logger::LogWarning("level01.json: isStageBoss=trueの配置物が複数あります（" + ref.name + "は無視）");
                continue;
            }
            enemy_ = ref.enemy;
            int maxHp = 20;
            if (runData->GetCurrentNode() == RunData::NodeType::Elite) {
                maxHp = 35;
            } else if (runData->GetCurrentNode() == RunData::NodeType::Boss) {
                maxHp = 60;
            }
            if (runData->IsRunActive()) {
                enemy_->SetMaxHp(maxHp);
            }
            enemy_->SetColor({ 0.9f, 0.65f, 0.15f, 1.0f });
            continue;
        }

        WeaponEnemyEntry entry;
        entry.enemy = ref.enemy;
        entry.weaponType = ref.weaponType;
        entry.enemy->SetMaxHp(5);
        entry.enemy->SetColor(colorForWeapon(ref.weaponType));
        weaponEnemies_.push_back(entry);
    }

    if (!enemy_) {
        Logger::LogError("level01.jsonにisStageBoss=trueの敵(enemy_basic)が配置されていません");
    }
}

} // namespace engine::game
