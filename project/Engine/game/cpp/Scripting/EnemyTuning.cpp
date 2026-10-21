/**
 * @file EnemyTuning.cpp
 * @brief EnemyTuningの敵調整値の読み込みを実装するファイル
 */
#include "EnemyTuning.h"
#include "GameConstants.h"
#include <utility>
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "Logger.h"

namespace engine::game {
namespace {
constexpr const char* kEnemyParamsPath = "Resources/Config/enemy_params.json";

constexpr JsonField<BasicEnemyTuning> kBasicFields[] = {
    { "basic", "attackInterval", &BasicEnemyTuning::attackInterval },
    { "basic", "attackTelegraph", &BasicEnemyTuning::attackTelegraph },
    { "basic", "attackActive", &BasicEnemyTuning::attackActive },
    { "basic", "attackDamage", &BasicEnemyTuning::attackDamage },
    { "basic", "heavyAttackDamage", &BasicEnemyTuning::heavyAttackDamage },
    { "basic", "daggerTelegraph", &BasicEnemyTuning::daggerTelegraph },
    { "basic", "spearTelegraph", &BasicEnemyTuning::spearTelegraph },
    { "basic", "heavyTelegraph", &BasicEnemyTuning::heavyTelegraph },
    { "basic", "daggerRecovery", &BasicEnemyTuning::daggerRecovery },
    { "basic", "spearRecovery", &BasicEnemyTuning::spearRecovery },
    { "basic", "heavyRecovery", &BasicEnemyTuning::heavyRecovery },
    { "basic", "approachSpeed", &BasicEnemyTuning::approachSpeed },
    { "basic", "engageRange", &BasicEnemyTuning::engageRange },
    { "basic", "aggroRange", &BasicEnemyTuning::aggroRange },
    { "basic", "leashDistance", &BasicEnemyTuning::leashDistance },
    { "basic", "meleeAttackRange", &BasicEnemyTuning::meleeAttackRange },
    { "basic", "meleeReach", &BasicEnemyTuning::meleeReach },
    { "basic", "meleeHitHalfHeight", &BasicEnemyTuning::meleeHitHalfHeight },
    { "basic", "gravity", &BasicEnemyTuning::gravity },
    { "basic", "ceilingY", &BasicEnemyTuning::ceilingY },
    { "basic", "knockbackDecay", &BasicEnemyTuning::knockbackDecay },
    { "basic", "knockbackSlowMultiplier", &BasicEnemyTuning::knockbackSlowMultiplier },
    { "basic", "airComboGravityScale", &BasicEnemyTuning::airComboGravityScale },
    { "basic", "airComboHold", &BasicEnemyTuning::airComboHold },
    { "basic", "flyingBobAmplitude", &BasicEnemyTuning::flyingBobAmplitude },
    { "basic", "flyingBobSpeed", &BasicEnemyTuning::flyingBobSpeed },
    { "basic", "healerPulseSeconds", &BasicEnemyTuning::healerPulseSeconds },
    { "basic", "healerCastSeconds", &BasicEnemyTuning::healerCastSeconds },
    { "basic", "healerRange", &BasicEnemyTuning::healerRange },
    { "basic", "healerAmount", &BasicEnemyTuning::healerAmount },
    { "basic", "corpseFootOffset", &BasicEnemyTuning::corpseFootOffset },
};

constexpr JsonField<KnightEnemyTuning> kKnightFields[] = {
    { "knight", "maxHp", &KnightEnemyTuning::maxHp },
    { "knight", "idleDuration", &KnightEnemyTuning::idleDuration },
    { "knight", "telegraphDuration", &KnightEnemyTuning::telegraphDuration },
    { "knight", "dashDuration", &KnightEnemyTuning::dashDuration },
    { "knight", "recoverDuration", &KnightEnemyTuning::recoverDuration },
    { "knight", "maxDashDistance", &KnightEnemyTuning::maxDashDistance },
    { "knight", "knockbackSpeed", &KnightEnemyTuning::knockbackSpeed },
    { "knight", "knockbackDecay", &KnightEnemyTuning::knockbackDecay },
};

constexpr JsonField<EnemyBulletTuning> kBulletFields[] = {
    { "bullet", "speed", &EnemyBulletTuning::speed },
    { "bullet", "lifetime", &EnemyBulletTuning::lifetime },
    { "bullet", "fireRange", &EnemyBulletTuning::fireRange },
};

constexpr JsonField<BossSlamTuning> kBossSlamFields[] = {
    { "bossSlam", "engageRange", &BossSlamTuning::engageRange },
    { "bossSlam", "interval", &BossSlamTuning::interval },
    { "bossSlam", "warningDuration", &BossSlamTuning::warningDuration },
    { "bossSlam", "radius", &BossSlamTuning::radius },
    { "bossSlam", "damage", &BossSlamTuning::damage },
};

constexpr JsonField<EnemySeparationTuning> kSeparationFields[] = {
    { "separation", "minDistanceX", &EnemySeparationTuning::minDistanceX },
    { "separation", "minDistanceY", &EnemySeparationTuning::minDistanceY },
    { "separation", "pushRate", &EnemySeparationTuning::pushRate },
    { "separation", "bodyHalfWidth", &EnemySeparationTuning::bodyHalfWidth },
    { "separation", "wallBandBottom", &EnemySeparationTuning::wallBandBottom },
    { "separation", "wallBandTop", &EnemySeparationTuning::wallBandTop },
    { "separation", "wallContactMargin", &EnemySeparationTuning::wallContactMargin },
};
}

/** @brief 唯一のEnemyTuningインスタンスを取得する（初回はJSONを読み込む） */
EnemyTuning* EnemyTuning::GetInstance()
{
    static EnemyTuning instance;
    return &instance;
}

/** @brief enemy_params.json を読み込んで初期化する */
EnemyTuning::EnemyTuning()
{
    Reload();
}

/** @brief JSONを読み直す */
void EnemyTuning::Reload()
{
    // 設定が無い場合のモデル・縮尺は既存の配置に合わせる
    visuals_ = {
        { "Slime", { "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Slime.obj", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SlimePalette.png", 0.47f, -GameConstants::kHalfPi } },
        { "Bat", { "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Bat_Palette.obj", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/BatPalette.png", 0.21f, 0.0f } },
        { "Dragon", { "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Dragon_Palette.obj", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/DragonPalette.png", 0.38f, 0.0f } },
        { "Skeleton", { "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Skeleton_Palette.obj", "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SkeletonPalette.png", 0.215f, 0.0f } },
    };
    basic_ = BasicEnemyTuning { };
    knight_ = KnightEnemyTuning { };
    bullet_ = EnemyBulletTuning { };
    bossSlam_ = BossSlamTuning { };
    separation_ = EnemySeparationTuning { };

    const nlohmann::json root = JsonHelper::Load(kEnemyParamsPath);
    if (!root.is_object())
    {
        Logger::LogWarning(std::string("EnemyTuning: ") + kEnemyParamsPath + " が読めません。既定値で動きます");
        return;
    }

    // 名前ごとの見た目は共通の項目表で読み、未知の種類も同じ方式で追加できる
    if (root.contains("visuals") && root["visuals"].is_object())
    {
        constexpr JsonField<EnemyVisualTuning> kVisualFields[] = {
            { nullptr, "model", &EnemyVisualTuning::model },
            { nullptr, "texture", &EnemyVisualTuning::texture },
            { nullptr, "scale", &EnemyVisualTuning::scale },
            { nullptr, "yawOffset", &EnemyVisualTuning::yawOffset },
        };
        for (const auto& [kind, value] : root["visuals"].items())
        {
            if (!value.is_object())
            {
                continue;
            }
            EnemyVisualTuning visual = visuals_.contains(kind) ? visuals_.at(kind) : EnemyVisualTuning {};
            ReadJsonFields(value, kVisualFields, visual);
            if (!visual.model.empty() && !visual.texture.empty() && visual.scale > 0.0f)
            {
                visuals_[kind] = std::move(visual);
            }
        }
    }
    ReadJsonFields(root, kBasicFields, basic_);
    ReadJsonFields(root, kKnightFields, knight_);
    ReadJsonFields(root, kBulletFields, bullet_);
    ReadJsonFields(root, kBossSlamFields, bossSlam_);
    ReadJsonFields(root, kSeparationFields, separation_);
}

/** @brief 見た目の設定を種類名で検索する */
const EnemyVisualTuning* EnemyTuning::FindVisual(const std::string& kind) const
{
    const auto found = visuals_.find(kind);
    return found != visuals_.end() ? &found->second : nullptr;
}

} // namespace engine::game
