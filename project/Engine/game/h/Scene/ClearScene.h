/**
 * @file ClearScene.h
 * @brief 全ステージを終えた後のエンディング画面のシーンを管理するファイル
 */
#pragma once
#include "Audio.h"
#include "BaseScene.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "ResultBackdrop.h"
#include "RunData.h"
#include "SpriteCommon.h"
#include <memory>
#include <string>
#include <vector>
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::Input;
using engine::graphics::ImGuiManager;
using engine::graphics::SpriteCommon;

class UILayout;

/**
 * @brief 全ステージクリアの見出しと、ステージごとの戦績・合計スコア・ランキングを順に見せるシーンクラス
 * @note 決定キーで演出を飛ばし、すべて出た後にもう一度押すとタイトルへ戻る。
 *       時間の調整は Resources/Config/clear_scene.json、配置は Resources/Config/UI/clear.json
 */
class ClearScene : public BaseScene {
public:
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    void Finalize() override;
    void Update() override;
    void Draw() override;

    /** @brief このシーンのスコアウィンドウを ImGui で描画 */
    void DrawScoreUI();

    /** @brief 調整値をファイルから読み直す（調整値エディタで保存した時に呼ぶ） */
    static void ReloadTuning();

private:
    /** @brief ステージの戦績の行が出始める時刻（秒） */
    float RowsStartTime() const;
    /** @brief 合計スコアが出始める時刻（秒） */
    float TotalStartTime() const;
    /** @brief ランキングが出始める時刻（秒） */
    float RankingStartTime() const;
    /** @brief すべてを出し終える時刻（秒） */
    float RevealEndTime() const;

    /** @brief 全ステージクリアの見出しと、広がる線を積む */
    void QueueTitle(UILayout& layout, float time);
    /** @brief ステージごとの戦績を1行ずつ積む */
    void QueueStageRows(UILayout& layout, float time);
    /** @brief 数え上がる合計スコアと、合計時間・最高ランクを積む */
    void QueueTotal(UILayout& layout, float time);
    /** @brief ランキングと通算記録を積む */
    void QueueRanking(UILayout& layout, float time);
    /** @brief タイトルへ戻る案内を積む */
    void QueuePrompt(UILayout& layout, float time);

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    ResultBackdrop backdrop_;
    FontRenderer fontRenderer_;

    std::vector<RunData::StageRecord> stageRecords_; ///< このランのステージごとの戦績
    std::vector<int> ranking_; ///< 今回のスコアを登録した後のランキング
    int totalSeconds_ = 0; ///< 全ステージの合計時間（秒）
    std::string bestRank_; ///< このランの最高ランク（ランを経由しない時は空）

    /** @brief 表示・ランキング登録するこのランのスコア（ランを経由しない時は0） */
    int runScore_ = 0;
    bool isNewRecord_ = false; ///< 今回のスコアがランキングの1位を更新したか
    float time_ = 0.0f; ///< シーンが始まってからの時間（秒）
};

} // namespace engine::game
