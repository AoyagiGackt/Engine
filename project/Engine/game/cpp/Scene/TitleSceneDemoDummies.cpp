/** @file TitleSceneDemoDummies.cpp
 * @brief タイトルの自動戦闘デモの的（復帰・接近と攻撃・体の押し出し・見た目の反映）
 * @note 分割ファイル。クラスは TitleScene のまま、的の処理だけをここに置く
 */
#include "TitleScene.h"
#include "GameConstants.h"
#include "TitleDemoTuning.h"
#include <algorithm>
#include <cmath>
#include <random>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr Vector4 kDummyNormalColor = { 1.0f, 1.0f, 1.0f, 1.0f };

/** @brief x が bodyX から gap 未満なら、今いる側へ gap の距離まで押し出した位置を返す */
float KeepOutOfBody(float x, float bodyX, float gap) {
    const float delta = x - bodyX;
    if (std::abs(delta) >= gap) {
        return x;
    }
    return bodyX + (delta >= 0.0f ? gap : -gap);
}
} // namespace

/** @brief 倒された的のうち復帰時間を過ぎたものを、プレイヤーの左右どちらかに出し直す */
void TitleScene::RespawnDefeatedDummies(float arenaLeft, float arenaRight) {
    const TitleDemoTuning& tune = TitleDemoTune();
    // 復帰ごとに位置・速さ・攻撃までの時間をばらつかせ、同じ動きの繰り返しに見せない
    std::uniform_real_distribution<float> respawnDistanceDist(tune.respawnDistanceMin, tune.respawnDistanceMax);
    std::uniform_real_distribution<float> moveSpeedDist(tune.respawnMoveSpeedMin, tune.respawnMoveSpeedMax);
    std::uniform_real_distribution<float> attackDelayDist(tune.respawnAttackDelayMin, tune.respawnAttackDelayMax);
    std::uniform_real_distribution<float> phaseDist(0.0f, gameConstants::kTwoPi);
    std::bernoulli_distribution sideDist(std::clamp(tune.respawnRightChance, 0.0f, 1.0f));
    for (auto& dummy : demoDummies_) {
        // 倒されていて、復帰の待ち時間が終わった的だけを出し直す
        if (!dummy.defeated) {
            continue;
        }
        dummy.respawnTimer -= gameConstants::kFrameDeltaTime;
        if (dummy.respawnTimer > 0.0f) {
            continue;
        }
        // プレイヤーの左右どちらかに、舞台からはみ出さない位置で地上に出す
        // 端に寄っている時は中央側に出して、次の的を追ううちに中央へ戻るようにする
        const float offsetFromCenter = player_->GetPosition().x - (arenaLeft + arenaRight) * 0.5f;
        const float side = std::abs(offsetFromCenter) > tune.respawnCenterBiasDistance
            ? (offsetFromCenter > 0.0f ? -1.0f : 1.0f)
            : (sideDist(demoRng_) ? 1.0f : -1.0f);
        dummy.x = std::clamp(player_->GetPosition().x + side * respawnDistanceDist(demoRng_), arenaLeft, arenaRight);
        dummy.airborne = false;
        dummy.baseY = tune.groundY + tune.dummyFootOffsetY;
        dummy.currentY = dummy.baseY;
        dummy.bobPhase = phaseDist(demoRng_);
        dummy.moveSpeed = moveSpeedDist(demoRng_);
        dummy.attackCooldown = attackDelayDist(demoRng_);
        // 撃破前の攻撃・被弾の途中状態を消す
        dummy.attackAnimTimer = 0.0f;
        dummy.lungeOffset = 0.0f;
        dummy.hitFlashTimer = 0.0f;
        dummy.defeated = false;
        // 見た目を通常の色・大きさに戻して、新しい位置へ置く
        dummy.object->SetColor(kDummyNormalColor);
        dummy.object->SetScale({ tune.dummyScale, tune.dummyScale, tune.dummyScale });
        dummy.object->SetPosition({ dummy.x, dummy.currentY, 0.0f });
        dummy.object->Update();
    }
}

/** @brief 生きている的をそれぞれの速度でプレイヤーへ近づけ、間合いに入ったら攻撃動作を始めさせる */
void TitleScene::UpdateDummyBehavior(float playerX) {
    const TitleDemoTuning& tune = TitleDemoTune();
    const float attackAnimSeconds = (std::max)(tune.dummyAttackAnimSeconds, gameConstants::kFrameDeltaTime);
    const int distanceVariants = (std::max)(tune.dummyPreferredDistanceVariants, 1);
    // 各敵は独立して行動する。それぞれの速度でプレイヤーに近づき、
    // 少しずつ異なる距離で止まって、個別の待機時間で攻撃する。
    for (size_t i = 0; i < demoDummies_.size(); ++i) {
        auto& dummy = demoDummies_[i];
        if (dummy.defeated) {
            continue;
        }
        // 止まる距離は的の番号ごとに数段階ずらす（全員が同じ位置に重ならないように）
        const float delta = playerX - dummy.x;
        const float distance = std::abs(delta);
        const float preferredDistance = tune.dummyPreferredDistanceBase
            + static_cast<float>(static_cast<int>(i) % distanceVariants) * tune.dummyPreferredDistanceStep;
        dummy.attackCooldown = (std::max)(0.0f,
            dummy.attackCooldown - gameConstants::kFrameDeltaTime);
        dummy.attackAnimTimer = (std::max)(0.0f,
            dummy.attackAnimTimer - gameConstants::kFrameDeltaTime);
        // 遠ければ近づき、止まる距離まで来たら待ち時間が明けしだい攻撃を始める
        if (distance > preferredDistance && dummy.attackAnimTimer <= 0.0f) {
            const float direction = delta >= 0.0f ? 1.0f : -1.0f;
            dummy.x += direction * dummy.moveSpeed * gameConstants::kFrameDeltaTime;
            dummy.x = std::clamp(dummy.x, tune.stageMinX + tune.arenaEdgeMargin, tune.stageMaxX - tune.arenaEdgeMargin);
        } else if (dummy.attackCooldown <= 0.0f) {
            dummy.attackAnimTimer = attackAnimSeconds;
            std::uniform_real_distribution<float> nextAttackDist(tune.dummyNextAttackMin, tune.dummyNextAttackMax);
            dummy.attackCooldown = nextAttackDist(demoRng_);
        }
        // 攻撃動作中はプレイヤー側へ半周期の sin で踏み込んで戻る
        const float attackPhase = 1.0f - dummy.attackAnimTimer / attackAnimSeconds;
        const float lungeDirection = delta >= 0.0f ? 1.0f : -1.0f;
        dummy.lungeOffset = dummy.attackAnimTimer > 0.0f
            ? std::sin(std::clamp(attackPhase, 0.0f, 1.0f) * gameConstants::kPi) * tune.dummyLungeDistance * lungeDirection
            : 0.0f;
    }
}

/** @brief 的を押し出して、的どうしとプレイヤーとの体の重なりをなくす（プレイヤーは動かさない） */
void TitleScene::SeparateDummies() {
    const TitleDemoTuning& tune = TitleDemoTune();
    const Vector3 playerPosition = player_->GetPosition();
    // 地上の的どうしは、プレイヤーから遠い方を外へ押す（押した先でプレイヤーに重ならないように）
    for (size_t i = 0; i < demoDummies_.size(); ++i) {
        for (size_t j = i + 1; j < demoDummies_.size(); ++j) {
            DemoDummy& first = demoDummies_[i];
            DemoDummy& second = demoDummies_[j];
            if (first.defeated || second.defeated || first.airborne || second.airborne) {
                continue;
            }
            const bool firstIsNearer = std::abs(first.x - playerPosition.x) <= std::abs(second.x - playerPosition.x);
            DemoDummy& nearer = firstIsNearer ? first : second;
            DemoDummy& farther = firstIsNearer ? second : first;
            // 画面の外へは押し出さない（少し重なる方が、見えなくなるよりよい）
            farther.x = std::clamp(KeepOutOfBody(farther.x, nearer.x, tune.dummySpacing), tune.arenaMinX, tune.arenaMaxX);
        }
    }
    // プレイヤーとの重なりは最後に解消する（プレイヤーを押すと端へ追いやられるので、的の方を動かす）
    for (DemoDummy& dummy : demoDummies_) {
        if (!dummy.defeated && std::abs(dummy.currentY - playerPosition.y) < tune.dummyBodyHeight) {
            dummy.x = KeepOutOfBody(dummy.x, playerPosition.x, tune.dummyBodyGap);
        }
    }
}

/** @brief 的の上下の揺れ・被弾時の点滅・向き・位置をモデルへ反映する */
void TitleScene::UpdateDummyVisuals() {
    const TitleDemoTuning& tune = TitleDemoTune();
    for (auto& dummy : demoDummies_) {
        dummy.hitFlashTimer = (std::max)(dummy.hitFlashTimer - gameConstants::kFrameDeltaTime, 0.0f);
        if (dummy.defeated) {
            continue;
        }
        // 空中の的だけ sin で上下に揺らす
        if (dummy.airborne) {
            dummy.bobPhase += gameConstants::kFrameDeltaTime * dummy.bobSpeed;
            dummy.currentY = dummy.baseY + std::sin(dummy.bobPhase) * tune.dummyBobAmplitude;
        } else {
            dummy.currentY = dummy.baseY;
        }
        // 被弾直後は色を変えて少し縮め、当たったことを見せる
        const bool flashing = dummy.hitFlashTimer > 0.0f;
        dummy.object->SetColor(flashing ? tune.dummyFlashColor : kDummyNormalColor);
        const float shrink = flashing ? tune.dummyFlashShrink : 1.0f;
        dummy.object->SetScale({ tune.dummyScale * shrink, tune.dummyScale * shrink, tune.dummyScale * shrink });
        // プレイヤーのいる側を向かせ、攻撃の踏み込みぶんずらして置く（踏み込んでも体には重ならない）
        const float facingYaw = player_->GetPosition().x >= dummy.x
            ? 0.0f
            : gameConstants::kPi;
        dummy.object->SetRotation({ 0.0f, facingYaw, 0.0f });
        const Vector3 playerPosition = player_->GetPosition();
        const float lungedX = dummy.x + dummy.lungeOffset;
        const float visualX = std::abs(dummy.currentY - playerPosition.y) < tune.dummyBodyHeight
            ? KeepOutOfBody(lungedX, playerPosition.x, tune.dummyBodyGap)
            : lungedX;
        dummy.object->SetPosition({ visualX, dummy.currentY, 0.0f });
        dummy.object->Update();
    }
}

} // namespace engine::game
