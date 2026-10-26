/**
 * @file SmokeTestRunner.h
 * @brief 自動テストプレイ（--smoke-test で起動すると、手順の表のとおりにゲームを自分で遊び、落ちる・止まる所を見つける）
 * @note 手順は Resources/Config/smoke_test.json に書く（画面を待つ・キーを押す・押し続ける・ランダムに操作する・繰り返す）。
 *       操作は Input::InjectKey で本物のキーと同じ経路から流すので、ゲーム側のコードはテスト用に変えなくてよい。
 *       結果は smoke_test フォルダに書き出す。途中で落ちた場合は CrashHandler の報告に、どの手順の途中だったかが残る
 */
#pragma once
#include "json.hpp"
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace engine {
class Input;
}

namespace engine::game {

/** @brief 自動テストプレイの進行役 */
class SmokeTestRunner {
public:
    /**
     * @brief コマンド引数に --smoke-test があれば手順を読み込んで始める
     * @return 自動テストを始めたら true
     */
    bool StartIfRequested(engine::Input* input);

    /** @brief 自動テスト中か */
    bool IsRunning() const {
        return running_;
    }

    /**
     * @brief 手順を1フレーム進める（シーンの更新の後、毎フレーム呼ぶ。押したキーは次のフレームの入力から効く）
     * @return 自動テストを終えてゲームを閉じてよければ true
     */
    bool Update();

private:
    /** @brief 手順1つ（smoke_test.json の steps の1要素を、繰り返しを展開して平らにしたもの） */
    struct Step {
        std::string action; ///< waitScene / waitSceneChange / pressUntilScene / press / hold / wait / random / changeScene / mark
        std::string scene;
        std::vector<uint8_t> keys;
        std::string label; ///< 報告に出す説明
        int frames = 0; ///< 長さ（秒をフレームに直したもの）
        int timeoutFrames = 0; ///< これを過ぎたら「止まった」とみなす
        int intervalFrames = 0; ///< pressUntilScene でキーを押し直す間隔
    };

    /** @brief JSON の手順の配列を、繰り返しを展開しながら steps_ へ積む */
    void AppendSteps(const nlohmann::json& steps, int depth);
    /** @brief 今の手順を1フレーム進める（終わったら true） */
    bool UpdateStep(const Step& step);
    /** @brief 今の画面が scene で、切り替えも落ち着いているか */
    bool IsSettledOn(const std::string& scene) const;
    /** @brief 押したことにしているキーを全部離す */
    void ReleaseKeys();
    /** @brief 失敗として記録して終える */
    void Fail(const std::string& reason);
    /** @brief 結果のファイルを書き出す */
    void WriteReport(bool passed, const std::string& reason) const;

    engine::Input* input_ = nullptr;
    std::vector<Step> steps_;
    std::vector<std::string> log_; ///< 報告に出す、終えた手順の記録
    std::mt19937 rng_ { std::random_device {}() };
    size_t stepIndex_ = 0;
    int stepFrame_ = 0; ///< 今の手順を始めてからのフレーム数
    uint32_t switchCountAtStep_ = 0; ///< 今の手順を始めた時の画面の切り替え回数
    uint32_t lastSwitchCount_ = 0;
    int framesSinceSwitch_ = 0; ///< 最後に画面が切り替わってからのフレーム数（切り替え直後の不安定な間は待つ）
    int settleFrames_ = 0; ///< 画面が切り替わってから落ち着いたとみなすまでのフレーム数
    uint64_t totalFrames_ = 0;
    bool running_ = false;
    bool finished_ = false;
};

} // namespace engine::game
