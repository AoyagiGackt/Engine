/** @file EnemySeparation.cpp
 * @brief 敵の壁接触・敵どうし・プレイヤーとの重なりに基づく位置補正
 */
#include "EnemySeparation.h"
#include "IEnemyEntity.h"
#include "EnemyTuning.h"
#include <algorithm>
#include <cmath>
namespace engine::game {
namespace {
/** @brief 敵の体のうち壁と当たる範囲（足元の床に掛からないよう、位置より少し上から頭まで） */
engine::AABB EnemyWallBody(const Vector3& pos, const EnemySeparationTuning& tuning) {
    return { { pos.x - tuning.bodyHalfWidth, pos.y + tuning.wallBandBottom, pos.z },
        { pos.x + tuning.bodyHalfWidth, pos.y + tuning.wallBandTop, pos.z } };
}

/** @brief 体がブロックと横に重なっているか（奥行きは敵の位置の1点で見る） */
bool OverlapsWall(const engine::AABB& body, const engine::AABB& wall) {
    return body.max.y > wall.min.y && body.min.y < wall.max.y
        && body.min.z >= wall.min.z && body.max.z <= wall.max.z
        && body.max.x > wall.min.x && body.min.x < wall.max.x;
}

/** @brief 壁にめり込んでいれば、入り込みの浅い側へ押し出した後のX座標を返す */
float PushOutOfWalls(const Vector3& pos, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning) {
    Vector3 resolved = pos;
    for (const engine::AABB& wall : walls) {
        const engine::AABB body = EnemyWallBody(resolved, tuning);
        if (!OverlapsWall(body, wall)) {
            continue;
        }
        const float pushLeft = wall.min.x - body.max.x; // 負の値: 左へ押し出す量
        const float pushRight = wall.max.x - body.min.x; // 正の値: 右へ押し出す量
        resolved.x += (std::abs(pushLeft) < std::abs(pushRight)) ? pushLeft : pushRight;
    }
    return resolved.x;
}

/** @brief 指定した向き（左=-1, 右=+1）のすぐ横に壁があるか（その向きへは押されても動けない） */
bool WallOnSide(const Vector3& pos, float side, const std::vector<engine::AABB>& walls, const EnemySeparationTuning& tuning) {
    // 体のその向きの端だけを細い帯で調べる（反対側の壁に触れていても誤判定しない）
    const float margin = tuning.wallContactMargin;
    engine::AABB probe = EnemyWallBody(pos, tuning);
    const float edgeX = side < 0.0f ? probe.min.x : probe.max.x;
    probe.min.x = edgeX - margin;
    probe.max.x = edgeX + margin;
    return std::any_of(walls.begin(), walls.end(), [&probe](const engine::AABB& wall) {
        return OverlapsWall(probe, wall);
    });
}
} // namespace
/** @brief 壁・敵どうし・プレイヤーとの重なりを解消し、見た目を更新した敵を返す */
std::vector<IEnemyEntity*> EnemySeparation::Resolve(const std::vector<Target>& targets, const std::vector<AABB>& walls,
    const std::optional<Vector3>& playerPosition) {
    // 押し出しの強さは 0〜1 に丸める（1で一度に重なりを全部解く）
    const EnemySeparationTuning& tuning = EnemyTuning::GetInstance()->Separation();
    const float pushRate = std::clamp(tuning.pushRate, 0.0f, 1.0f);

    /** @brief 押し離し対象の敵と壁への接触状態 */
    struct Body {
        IEnemyEntity* enemy;
        bool anchored; ///< 押されても動かない（ボス）
        bool wallLeft = false; ///< 左に壁が接していて左へは動けない
        bool wallRight = false; ///< 右に壁が接していて右へは動けない
        bool moved = false;
        /** @brief 指定した向き（左=-1, 右=+1）に壁が無く、横へずれられるか（固定かどうかは見ない） */
        bool CanSlide(float side) const {
            return !(side < 0.0f ? wallLeft : wallRight);
        }
        /** @brief 指定した向き（左=-1, 右=+1）へほかの敵に押された時に動けるか */
        bool CanMove(float side) const {
            return !anchored && CanSlide(side);
        }
    };
    // 敵ごとに、動かせるか・左右に壁があるかを持つ
    std::vector<Body> bodies;
    for (const auto& [enemy, anchored] : targets) {
        bodies.push_back({ enemy, anchored });
    }
    std::vector<IEnemyEntity*> moved;
    // 自分の移動で壁にめり込んだ敵を先に押し戻し、壁際の敵は壁の方へは押されないようにする
    constexpr float kLeft = -1.0f;
    constexpr float kRight = 1.0f;
    for (Body& body : bodies) {
        Vector3 pos = body.enemy->GetPosition();
        const float resolvedX = PushOutOfWalls(pos, walls, tuning);
        body.moved = resolvedX != pos.x;
        pos.x = resolvedX;
        body.enemy->SetPosition(pos);
        body.wallLeft = WallOnSide(pos, kLeft, walls, tuning);
        body.wallRight = WallOnSide(pos, kRight, walls, tuning);
    }

    // 近すぎる2体の組ごとに、横へ押し離す
    for (size_t i = 0; i < bodies.size(); ++i) {
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            Body& a = bodies[i];
            Body& b = bodies[j];
            Vector3 posA = a.enemy->GetPosition();
            Vector3 posB = b.enemy->GetPosition();
            const float dx = posB.x - posA.x;
            const float distanceX = std::abs(dx);
            if (distanceX >= tuning.minDistanceX || std::abs(posB.y - posA.y) >= tuning.minDistanceY) {
                continue;
            }
            // 真上に重なっている時は並び順で向きを決め、2体が同じ側へ逃げないようにする
            const float direction = distanceX > 0.0f ? (dx > 0.0f ? 1.0f : -1.0f) : 1.0f;
            const float push = (tuning.minDistanceX - distanceX) * pushRate;
            // A は -direction、B は +direction へずれる。片方が動けない時はもう片方が全部ずれ、両方動ける時は半分ずつ
            const bool canMoveA = a.CanMove(-direction);
            const bool canMoveB = b.CanMove(direction);
            if (!canMoveA && !canMoveB) {
                continue;
            }
            const float shareA = !canMoveB ? 1.0f : (!canMoveA ? 0.0f : 0.5f);
            const float shareB = 1.0f - shareA;
            posA.x -= direction * push * shareA;
            posB.x += direction * push * shareB;
            a.enemy->SetPosition(posA);
            b.enemy->SetPosition(posB);
            a.moved = a.moved || shareA > 0.0f;
            b.moved = b.moved || shareB > 0.0f;
        }
    }

    // プレイヤーに近すぎる敵を、プレイヤーと反対側へ離す（プレイヤーはボスにも押されないよう、ボスも離す）
    // 壁際で離れられない時は、プレイヤー側の押し出し（Player::ResolveEnemyOverlap）に任せる
    if (playerPosition) {
        const float playerPushRate = std::clamp(tuning.playerPushRate, 0.0f, 1.0f);
        for (Body& body : bodies) {
            Vector3 pos = body.enemy->GetPosition();
            const float dx = pos.x - playerPosition->x;
            const float distanceX = std::abs(dx);
            if (distanceX >= tuning.playerMinDistanceX || std::abs(pos.y - playerPosition->y) >= tuning.playerMinDistanceY) {
                continue;
            }
            // 真上に重なっている時は右へ逃がす
            const float direction = dx < 0.0f ? kLeft : kRight;
            if (!body.CanSlide(direction)) {
                continue;
            }
            pos.x += direction * (tuning.playerMinDistanceX - distanceX) * playerPushRate;
            body.enemy->SetPosition(pos);
            body.moved = true;
        }
    }

    // 動いた敵だけを後始末して、呼び出し側へ返す
    for (Body& body : bodies) {
        if (!body.moved) {
            continue;
        }
        // 押し離しで壁へ入った分も戻してから見た目へ反映する
        Vector3 pos = body.enemy->GetPosition();
        pos.x = PushOutOfWalls(pos, walls, tuning);
        body.enemy->SetPosition(pos);
        body.enemy->RefreshVisualTransforms();
        moved.push_back(body.enemy);
    }
    return moved;
}
}
