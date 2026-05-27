#pragma once
#include "Collision.h"
#include "GameObject.h"
#include "Octree.h"
#include <list>
#include <vector>
#include <utility>

/**
 * @brief 当たり判定を管理するクラス
 *
 * ■ 2 つの判定モードを提供します
 *
 * [1] 明示ペア登録方式（少数の特定ペア向け）
 *   AddCollisionPair(a, b) → UpdateAllCollisions()
 *   登録したペアのみ判定。形状（AABB / Sphere / Capsule）を自動ディスパッチ。
 *
 * [2] Octree ブロードフェーズ方式（多数オブジェクト向け）
 *   SetWorldBounds(bounds)
 *   AddObject(obj) × N
 *   UpdateBroadPhase()
 *   空間を 8 分木で分割して候補ペアを絞り込み、ナローフェーズを実行。
 */
class CollisionManager {
public:
    // ===========================================================
    // [1] 明示ペア登録方式
    // ===========================================================

    /**
     * @brief 判定したいペアを登録する
     * @param objA 物体A
     * @param objB 物体B
     */
    void AddCollisionPair(GameObject* objA, GameObject* objB) {
        checkPairs_.emplace_back(objA, objB);
    }

    /**
     * @brief 登録された全ペアを判定し isHit を更新する
     * AABB / Sphere / Capsule を Collider::shape に応じて自動選択します。
     */
    void UpdateAllCollisions() {
        // まず全フラグをリセット
        for (auto& [a, b] : checkPairs_) {
            a->GetCollider().isHit = false;
            b->GetCollider().isHit = false;
        }
        // ナローフェーズ（形状ディスパッチ）
        for (auto& [a, b] : checkPairs_) {
            if (Collision::CheckCollision(a->GetCollider(), b->GetCollider())) {
                a->GetCollider().isHit = true;
                b->GetCollider().isHit = true;
            }
        }
    }

    /// 登録ペアを全削除（シーン遷移時などに呼ぶ）
    void ClearPairs() { checkPairs_.clear(); }

    // ===========================================================
    // [2] Octree ブロードフェーズ方式
    // ===========================================================

    /**
     * @brief Octreeがカバーする空間範囲を設定する（デフォルト: ±100m）
     * @param bounds 全オブジェクトを包む AABB。余裕をもって設定してください。
     */
    void SetWorldBounds(const AABB& bounds) { worldBounds_ = bounds; }

    /**
     * @brief ブロードフェーズ判定に参加させるオブジェクトを登録する
     * @param obj 登録するオブジェクト（毎フレーム AddObject → UpdateBroadPhase の流れで使う）
     */
    void AddObject(GameObject* obj) { broadObjects_.push_back(obj); }

    /**
     * @brief Octree を使ったブロードフェーズ＋ナローフェーズ判定を実行する
     *
     * 処理の流れ:
     *   1. 全オブジェクトの isHit をリセット
     *   2. Octree を構築して空間的に近いペアを絞り込む
     *   3. 候補ペアのみナローフェーズ（Collision::CheckCollision）を実行
     *
     * @note AddCollisionPair 方式とは独立しています。
     *       毎フレーム AddObject でオブジェクトを登録し直してから呼んでください。
     */
    void UpdateBroadPhase() {
        // フラグリセット
        for (auto* obj : broadObjects_) {
            obj->GetCollider().isHit = false;
        }

        // Octree 構築
        octree_.Build(worldBounds_, broadObjects_);

        // 衝突候補ペアを収集
        candidatePairs_.clear();
        octree_.GetPotentialPairs(candidatePairs_);

        // ナローフェーズ（形状ディスパッチ）
        for (auto& [a, b] : candidatePairs_) {
            if (Collision::CheckCollision(a->GetCollider(), b->GetCollider())) {
                a->GetCollider().isHit = true;
                b->GetCollider().isHit = true;
            }
        }
    }

    /// 登録オブジェクトを全削除（シーン遷移時などに呼ぶ）
    void ClearObjects() {
        broadObjects_.clear();
        candidatePairs_.clear();
    }

    // ===========================================================
    // レイキャスト
    // ===========================================================

    /**
     * @brief broadObjects_ に登録されているオブジェクト全体に対してレイキャストを実行する
     *
     * 最も近いヒットを result に格納します。
     *
     * @param ray        テストするレイ（direction は正規化済みであること）
     * @param[out] result  ヒット情報（最も近いもの）
     * @param[out] hitObject ヒットしたオブジェクト（ヒットなしなら nullptr）
     * @return ヒットした場合 true
     *
     * 使い方:
     *   Ray ray{ cameraPos, Normalize(cameraForward) };
     *   RaycastResult res;
     *   GameObject* hitObj = nullptr;
     *   if (collisionManager.Raycast(ray, res, &hitObj)) {
     *       // res.point が衝突点, hitObj が当たったオブジェクト
     *   }
     */
    bool Raycast(const Ray& ray, RaycastResult& result, GameObject** hitObject = nullptr) const
    {
        return RaycastAgainst(ray, broadObjects_, result, hitObject);
    }

    /**
     * @brief 任意のオブジェクトリストに対してレイキャストを実行する（静的ユーティリティ）
     *
     * CollisionManager を使わず、自分でリストを渡したい場合に使います。
     *
     * @param ray        テストするレイ
     * @param objects    テスト対象のオブジェクト一覧
     * @param[out] result  ヒット情報
     * @param[out] hitObject ヒットしたオブジェクト
     * @return ヒットした場合 true
     */
    static bool RaycastAgainst(const Ray& ray,
                                const std::vector<GameObject*>& objects,
                                RaycastResult& result,
                                GameObject** hitObject = nullptr)
    {
        result = {};
        GameObject* best = nullptr;
        float bestDist = 1e30f;

        for (auto* obj : objects) {
            RaycastResult tmp;
            if (Collision::Raycast(ray, obj->GetCollider(), tmp)) {
                if (tmp.distance < bestDist) {
                    bestDist = tmp.distance;
                    result   = tmp;
                    best     = obj;
                }
            }
        }

        if (hitObject) { *hitObject = best; }
        return best != nullptr;
    }

private:
    // --- 明示ペア方式 ---
    std::list<std::pair<GameObject*, GameObject*>> checkPairs_;

    // --- Octree 方式 ---
    std::vector<GameObject*>                         broadObjects_;
    AABB                                             worldBounds_  = { {-100,-100,-100}, {100,100,100} };
    Octree                                           octree_;
    std::vector<std::pair<GameObject*, GameObject*>> candidatePairs_;
};
