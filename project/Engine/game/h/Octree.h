#pragma once
#include "CollisionConfig.h"
#include "GameObject.h"
#include <vector>
#include <utility>

/**
 * @file Octree.h
 * @brief ブロードフェーズ衝突判定用オクトツリー
 *
 * 空間を再帰的に 8 分割し、同じノードにいるオブジェクト同士のみ
 * ナローフェーズを実行することで、O(n²) から O(n log n) に計算量を削減します。
 *
 * 使い方:
 *   1. Build(worldBounds, objects) で毎フレーム再構築
 *   2. GetPotentialPairs(pairs) で衝突候補ペアを取得
 *   3. Collision::CheckCollision でナローフェーズを実行
 */
class Octree {
public:
    static constexpr int kMaxDepth   = 5; ///< 最大分割深度（深すぎると遅くなる）
    static constexpr int kMaxPerNode = 8; ///< 1 ノードに直接格納できる最大オブジェクト数

    /**
     * @brief ワールド境界と登録オブジェクトからオクトツリーを構築する
     * @param worldBounds 全オブジェクトを包む AABB（大きめに設定してください）
     * @param objects     判定対象のオブジェクト一覧
     */
    void Build(const AABB& worldBounds, const std::vector<GameObject*>& objects) {
        objects_ = objects;                 // コピー（毎フレーム差し替え可）
        nodes_.clear();
        nodes_.reserve(objects.size() * 4 + 16); // 再アロケーションを抑制

        AllocNode(worldBounds);             // index 0 = ルートノード
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            Insert(0, i, 0);
        }
    }

    /**
     * @brief 衝突可能性があるオブジェクトペアを列挙する
     * @param[out] outPairs 候補ペアを追記するベクタ（クリアはしない）
     *
     * @note 同じペアが複数回出力されることがあります（重複チェックはナロー側で行ってください）。
     */
    void GetPotentialPairs(std::vector<std::pair<GameObject*, GameObject*>>& outPairs) const {
        if (nodes_.empty()) { return; }
        std::vector<int> ancestors;
        CollectPairs(0, ancestors, outPairs);
    }

private:
    // -------- 内部データ --------

    struct Node {
        AABB            bounds;
        std::vector<int> objIndices; ///< このノードに直接所属するオブジェクトのインデックス
        int              children[8] = { -1,-1,-1,-1,-1,-1,-1,-1 };
        bool             isLeaf      = true;
        explicit Node(const AABB& b) : bounds(b) {}
    };

    std::vector<Node>        nodes_;
    std::vector<GameObject*> objects_; ///< Build 時のオブジェクト一覧コピー

    // -------- ヘルパー --------

    int AllocNode(const AABB& b) {
        nodes_.emplace_back(b);
        return static_cast<int>(nodes_.size()) - 1;
    }

    /// 親ノードの AABB を 8 等分した子 idx の AABB を返す
    static AABB ChildBounds(const AABB& parent, int idx) {
        Vector3 c = { (parent.min.x + parent.max.x) * 0.5f,
                      (parent.min.y + parent.max.y) * 0.5f,
                      (parent.min.z + parent.max.z) * 0.5f };
        return {
            { (idx & 1) ? c.x : parent.min.x,
              (idx & 2) ? c.y : parent.min.y,
              (idx & 4) ? c.z : parent.min.z },
            { (idx & 1) ? parent.max.x : c.x,
              (idx & 2) ? parent.max.y : c.y,
              (idx & 4) ? parent.max.z : c.z }
        };
    }

    static bool AABBOverlap(const AABB& a, const AABB& b) {
        return (a.min.x <= b.max.x && a.max.x >= b.min.x)
            && (a.min.y <= b.max.y && a.max.y >= b.min.y)
            && (a.min.z <= b.max.z && a.max.z >= b.min.z);
    }

    // -------- 挿入（再帰） --------

    void Insert(int nodeIdx, int objIdx, int depth) {
        AABB objBounds = objects_[objIdx]->GetCollider().GetBroadAABB();
        if (!AABBOverlap(nodes_[nodeIdx].bounds, objBounds)) { return; }

        // 葉ノードで余裕があれば直接格納
        if (nodes_[nodeIdx].isLeaf) {
            if (depth >= kMaxDepth ||
                static_cast<int>(nodes_[nodeIdx].objIndices.size()) < kMaxPerNode) {
                nodes_[nodeIdx].objIndices.push_back(objIdx);
                return;
            }

            // 分割が必要 — まず子ノードを生成
            AABB parentBounds = nodes_[nodeIdx].bounds;
            std::vector<int> existing = std::move(nodes_[nodeIdx].objIndices);
            nodes_[nodeIdx].isLeaf = false;

            // AllocNode は nodes_ を再アロケーションする可能性があるため
            // 子インデックスを全て先に確保してから nodes_[nodeIdx] に書き戻す
            int childIds[8];
            for (int c = 0; c < 8; ++c) {
                childIds[c] = AllocNode(ChildBounds(parentBounds, c));
            }
            for (int c = 0; c < 8; ++c) {
                nodes_[nodeIdx].children[c] = childIds[c];
            }

            // 既存オブジェクトを子に再配置
            for (int ei : existing) {
                for (int c = 0; c < 8; ++c) {
                    Insert(nodes_[nodeIdx].children[c], ei, depth + 1);
                }
            }
        }

        // 内部ノード: 適合する子へ挿入（複数子にまたがる場合は全子に入れる）
        bool inserted = false;
        for (int c = 0; c < 8; ++c) {
            int childIdx = nodes_[nodeIdx].children[c];
            if (childIdx == -1) { continue; }
            if (AABBOverlap(nodes_[childIdx].bounds, objBounds)) {
                Insert(childIdx, objIdx, depth + 1);
                inserted = true;
            }
        }
        // どの子にも完全に収まらなければ親ノードに留める
        if (!inserted) {
            nodes_[nodeIdx].objIndices.push_back(objIdx);
        }
    }

    // -------- ペア収集（再帰） --------

    void CollectPairs(int nodeIdx,
                      const std::vector<int>& ancestors,
                      std::vector<std::pair<GameObject*, GameObject*>>& pairs) const
    {
        const Node& node = nodes_[nodeIdx];

        // このノードのオブジェクト同士
        for (int i = 0; i < static_cast<int>(node.objIndices.size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(node.objIndices.size()); ++j) {
                pairs.emplace_back(objects_[node.objIndices[i]],
                                   objects_[node.objIndices[j]]);
            }
        }

        // 祖先ノードのオブジェクト × このノードのオブジェクト
        for (int ai : ancestors) {
            for (int bi : node.objIndices) {
                pairs.emplace_back(objects_[ai], objects_[bi]);
            }
        }

        if (!node.isLeaf) {
            // 今のノードのオブジェクトを祖先リストに追加して子を再帰処理
            std::vector<int> newAncestors = ancestors;
            newAncestors.insert(newAncestors.end(),
                                node.objIndices.begin(), node.objIndices.end());
            for (int c : node.children) {
                if (c != -1) {
                    CollectPairs(c, newAncestors, pairs);
                }
            }
        }
    }
};
