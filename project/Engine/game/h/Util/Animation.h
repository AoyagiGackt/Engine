/**
 * @file Animation.h
 * @brief glTFアニメーションの読み込み・再生・補間を管理するファイル
 *
 * 使い方:
 *   Animation anim = LoadAnimationFile("./resources/AnimatedCube", "AnimatedCube.gltf");
 *   float animTime = 0.0f;
 *   // 毎フレーム
 *   animTime = std::fmod(animTime + 1.0f/60.0f, anim.duration);
 *   NodeAnimation& nodeAnim = anim.nodeAnimations["Armature"];
 *   Vector3    t = CalculateValue(nodeAnim.translate, animTime);
 *   Quaternion r = CalculateValue(nodeAnim.rotate,    animTime);
 *   Vector3    s = CalculateValue(nodeAnim.scale,     animTime);
 *   Matrix4x4 localMatrix = MakeAffineMatrix(s, r, t);
 */
#pragma once
#include "MakeAffine.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
namespace engine::game {
// キーフレーム

/**
 * @brief 任意の型に対応する汎用キーフレーム構造体
 * @tparam tValue キーフレームの値の型（Vector3 or Quaternion）
 */
template <typename tValue>
struct Keyframe {
    float time; ///< このキーフレームの時刻（秒）
    tValue value; ///< このキーフレームの値
};

using KeyframeVector3 = Keyframe<Vector3>;
using KeyframeQuaternion = Keyframe<Quaternion>;

// アニメーションカーブ

/**
 * @brief キーフレームの列を持つアニメーションカーブ
 * @tparam tValue Vector3 または Quaternion
 */
template <typename tValue>
struct AnimationCurve {
    std::vector<Keyframe<tValue>> keyframes; ///< 時刻順に並んだキーフレームの配列
};

// ノードアニメーション・アニメーション全体

/**
 * @brief 1つのノード（ボーン）のアニメーションデータ
 */
struct NodeAnimation {
    AnimationCurve<Vector3> translate; ///< 位置のアニメーション（線形補間）
    AnimationCurve<Quaternion> rotate; ///< 回転のアニメーション（球面線形補間）
    AnimationCurve<Vector3> scale; ///< スケールのアニメーション（線形補間）
};

/**
 * @brief アニメーション全体のデータ
 */
struct Animation {
    float duration = 0.0f; ///< アニメーション全体の長さ（秒）
    std::map<std::string, NodeAnimation> nodeAnimations; ///< ノード名 → NodeAnimation
};

/** @brief モーション再生中の任意時刻で通知するゲームイベント */
struct AnimationEvent {
    float time = 0.0f; ///< 発火時刻を秒で指定する
    std::string name; ///< 攻撃判定や足音など呼び出し側が解釈する名前
};

/**
 * @brief 前回時刻から現在時刻までに通過したイベントを取得する
 * @param events 時刻順に並べたイベント一覧
 * @param previousTime 前フレームの再生時刻
 * @param currentTime 現在の再生時刻
 * @param duration アニメーション尺
 * @param loop 末尾から先頭へループした可能性があるか
 * @return 今回発火するイベントへのポインタ一覧
 */
std::vector<const AnimationEvent*> CollectAnimationEvents(
    const std::vector<AnimationEvent>& events, float previousTime,
    float currentTime, float duration, bool loop);

// ファイル読み込み

/**
 * @brief assimpを使ってglTF/FBX等からアニメーションデータを読み込む
 * @param directoryPath ファイルが置かれているフォルダのパス
 * @param filename       ファイル名（例: "AnimatedCube.gltf"）
 * @param animationName  読み込みたいアニメーション名（末尾一致、空文字なら先頭のアニメーションを使う）
 * @note 1つのファイルに複数のアニメーションが入っている場合（FBXの複数Take等）に animationName で選択する
 * @return Animation     読み込まれたアニメーションデータ
 */
Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename, const std::string& animationName = "");

// 補間計算

/**
 * @brief Vector3アニメーションカーブから指定時刻の値を線形補間で取得する
 * @param keyframes 時刻順に並んだキーフレーム配列
 * @param time      取得したい時刻（秒）
 * @return Vector3  補間された値
 */
Vector3 CalculateValue(const AnimationCurve<Vector3>& curve, float time);

/**
 * @brief Quaternionアニメーションカーブから指定時刻の値を球面線形補間で取得する
 * @param keyframes 時刻順に並んだキーフレーム配列
 * @param time      取得したい時刻（秒）
 * @return Quaternion 補間された値
 */
Quaternion CalculateValue(const AnimationCurve<Quaternion>& curve, float time);

} // namespace engine::game
