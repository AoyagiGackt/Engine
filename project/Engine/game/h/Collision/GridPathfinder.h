#pragma once
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
#include <functional>
namespace engine {
// 2D グリッドベース A* 経路探索
class GridPathfinder {
public:
    struct Cell { int x, y; };

    void Resize(int w, int h) {
        w_ = w; h_ = h;
        passable_.assign(w * h, true);
    }

    void SetPassable(int x, int y, bool v) {
        if (InBounds(x, y)) { passable_[Idx(x, y)] = v; }
    }

    bool IsPassable(int x, int y) const {
        return InBounds(x, y) && passable_[Idx(x, y)];
    }

    // A* 探索。戻り値: 終点に至るまでのグリッドセルリスト（始点は含まず）
    std::vector<Cell> FindPath(int sx, int sy, int gx, int gy) const {
        if (!InBounds(sx, sy) || !InBounds(gx, gy)) { return {}; }
        if (!IsPassable(sx, sy) || !IsPassable(gx, gy)) { return {}; }
        if (sx == gx && sy == gy) { return {}; }

        struct Node {
            int x, y;
            float g, f;
            bool operator>(const Node& o) const { return f > o.f; }
        };

        std::vector<float> gCost(w_ * h_, 1e9f);
        std::vector<int>   parent(w_ * h_, -1);
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

        gCost[Idx(sx, sy)] = 0.0f;
        open.push({ sx, sy, 0.0f, Heuristic(sx, sy, gx, gy) });

        // 8方向（斜め移動コスト √2 ≈ 1.414）
        static constexpr int dx[] = { 1,-1, 0, 0, 1,-1, 1,-1 };
        static constexpr int dy[] = { 0, 0, 1,-1, 1,-1,-1, 1 };
        static constexpr float dc[] = { 1,1,1,1, 1.414f,1.414f,1.414f,1.414f };

        while (!open.empty()) {
            Node cur = open.top(); open.pop();
            if (cur.x == gx && cur.y == gy) { break; }
            if (cur.g > gCost[Idx(cur.x, cur.y)]) { continue; } // 古いノード

            for (int d = 0; d < 8; ++d) {
                int nx = cur.x + dx[d], ny = cur.y + dy[d];
                if (!IsPassable(nx, ny)) { continue; }
                // 斜め移動の場合は両隣が通れることを確認
                if (d >= 4 && (!IsPassable(cur.x + dx[d], cur.y) ||
                                !IsPassable(cur.x, cur.y + dy[d]))) { continue; }
                float ng = cur.g + dc[d];
                if (ng < gCost[Idx(nx, ny)]) {
                    gCost[Idx(nx, ny)]  = ng;
                    parent[Idx(nx, ny)] = Idx(cur.x, cur.y);
                    open.push({ nx, ny, ng, ng + Heuristic(nx, ny, gx, gy) });
                }
            }
        }

        // パス再構築
        if (parent[Idx(gx, gy)] == -1 && !(sx == gx && sy == gy)) { return {}; }
        std::vector<Cell> path;
        int cur = Idx(gx, gy);
        int start = Idx(sx, sy);
        while (cur != start && cur != -1) {
            path.push_back({ cur % w_, cur / w_ });
            cur = parent[cur];
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    int Width()  const { return w_; }
    int Height() const { return h_; }

private:
    int  w_ = 0, h_ = 0;
    std::vector<bool> passable_;

    bool InBounds(int x, int y) const { return x >= 0 && x < w_ && y >= 0 && y < h_; }
    int  Idx(int x, int y)      const { return y * w_ + x; }

    // オクタイル距離（8方向移動の admissible heuristic）
    static float Heuristic(int ax, int ay, int bx, int by) {
        float dx = float(std::abs(ax - bx));
        float dy = float(std::abs(ay - by));
        return (dx + dy) + (1.414f - 2.0f) * std::min(dx, dy);
    }
};

} // namespace engine
