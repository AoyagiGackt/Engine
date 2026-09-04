/**
 * @file GamePlaySceneInitializer.cpp
 * @brief メインステージのゲーム実体と進行ギミックを構築する
 */
#include "GamePlaySceneInitializer.h"

#include "AudioBridge.h"
#include "EnemyTuning.h"
#include "GamePlayScene.h"
#include "GameRules.h"
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
    const LevelData levelData = LevelLoader::Load(scene.GetEditorLevelPath());

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

    // 主敵・武器持ち雑魚敵・収集物・壊せる物はすべてStageEditorに配置されたレベルJSONの実体を使う
    // （OnEditorLevelLoaded()参照）。Initialize()の時点ではまだレベルJSONが未読み込みのためここでは生成しない。
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
    enemyBullets_.clear();
    bossSlamWarningActive_ = false;
    bossSlamTimer_ = EnemyTuning::GetInstance()->BossSlam().interval;

    const GameRulesData& rules = GameRules::GetInstance()->Get();
    const std::string levelPath = GetEditorLevelPath();
    auto* runData = RunData::GetInstance();
    for (const CombatEnemyRef& ref : GetStageEditor().GetCombatEnemies()) {
        if (ref.isStageBoss) {
            if (enemy_) {
                Logger::LogWarning(levelPath + ": isStageBoss=trueの配置物が複数あります（" + ref.name + "は無視）");
                continue;
            }
            enemy_ = ref.enemy;
            int maxHp = rules.bossHpCombat;
            if (runData->GetCurrentNode() == RunData::NodeType::Elite) {
                maxHp = rules.bossHpElite;
            } else if (runData->GetCurrentNode() == RunData::NodeType::Boss) {
                maxHp = rules.bossHpBoss;
            }
            if (runData->IsRunActive()) {
                enemy_->SetMaxHp(maxHp);
            }
            enemy_->SetColor(rules.bossColor);
            continue;
        }

        WeaponEnemyEntry entry;
        entry.enemy = ref.enemy;
        entry.weaponType = ref.weaponType;
        entry.enemy->SetMaxHp(rules.weaponEnemyHp);
        // flying/healerはSetArchetype()で付けた種別色（水色/緑）を優先し、武器色で上書きしない
        if (!entry.enemy->HasArchetypeColor()) {
            entry.enemy->SetColor(colorForWeapon(ref.weaponType));
        }
        entry.hpBarBg = std::make_unique<Sprite>();
        entry.hpBarBg->Initialize(spriteCommon_.get(), "Resources/white.png");
        entry.hpBarBg->SetColor({ 0.2f, 0.2f, 0.2f, 0.8f });
        entry.hpBarFg = std::make_unique<Sprite>();
        entry.hpBarFg->Initialize(spriteCommon_.get(), "Resources/white.png");
        weaponEnemies_.push_back(std::move(entry));
    }

    if (!enemy_) {
        Logger::LogError(levelPath + "にisStageBoss=trueの敵(enemy_basic)が配置されていません");
    }
}

} // namespace engine::game
