/**
 * @file GamePlaySceneFinalBoss.cpp
 * @brief GamePlayScene のうち、ラスボス戦（FinalBossBattle）へのつなぎ（用意・演出中の更新・闘技場のカメラ・ボスの攻撃の受け口）
 * @note 分割ファイル。クラスは GamePlayScene のまま、ラスボス戦まわりの定義だけをここに置く
 */
#include "GamePlayScene.h"
#include "CrashBreadcrumbs.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "RunData.h"
#include "SceneFlow.h"
#include "SlashMark.h"
#include "StageEditor.h"

namespace engine::game {

/** @brief ラスボス戦のレベルを開いているか */
bool GamePlayScene::IsFinalBossMode() const {
    return RunData::GetInstance()->IsFinalBossPending();
}

/** @brief ラスボス戦の進行役を用意する（Initialize の最後に、ラスボス戦の時だけ） */
void GamePlayScene::InitializeFinalBoss() {
    if (!IsFinalBossMode()) {
        return;
    }
    FinalBossBattle::Links links;
    links.modelCommon = modelCommon_.get();
    links.spriteCommon = spriteCommon_.get();
    links.particles = pm_;
    links.font = &fontRenderer_;
    links.input = input_;
    links.player = player_.get();
    links.shaker = &cameraShaker_;
    links.rng = &rng_;
    links.strikePlayer = [this](const AABB& box, const Vector3& effectPosition, int damage) {
        return StrikePlayerFromBoss(box, effectPosition, damage);
    };
    finalBoss_.Initialize(links);
    CrashBreadcrumbs::Add("game", "ラスボス戦を用意した");
}

/** @brief 演出の途中でプレイヤーの操作を止めている間の1フレーム（止めていなければ false） */
bool GamePlayScene::UpdateFinalBossCinematic() {
    if (!finalBoss_.IsActive() || !finalBoss_.BlocksGameplay()) {
        return false;
    }
    // 敵と仕掛けも止め、HUD の文字は出さないので文字コマンドの破棄だけ行う
    GetStageEditor().SetGameplayHeld(true);
    fontRenderer_.Reset();
    finalBoss_.UpdateCinematic();
    UpdateCamera();
    player_->RefreshVisualTransforms();
    SlashMark::GetInstance()->Update(TimeManager::GetInstance()->GetDeltaTime());
    return true;
}

/** @brief 闘技場のカメラにする（やられ演出の間は横からのカメラに戻す。使ったら true） */
bool GamePlayScene::UpdateArenaCamera() {
    if (!finalBoss_.UsesArenaCamera() || playerDefeat_.IsActive()) {
        return false;
    }
    finalBoss_.ApplyCamera(*camera_);
    // カメラ揺れは決めた位置へ直接足す
    const Vector3 shake = cameraShaker_.Update(gameConstants::kFrameDeltaTime);
    if (shake.x != 0.0f || shake.y != 0.0f) {
        const Vector3 cam = camera_->GetTranslate();
        camera_->SetTranslate({ cam.x + shake.x, cam.y + shake.y, cam.z });
    }
    // 空も闘技場と同じだけ回す（回り込んだ時に景色が流れて見えるように）
    skydome_->SetRotationOffsetY(skyRotOffsetY_ + finalBoss_.GetSkyYaw());
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    return true;
}

/** @brief ラスボスの攻撃の判定をプレイヤーへ当てる（当たったら true。回避中・無敵中は当たらない） */
bool GamePlayScene::StrikePlayerFromBoss(const AABB& box, const Vector3& effectPosition, int damage) {
    if (!Collision::CheckCollision(player_->GetCollider().aabb, box)) {
        return false;
    }
    // 回避で体を抜けたらジャスト回避、無敵中は素通り
    if (TryJustDodge(effectPosition) || player_->IsInvincible()) {
        return false;
    }
    RunData::GetInstance()->TakeDamage(damage);
    player_->OnHit();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Impact("enemy_swing");
    fx.Sparks("player_hit", player_->GetPosition());
    return true;
}

/** @brief 確認用にラスボス戦を始める（今のシーンを、ラスボス戦のレベルで開き直す） */
void GamePlayScene::StartDebugFinalBoss() {
    CrashBreadcrumbs::Add("game", "デバッグ: ラスボス戦を始める");
    RunData::GetInstance()->SetFinalBossPending(true);
    SceneFlow::GetInstance()->Transition(kSceneName, "final_boss", "GAMEPLAY");
}

} // namespace engine::game
