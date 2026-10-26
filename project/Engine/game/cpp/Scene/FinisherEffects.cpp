/**
 * @file FinisherEffects.cpp
 * @brief フィニッシャースラッシュの演出のうち、本編とバトルテストで共通の部分の実装
 */
#include "FinisherEffects.h"
#include "HitEffectLibrary.h"
#include "CombatTuning.h"
#include "BladeFlashEffect.h"
#include "Camera.h"
#include "GameConstants.h"
#include "GlassShatterEffect.h"
#include "SceneShared.h"
#include "SlashMark.h"
#include "SpaceDistortionEffect.h"
#include <cmath>

namespace engine::game::finisherEffects {
namespace {
// 画面割れの中心
constexpr float kMinClipW = 0.0001f; // これ以下のw成分ではスクリーン座標へ変換しない（カメラの背後）
constexpr float kNdcToUvScale = 0.5f; // NDC（-1〜1）を UV（0〜1）へ変える倍率と中心
} // namespace

/** @brief カメラの視界のどこかに、解放の瞬間まで残る斬撃線を1本出す */
BeatSlash SpawnBeatSlash(std::mt19937& rng, const Vector3& cameraPos, int lineIndex) {
    const auto& visuals = HitEffectLibrary::GetInstance()->FinisherVisuals();
    // カメラ視界全体のランダムな位置と向き・長さ・太さを決める
    std::uniform_real_distribution<float> angleDist(0.0f, gameConstants::kTwoPi);
    std::uniform_real_distribution<float> offXDist(-gameConstants::kCameraHalfW, gameConstants::kCameraHalfW);
    std::uniform_real_distribution<float> offYDist(-gameConstants::kCameraHalfH, gameConstants::kCameraHalfH);
    std::uniform_real_distribution<float> lenDist(visuals.slashLineLengthMin, visuals.slashLineLengthMax);
    std::uniform_real_distribution<float> thickDist(visuals.slashLineThicknessMin, visuals.slashLineThicknessMax);
    BeatSlash slash;
    slash.angle = angleDist(rng);
    slash.center = { cameraPos.x + offXDist(rng), cameraPos.y + offYDist(rng) };
    slash.length = lenDist(rng);
    const Vector2 dir = { std::cos(slash.angle), std::sin(slash.angle) };

    // 解放の瞬間まで全ての斬撃線を画面に残す（後の線ほど残す時間が短い）
    const float duration = (CombatTune().finisherSlashLines - 1 - lineIndex) * CombatTune().finisherLineInterval
        + CombatTune().finisherImpactDelay + visuals.slashLineLingerSeconds;
    sceneShared::SpawnSlashMarkWorld(
        { slash.center.x - dir.x * slash.length, slash.center.y - dir.y * slash.length },
        { slash.center.x + dir.x * slash.length, slash.center.y + dir.y * slash.length },
        cameraPos.x, cameraPos.y, visuals.slashLineColor, thickDist(rng), duration);
    return slash;
}

/** @brief 斬撃線の位置で、ガラス質の刃を明滅させる */
void EmitBeatBlades(engine::graphics::BladeFlashEffect& blades, const Vector3& center) {
    const auto& visuals = HitEffectLibrary::GetInstance()->FinisherVisuals();
    blades.Emit(center, visuals.beatBladeCount, visuals.beatBladeRadius, visuals.beatBladeSpeedMin, visuals.beatBladeSpeedMax);
}

/** @brief 解放の瞬間に、刃を一斉に放ち、空間のゆがみを最大にする */
void EmitReleaseBlades(engine::graphics::BladeFlashEffect& blades, engine::graphics::SpaceDistortionEffect& warp,
    const Vector3& center) {
    const auto& visuals = HitEffectLibrary::GetInstance()->FinisherVisuals();
    blades.Emit(center, visuals.releaseBladeCount, HitEffectLibrary::GetInstance()->SceneParticles().finisherSlashRadius, visuals.releaseBladeSpeedMin, visuals.releaseBladeSpeedMax);
    warp.AddImpulse(visuals.releaseWarpImpulse);
}

/** @brief 溜めた斬撃線を白く光らせてから消し、center を通る太く短い斬撃線を重ねる */
void SpawnReleaseSlashes(std::mt19937& rng, const Vector3& center, const Vector3& cameraPos) {
    const auto& visuals = HitEffectLibrary::GetInstance()->FinisherVisuals();
    SlashMark::GetInstance()->FlashAll(visuals.slashFlashColor, visuals.slashFlashSeconds);
    std::uniform_real_distribution<float> angleDist(0.0f, gameConstants::kTwoPi);
    // center を通るランダムな向きの斬撃線を、フィニッシャーの半径いっぱいに引く
    for (int i = 0; i < visuals.releaseSlashCount; ++i) {
        const float ang = angleDist(rng);
        const Vector2 dir = { std::cos(ang), std::sin(ang) };
        sceneShared::SpawnSlashMarkWorld(
            { center.x - dir.x * HitEffectLibrary::GetInstance()->SceneParticles().finisherSlashRadius,
                center.y - dir.y * HitEffectLibrary::GetInstance()->SceneParticles().finisherSlashRadius },
            { center.x + dir.x * HitEffectLibrary::GetInstance()->SceneParticles().finisherSlashRadius,
                center.y + dir.y * HitEffectLibrary::GetInstance()->SceneParticles().finisherSlashRadius },
            cameraPos.x, cameraPos.y, visuals.slashFlashColor, visuals.releaseSlashThickness, visuals.releaseSlashSeconds);
    }
}

/** @brief world を画面の UV へ投影し、そこから画面が割れる演出を始める（カメラの後ろなら中心は前回のまま） */
void StartShatterAt(engine::graphics::GlassShatterEffect& shatter, const engine::graphics::Camera& camera,
    const Vector3& world) {
    Vector2 uv;
    if (ProjectToUv(camera, world, uv)) {
        shatter.SetImpactUV(uv.x, uv.y);
    }
    shatter.Reset();
    shatter.Start();
}

/** @brief 行ベクトルの射影規約に従ってUVを求め、透視除算できない位置を除外する */
bool ProjectToUv(const engine::graphics::Camera& camera, const Vector3& world, Vector2& uv) {
    // 行ベクトル規約  clip = world * VP
    const Matrix4x4 vp = Multiply(camera.GetViewMatrix(), camera.GetProjectionMatrix());
    const float cx = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
    const float cy = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
    const float cw = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
    if (cw > kMinClipW) {
        // NDC は上が +Y、UV は下が +V なので Y だけ反転する
        uv = { cx / cw * kNdcToUvScale + kNdcToUvScale, kNdcToUvScale - cy / cw * kNdcToUvScale };
        return true;
    }
    return false;
}

/** @brief ガラス割れと共通の射影・背面判定を使って空間歪みの中心を更新する */
void UpdateWarpCenter(engine::graphics::SpaceDistortionEffect& warp, const engine::graphics::Camera& camera,
    const Vector3& world) {
    Vector2 uv;
    if (ProjectToUv(camera, world, uv)) {
        warp.SetCenterUV(uv.x, uv.y);
    }
}

/** @brief シーン終了時に、画面割れ・空間のゆがみ・刃・斬撃の跡をまとめて片付ける */
void FinalizeAll(engine::graphics::GlassShatterEffect& shatter, engine::graphics::GlassShatterEffect& finisherShatter,
    engine::graphics::SpaceDistortionEffect& warp, engine::graphics::BladeFlashEffect& blades) {
    shatter.Finalize();
    finisherShatter.Finalize();
    warp.Finalize();
    blades.Clear();
    SlashMark::GetInstance()->Clear();
}

} // namespace engine::game::FinisherEffects
