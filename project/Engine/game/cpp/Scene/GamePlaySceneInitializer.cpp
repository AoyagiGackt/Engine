/**
 * @file GamePlaySceneInitializer.cpp
 * @brief メインステージのゲーム実体と進行ギミックを構築する
 */
#include "GamePlaySceneInitializer.h"
#include "PlayerTuning.h"
#include "SceneVisualTuning.h"

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
#include <cmath>
#include <iterator>

using namespace engine::graphics;

namespace engine::game {
namespace {
/** @brief 基準のHPに敵ごとの倍率を掛ける（倍率が小さすぎても一撃で倒れる1は残す） */
int ScaledHp(int baseHp, float scale) {
    return (std::max)(1, static_cast<int>(std::round(static_cast<float>(baseHp) * scale)));
}
} // namespace

/** @brief レベルデータとラン情報からゲーム実体を構築する */
void GamePlaySceneInitializer::InitializeStageActors(GamePlayScene& scene) {
    const LevelData levelData = levelLoader::Load(scene.GetEditorLevelPath());

    // プレイヤーを生成し、ラン中に取得した強化を開始状態へ反映する
    scene.player_ = std::make_unique<Player>();
    scene.player_->Initialize(scene.modelCommon_.get());
    scene.player_->SetPosition(levelData.playerSpawn);
    PlayerBridge::GetInstance()->SetPlayer(scene.player_.get());
    AudioBridge::GetInstance()->SetAudio(scene.audio_);
    auto* runData = RunData::GetInstance();
    if (runData->IsRunActive()) {
        Player::SkillMods mods;
        mods.blinkDistMult = runData->HasSkill(RunData::Skill::kBlinkPlus) ? PlayerTuning::Get().skillBlinkPlusDistanceMult : 1.0f;
        mods.comboMaxBonus = runData->HasSkill(RunData::Skill::kComboExtend) ? PlayerTuning::Get().skillComboExtendBonus : 0;
        mods.fireIntervalMult = runData->HasSkill(RunData::Skill::kFastFire) ? PlayerTuning::Get().skillFastFireIntervalMult : 1.0f;
        mods.gaugeChargeMult = runData->HasSkill(RunData::Skill::kAwakenBoost) ? PlayerTuning::Get().skillAwakenBoostChargeMult : 1.0f;
        mods.speedMult = runData->HasSkill(RunData::Skill::kSpeedUp) ? PlayerTuning::Get().skillSpeedUpMult : 1.0f;
        mods.jumpMult = runData->HasSkill(RunData::Skill::kHighJump) ? PlayerTuning::Get().skillHighJumpMult : 1.0f;
        mods.juggleMaxBonus = runData->HasSkill(RunData::Skill::kJuggleExtend) ? PlayerTuning::Get().skillJuggleExtendBonus : 0;
        scene.player_->ApplySkillMods(mods);
    }

    // 主敵・武器持ち雑魚敵・収集物・壊せる物はすべてStageEditorに配置されたレベルJSONの実体を使う
    // （OnEditorLevelLoaded()参照）。Initialize()の時点ではまだレベルJSONが未読み込みのためここでは生成しない。
}

/** @brief StageEditorへ配置済みの敵（武器持ち雑魚・ボス）をenemy_/weaponEnemies_へ結び付ける */
void GamePlayScene::OnEditorLevelLoaded() {
    enemy_ = nullptr;
    weaponEnemies_.clear();
    enemyBullets_.clear();
    bossSlamWarningActive_ = false;
    floorElapsedSeconds_ = 0.0f;
    bossSlamTimer_ = EnemyTuning::GetInstance()->BossSlam().interval;
    lockedEnemy_ = nullptr;

    bossInstanceId_ = 0;
    BindEditorBoss();
    if (!enemy_) {
        Logger::LogError(GetEditorLevelPath() + "にisStageBoss=trueの敵(enemy_basic)が配置されていません");
    }

    SyncCombatEnemies();

    if (!stageIntroStarted_) {
        stageIntroStarted_ = true;
        StartStageIntro();
    }
}

/** @brief 配置されたボスを取得し、新しい実体の時だけ初期のHPと色を設定する */
void GamePlayScene::BindEditorBoss() {
    // 今いるボスの実体を探す（複数あれば最初の1体を使う）
    enemy_ = nullptr;
    const std::vector<CombatEnemyRef> refs = GetStageEditor().GetCombatEnemies();
    const auto isBoss = [](const CombatEnemyRef& ref) {
        return ref.isStageBoss;
    };
    const auto boss = std::find_if(refs.begin(), refs.end(), isBoss);
    // フラグで一時的に無効なだけの時は番号を残し、戻ってきた同じ実体を初期化し直さない
    if (boss == refs.end()) {
        return;
    }
    enemy_ = boss->enemy;
    if (boss->instanceId == bossInstanceId_) {
        return;
    }
    bossInstanceId_ = boss->instanceId;

    // ボスが複数置かれていれば、2体目以降は使わないことを知らせる
    const auto extraBoss = std::find_if(std::next(boss), refs.end(), isBoss);
    if (extraBoss != refs.end()) {
        Logger::LogWarning(GetEditorLevelPath() + ": isStageBoss=trueの配置物が複数あります（" + extraBoss->name + "は無視）");
    }

    // 新しい実体にだけ、ボスの戦闘と演出を初期設定する
    weaponStealTriggered_ = false;
    mainEnemyDefeatEffectEmitted_ = false;
    mainWeaponAbsorbing_ = false;
    bossSlamWarningActive_ = false;
    bossWeaponType_ = boss->weaponType;
    // NodeType の並び順に、そのマスのボスが使うHPの項目（戦闘マス以外は通常戦闘と同じ）
    static constexpr int GameRulesData::* kBossHpByNode[] = {
        &GameRulesData::bossHpCombat, // Combat
        &GameRulesData::bossHpElite, // Elite
        &GameRulesData::bossHpCombat, // Shop
        &GameRulesData::bossHpCombat, // Rest
        &GameRulesData::bossHpBoss, // Boss
    };
    const GameRulesData& rules = GameRules::GetInstance()->Get();
    auto* runData = RunData::GetInstance();
    const size_t nodeIndex = static_cast<size_t>(runData->GetCurrentNode());
    const int maxHp = rules.*kBossHpByNode[nodeIndex < std::size(kBossHpByNode) ? nodeIndex : 0];
    if (runData->IsRunActive()) {
        enemy_->SetMaxHp(ScaledHp(maxHp, enemy_->GetHpScale()));
    }
    enemy_->SetColor(rules.bossColor);
}

/** @brief エディタが配置物の実体を作り直していたら、ボスとロック対象を含む参照を取得し直す */
void GamePlayScene::SynchronizeEditorReferences() {
    // 古い参照を参照解除せず、現存する実体の番号と照合する
    const auto refs = GetStageEditor().GetCombatEnemies();
    const bool lockIsAlive = lockedEnemy_ == nullptr
        || (lockedEnemy_ == enemy_ && std::any_of(refs.begin(), refs.end(), [&](const CombatEnemyRef& ref) {
            return ref.enemy == lockedEnemy_ && ref.instanceId == bossInstanceId_;
        }))
        || std::any_of(weaponEnemies_.begin(), weaponEnemies_.end(), [&](const WeaponEnemyEntry& entry) {
            return entry.enemy == lockedEnemy_ && std::any_of(refs.begin(), refs.end(), [&](const CombatEnemyRef& ref) {
                return ref.enemy == entry.enemy && ref.instanceId == entry.instanceId;
            });
        });
    if (!lockIsAlive) {
        lockedEnemy_ = nullptr;
    }
    BindEditorBoss();
    SyncCombatEnemies();
}

/** @brief StageEditorの戦闘対象一覧とweaponEnemies_を同期する（毎フレーム呼ぶ） */
void GamePlayScene::SyncCombatEnemies() {
    // 見た目の色分けは武器種別ごとに固定（倒せば何が手に入るかを見た目で予測できるように）
    // WeaponType の並び順と一致させる
    static constexpr Vector4 InitialActorsVisualSettings::* kEnemyColorByWeapon[] = {
        &InitialActorsVisualSettings::swordEnemyColor, // Sword
        &InitialActorsVisualSettings::spearEnemyColor, // Spear
        &InitialActorsVisualSettings::heavyEnemyColor, // Hammer
        &InitialActorsVisualSettings::daggerEnemyColor, // Dagger
        &InitialActorsVisualSettings::swordEnemyColor, // Ball
        &InitialActorsVisualSettings::swordEnemyColor, // Greatsword
        &InitialActorsVisualSettings::swordEnemyColor, // Scythe
        &InitialActorsVisualSettings::heavyEnemyColor, // Axe
    };
    auto colorForWeapon = [](WeaponType type) -> Vector4 {
        const size_t index = static_cast<size_t>(type);
        const auto& colors = SceneVisualTuning::InitialActors();
        return index < std::size(kEnemyColorByWeapon) ? colors.*kEnemyColorByWeapon[index] : colors.swordEnemyColor;
    };

    const std::vector<CombatEnemyRef> refs = GetStageEditor().GetCombatEnemies();

    // 無効化・破棄された敵の項目を外す（spawn_pointの敵はレベル側の都合で消えることがある）
    const size_t beforeCount = weaponEnemies_.size();
    weaponEnemies_.erase(std::remove_if(weaponEnemies_.begin(), weaponEnemies_.end(),
                             [&](const WeaponEnemyEntry& entry) {
                                 return std::none_of(refs.begin(), refs.end(),
                                     [&](const CombatEnemyRef& ref) {
                                         return !ref.isStageBoss && ref.enemy == entry.enemy && ref.instanceId == entry.instanceId;
                                     });
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
            [&](const WeaponEnemyEntry& entry) {
                return entry.enemy == ref.enemy && entry.instanceId == ref.instanceId;
            });
        if (known) {
            continue;
        }
        WeaponEnemyEntry entry;
        entry.enemy = ref.enemy;
        entry.instanceId = ref.instanceId;
        entry.weaponType = ref.weaponType;
        entry.hasWeapon = ref.hasWeapon;
        entry.enemy->SetMaxHp(ScaledHp(entry.enemy->IsFlying() ? rules.flyingWeaponEnemyHp : rules.weaponEnemyHp, entry.enemy->GetHpScale()));
        // flying/healerはSetArchetype()で付けた種別色（水色/緑）を優先し、武器色で上書きしない
        if (!entry.enemy->HasArchetypeColor()) {
            entry.enemy->SetColor(colorForWeapon(ref.weaponType));
        }
        sceneShared::CreateHpBarSprites(spriteCommon_.get(), entry.hpBarBg, entry.hpBarFg,
            SceneVisualTuning::InitialActors().hpBarBackgroundColor);
        weaponEnemies_.push_back(std::move(entry));
    }
}

} // namespace engine::game
