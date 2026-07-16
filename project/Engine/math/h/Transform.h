/**
 * @file Transform.h
 * @brief オブジェクトのトランスフォーム情報（拡縮・回転・座標）を定義するファイル
 */
#pragma once
#include "Vector3.h"

namespace engine {

/** @brief オブジェクトのトランスフォーム情報をまとめた構造体 */
struct Transform {
    Vector3 scale; ///< 拡大縮小
    Vector3 rotate; ///< 回転（ラジアン）
    Vector3 translate; ///< 座標
};

} // namespace engine
