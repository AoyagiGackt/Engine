/** @file GamePlaySceneCamera.cpp
 * @brief ステージ範囲とロック対象に基づくカメラ追従
 */
#include "GamePlayScene.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "EnemyTuning.h"
#include "GameConstants.h"
#include "GameFlags.h"
#include "GameRules.h"
#include "GamePlaySceneInitializer.h"
#include "GrayscaleEffect.h"
#include "HitEffectLibrary.h"
#include "HsvFilter.h"
#include "ImGuiControl.h"
#include "ImageFilter.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <random>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr float kDefaultStageLeft = 1.5f;
constexpr float kDefaultStageRight = 36.5f;
}

/** @brief ロック操作中に画面内で最も近い敵を選ぶ */
void GamePlayScene::UpdateTargetLock() {
    // Shiftを押している間だけロックオンし、その間は常に一番近い敵を対象にし続ける
    // （BattleTestSceneのダミーロックオンと同じ規約。押した瞬間の対象に固定しない）
    if (!input_->PushAction(Input::Action::kLockOn)) {
        lockedEnemy_ = nullptr;
        return;
    }

    // 画面外の敵まで拾うとロック対象の方へカメラが大きく寄ってしまうため、
    // 画面内に映る範囲（カメラ半幅）より遠い敵はロック対象から除外する
    constexpr float kMaxLockRange = gameConstants::kCameraHalfW;

    const Vector3& pp = player_->GetPosition();
    float minDist = FLT_MAX;
    const EnemyEntity* nearest = nullptr;
    auto consider = [&](const EnemyEntity* candidate) {
        if (candidate->IsDefeated()) {
            return;
        }
        const float dist = std::abs(candidate->GetPosition().x - pp.x);
        if (dist <= kMaxLockRange && dist < minDist) {
            minDist = dist;
            nearest = candidate;
        }
    };
    consider(enemy_);
    for (const auto& weaponEnemy : weaponEnemies_) {
        consider(weaponEnemy.enemy);
    }
    lockedEnemy_ = nearest;
}

/** @brief ステージ境界と開始演出に合わせて追従カメラを更新する */
void GamePlayScene::UpdateCamera() {
    // カメラをプレイヤーに追従（境界ブロックが画面外に出ないよう clamp）。開始演出とやられ演出の間は演出が決めた位置と引きで映す
    Vector3 ppos = player_->GetPosition();
    // 引いた分だけ画面に映る範囲も広がる（透視投影なので距離に比例する）
    float zoom = 1.0f;
    float followHeight = gameConstants::kCameraFollowOffsetY;
    if (stageIntro_.IsPlaying()) {
        ppos = stageIntro_.CameraFocus();
        zoom = stageIntro_.CameraZoom();
    } else if (playerDefeat_.IsActive()) {
        ppos = playerDefeat_.CameraFocus();
        zoom = playerDefeat_.CameraZoom();
        followHeight = playerDefeat_.CameraHeight();
    }
    const float viewHalfW = gameConstants::kCameraHalfW * zoom;
    const float viewHalfH = gameConstants::kCameraHalfH * zoom;
    const StageBounds bounds = GetStageBounds();
    const float stageLeft = bounds.left;
    const float stageRight = bounds.right;
    const float stageBottom = bounds.bottom;
    const float stageTop = bounds.top;
    const float cameraMinX = stageLeft + viewHalfW;
    const float cameraMaxX = stageRight - viewHalfW;
    float cameraX = cameraMinX <= cameraMaxX
        ? std::clamp(ppos.x, cameraMinX, cameraMaxX)
        : (stageLeft + stageRight) * 0.5f;

    // ロック中はカメラをほんの少しだけ対象側へ寄せて、狙っていることに気付きやすくする
    // （BattleTestScene/sceneShared::UpdateCameraFollowと同じ控えめな比率）
    if (lockedEnemy_ != nullptr) {
        cameraX += (lockedEnemy_->GetPosition().x - ppos.x) * CombatTuning::GetInstance()->Get().lockOnCameraShiftRatio;
        if (cameraMinX <= cameraMaxX) {
            cameraX = std::clamp(cameraX, cameraMinX, cameraMaxX);
        }
    }

    // 縦も床の下・天井の上の何も無い空間が映らないよう、当たり判定ブロックの上下端で止める
    float cameraY = ppos.y + followHeight;
    const float cameraMinY = stageBottom + viewHalfH;
    const float cameraMaxY = stageTop - viewHalfH;
    if (bounds.fromLevel && cameraMinY <= cameraMaxY) {
        cameraY = std::clamp(cameraY, cameraMinY, cameraMaxY);
    }

    cameraTargetPos_ = {
        cameraX,
        cameraY,
        gameConstants::kCameraDistanceZ * zoom
    };

    UpdateCameraSmoothing();

    // カメラシェイク（スムージングの後に直接カメラ座標へ加算）
    Vector3 shake = cameraShaker_.Update(gameConstants::kFrameDeltaTime);
    if (shake.x != 0.0f || shake.y != 0.0f) {
        Vector3 cam = camera_->GetTranslate();
        camera_->SetTranslate({ cam.x + shake.x, cam.y + shake.y, cam.z });
    }
    comboFeel_.ApplyCameraZoom(camera_.get());

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
}

/** @brief 地形コライダー全体からステージの表示範囲を求める */
GamePlayScene::StageBounds GamePlayScene::GetStageBounds() {
    // 当たり判定ブロック全体を囲む範囲（ブロックが無ければ既定の広さ）
    StageBounds bounds { kDefaultStageLeft, kDefaultStageRight, 0.0f, 0.0f, false };
    const std::vector<AABB> stageSolids = GetStageEditor().GetSolidColliders();
    if (stageSolids.empty()) {
        return bounds;
    }
    bounds = { stageSolids.front().min.x, stageSolids.front().max.x, stageSolids.front().min.y, stageSolids.front().max.y, true };
    for (const AABB& solid : stageSolids) {
        bounds.left = (std::min)(bounds.left, solid.min.x);
        bounds.right = (std::max)(bounds.right, solid.max.x);
        bounds.bottom = (std::min)(bounds.bottom, solid.min.y);
        bounds.top = (std::max)(bounds.top, solid.max.y);
    }
    return bounds;
}


} // namespace engine::game
