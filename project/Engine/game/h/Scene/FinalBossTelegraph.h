/**
 * @file FinalBossTelegraph.h
 * @brief ラスボスの攻撃の予兆（床に出す当たる範囲の円と、突進の通り道の帯）
 * @note 毎フレーム BeginFrame で全部消し、技がその時に出したい印だけを Circle / Line で置く
 */
#pragma once
#include "Object3d.h"
#include "Vector4.h"
#include <memory>
#include <vector>

namespace engine::graphics {
class ModelCommon;
}

namespace engine::game {

/** @brief 床に出す攻撃の予兆の印（円と帯）。印の実体は最初に全部作って使い回す */
class FinalBossTelegraph {
public:
    /** @brief 印のモデルを読み込み、使い回す実体を作る */
    void Initialize(engine::graphics::ModelCommon* modelCommon);
    /** @brief このフレームの印を全部消す（技を進める前に呼ぶ） */
    void BeginFrame();
    /** @brief 当たる範囲の円を床に置く（x はシーンの X、radius は半径） */
    void Circle(float x, float floorY, float radius, const Vector4& color);
    /** @brief 通り道の帯を床に置く（fromX〜toX、width は太さ） */
    void Line(float fromX, float toX, float floorY, float width, const Vector4& color);
    /** @brief 置いた印を描く（3D の描画の途中で呼ぶ） */
    void Draw();

private:
    /** @brief 印1つ（使っていない時は描かない） */
    struct Mark {
        std::unique_ptr<engine::graphics::Object3d> object;
        bool active = false;
    };
    /** @brief 床に寝かせた印を置く */
    static void Place(Mark& mark, const Vector3& center, float halfWidth, float halfDepth, const Vector4& color);

    std::vector<Mark> circles_;
    Mark line_;
    size_t usedCircles_ = 0; ///< このフレームに使った円の数
};

} // namespace engine::game
