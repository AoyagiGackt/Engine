/**
 * @file GamePlaySceneCombat.cpp
 * @brief GamePlaySceneの戦闘関連更新処理（近接/銃コンボ・武器付き敵・ギミック・被弾・スタイル演出）を実装するファイル
 * @note GamePlayScene の戦闘の更新だけを GamePlayScene.cpp から分けて置いている（クラスは GamePlayScene のまま）
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "EnemyTuning.h"
#include "GameConstants.h"
#include "GameFlags.h"
#include "GameRules.h"
#include "GamePlaySceneInitializer.h"
#include "GrayscaleEffect.h"
#include "HitEffectLibrary.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ImageFilter.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <random>
using namespace engine::graphics;

// スタイルゲージ・覚醒ゲージ・ヒット判定範囲の調整値はResources/Config/combat_tuning.json（CombatTuning）で持つ
namespace engine::game {

namespace {
constexpr float kStageHalfDepth = 0.5f; // 2.5Dステージの奥行き半分
constexpr int kTechniqueIdStride = 16; // 近接の技IDは 武器種 * これ + 段（段数がこれを超えない前提）
constexpr int kSkillTechniqueIdBase = 1000; // 近接段のIDと重ならないよう固有技に割り当てる番号の起点
constexpr float kMinFatigueRange = 0.01f; // 無料区間を1以上に設定されても0除算しないための下限




/** @brief 近接ヒット時の武器固有効果に渡す対象 */
struct WeaponHitBonusContext {
    EnemyEntity& target; ///< 今回殴った敵
    int comboStep; ///< 現在のコンボ段
    const std::vector<EnemyEntity*>& weaponEnemies; ///< 連鎖の対象になりうる武器持ちの敵
    float chainRange; ///< 連鎖が届く横距離
};

/** @brief 近接ヒット時に武器ごとに乗る追加効果 */
class WeaponHitBonus {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~WeaponHitBonus() = default;
    /** @brief 追加効果の無い武器では何もしない */
    virtual void Apply(const WeaponHitBonusContext&) const { }
    /** @brief 武器種に対応する追加効果を取得する */
    static const WeaponHitBonus& For(WeaponType type);
};

/** @brief 剣: コンボ後半で追加ダメージ */
class SwordHitBonus final : public WeaponHitBonus {
public:
    /** @brief 剣: コンボ後半の段なら追加ダメージを与える */
    void Apply(const WeaponHitBonusContext& ctx) const override {
        if (ctx.comboStep >= CombatTune().swordBonusStartStep) {
            ctx.target.TakeDamage(CombatTune().swordBonusDamage);
        }
    }
};

/** @brief 短剣: 行動速度を落とす */
class DaggerHitBonus final : public WeaponHitBonus {
public:
    /** @brief 短剣: 当てた敵の行動速度を落とす */
    void Apply(const WeaponHitBonusContext& ctx) const override {
        ctx.target.ApplySlow(CombatTune().daggerSlowSeconds);
    }
};

/** @brief 槍: 周囲の武器持ちの敵へ連鎖する */
class SpearHitBonus final : public WeaponHitBonus {
public:
    /** @brief 槍: 周りの武器を持った敵へダメージを連鎖させる */
    void Apply(const WeaponHitBonusContext& ctx) const override {
        const float targetX = ctx.target.GetPosition().x;
        for (EnemyEntity* enemy : ctx.weaponEnemies) {
            if (!enemy->IsDefeated() && std::abs(enemy->GetPosition().x - targetX) < ctx.chainRange) {
                enemy->TakeDamage(CombatTune().spearChainDamage);
            }
        }
    }
};

/** @brief 武器種に対応する追加効果を取得する */
const WeaponHitBonus& WeaponHitBonus::For(WeaponType type) {
    static const SwordHitBonus kSword {};
    static const DaggerHitBonus kDagger {};
    static const SpearHitBonus kSpear {};
    static const WeaponHitBonus kNone {};
    // WeaponType の並び順。ヒット時の追加効果が無い武器は kNone
    static const std::array<const WeaponHitBonus*, kWeaponTypeCount> kBonuses = {
        &kSword, // Sword
        &kSpear, // Spear
        &kNone, // Hammer
        &kDagger, // Dagger
        &kNone, // Ball
        &kNone, // Greatsword
        &kNone, // Scythe
        &kNone, // Axe
    };
    const size_t index = static_cast<size_t>(type);
    return index < kBonuses.size() ? *kBonuses[index] : kNone;
}
} // namespace

/** @brief UpdateStyleAndUI()の下請け 敵の当たり判定AABBを現在位置から算出する */
AABB GamePlayScene::GetEnemyHitBox() const {
    return EnemyHitBoxAt(enemy_->GetPosition());
}

/** @brief 空中の近接判定を上下と左右に広げる（高さが合わずに空振りするのを減らす） */
void GamePlayScene::WidenAirMeleeRange(AABB& range, const Vector3& center, float halfHeight, float widthPadding) {
    range.min.y = center.y - halfHeight;
    range.max.y = center.y + halfHeight;
    range.min.x -= widthPadding;
    range.max.x += widthPadding;
}

/** @brief 固有技の判定範囲（プレイヤーを中心にした横長の箱）を返す */
AABB GamePlayScene::SkillRangeAround(const Vector3& center, float radius) {
    // 高さは調整値の固定値、奥行きは2.5Dステージの厚みいっぱいに取る
    return {
        { center.x - radius, center.y - CombatTune().skillRangeHalfHeight, -kStageHalfDepth },
        { center.x + radius, center.y + CombatTune().skillRangeHalfHeight, kStageHalfDepth }
    };
}

/** @brief 敵の当たり判定AABBを指定位置から算出する（ボス・道中の敵で共通の大きさ） */
AABB GamePlayScene::EnemyHitBoxAt(const Vector3& position) {
    // 横と縦は調整値の半径、奥行きは2.5Dステージの厚みいっぱいに取る
    const float halfExtent = CombatTune().enemyHitBoxHalfExtent;
    return { { position.x - halfExtent, position.y - halfExtent, -kStageHalfDepth },
        { position.x + halfExtent, position.y + halfExtent, kStageHalfDepth } };
}

/** @brief UpdateStyleAndUI()の下請け 近接コンボのヒット判定とスタイル加点・属性追撃を処理する */
void GamePlayScene::ApplyMeleeComboStyleHit(const AABB& enemyAABB) {
    const auto* wm = WeaponManager::GetInstance();
    if (!(wm->HasEquippedWeapon() && player_->JustComboHit())) {
        return;
    }
    const Vector3& ppos = player_->GetPosition();
    // 前方に厚く、背後は振り抜きぶんだけ（左右対称だと背後の遠い敵にまで当たってしまう）
    AABB meleeRange = sceneShared::MakeDirectionalRange(
        ppos, player_->GetLastDirX(), wm->GetCurrent().range,
        wm->GetCurrent().range * CombatTune().skillRearReachMult);
    if (!player_->IsOnGround()) {
        // 空中では上下移動が同時に起きるため、見た目の武器軌跡に合わせて縦判定と先端を少し広げる。
        WidenAirMeleeRange(meleeRange, ppos, CombatTune().airMeleeRangeHalfHeight, CombatTune().airMeleeRangeWidthPadding);
    }
    if (!Collision::CheckCollision(meleeRange, enemyAABB)) {
        return;
    }
    // 同じ武器の同じ段を続けて当てた回数を数える（連打を減点するため）
    const int techniqueId = static_cast<int>(wm->GetCurrent().type) * kTechniqueIdStride
        + player_->GetComboStep();
    if (techniqueId == lastTechniqueId_) {
        repeatedTechniqueCount_++;
    } else {
        lastTechniqueId_ = techniqueId;
        repeatedTechniqueCount_ = 0;
    }
    // スタイル: 段が進むほど・持ち替え直後・空中ほど高く、同じ技の連打ほど低くする
    const float repeatPenalty = (std::min)(repeatedTechniqueCount_ * CombatTune().meleeRepeatPenaltyPerHit, CombatTune().meleeRepeatPenaltyCap);
    const float switchBonus = player_->JustWeaponSwitchHit() ? CombatTune().meleeWeaponSwitchBonus : 0.0f;
    // 足場に乗って空中から仕掛けたコンボは地上より評価を上げる（高い位置のブロックに戦闘上の意味を持たせる）
    const float airborneBonus = player_->IsOnGround() ? 0.0f : CombatTune().airborneHitStyleBonus;
    styleMeter_ = std::clamp(styleMeter_ + CombatTune().meleeBaseStyleGain
            + player_->GetComboStep() * CombatTune().meleeComboStepStyleGain + switchBonus + airborneBonus - repeatPenalty,
        0.0f, 1.0f);
    ChargeAwakenGaugeByStyle(CombatTune().meleeAwakenGaugeGain);
    // ダメージ: 武器の威力 × 段の倍率 × 全体倍率（最低1）
    const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
    const WeaponData& weapon = wm->GetCurrent();
    const float damageMult = attack != nullptr ? attack->damageMult : 1.0f;
    const int damage = (std::max)(1,
        static_cast<int>(std::round(weapon.damage * damageMult / CombatTune().meleeDamageDivisor * CurrentDamageMult())));
    // 覚醒中の多段ヒットの2発目以降は、押し出し・止め・締め演出を軽くして刻みの速さを見せる
    const bool extraHit = player_->JustComboExtraHit();
    const float knockbackMult = weapon.knockbackMult * player_->GetAwakenedKnockbackMult() * CombatTune().knockbackScale
        * (extraHit ? CombatTune().awakenExtraHitKnockScale : 1.0f);
    const bool comboFinisher = !extraHit && IsMeleeFinisher(weapon.type, attack);
    // 固有技の連撃（剣舞など）は武器の疲労の対象外
    const bool skillSequence = player_->IsSkillSequenceActive();
    const int fatigueIndex = skillSequence ? -1 : HeldMeleeFatigueIndex();
    const float fatigueMult = WeaponFatigueDamageMult(fatigueIndex);
    // 疲労ぶん弱めたダメージを与え、使った武器の疲労を溜める
    enemy_->TakeDamageScaled(static_cast<float>(damage) * fatigueMult);
    RegisterFatigueHit(fatigueIndex, CombatTune().weaponFatiguePerHit);
    if (skillSequence && attack != nullptr && attack->finisher) {
        enemy_->ApplyHomeRun(player_->GetLastDirX());
    } else {
        enemy_->ApplyComboReaction(player_->GetLastDirX() * knockbackMult,
            (attack != nullptr ? attack->knockY : CombatTune().defaultKnockY) * knockbackMult,
            player_->JustWeaponSwitchHit(), ppos.x, comboFinisher);
    }
    // ボスは長く拘束しすぎると一方的になるため、雑魚より硬直を短くする
    enemy_->ApplyHitstun(MeleeHitstunSeconds(attack) * CombatTune().bossHitstunScale, true);
    styleRankHud_.RegisterHit(attack != nullptr ? attack->id : "melee", 1.0f);
    // 段を出し切った時だけ HUD に FINISH! を出す
    if (comboFinisher) {
        styleRankHud_.RegisterFinisher();
    }
    const int chainHits = styleRankHud_.GetHitCount();

    // 段が進むほど敵側の弾け方も大きくする（コンボの伸びを敵の見た目でも返す）
    float hitStrength = player_->JustWeaponSwitchHit()
        ? CombatTune().weaponSwitchHitStrength
        : 1.0f + static_cast<float>((std::max)(player_->GetComboStep() - 1, 0)) * CombatTune().comboStepHitStrengthGain;
    Vector4 hitColor = ColorFromArray(weapon.effectColor);
    ApplyFatigueToHitEffect(fatigueMult, hitColor, hitStrength);
    EmitEnemyHitEffect(enemy_->GetPosition(), hitColor, hitStrength,
        weapon.effectBurstCount, weapon.effectRingRadius);
    EmitWeaponStyleHit(enemy_->GetPosition(), comboFinisher);
    comboFeel_.OnMeleeHit(attack, comboFinisher, enemy_->GetPosition(), ColorFromArray(weapon.effectColor),
        player_->GetLastDirX(), camera_.get());
    // 属性の弾けは効きが落ちていない武器でだけ出す（持ち替えた瞬間に派手さが戻るのを見せる）
    if (fatigueMult >= 1.0f) {
        EmitElementalHitEffect(weapon, enemy_->GetPosition(), player_->GetComboStep());
    }

    // 段ごとのヒットストップとカメラ揺れ（多段ヒットの2発目以降は軽く）
    if (extraHit) {
        RequestMeleeHitStopAndShake(nullptr, false, HitEffectLibrary::GetInstance()->SceneParticles().hitStopAwakenExtra);
    } else {
        RequestMeleeHitStopAndShake(attack, comboFinisher, CombatTune().defaultHitStopFrames);
    }

    // 段の締めと一定ヒット刻みを明確な山にして、連続攻撃の途中と成功時の手応えを分ける。
    const bool chainMilestone = chainHits > 0 && (chainHits % CombatTune().comboMilestoneInterval) == 0;
    if (comboFinisher || chainMilestone) {
        const std::string recipe = comboFinisher ? "combo_finisher" : "combo_milestone";
        const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
        const Vector3 weaponRgb = ColorRgb(weapon.effectColor);
        fx.Flash(recipe, { .tint = weaponRgb });
        fx.Impact(recipe);
        fx.Ring(recipe, enemy_->GetPosition(), { .tint = weaponRgb });
        if (comboFinisher) {
            fx.Slash(recipe, enemy_->GetPosition(), player_->GetLastDirX(), { .tint = weaponRgb });
        }
    }

    // 武器ごとの追加効果（周りの敵へ連鎖する等）を、道中の敵も候補にして当てる
    std::vector<EnemyEntity*> chainCandidates;
    chainCandidates.reserve(weaponEnemies_.size());
    for (const auto& entry : weaponEnemies_) {
        chainCandidates.push_back(entry.enemy);
    }
    WeaponHitBonus::For(wm->GetCurrent().type)
        .Apply({ *enemy_, player_->GetComboStep(), chainCandidates, CombatTune().chainSkillRange });
}

/** @brief UpdateStyleAndUI()の下請け 武器固有技（SPACE）のヒット判定とスタイル加点を処理する */
void GamePlayScene::ApplyWeaponSkillStyleHit(const AABB& enemyAABB) {
    if (!player_->JustWeaponSkillHit()) {
        return;
    }
    const auto* wm = WeaponManager::GetInstance();
    const Vector3& ppos = player_->GetPosition();
    const bool slam = player_->JustGreatswordSlam();
    const float radius = SkillRadiusFor(slam ? CombatTune().skillSlamRadius : CombatTune().skillDefaultRadius, slam);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    if (slam && HasBossSlamTechnique()) {
        // 習得したボス技: 叩きつけの衝撃波を広げて見せる（判定半径の拡大と対応）
        fx.Ring("slam_technique", ppos, { .speed = radius });
    }
    if (!Collision::CheckCollision(SkillRangeAround(ppos, radius), enemyAABB)) {
        return;
    }
    // スタイル: 前回と違う技なら多く、同じ技の繰り返しなら少なく加える
    const int techniqueId = kSkillTechniqueIdBase + static_cast<int>(wm->GetCurrent().type);
    const float varietyBonus = techniqueId == lastTechniqueId_ ? CombatTune().skillVarietyBonusRepeat : CombatTune().skillVarietyBonusFresh;
    lastTechniqueId_ = techniqueId;
    styleMeter_ = std::clamp(styleMeter_ + varietyBonus, 0.0f, 1.0f);
    ChargeAwakenGaugeByStyle(CombatTune().skillAwakenGaugeGain);
    // ダメージ: 叩きつけか通常の固有技かの固定値（ボス技を奪っていれば上乗せ）
    const int slamBonus = (slam && HasBossSlamTechnique()) ? GameRules::GetInstance()->Get().bossTechniqueBonusDamage : 0;
    const float rawDamage = static_cast<float>((slam ? CombatTune().bossSlamBaseDamage : CombatTune().bossSkillBaseDamage) + slamBonus);
    enemy_->TakeDamage((std::max)(1, static_cast<int>(std::round(rawDamage * CurrentDamageMult()))));
    enemy_->ApplyHitstun(CombatTune().skillHitstunSeconds, true);
    enemy_->ApplyComboReaction(player_->GetLastDirX() * wm->GetCurrent().knockbackMult, CombatTune().skillKnockY,
        false, ppos.x, true);
    styleRankHud_.RegisterHit("skill_" + std::to_string(static_cast<int>(wm->GetCurrent().type)), 1.0f);

    // 固有技は通常段より大きく弾けさせる（叩きつけはさらに大きく）
    const WeaponData& weapon = wm->GetCurrent();
    EmitEnemyHitEffect(enemy_->GetPosition(), ColorFromArray(weapon.effectColor),
        slam ? CombatTune().slamHitStrength : CombatTune().skillHitStrength,
        weapon.effectBurstCount, weapon.effectRingRadius);
    EmitWeaponStyleHit(enemy_->GetPosition(), true);
    EmitElementalHitEffect(weapon.element, ColorFromArray(weapon.effectColor), enemy_->GetPosition(), CombatTune().skillElementScale);

    // 固有技は通常コンボより一段大きいヒットストップ・カメラ揺れにする（叩きつけはさらに大きく）
    fx.Impact(slam ? "skill_slam" : "skill");
}

/** @brief 覚醒中の武器倍率とジャスト回避直後の強化窓を合わせた、現在の攻撃ダメージ倍率 */
float GamePlayScene::CurrentDamageMult() const {
    const float dodgeBonus = player_->IsJustDodgeWindowActive() ? CombatTune().justDodgeDamageMult : 1.0f;
    return player_->GetAwakenedDamageMult() * dodgeBonus;
}

/** @brief 固有技の判定半径を返す（覚醒中の武器倍率と、習得済みボス技による叩きつけ強化を含む） */
float GamePlayScene::SkillRadiusFor(float baseRadius, bool slam) const {
    float radius = baseRadius * player_->GetAwakenedSkillRadiusMult();
    if (slam && HasBossSlamTechnique()) {
        radius *= GameRules::GetInstance()->Get().bossTechniqueRadiusMult;
    }
    return radius;
}

/** @brief 銃の疲労の添字（近接武器の後ろ） */
int GamePlayScene::GunFatigueIndex() const {
    return static_cast<int>(WeaponManager::GetInstance()->GetList().size());
}

/** @brief 手に持っている近接武器の疲労の添字（素手なら-1） */
int GamePlayScene::HeldMeleeFatigueIndex() const {
    const auto* wm = WeaponManager::GetInstance();
    return wm->HasEquippedWeapon() ? wm->GetIndex() : -1;
}

/** @brief 指定した武器の疲労から、今のダメージ倍率を返す（1.0=万全〜weaponFatigueMinDamageMult） */
float GamePlayScene::WeaponFatigueDamageMult(int fatigueIndex) const {
    if (fatigueIndex < 0 || fatigueIndex >= static_cast<int>(weaponFatigue_.size())) {
        return 1.0f;
    }
    const float freeRatio = CombatTune().weaponFatigueFreeRatio;
    const float t = std::clamp((weaponFatigue_[fatigueIndex] - freeRatio) / (std::max)(1.0f - freeRatio, kMinFatigueRange),
        0.0f, 1.0f);
    return 1.0f + (CombatTune().weaponFatigueMinDamageMult - 1.0f) * t;
}

/** @brief このフレームに当てた武器を記録する（疲労の加算はUpdateWeaponFatigue()でまとめて1回だけ行う） */
void GamePlayScene::RegisterFatigueHit(int fatigueIndex, float amount) {
    if (fatigueIndex < 0) {
        return;
    }
    pendingFatigueIndex_ = fatigueIndex;
    pendingFatigueAmount_ = (std::max)(pendingFatigueAmount_, amount);
}

/** @brief 当てた武器へ疲労を足し、それ以外の武器の疲労を抜く（ヒット判定の後に毎フレーム呼ぶ） */
void GamePlayScene::UpdateWeaponFatigue() {
    weaponFatigue_.resize(static_cast<size_t>(GunFatigueIndex()) + 1, 0.0f);
    if (pendingFatigueIndex_ >= 0 && pendingFatigueIndex_ < static_cast<int>(weaponFatigue_.size())) {
        float& fatigue = weaponFatigue_[pendingFatigueIndex_];
        fatigue = (std::min)(fatigue + pendingFatigueAmount_, 1.0f);
        lastFatigueIndex_ = pendingFatigueIndex_;
        fatigueIdleTimer_ = 0.0f;
    } else {
        fatigueIdleTimer_ += gameConstants::kFrameDeltaTime;
    }
    pendingFatigueIndex_ = -1;
    pendingFatigueAmount_ = 0.0f;

    // 最後に当てた武器以外は休ませている扱いで回復する（近接と銃を交互に使うのも持ち替えになる）
    const float recover = CombatTune().weaponFatigueRecoverPerSecond * gameConstants::kFrameDeltaTime;
    const bool lastWeaponResting = fatigueIdleTimer_ >= CombatTune().weaponFatigueRestDelay;
    for (int i = 0; i < static_cast<int>(weaponFatigue_.size()); ++i) {
        if (i != lastFatigueIndex_ || lastWeaponResting) {
            weaponFatigue_[i] = (std::max)(weaponFatigue_[i] - recover, 0.0f);
        }
    }
}

/** @brief 疲労で効きが落ちた命中の弾けを、威力に合わせて小さく灰色に寄せる */
void GamePlayScene::ApplyFatigueToHitEffect(float damageMult, Vector4& color, float& strength) const {
    // 効きが落ちるほど鈍い灰色へ寄せ、弾けも小さくする
    const Vector4& dullColor = HitEffectLibrary::GetInstance()->Color("fatigue_dull");
    const float minMult = CombatTune().weaponFatigueMinDamageMult;
    if (damageMult >= 1.0f || minMult >= 1.0f) {
        return;
    }
    const float dull = std::clamp((1.0f - damageMult) / (1.0f - minMult), 0.0f, 1.0f);
    color = { color.x + (dullColor.x - color.x) * dull, color.y + (dullColor.y - color.y) * dull,
        color.z + (dullColor.z - color.z) * dull, color.w };
    strength *= 1.0f + (CombatTune().fatigueDullStrengthMin - 1.0f) * dull;
}

/** @brief 近接ヒットで敵に与える被弾硬直の秒数（次の段が出るまで拘束できる長さ） */
float GamePlayScene::MeleeHitstunSeconds(const MeleeAttackDef* attack) const {
    return attack != nullptr ? attack->duration + CombatTune().hitstunPadding : CombatTune().defaultHitstunSeconds;
}

/** @brief 近接ヒットのヒットストップとカメラ揺れを要求する（途中の段は控えめ、締めだけ大きく） */
void GamePlayScene::RequestMeleeHitStopAndShake(const MeleeAttackDef* attack, bool finisher, int defaultFrames) {
    // 途中の段は揺れを控えめにして刻みを軽く、締めだけ停止も揺れも大きくして山を作る
    int hitStopFrames = attack != nullptr
        ? (attack->launcher ? HitEffectLibrary::GetInstance()->SceneParticles().hitStopLaunch : attack->hitStop)
        : defaultFrames;
    if (finisher) {
        hitStopFrames = (std::max)(hitStopFrames, CombatTune().finisherMinHitStopFrames);
    }
    TimeManager::GetInstance()->RequestHitStop(hitStopFrames);
    const float frames = static_cast<float>(hitStopFrames);
    cameraShaker_.Request((finisher ? CombatTune().finisherShakePerFrame : CombatTune().midStepShakePerFrame) * frames,
        CombatTune().hitShakeDurationBase + CombatTune().hitShakeDurationPerFrame * frames);
}

/** @brief 叩きつけ系固有技が習得済みボス技で強化されているか */
bool GamePlayScene::HasBossSlamTechnique() const {
    return RunData::GetInstance()->HasBossTechnique(GameRules::GetInstance()->Get().bossTechnique);
}

/** @brief UpdateStyleAndUI()の下請け 銃コンボのヒット判定とスタイル加点を処理する */
void GamePlayScene::ApplyGunShotStyleHit() {
    if (!player_->JustFired()) {
        return;
    }
    const auto* wm = WeaponManager::GetInstance();
    const GunShotDef* shot = player_->GetActiveGunShot();
    const RangedWeaponData& gun = wm->GetRanged();
    // 弾は手前の相手で止まるので、射撃の的に選ばれた時だけ当たる（BuildGunShotTarget()で選択済み）
    if (!gunShotPicker_.IsTarget(enemy_)) {
        return;
    }
    // 段が進むほどスタイルが伸びる（銃コンボを回す動機付け）
    float gain = CombatTune().gunBaseStyleGain + ((shot != nullptr) ? player_->GetGunComboStep() * CombatTune().gunComboStepStyleGain : 0.0f);
    styleMeter_ = std::clamp(styleMeter_ + gain, 0.0f, 1.0f);
    ChargeAwakenGaugeByStyle(CombatTune().gunAwakenGaugeGain);
    const float gunFatigueMult = WeaponFatigueDamageMult(GunFatigueIndex());
    const bool wasDefeated = enemy_->IsDefeated();
    enemy_->TakeDamageScaled(static_cast<float>((std::max)(1, static_cast<int>(std::round(CombatTune().gunBaseDamage * CurrentDamageMult()))))
        * gunFatigueMult);
    // この弾がとどめなら、クリアの決め演出を銃の見せ方にする
    if (!wasDefeated && enemy_->IsDefeated()) {
        bossFinishedByGun_ = true;
    }
    RegisterFatigueHit(GunFatigueIndex(), CombatTune().gunFatiguePerHit);
    // 連射で一方的にならないよう、銃は怯ませるだけで攻撃は潰さない
    enemy_->ApplyHitstun(CombatTune().gunHitstunSeconds, false);
    styleRankHud_.RegisterHit(shot != nullptr ? shot->id : "shot", 1.0f);

    // 銃は連射するので小さめの弾けで着弾を示す
    Vector4 gunHitColor = HitEffectLibrary::GetInstance()->Color("gun_hit");
    float gunHitStrength = CombatTune().gunHitStrength;
    ApplyFatigueToHitEffect(gunFatigueMult, gunHitColor, gunHitStrength);
    EmitEnemyHitEffect(enemy_->GetPosition(), gunHitColor, gunHitStrength);
    if (gunFatigueMult >= 1.0f) {
        EmitElementalHitEffect(gun.element, ColorFromArray(gun.effectColor), enemy_->GetPosition(), CombatTune().gunElementScale);
    }
}

/** @brief UpdateStyleAndUI()の下請け 覚醒乱舞ラッシュのヒット判定とスタイル加点を処理する */
void GamePlayScene::ApplyRampageStyleHit(const AABB& enemyAABB) {
    if (!player_->JustRampageHit()) {
        return;
    }
    const Vector3& ppos = player_->GetPosition();
    AABB rushRange = { { ppos.x - CombatTune().rampageRushRadiusX, ppos.y - CombatTune().rampageRushRadiusY, -kStageHalfDepth },
        { ppos.x + CombatTune().rampageRushRadiusX, ppos.y + CombatTune().rampageRushRadiusY, kStageHalfDepth } };
    if (!Collision::CheckCollision(rushRange, enemyAABB)) {
        return;
    }
    // 乱舞スラッシュ 回数が増えるほど多くゲージが溜まる
    styleMeter_ = std::clamp(
        styleMeter_ + CombatTune().rampageBaseStyleGain + player_->GetJuggleCount() * CombatTune().rampageJuggleStyleGain, 0.0f, 1.0f);
    enemy_->TakeDamage(CombatTune().rampageDamage);
    styleRankHud_.RegisterHit("rampage", 1.0f);

    // 乱舞は手数が多いので控えめな弾けで1発ごとの命中を示す
    EmitEnemyHitEffect(enemy_->GetPosition(), HitEffectLibrary::GetInstance()->Color("rampage_hit"), CombatTune().rampageHitStrength);
}

/** @brief UpdateStyleAndUI()の下請け スタイルメーターの時間経過による減衰を処理する */
void GamePlayScene::DecayStyleMeter(float dt) {
    const float decayMult = RunData::GetInstance()->HasSkill(RunData::Skill::kStylePersist) ? CombatTune().stylePersistDecayMult : 1.0f;
    styleMeter_ = std::clamp(styleMeter_ - CombatTune().styleDecayRate * dt * decayMult, 0.0f, 1.0f);
    peakStyle_ = (std::max)(peakStyle_, styleMeter_);
}

/** @brief 現在のスタイルランクが高いほど多く覚醒ゲージを溜める（攻撃ヒット・ジャスト回避用） */
void GamePlayScene::ChargeAwakenGaugeByStyle(float amount) {
    const float rankMult = 1.0f + static_cast<float>(styleRankHud_.GetRankIndex()) * CombatTune().styleRankAwakenGaugeBonus;
    player_->ChargeAwakenGauge(amount * rankMult);
}

/** @brief UpdateStyleAndUI()の下請け 回避の連打ペナルティを処理する（ジャスト回避の加点はTryJustDodge側） */
void GamePlayScene::UpdateDodgeStyle() {
    // 回避そのものは加点しない。危険の無い場面での連打だけスタイルを削る（引きこもり回避の抑止）
    if (!player_->JustDodged()) {
        return;
    }
    const int spam = player_->GetDodgeSpamCount();
    const int startCount = CombatTune().dodgeSpamPenaltyStartCount;
    if (spam >= startCount) {
        styleMeter_ = std::clamp(styleMeter_ - CombatTune().dodgeSpamPenalty * static_cast<float>(spam - startCount + 1), 0.0f, 1.0f);
    }
}

/** @brief 敵の攻撃がプレイヤーに届いた瞬間、回避中ならジャスト回避として成立させる */
bool GamePlayScene::TryJustDodge(const Vector3& hitPos) {
    if (!player_->IsDodging()) {
        return false;
    }
    if (!player_->ConsumeJustDodge()) {
        return true; // 同じ回避の2発目以降は無効化だけ
    }
    styleMeter_ = std::clamp(styleMeter_ + CombatTune().justDodgeStyleGain, 0.0f, 1.0f);
    ChargeAwakenGaugeByStyle(CombatTune().justDodgeGaugeGain);
    player_->BeginJustDodgeWindow(CombatTune().justDodgeBonusSeconds); // 直後の攻撃を強化し、回避を攻めの起点にする
    TimeManager::GetInstance()->RequestHitStop(CombatTune().justDodgeHitStopFrames);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Impact("just_dodge");
    fx.Ring("just_dodge", hitPos);
    fx.Flash("just_dodge");
    return true;
}

/** @brief 攻撃の命中判定・スタイル加点・UIを1フレーム進める */
void GamePlayScene::UpdateStyleAndUI(float dt) {
    if (dt <= 0.0f) {
        UpdateWeaponSlotHud();
        DrawStyleUI();
        return;
    }

    UpdateWeaponSlotHud();

    const AABB enemyAABB = GetEnemyHitBox();
    ApplyMeleeComboStyleHit(enemyAABB);
    ApplyWeaponSkillStyleHit(enemyAABB);
    ApplyGunShotStyleHit();
    ApplyRampageStyleHit(enemyAABB);
    UpdateDodgeStyle();
    // フィニッシャースラッシュのダメージは UpdateFinisherSlash の本命ヒットで適用する
    DecayStyleMeter(dt);

    // 採点はstyleMeter_(0〜1)のまま、右上ランクの表示だけStyleMeterへ渡す
    styleRankHud_.SetNormalizedPoints(styleMeter_);
    styleRankHud_.Update(dt);

    DrawStyleUI();
}

// ══════════════════════════════════════════════════════
// パーティクルとヒット演出
// ══════════════════════════════════════════════════════

/** @brief 命中演出の段階を時間で戻し、土煙・接触や敵攻撃の当たり・技の粒子などを更新する（時間が止まっているフレームは何もしない） */
void GamePlayScene::UpdateParticles(float dt) {
    if (dt <= 0.0f) {
        return;
    }

    hitEscalationTimer_ = (std::max)(hitEscalationTimer_ - dt, 0.0f);
    if (hitEscalationTimer_ <= 0.0f) {
        hitEscalationCount_ = 0;
    }

    UpdateLandingAndJumpDustParticles();
    UpdateGhostTrail(dt);
    UpdatePlayerEnemyContactHit(dt);
    UpdateEnemyAttackOnPlayer(dt);
    UpdateBossSlamAttack(dt);
    UpdateStyleTechniqueParticles(dt);

    auto emitDefeat = [this](const Vector3& pos) {
        const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
        fx.Star("defeat", pos);
        fx.Ring("defeat", pos);
        fx.Ring("defeat_orb", pos);
        fx.Sparks("defeat", pos);
    };
    if (enemy_ && enemy_->IsDefeated() && !mainEnemyDefeatEffectEmitted_) {
        mainEnemyDefeatEffectEmitted_ = true;
        emitDefeat(enemy_->GetPosition());
    }
    for (auto& entry : weaponEnemies_) {
        if (entry.enemy && entry.enemy->IsDefeated() && !entry.defeatEffectEmitted) {
            entry.defeatEffectEmitted = true;
            emitDefeat(entry.enemy->GetPosition());
        }
    }
}

/** @brief UpdateParticles()の下請け 着地ほこりとジャンプ煙のパーティクルを更新する */
void GamePlayScene::UpdateLandingAndJumpDustParticles() {
    const Vector3& ppos = player_->GetPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

    // 着地ほこり
    if (player_->JustLanded()) {
        fx.Sparks("land_dust", ppos);
    }

    // ジャンプ煙
    if (player_->JustJumped()) {
        fx.Ring("jump_smoke", ppos);
    }
}

/** @brief UpdateParticles()の下請け 横移動・空中時の残像トレイルをスポーン・経年・削除する */
void GamePlayScene::UpdateGhostTrail(float dt) {
    const Vector3& ppos = player_->GetPosition();

    // 残像: 覚醒中/乱舞中の横移動 or 空中 → プレイヤーモデルのゴーストを一定間隔でスポーン
    // （Player::afterImageRenderer_ と同じ、残像=覚醒時だけの演出という前提に揃える）
    bool movingX = input_->PushAction(Input::Action::kMoveLeft) || input_->PushAction(Input::Action::kMoveRight);
    bool awakenActive = player_->IsRampaging();
    if (player_->IsWarping() || (awakenActive && (movingX || !player_->IsOnGround()))) {
        ghostSpawnTimer_ -= dt;
        if (ghostSpawnTimer_ <= 0.0f) {
            ghostSpawnTimer_ = HitEffectLibrary::GetInstance()->SceneParticles().ghostSpawnInterval;
            ghostTrail_.push_back({ ppos, 0.0f });
        }
    } else {
        ghostSpawnTimer_ = 0.0f;
    }
    for (auto& g : ghostTrail_) {
        g.age += dt;
    }
    while (!ghostTrail_.empty() && ghostTrail_.front().age >= HitEffectLibrary::GetInstance()->SceneParticles().ghostLifetime) {
        ghostTrail_.pop_front();
    }
}

// ══════════════════════════════════════════════════════
// フィニッシャーとクリア判定

} // namespace engine::game
