/**
 * @file FinisherEffects.h
 * @brief フィニッシャースラッシュ（斬り刻み→解放）の演出のうち、本編とバトルテストで共通の部分
 */
#pragma once
#include "MakeAffine.h"
#include <random>

namespace engine::graphics {
class BladeFlashEffect;
class Camera;
class GlassShatterEffect;
class SpaceDistortionEffect;
}

namespace engine::game::finisherEffects {

/** @brief 斬り刻みで出した斬撃線1本の位置と向き（演出をそろえて出すのに使う） */
struct BeatSlash {
    Vector2 center; ///< 斬撃線の中心（ワールド座標の XY）
    float angle; ///< 斬撃線の向き（ラジアン）
    float length; ///< 中心から片側の長さ
};

/**
 * @brief カメラの視界のどこかに、解放の瞬間まで残る斬撃線を1本出す
 * @param rng 位置と向きを決める乱数
 * @param cameraPos カメラの位置
 * @param lineIndex 何本目の斬撃線か（後の線ほど残す時間が短い）
 * @return 出した斬撃線の位置と向き
 */
BeatSlash SpawnBeatSlash(std::mt19937& rng, const Vector3& cameraPos, int lineIndex);

/** @brief 斬撃線の位置で、ガラス質の刃を明滅させる */
void EmitBeatBlades(engine::graphics::BladeFlashEffect& blades, const Vector3& center);

/** @brief 解放の瞬間に、刃を一斉に放ち、空間のゆがみを最大にする */
void EmitReleaseBlades(engine::graphics::BladeFlashEffect& blades, engine::graphics::SpaceDistortionEffect& warp,
    const Vector3& center);

/** @brief 溜めた斬撃線を白く光らせてから消し、center を通る太く短い斬撃線を重ねる */
void SpawnReleaseSlashes(std::mt19937& rng, const Vector3& center, const Vector3& cameraPos);

/** @brief world を画面の UV へ投影し、そこから画面が割れる演出を始める（カメラの後ろなら中心は前回のまま） */
void StartShatterAt(engine::graphics::GlassShatterEffect& shatter, const engine::graphics::Camera& camera,
    const Vector3& world);

/** @brief カメラ前方のワールド座標を画面UVへ変換する。背後の場合は出力を変更せずfalseを返す */
bool ProjectToUv(const engine::graphics::Camera& camera, const Vector3& world, Vector2& uv);

/** @brief シーン終了時に、画面割れ・空間のゆがみ・刃・斬撃の跡をまとめて片付ける */
void FinalizeAll(engine::graphics::GlassShatterEffect& shatter, engine::graphics::GlassShatterEffect& finisherShatter,
    engine::graphics::SpaceDistortionEffect& warp, engine::graphics::BladeFlashEffect& blades);

/** @brief 空間歪みの中心を対象の画面UVへ追従させる。カメラ背後では前回の中心を保つ */
void UpdateWarpCenter(engine::graphics::SpaceDistortionEffect& warp, const engine::graphics::Camera& camera,
    const Vector3& world);

} // namespace engine::game::FinisherEffects
