/**
 * @file GamePlaySceneClear.cpp
 * @brief GamePlayScene のうち、クリアの決め演出の開始・クリア後の結果の反映・次の画面への切り替えを受け持つ部分
 * @note 分割ファイル。クラスは GamePlayScene のまま、クリアまわりの定義だけをここに置く
 */
#include "GamePlayScene.h"
#include "CrashBreadcrumbs.h"
#include "GameConstants.h"
#include "GameRules.h"
#include "PlaytestLog.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneFlow.h"
#include "StageEditor.h"

namespace engine::game {
namespace {
// 確認用のクリアで、次に銃の演出を見せるか（剣と銃を交互に見せる。シーンが作り直されても順番を覚えておく）
bool sNextDebugClearByGun = false;
}

/** @brief クリアの決め演出を始める（ボスを倒した最後の一撃が銃なら弾痕、それ以外なら斬線） */
void GamePlayScene::StartClearFinish() {
    CrashBreadcrumbs::Add("game", std::string("クリアの決め演出を開始（") + (bossFinishedByGun_ ? "銃" : "斬撃") + "）");
    // ラン中は結果画面を決めた秒数だけ見せ、サンドボックスは割れ落ちたらすぐクリア画面へ
    ClearFinish::Setup setup;
    setup.style = bossFinishedByGun_ ? ClearFinish::Style::kGun : ClearFinish::Style::kBlade;
    setup.resultSeconds = IsSandboxClear() ? 0.0f : GameRules::GetInstance()->Get().resultDisplaySeconds;
    clearFinish_.Start(setup, { &glassShatter_, pm_, &cameraShaker_, &rng_ });
    // 結果画面の背景は取ったランクの色で塗る（ラン外のクリアはランクが無いので既定の色）
    resultBackdrop_.Start(IsSandboxClear() ? ResultBackdrop::NeutralColor() : StyleMeter::ColorForRank(RunData::CalcRank(peakStyle_)));
    // 決めから割れて集まり直すまでは無音にし、結果画面が出た所でクリアの曲を流す
    audio_->StopBGM();
    GetStageEditor().SetGameplayHeld(true);
}

/** @brief クリア後の処理を進める（クリアしていなければ false） */
bool GamePlayScene::UpdateClearState() {
    // クリアしていなければ何もせず、通常の更新を続けさせる
    if (!clearTriggered_) {
        return false;
    }
    // 世界を止めたまま決め演出だけを進める（HUDの文字は出さないので、文字コマンドの破棄だけ行う）
    fontRenderer_.Reset();
    GetStageEditor().SetGameplayHeld(true);
    auto* rd = RunData::GetInstance();
    const GameRulesData& rules = GameRules::GetInstance()->Get();
    const bool runResult = !IsSandboxClear();
    // ラン中は結果（ランク・ゴールド・スコア）を最初の1回だけ求める（確認用の再生では反映しない）
    if (runResult && !showResult_) {
        showResult_ = true;
        RecordRunClear(!glassShatterDebugTest_);
    }
    // 決め → 画面が割れ落ちる → 結果画面、を見せ終えたら次へ（ラン中は次のフロアか最終クリア、確認用はフロアを進めずにマップ、サンドボックスはクリア画面）
    clearFinish_.Update();
    resultBackdrop_.Update(gameConstants::kFrameDeltaTime);
    if (clearFinish_.ConsumeResultShown()) {
        audio_->PlaySceneBGM(bgmKey::kGameClear, false);
    }
    if (!clearFinish_.ConsumeFinished()) {
        return true;
    }
    if (!runResult) {
        SceneFlow::GetInstance()->Transition(kSceneName, "sandbox_clear", "CLEAR");
    } else if (glassShatterDebugTest_) {
        SceneFlow::GetInstance()->Transition(kSceneName, "next", "MAP");
    } else if (rd->GetFloor() >= rules.finalFloor) {
        SceneFlow::GetInstance()->Transition(kSceneName, "clear", "CLEAR");
    } else {
        SceneFlow::GetInstance()->Transition(kSceneName, "next", "MAP");
    }
    return true;
}

/** @brief ラン外（エディタのテストプレイなど）のクリアか。結果画面は CLEAR! だけにして、終わったらクリア画面へ */
bool GamePlayScene::IsSandboxClear() const {
    return !RunData::GetInstance()->IsRunActive();
}

/** @brief ラン中のクリアの結果（ランク・ゴールド・スコア）を求め、apply なら進行とセーブにも反映する */
void GamePlayScene::RecordRunClear(bool apply) {
    // 結果画面に出す値は確認用の再生でも求める
    lastGold_ = RunData::CalcGold(peakStyle_);
    lastScore_ = GameRules::GetInstance()->ScoreForRank(RunData::CalcRank(peakStyle_));
    if (!apply) {
        return;
    }
    auto* rd = RunData::GetInstance();
    rd->AddGold(lastGold_);
    rd->AddScore(lastScore_);
    PlaytestLog::GetInstance()->RecordRunResult(true, rd->GetPlayFloor(), floorElapsedSeconds_,
        peakStyle_, styleRankHud_.GetBestChain(), player_->GetPosition());
    // クリア済みのステージを遊び直した時は、報酬だけ受け取って進行度と解放演出はそのまま
    if (!rd->IsReplayingClearedStage()) {
        rd->AdvanceFloor();
        CrashBreadcrumbs::Add("game", "フロアを進めた（次は " + std::to_string(rd->GetFloor()) + "）");
    }
    // フロアクリア毎に自動セーブ（最終フロア到達時はコンティニュー不要なので破棄）
    if (rd->GetFloor() >= GameRules::GetInstance()->Get().finalFloor) {
        SaveDataManager::GetInstance()->ClearContinue();
    } else {
        SaveDataManager::GetInstance()->SaveContinue(*rd);
    }
}

/** @brief クリアの決め演出を確認用に始める（剣と銃を交互に見せる。結果画面は見せるが、ランの進行やセーブには反映せず、終わったらマップへ戻る） */
void GamePlayScene::TriggerGlassShatterTest() {
    StartDebugClear(true);
}

/** @brief デバッグ用にその場でクリアする（普通にクリアした時と同じく、進行度を進めてマップで解放演出を見せる） */
void GamePlayScene::TriggerDebugClear() {
    StartDebugClear(false);
}

/** @brief ボスを倒さずにクリアの決め演出を始める（剣と銃を交互に見せる。previewOnly なら進行とセーブに反映しない） */
void GamePlayScene::StartDebugClear(bool previewOnly) {
    CrashBreadcrumbs::Add("game", previewOnly ? "デバッグ: クリア演出の確認" : "デバッグ: すぐクリア");
    if (clearTriggered_) {
        return;
    }
    // 押すたびに剣 → 銃 → 剣 … と切り替え、両方の見え方を確かめられるようにする
    bossFinishedByGun_ = sNextDebugClearByGun;
    sNextDebugClearByGun = !sNextDebugClearByGun;
    glassShatterDebugTest_ = previewOnly;
    clearTriggered_ = true;
    StartClearFinish();
}

} // namespace engine::game
