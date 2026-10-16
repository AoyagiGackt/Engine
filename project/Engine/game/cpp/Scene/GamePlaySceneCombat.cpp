/**
 * @file GamePlaySceneCombat.cpp
 * @brief GamePlaySceneの戦闘関連更新処理（近接/銃コンボ・武器付き敵・ギミック・被弾・スタイル演出）を実装するファイル
 * @note GamePlayScene.cppからの分割ファイルクラス自体はGamePlaySceneのまま、定義の置き場所だけを分けている
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
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// スタイルゲージ・覚醒ゲージ・ヒット判定範囲の調整値はResources/Config/combat_tuning.json（CombatTuning）で持つ
static const CombatTuningData& Tune() { return CombatTuning::GetInstance()->Get(); }

namespace {
constexpr float kStageHalfDepth = 0.5f; // 2.5Dステージの奥行き半分
constexpr int kTechniqueIdStride = 16; // 近接の技IDは 武器種 * これ + 段（段数がこれを超えない前提）
constexpr int kSkillTechniqueIdBase = 1000; // 近接段のIDと重ならないよう固有技に割り当てる番号の起点
constexpr float kMinFatigueRange = 0.01f; // 無料区間を1以上に設定されても0除算しないための下限
// レベルに当たり判定ブロックがない時の既定のステージ範囲
constexpr float kDefaultStageLeft = 1.5f;
constexpr float kDefaultStageRight = 36.5f;

Vector3 Rgb(const float (&color)[4])
{
    return { color[0], color[1], color[2] };
}

Vector4 Rgba(const float (&color)[4])
{
    return { color[0], color[1], color[2], color[3] };
}

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
    virtual ~WeaponHitBonus() = default;
    virtual void Apply(const WeaponHitBonusContext&) const { }
    /** @brief 武器種に対応する追加効果を取得する */
    static const WeaponHitBonus& For(WeaponType type);
};

/** @brief 剣: コンボ後半で追加ダメージ */
class SwordHitBonus final : public WeaponHitBonus {
public:
    void Apply(const WeaponHitBonusContext& ctx) const override
    {
        if (ctx.comboStep >= Tune().swordBonusStartStep) {
            ctx.target.TakeDamage(Tune().swordBonusDamage);
        }
    }
};

/** @brief 短剣: 行動速度を落とす */
class DaggerHitBonus final : public WeaponHitBonus {
public:
    void Apply(const WeaponHitBonusContext& ctx) const override
    {
        ctx.target.ApplySlow(Tune().daggerSlowSeconds);
    }
};

/** @brief 槍: 周囲の武器持ちの敵へ連鎖する */
class SpearHitBonus final : public WeaponHitBonus {
public:
    void Apply(const WeaponHitBonusContext& ctx) const override
    {
        const float targetX = ctx.target.GetPosition().x;
        for (EnemyEntity* enemy : ctx.weaponEnemies) {
            if (!enemy->IsDefeated() && std::abs(enemy->GetPosition().x - targetX) < ctx.chainRange) {
                enemy->TakeDamage(Tune().spearChainDamage);
            }
        }
    }
};

const WeaponHitBonus& WeaponHitBonus::For(WeaponType type)
{
    static const SwordHitBonus sword {};
    static const DaggerHitBonus dagger {};
    static const SpearHitBonus spear {};
    static const WeaponHitBonus none {};
    // WeaponType の並び順。ヒット時の追加効果が無い武器は none
    static const std::array<const WeaponHitBonus*, kWeaponTypeCount> kBonuses = {
        &sword, // Sword
        &spear, // Spear
        &none, // Hammer
        &dagger, // Dagger
        &none, // Ball
        &none, // Greatsword
        &none, // Scythe
        &none, // Axe
    };
    const size_t index = static_cast<size_t>(type);
    return index < kBonuses.size() ? *kBonuses[index] : none;
}
} // namespace

void GamePlayScene::UpdateTargetLock()
{
    // Shiftを押している間だけロックオンし、その間は常に一番近い敵を対象にし続ける
    // （BattleTestSceneのダミーロックオンと同じ規約。押した瞬間の対象に固定しない）
    if (!input_->PushAction(Input::Action::LockOn)) {
        lockedEnemy_ = nullptr;
        return;
    }

    // 画面外の敵まで拾うとロック対象の方へカメラが大きく寄ってしまうため、
    // 画面内に映る範囲（カメラ半幅）より遠い敵はロック対象から除外する
    constexpr float kMaxLockRange = GameConstants::kCameraHalfW;

    const Vector3& pp = player_->GetPosition();
    float minDist = FLT_MAX;
    const EnemyEntity* nearest = nullptr;
    auto consider = [&](const EnemyEntity* candidate) {
        if (candidate->IsDefeated()) {
            return;
        }
        const float dist = std::abs(candidate->GetPosition().x - pp.x);
        if (dist <= kMaxLockRange && dist < minDist) {
            minDist = dist;
            nearest = candidate;
        }
    };
    consider(enemy_);
    for (const auto& weaponEnemy : weaponEnemies_) {
        consider(weaponEnemy.enemy);
    }
    lockedEnemy_ = nearest;
}

void GamePlayScene::UpdateCamera()
{
    // カメラをプレイヤーに追従（境界ブロックが画面外に出ないよう clamp）。開始演出の間は演出が決めた位置と引きで映す
    const bool introCamera = stageIntro_.IsPlaying();
    const Vector3 ppos = introCamera ? stageIntro_.CameraFocus() : player_->GetPosition();
    // 引いた分だけ画面に映る範囲も広がる（透視投影なので距離に比例する）
    const float zoom = introCamera ? stageIntro_.CameraZoom() : 1.0f;
    const float viewHalfW = GameConstants::kCameraHalfW * zoom;
    const float viewHalfH = GameConstants::kCameraHalfH * zoom;
    const StageBounds bounds = GetStageBounds();
    const float stageLeft = bounds.left;
    const float stageRight = bounds.right;
    const float stageBottom = bounds.bottom;
    const float stageTop = bounds.top;
    const float cameraMinX = stageLeft + viewHalfW;
    const float cameraMaxX = stageRight - viewHalfW;
    float cameraX = cameraMinX <= cameraMaxX
        ? std::clamp(ppos.x, cameraMinX, cameraMaxX)
        : (stageLeft + stageRight) * 0.5f;

    // ロック中はカメラをほんの少しだけ対象側へ寄せて、狙っていることに気付きやすくする
    // （BattleTestScene/SceneShared::UpdateCameraFollowと同じ控えめな比率）
    if (lockedEnemy_ != nullptr) {
        cameraX += (lockedEnemy_->GetPosition().x - ppos.x) * Tune().lockOnCameraShiftRatio;
        if (cameraMinX <= cameraMaxX) {
            cameraX = std::clamp(cameraX, cameraMinX, cameraMaxX);
        }
    }

    // 縦も床の下・天井の上の何も無い空間が映らないよう、当たり判定ブロックの上下端で止める
    float cameraY = ppos.y + GameConstants::kCameraFollowOffsetY;
    const float cameraMinY = stageBottom + viewHalfH;
    const float cameraMaxY = stageTop - viewHalfH;
    if (bounds.fromLevel && cameraMinY <= cameraMaxY) {
        cameraY = std::clamp(cameraY, cameraMinY, cameraMaxY);
    }

    cameraTargetPos_ = {
        cameraX,
        cameraY,
        GameConstants::kCameraDistanceZ * zoom
    };

    UpdateCameraSmoothing();

    // カメラシェイク（スムージングの後に直接カメラ座標へ加算）
    Vector3 shake = cameraShaker_.Update(GameConstants::kFrameDeltaTime);
    if (shake.x != 0.0f || shake.y != 0.0f) {
        Vector3 cam = camera_->GetTranslate();
        camera_->SetTranslate({ cam.x + shake.x, cam.y + shake.y, cam.z });
    }
    comboFeel_.ApplyCameraZoom(camera_.get());

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
}

GamePlayScene::StageBounds GamePlayScene::GetStageBounds()
{
    // 当たり判定ブロック全体を囲む範囲（ブロックが無ければ既定の広さ）
    StageBounds bounds { kDefaultStageLeft, kDefaultStageRight, 0.0f, 0.0f, false };
    const std::vector<AABB> stageSolids = GetStageEditor().GetSolidColliders();
    if (stageSolids.empty()) {
        return bounds;
    }
    bounds = { stageSolids.front().min.x, stageSolids.front().max.x, stageSolids.front().min.y, stageSolids.front().max.y, true };
    for (const AABB& solid : stageSolids) {
        bounds.left = (std::min)(bounds.left, solid.min.x);
        bounds.right = (std::max)(bounds.right, solid.max.x);
        bounds.bottom = (std::min)(bounds.bottom, solid.min.y);
        bounds.top = (std::max)(bounds.top, solid.max.y);
    }
    return bounds;
}

AABB GamePlayScene::GetEnemyHitBox() const
{
    const Vector3& epos = enemy_->GetPosition();
    return { { epos.x - Tune().enemyHitBoxHalfExtent, epos.y - Tune().enemyHitBoxHalfExtent, -kStageHalfDepth },
        { epos.x + Tune().enemyHitBoxHalfExtent, epos.y + Tune().enemyHitBoxHalfExtent, kStageHalfDepth } };
}

void GamePlayScene::ApplyMeleeComboStyleHit(const AABB& enemyAABB)
{
    const auto* wm = WeaponManager::GetInstance();
    if (!(wm->HasEquippedWeapon() && player_->JustComboHit())) {
        return;
    }
    const Vector3& ppos = player_->GetPosition();
    // 前方に厚く、背後は振り抜きぶんだけ（左右対称だと背後の遠い敵にまで当たってしまう）
    AABB meleeRange = SceneShared::MakeDirectionalRange(
        ppos, player_->GetLastDirX(), wm->GetCurrent().range,
        wm->GetCurrent().range * GameConstants::kSkillRearReachMult);
    if (!player_->IsOnGround()) {
        // 空中では上下移動が同時に起きるため、見た目の武器軌跡に合わせて縦判定と先端を少し広げる。
        meleeRange.min.y = ppos.y - Tune().airMeleeRangeHalfHeight;
        meleeRange.max.y = ppos.y + Tune().airMeleeRangeHalfHeight;
        meleeRange.min.x -= Tune().airMeleeRangeWidthPadding;
        meleeRange.max.x += Tune().airMeleeRangeWidthPadding;
    }
    if (!Collision::CheckCollision(meleeRange, enemyAABB)) {
        return;
    }
    const int techniqueId = static_cast<int>(wm->GetCurrent().type) * kTechniqueIdStride
        + player_->GetComboStep();
    if (techniqueId == lastTechniqueId_) {
        repeatedTechniqueCount_++;
    } else {
        lastTechniqueId_ = techniqueId;
        repeatedTechniqueCount_ = 0;
    }
    const float repeatPenalty = (std::min)(repeatedTechniqueCount_ * Tune().meleeRepeatPenaltyPerHit, Tune().meleeRepeatPenaltyCap);
    const float switchBonus = player_->JustWeaponSwitchHit() ? Tune().meleeWeaponSwitchBonus : 0.0f;
    // 足場に乗って空中から仕掛けたコンボは地上より評価を上げる（高い位置のブロックに戦闘上の意味を持たせる）
    const float airborneBonus = player_->IsOnGround() ? 0.0f : Tune().airborneHitStyleBonus;
    styleMeter_ = std::clamp(styleMeter_ + Tune().meleeBaseStyleGain
            + player_->GetComboStep() * Tune().meleeComboStepStyleGain + switchBonus + airborneBonus - repeatPenalty,
        0.0f, 1.0f);
    ChargeAwakenGaugeByStyle(Tune().meleeAwakenGaugeGain);
    const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
    const WeaponData& weapon = wm->GetCurrent();
    const float damageMult = attack != nullptr ? attack->damageMult : 1.0f;
    const int damage = (std::max)(1,
        static_cast<int>(std::round(weapon.damage * damageMult / Tune().meleeDamageDivisor * CurrentDamageMult())));
    // 覚醒中の多段ヒットの2発目以降は、押し出し・止め・締め演出を軽くして刻みの速さを見せる
    const bool extraHit = player_->JustComboExtraHit();
    const float knockbackMult = weapon.knockbackMult * player_->GetAwakenedKnockbackMult() * Tune().knockbackScale
        * (extraHit ? GameConstants::kAwakenExtraHitKnockScale : 1.0f);
    const bool comboFinisher = !extraHit && IsMeleeFinisher(weapon.type, attack);
    // 固有技の連撃（剣舞など）は武器の疲労の対象外
    const bool skillSequence = player_->IsSkillSequenceActive();
    const int fatigueIndex = skillSequence ? -1 : HeldMeleeFatigueIndex();
    const float fatigueMult = WeaponFatigueDamageMult(fatigueIndex);
    enemy_->TakeDamageScaled(static_cast<float>(damage) * fatigueMult);
    RegisterFatigueHit(fatigueIndex, Tune().weaponFatiguePerHit);
    if (skillSequence && attack != nullptr && attack->finisher) {
        enemy_->ApplyHomeRun(player_->GetLastDirX());
    } else {
        enemy_->ApplyComboReaction(player_->GetLastDirX() * knockbackMult,
            (attack != nullptr ? attack->knockY : Tune().defaultKnockY) * knockbackMult,
            player_->JustWeaponSwitchHit(), ppos.x, comboFinisher);
    }
    // ボスは長く拘束しすぎると一方的になるため、雑魚より硬直を短くする
    enemy_->ApplyHitstun(MeleeHitstunSeconds(attack) * Tune().bossHitstunScale, true);
    styleRankHud_.RegisterHit(attack != nullptr ? attack->id : "melee", 1.0f);
    const int chainHits = styleRankHud_.GetHitCount();

    // 段が進むほど敵側の弾け方も大きくする（コンボの伸びを敵の見た目でも返す）
    float hitStrength = player_->JustWeaponSwitchHit()
        ? Tune().weaponSwitchHitStrength
        : 1.0f + static_cast<float>((std::max)(player_->GetComboStep() - 1, 0)) * Tune().comboStepHitStrengthGain;
    Vector4 hitColor = Rgba(weapon.effectColor);
    ApplyFatigueToHitEffect(fatigueMult, hitColor, hitStrength);
    EmitEnemyHitEffect(enemy_->GetPosition(), hitColor, hitStrength,
        weapon.effectBurstCount, weapon.effectRingRadius);
    comboFeel_.OnMeleeHit(attack, comboFinisher, enemy_->GetPosition(), Rgba(weapon.effectColor),
        player_->GetLastDirX(), camera_.get());
    // 属性の弾けは効きが落ちていない武器でだけ出す（持ち替えた瞬間に派手さが戻るのを見せる）
    if (fatigueMult >= 1.0f) {
        EmitElementalHitEffect(weapon, enemy_->GetPosition(), player_->GetComboStep());
    }

    if (extraHit) {
        RequestMeleeHitStopAndShake(nullptr, false, GameConstants::kHitStopAwakenExtra);
    } else {
        RequestMeleeHitStopAndShake(attack, comboFinisher, Tune().defaultHitStopFrames);
    }

    // 段の締めと一定ヒット刻みを明確な山にして、連続攻撃の途中と成功時の手応えを分ける。
    const bool chainMilestone = chainHits > 0 && (chainHits % Tune().comboMilestoneInterval) == 0;
    if (comboFinisher || chainMilestone) {
        const std::string recipe = comboFinisher ? "combo_finisher" : "combo_milestone";
        const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
        const Vector3 weaponRgb = Rgb(weapon.effectColor);
        fx.Flash(recipe, { .tint = weaponRgb });
        fx.Impact(recipe);
        fx.Ring(recipe, enemy_->GetPosition(), { .tint = weaponRgb });
        if (comboFinisher) {
            fx.Slash(recipe, enemy_->GetPosition(), player_->GetLastDirX(), { .tint = weaponRgb });
        }
    }

    std::vector<EnemyEntity*> chainCandidates;
    chainCandidates.reserve(weaponEnemies_.size());
    for (const auto& entry : weaponEnemies_) {
        chainCandidates.push_back(entry.enemy);
    }
    WeaponHitBonus::For(wm->GetCurrent().type)
        .Apply({ *enemy_, player_->GetComboStep(), chainCandidates, Tune().chainSkillRange });
}

void GamePlayScene::ApplyWeaponSkillStyleHit(const AABB& enemyAABB)
{
    if (!player_->JustWeaponSkillHit()) {
        return;
    }
    const auto* wm = WeaponManager::GetInstance();
    const Vector3& ppos = player_->GetPosition();
    const bool slam = player_->JustGreatswordSlam();
    const float radius = SkillRadiusFor(slam ? Tune().skillSlamRadius : Tune().skillDefaultRadius, slam);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    if (slam && HasBossSlamTechnique()) {
        // 習得したボス技: 叩きつけの衝撃波を広げて見せる（判定半径の拡大と対応）
        fx.Ring("slam_technique", ppos, { .speed = radius });
    }
    const AABB skillRange = {
        { ppos.x - radius, ppos.y - Tune().skillRangeHalfHeight, -kStageHalfDepth },
        { ppos.x + radius, ppos.y + Tune().skillRangeHalfHeight, kStageHalfDepth }
    };
    if (!Collision::CheckCollision(skillRange, enemyAABB)) {
        return;
    }
    const int techniqueId = kSkillTechniqueIdBase + static_cast<int>(wm->GetCurrent().type);
    const float varietyBonus = techniqueId == lastTechniqueId_ ? Tune().skillVarietyBonusRepeat : Tune().skillVarietyBonusFresh;
    lastTechniqueId_ = techniqueId;
    styleMeter_ = std::clamp(styleMeter_ + varietyBonus, 0.0f, 1.0f);
    ChargeAwakenGaugeByStyle(Tune().skillAwakenGaugeGain);
    const int slamBonus = (slam && HasBossSlamTechnique()) ? GameRules::GetInstance()->Get().bossTechniqueBonusDamage : 0;
    const float rawDamage = static_cast<float>((slam ? Tune().bossSlamBaseDamage : Tune().bossSkillBaseDamage) + slamBonus);
    enemy_->TakeDamage((std::max)(1, static_cast<int>(std::round(rawDamage * CurrentDamageMult()))));
    enemy_->ApplyHitstun(Tune().skillHitstunSeconds, true);
    enemy_->ApplyComboReaction(player_->GetLastDirX() * wm->GetCurrent().knockbackMult, Tune().skillKnockY,
        false, ppos.x, true);
    styleRankHud_.RegisterHit("skill_" + std::to_string(static_cast<int>(wm->GetCurrent().type)), 1.0f);

    // 固有技は通常段より大きく弾けさせる（叩きつけはさらに大きく）
    const WeaponData& weapon = wm->GetCurrent();
    EmitEnemyHitEffect(enemy_->GetPosition(), Rgba(weapon.effectColor),
        slam ? Tune().slamHitStrength : Tune().skillHitStrength,
        weapon.effectBurstCount, weapon.effectRingRadius);
    EmitElementalHitEffect(weapon.element, Rgba(weapon.effectColor), enemy_->GetPosition(), Tune().skillElementScale);

    // 固有技は通常コンボより一段大きいヒットストップ・カメラ揺れにする（叩きつけはさらに大きく）
    fx.Impact(slam ? "skill_slam" : "skill");
}

float GamePlayScene::CurrentDamageMult() const
{
    const float dodgeBonus = player_->IsJustDodgeWindowActive() ? Tune().justDodgeDamageMult : 1.0f;
    return player_->GetAwakenedDamageMult() * dodgeBonus;
}

float GamePlayScene::SkillRadiusFor(float baseRadius, bool slam) const
{
    float radius = baseRadius * player_->GetAwakenedSkillRadiusMult();
    if (slam && HasBossSlamTechnique()) {
        radius *= GameRules::GetInstance()->Get().bossTechniqueRadiusMult;
    }
    return radius;
}

int GamePlayScene::GunFatigueIndex() const
{
    return static_cast<int>(WeaponManager::GetInstance()->GetList().size());
}

int GamePlayScene::HeldMeleeFatigueIndex() const
{
    const auto* wm = WeaponManager::GetInstance();
    return wm->HasEquippedWeapon() ? wm->GetIndex() : -1;
}

float GamePlayScene::WeaponFatigueDamageMult(int fatigueIndex) const
{
    if (fatigueIndex < 0 || fatigueIndex >= static_cast<int>(weaponFatigue_.size())) {
        return 1.0f;
    }
    const float freeRatio = Tune().weaponFatigueFreeRatio;
    const float t = std::clamp((weaponFatigue_[fatigueIndex] - freeRatio) / (std::max)(1.0f - freeRatio, kMinFatigueRange),
        0.0f, 1.0f);
    return 1.0f + (Tune().weaponFatigueMinDamageMult - 1.0f) * t;
}

void GamePlayScene::RegisterFatigueHit(int fatigueIndex, float amount)
{
    if (fatigueIndex < 0) {
        return;
    }
    pendingFatigueIndex_ = fatigueIndex;
    pendingFatigueAmount_ = (std::max)(pendingFatigueAmount_, amount);
}

void GamePlayScene::UpdateWeaponFatigue()
{
    weaponFatigue_.resize(static_cast<size_t>(GunFatigueIndex()) + 1, 0.0f);
    if (pendingFatigueIndex_ >= 0 && pendingFatigueIndex_ < static_cast<int>(weaponFatigue_.size())) {
        float& fatigue = weaponFatigue_[pendingFatigueIndex_];
        fatigue = (std::min)(fatigue + pendingFatigueAmount_, 1.0f);
        lastFatigueIndex_ = pendingFatigueIndex_;
        fatigueIdleTimer_ = 0.0f;
    } else {
        fatigueIdleTimer_ += GameConstants::kFrameDeltaTime;
    }
    pendingFatigueIndex_ = -1;
    pendingFatigueAmount_ = 0.0f;

    // 最後に当てた武器以外は休ませている扱いで回復する（近接と銃を交互に使うのも持ち替えになる）
    const float recover = Tune().weaponFatigueRecoverPerSecond * GameConstants::kFrameDeltaTime;
    const bool lastWeaponResting = fatigueIdleTimer_ >= Tune().weaponFatigueRestDelay;
    for (int i = 0; i < static_cast<int>(weaponFatigue_.size()); ++i) {
        if (i != lastFatigueIndex_ || lastWeaponResting) {
            weaponFatigue_[i] = (std::max)(weaponFatigue_[i] - recover, 0.0f);
        }
    }
}

void GamePlayScene::ApplyFatigueToHitEffect(float damageMult, Vector4& color, float& strength) const
{
    // 効きが落ちるほど鈍い灰色へ寄せ、弾けも小さくする
    const Vector4& dullColor = HitEffectLibrary::GetInstance()->Color("fatigue_dull");
    const float minMult = Tune().weaponFatigueMinDamageMult;
    if (damageMult >= 1.0f || minMult >= 1.0f) {
        return;
    }
    const float dull = std::clamp((1.0f - damageMult) / (1.0f - minMult), 0.0f, 1.0f);
    color = { color.x + (dullColor.x - color.x) * dull, color.y + (dullColor.y - color.y) * dull,
        color.z + (dullColor.z - color.z) * dull, color.w };
    strength *= 1.0f + (Tune().fatigueDullStrengthMin - 1.0f) * dull;
}

float GamePlayScene::MeleeHitstunSeconds(const MeleeAttackDef* attack) const
{
    return attack != nullptr ? attack->duration + Tune().hitstunPadding : Tune().defaultHitstunSeconds;
}

void GamePlayScene::RequestMeleeHitStopAndShake(const MeleeAttackDef* attack, bool finisher, int defaultFrames)
{
    // 途中の段は揺れを控えめにして刻みを軽く、締めだけ停止も揺れも大きくして山を作る
    int hitStopFrames = attack != nullptr
        ? (attack->launcher ? GameConstants::kHitStopLaunch : attack->hitStop)
        : defaultFrames;
    if (finisher) {
        hitStopFrames = (std::max)(hitStopFrames, Tune().finisherMinHitStopFrames);
    }
    TimeManager::GetInstance()->RequestHitStop(hitStopFrames);
    const float frames = static_cast<float>(hitStopFrames);
    cameraShaker_.Request((finisher ? Tune().finisherShakePerFrame : Tune().midStepShakePerFrame) * frames,
        Tune().hitShakeDurationBase + Tune().hitShakeDurationPerFrame * frames);
}

bool GamePlayScene::HasBossSlamTechnique() const
{
    return RunData::GetInstance()->HasBossTechnique(GameRules::GetInstance()->Get().bossTechnique);
}

void GamePlayScene::ApplyGunShotStyleHit()
{
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
    float gain = Tune().gunBaseStyleGain + ((shot != nullptr) ? player_->GetGunComboStep() * Tune().gunComboStepStyleGain : 0.0f);
    styleMeter_ = std::clamp(styleMeter_ + gain, 0.0f, 1.0f);
    ChargeAwakenGaugeByStyle(Tune().gunAwakenGaugeGain);
    const float gunFatigueMult = WeaponFatigueDamageMult(GunFatigueIndex());
    enemy_->TakeDamageScaled(static_cast<float>((std::max)(1, static_cast<int>(std::round(Tune().gunBaseDamage * CurrentDamageMult()))))
        * gunFatigueMult);
    RegisterFatigueHit(GunFatigueIndex(), Tune().gunFatiguePerHit);
    // 連射で一方的にならないよう、銃は怯ませるだけで攻撃は潰さない
    enemy_->ApplyHitstun(Tune().gunHitstunSeconds, false);
    styleRankHud_.RegisterHit(shot != nullptr ? shot->id : "shot", 1.0f);

    // 銃は連射するので小さめの弾けで着弾を示す
    Vector4 gunHitColor = HitEffectLibrary::GetInstance()->Color("gun_hit");
    float gunHitStrength = Tune().gunHitStrength;
    ApplyFatigueToHitEffect(gunFatigueMult, gunHitColor, gunHitStrength);
    EmitEnemyHitEffect(enemy_->GetPosition(), gunHitColor, gunHitStrength);
    if (gunFatigueMult >= 1.0f) {
        EmitElementalHitEffect(gun.element, Rgba(gun.effectColor), enemy_->GetPosition(), Tune().gunElementScale);
    }
}

void GamePlayScene::ApplyRampageStyleHit(const AABB& enemyAABB)
{
    if (!player_->JustRampageHit()) {
        return;
    }
    const Vector3& ppos = player_->GetPosition();
    AABB rushRange = { { ppos.x - Tune().rampageRushRadiusX, ppos.y - Tune().rampageRushRadiusY, -kStageHalfDepth },
        { ppos.x + Tune().rampageRushRadiusX, ppos.y + Tune().rampageRushRadiusY, kStageHalfDepth } };
    if (!Collision::CheckCollision(rushRange, enemyAABB)) {
        return;
    }
    // 乱舞スラッシュ 回数が増えるほど多くゲージが溜まる
    styleMeter_ = std::clamp(
        styleMeter_ + Tune().rampageBaseStyleGain + player_->GetJuggleCount() * Tune().rampageJuggleStyleGain, 0.0f, 1.0f);
    enemy_->TakeDamage(Tune().rampageDamage);
    styleRankHud_.RegisterHit("rampage", 1.0f);

    // 乱舞は手数が多いので控えめな弾けで1発ごとの命中を示す
    EmitEnemyHitEffect(enemy_->GetPosition(), HitEffectLibrary::GetInstance()->Color("rampage_hit"), Tune().rampageHitStrength);
}

void GamePlayScene::DecayStyleMeter(float dt)
{
    const float decayMult = RunData::GetInstance()->HasSkill(RunData::Skill::StylePersist) ? Tune().stylePersistDecayMult : 1.0f;
    styleMeter_ = std::clamp(styleMeter_ - Tune().styleDecayRate * dt * decayMult, 0.0f, 1.0f);
    peakStyle_ = (std::max)(peakStyle_, styleMeter_);
}

void GamePlayScene::ChargeAwakenGaugeByStyle(float amount)
{
    const float rankMult = 1.0f + static_cast<float>(styleRankHud_.GetRankIndex()) * Tune().styleRankAwakenGaugeBonus;
    player_->ChargeAwakenGauge(amount * rankMult);
}

void GamePlayScene::UpdateDodgeStyle()
{
    // 回避そのものは加点しない。危険の無い場面での連打だけスタイルを削る（引きこもり回避の抑止）
    if (!player_->JustDodged()) {
        return;
    }
    const int spam = player_->GetDodgeSpamCount();
    const int startCount = Tune().dodgeSpamPenaltyStartCount;
    if (spam >= startCount) {
        styleMeter_ = std::clamp(styleMeter_ - Tune().dodgeSpamPenalty * static_cast<float>(spam - startCount + 1), 0.0f, 1.0f);
    }
}

bool GamePlayScene::TryJustDodge(const Vector3& hitPos)
{
    if (!player_->IsDodging()) {
        return false;
    }
    if (!player_->ConsumeJustDodge()) {
        return true; // 同じ回避の2発目以降は無効化だけ
    }
    styleMeter_ = std::clamp(styleMeter_ + Tune().justDodgeStyleGain, 0.0f, 1.0f);
    ChargeAwakenGaugeByStyle(Tune().justDodgeGaugeGain);
    player_->BeginJustDodgeWindow(Tune().justDodgeBonusSeconds); // 直後の攻撃を強化し、回避を攻めの起点にする
    TimeManager::GetInstance()->RequestHitStop(Tune().justDodgeHitStopFrames);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Impact("just_dodge");
    fx.Ring("just_dodge", hitPos);
    fx.Flash("just_dodge");
    return true;
}

void GamePlayScene::UpdateStyleAndUI(float dt)
{
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

void GamePlayScene::UpdateParticles(float dt)
{
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

void GamePlayScene::UpdateLandingAndJumpDustParticles()
{
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

void GamePlayScene::UpdateGhostTrail(float dt)
{
    const Vector3& ppos = player_->GetPosition();

    // 残像: 覚醒中/乱舞中の横移動 or 空中 → プレイヤーモデルのゴーストを一定間隔でスポーン
    // （Player::afterImageRenderer_ と同じ、残像=覚醒時だけの演出という前提に揃える）
    bool movingX = input_->PushAction(Input::Action::MoveLeft) || input_->PushAction(Input::Action::MoveRight);
    bool awakenActive = player_->IsRampaging();
    if (player_->IsWarping() || (awakenActive && (movingX || !player_->IsOnGround()))) {
        ghostSpawnTimer_ -= dt;
        if (ghostSpawnTimer_ <= 0.0f) {
            constexpr float kGhostSpawnInterval = 0.05f;
            ghostSpawnTimer_ = kGhostSpawnInterval;
            ghostTrail_.push_back({ ppos, 0.0f });
        }
    } else {
        ghostSpawnTimer_ = 0.0f;
    }
    for (auto& g : ghostTrail_) {
        g.age += dt;
    }
    while (!ghostTrail_.empty() && ghostTrail_.front().age >= kGhostLifetime) {
        ghostTrail_.pop_front();
    }
}

// ══════════════════════════════════════════════════════
// フィニッシャーとクリア判定
