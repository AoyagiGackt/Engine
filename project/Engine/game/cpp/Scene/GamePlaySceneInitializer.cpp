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
#include <algorithm>
#include <iterator>

using namespace engine;
using namespace engine::graphics;

namespace engine::game {

namespace {
// ショップで取得したスキルの効果量
constexpr float kBlinkPlusDistanceMult = 1.5f;
constexpr int kComboExtendBonus = 1;
constexpr float kFastFireIntervalMult = 0.5f;
constexpr float kAwakenBoostChargeMult = 1.5f;
constexpr float kSpeedUpMult = 1.2f;
constexpr float kHighJumpMult = 1.25f;
constexpr int kJuggleExtendBonus = 4;

// 武器種別ごとの敵の色（倒せば何が手に入るかを見た目で予測できるように）
constexpr Vector4 kSpearEnemyColor = { 0.25f, 0.75f, 1.0f, 1.0f };
constexpr Vector4 kDaggerEnemyColor = { 0.15f, 0.85f, 1.0f, 1.0f };
constexpr Vector4 kHeavyEnemyColor = { 0.85f, 0.45f, 1.0f, 1.0f };
constexpr Vector4 kSwordEnemyColor = { 1.0f, 0.3f, 0.15f, 1.0f };
constexpr Vector4 kHpBarBackgroundColor = { 0.2f, 0.2f, 0.2f, 0.8f };
}

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
        mods.blinkDistMult = runData->HasSkill(RunData::Skill::BlinkPlus) ? kBlinkPlusDistanceMult : 1.0f;
        mods.comboMaxBonus = runData->HasSkill(RunData::Skill::ComboExtend) ? kComboExtendBonus : 0;
        mods.fireIntervalMult = runData->HasSkill(RunData::Skill::FastFire) ? kFastFireIntervalMult : 1.0f;
        mods.gaugeChargeMult = runData->HasSkill(RunData::Skill::AwakenBoost) ? kAwakenBoostChargeMult : 1.0f;
        mods.speedMult = runData->HasSkill(RunData::Skill::SpeedUp) ? kSpeedUpMult : 1.0f;
        mods.jumpMult = runData->HasSkill(RunData::Skill::HighJump) ? kHighJumpMult : 1.0f;
        mods.juggleMaxBonus = runData->HasSkill(RunData::Skill::JuggleExtend) ? kJuggleExtendBonus : 0;
        scene.player_->ApplySkillMods(mods);
    }

    // 主敵・武器持ち雑魚敵・収集物・壊せる物はすべてStageEditorに配置されたレベルJSONの実体を使う
    // （OnEditorLevelLoaded()参照）。Initialize()の時点ではまだレベルJSONが未読み込みのためここでは生成しない。
}

void GamePlayScene::OnEditorLevelLoaded()
{
    enemy_ = nullptr;
    weaponEnemies_.clear();
    enemyBullets_.clear();
    bossSlamWarningActive_ = false;
    floorElapsedSeconds_ = 0.0f;
    bossSlamTimer_ = EnemyTuning::GetInstance()->BossSlam().interval;
    lockedEnemy_ = nullptr;

    const GameRulesData& rules = GameRules::GetInstance()->Get();
    const std::string levelPath = GetEditorLevelPath();
    auto* runData = RunData::GetInstance();
    for (const CombatEnemyRef& ref : GetStageEditor().GetCombatEnemies()) {
        if (!ref.isStageBoss) {
            continue;
        }
        if (enemy_) {
            Logger::LogWarning(levelPath + ": isStageBoss=trueの配置物が複数あります（" + ref.name + "は無視）");
            continue;
        }
        enemy_ = ref.enemy;
        bossWeaponType_ = ref.weaponType;
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
    }

    if (!enemy_) {
        Logger::LogError(levelPath + "にisStageBoss=trueの敵(enemy_basic)が配置されていません");
    }

    SyncCombatEnemies();
}

void GamePlayScene::SyncCombatEnemies()
{
    // 見た目の色分けは武器種別ごとに固定（倒せば何が手に入るかを見た目で予測できるように）
    // WeaponType の並び順と一致させる
    static constexpr Vector4 kEnemyColorByWeapon[] = {
        kSwordEnemyColor, // Sword
        kSpearEnemyColor, // Spear
        kHeavyEnemyColor, // Hammer
        kDaggerEnemyColor, // Dagger
        kSwordEnemyColor, // Ball
        kSwordEnemyColor, // Greatsword
        kSwordEnemyColor, // Scythe
        kHeavyEnemyColor, // Axe
    };
    auto colorForWeapon = [](WeaponType type) -> Vector4 {
        const size_t index = static_cast<size_t>(type);
        return index < std::size(kEnemyColorByWeapon) ? kEnemyColorByWeapon[index] : kSwordEnemyColor;
    };

    const std::vector<CombatEnemyRef> refs = GetStageEditor().GetCombatEnemies();

    // 無効化・破棄された敵の項目を外す（spawn_pointの敵はレベル側の都合で消えることがある）
    const size_t beforeCount = weaponEnemies_.size();
    weaponEnemies_.erase(std::remove_if(weaponEnemies_.begin(), weaponEnemies_.end(),
                             [&](const WeaponEnemyEntry& entry) {
                                 return std::none_of(refs.begin(), refs.end(),
                                     [&](const CombatEnemyRef& ref) { return !ref.isStageBoss && ref.enemy == entry.enemy; });
                             }),
        weaponEnemies_.end());
    if (weaponEnemies_.size() != beforeCount) {
        lockedEnemy_ = nullptr; // 外れた敵を指したままにならないよう、ロックは張り直させる
    }

    // 新しく現れた敵を取り込む
    const GameRulesData& rules = GameRules::GetInstance()->Get();
    for (const CombatEnemyRef& ref : refs) {
        if (ref.isStageBoss || ref.enemy == enemy_) {
            continue;
        }
        const bool known = std::any_of(weaponEnemies_.begin(), weaponEnemies_.end(),
            [&](const WeaponEnemyEntry& entry) { return entry.enemy == ref.enemy; });
        if (known) {
            continue;
        }
        WeaponEnemyEntry entry;
        entry.enemy = ref.enemy;
        entry.weaponType = ref.weaponType;
        entry.hasWeapon = ref.hasWeapon;
        entry.enemy->SetMaxHp(entry.enemy->IsFlying() ? rules.flyingWeaponEnemyHp : rules.weaponEnemyHp);
        // flying/healerはSetArchetype()で付けた種別色（水色/緑）を優先し、武器色で上書きしない
        if (!entry.enemy->HasArchetypeColor()) {
            entry.enemy->SetColor(colorForWeapon(ref.weaponType));
        }
        entry.hpBarBg = std::make_unique<Sprite>();
        entry.hpBarBg->Initialize(spriteCommon_.get(), "Resources/white.png");
        entry.hpBarBg->SetColor(kHpBarBackgroundColor);
        entry.hpBarFg = std::make_unique<Sprite>();
        entry.hpBarFg->Initialize(spriteCommon_.get(), "Resources/white.png");
        weaponEnemies_.push_back(std::move(entry));
    }
}

} // namespace engine::game
