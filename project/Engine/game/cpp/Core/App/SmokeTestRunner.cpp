/**
 * @file SmokeTestRunner.cpp
 * @brief 自動テストプレイの手順の読み込み・実行・結果の書き出しを実装するファイル
 */
#include "SmokeTestRunner.h"
#include "CrashBreadcrumbs.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace engine::game {
namespace {
constexpr const wchar_t* kCommandLineFlag = L"--smoke-test";
constexpr const char* kDefaultScriptPath = "Resources/Config/smoke_test.json";
constexpr const char* kReportDirectory = "smoke_test";
constexpr float kDefaultTimeoutSeconds = 20.0f;
constexpr float kDefaultIntervalSeconds = 0.5f;
constexpr float kDefaultSettleSeconds = 0.5f;
constexpr float kDefaultRandomHoldSeconds = 0.4f;
constexpr int kMaxRepeatDepth = 4; // 繰り返しの入れ子の深さの上限（書き間違いで無限に展開しないように）
constexpr size_t kReportPathCapacity = 256;

/** @brief 秒をフレーム数に直す（少なくとも1フレーム） */
int SecondsToFrames(float seconds) {
    return (std::max)(1, static_cast<int>(std::lround(seconds / gameConstants::kFrameDeltaTime)));
}

/** @brief 手順の説明（報告とパンくずに出す） */
std::string Describe(const nlohmann::json& step) {
    std::string text = step.value("action", std::string("?"));
    for (const char* key : { "scene", "key", "seconds", "text" }) {
        const auto found = step.find(key);
        if (found != step.end()) {
            text += std::string(" ") + key + "=" + (found->is_string() ? found->get<std::string>() : found->dump());
        }
    }
    const auto keys = step.find("keys");
    if (keys != step.end()) {
        text += " keys=" + keys->dump();
    }
    return text;
}

/** @brief テスト中は assert や abort のダイアログで止まらないようにする（止まると無人で回せないため。報告は CrashHandler が書く） */
void SuppressErrorDialogs() {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
}
} // namespace

/** @brief コマンド引数に --smoke-test があれば手順を読み込んで始める */
bool SmokeTestRunner::StartIfRequested(engine::Input* input) {
    const std::wstring commandLine = GetCommandLineW();
    if (commandLine.find(kCommandLineFlag) == std::wstring::npos) {
        return false;
    }
    input_ = input;
    const nlohmann::json script = JsonHelper::Load(kDefaultScriptPath);
    if (!script.is_object() || !script.contains("steps")) {
        Logger::LogError(std::string("[SmokeTest] 手順のファイルを読めませんでした: ") + kDefaultScriptPath);
        return false;
    }
    settleFrames_ = SecondsToFrames(script.value("settleSeconds", kDefaultSettleSeconds));
    steps_.clear();
    AppendSteps(script["steps"], 0);
    SuppressErrorDialogs();
    // NEW GAME やクリアで書き換わるので、テスト中はプレイヤーのセーブとランキングをファイルへ書かない
    SaveDataManager::GetInstance()->SetWritesEnabled(false);
    ScoreManager::GetInstance()->SetWritesEnabled(false);
    running_ = true;
    stepIndex_ = 0;
    stepFrame_ = 0;
    lastSwitchCount_ = SceneManager::GetInstance()->GetSceneSwitchCount();
    switchCountAtStep_ = lastSwitchCount_;
    Logger::LogInfo("[SmokeTest] 自動テストを始めます（手順 " + std::to_string(steps_.size()) + " 個）");
    CrashBreadcrumbs::Add("smoke", "自動テスト開始");
    return true;
}

/** @brief JSON の手順の配列を、繰り返しを展開しながら steps_ へ積む */
void SmokeTestRunner::AppendSteps(const nlohmann::json& steps, int depth) {
    if (!steps.is_array() || depth > kMaxRepeatDepth) {
        return;
    }
    for (const nlohmann::json& value : steps) {
        if (!value.is_object()) {
            continue;
        }
        const std::string action = value.value("action", std::string());
        // 繰り返しはここで展開して、実行時は平らな手順として扱う
        if (action == "repeat") {
            const int count = (std::max)(0, value.value("count", 1));
            for (int i = 0; i < count; ++i) {
                Step mark;
                mark.action = "mark";
                mark.label = "繰り返し " + std::to_string(i + 1) + "/" + std::to_string(count) + " 回目";
                steps_.push_back(mark);
                AppendSteps(value.value("steps", nlohmann::json::array()), depth + 1);
            }
            continue;
        }
        Step step;
        step.action = action;
        step.label = Describe(value);
        step.scene = value.value("scene", std::string());
        step.frames = SecondsToFrames(value.value("seconds", 0.0f));
        step.timeoutFrames = SecondsToFrames(value.value("timeout", kDefaultTimeoutSeconds));
        step.intervalFrames = SecondsToFrames(value.value("interval", kDefaultIntervalSeconds));
        std::vector<std::string> keyNames;
        if (value.contains("key") && value["key"].is_string()) {
            keyNames.push_back(value["key"].get<std::string>());
        }
        if (value.contains("keys") && value["keys"].is_array()) {
            for (const nlohmann::json& name : value["keys"]) {
                if (name.is_string()) {
                    keyNames.push_back(name.get<std::string>());
                }
            }
        }
        for (const std::string& name : keyNames) {
            const BYTE key = engine::Input::KeyFromName(name);
            // Esc はゲームを閉じてしまうので押させない
            if (key == 0 || key == DIK_ESCAPE) {
                Logger::LogWarning("[SmokeTest] 押せないキーを飛ばしました: " + name);
                continue;
            }
            step.keys.push_back(key);
        }
        if (action == "mark") {
            step.label = value.value("text", std::string("目印"));
        }
        steps_.push_back(std::move(step));
    }
}

/** @brief 手順を1フレーム進める */
bool SmokeTestRunner::Update() {
    if (!running_) {
        return false;
    }
    if (finished_) {
        return true;
    }
    ++totalFrames_;
    // 画面が切り替わったら、落ち着くまでの数え直しを始める
    const uint32_t switchCount = SceneManager::GetInstance()->GetSceneSwitchCount();
    if (switchCount != lastSwitchCount_) {
        lastSwitchCount_ = switchCount;
        framesSinceSwitch_ = 0;
    } else {
        ++framesSinceSwitch_;
    }

    if (stepIndex_ >= steps_.size()) {
        ReleaseKeys();
        WriteReport(true, "");
        finished_ = true;
        return true;
    }
    const Step& step = steps_[stepIndex_];
    if (stepFrame_ == 0) {
        switchCountAtStep_ = switchCount;
        CrashBreadcrumbs::Add("smoke", "手順 " + std::to_string(stepIndex_ + 1) + ": " + step.label);
    }
    const bool done = UpdateStep(step);
    if (finished_) {
        return true;
    }
    ++stepFrame_;
    if (done) {
        log_.push_back("OK  手順 " + std::to_string(stepIndex_ + 1) + " (" + std::to_string(stepFrame_) + " フレーム): " + step.label);
        ReleaseKeys();
        ++stepIndex_;
        stepFrame_ = 0;
    }
    return false;
}

/** @brief 今の手順を1フレーム進める（終わったら true） */
bool SmokeTestRunner::UpdateStep(const Step& step) {
    SceneManager* scenes = SceneManager::GetInstance();
    const auto timedOut = [&]() {
        if (stepFrame_ < step.timeoutFrames) {
            return false;
        }
        Fail("止まりました: " + step.label + "（" + std::to_string(step.timeoutFrames) + " フレーム待っても進まない。今の画面は "
            + scenes->GetCurrentSceneName() + "）");
        return true;
    };

    if (step.action == "waitScene") {
        if (IsSettledOn(step.scene)) {
            return true;
        }
        timedOut();
        return false;
    }
    if (step.action == "waitSceneChange") {
        const bool changed = scenes->GetSceneSwitchCount() != switchCountAtStep_
            && framesSinceSwitch_ >= settleFrames_ && !scenes->IsChangingScene();
        if (changed) {
            return true;
        }
        timedOut();
        return false;
    }
    if (step.action == "pressUntilScene") {
        if (IsSettledOn(step.scene)) {
            return true;
        }
        // 間隔ごとに1フレームだけ押して、押した瞬間の判定を毎回起こす
        const bool press = stepFrame_ % step.intervalFrames == 0;
        for (const uint8_t key : step.keys) {
            input_->InjectKey(key, press);
        }
        timedOut();
        return false;
    }
    if (step.action == "press") {
        // 1フレーム目に押して2フレーム目に離す（押した瞬間の判定が1回だけ起きる）
        for (const uint8_t key : step.keys) {
            input_->InjectKey(key, stepFrame_ == 0);
        }
        return stepFrame_ >= 1;
    }
    if (step.action == "hold") {
        for (const uint8_t key : step.keys) {
            input_->InjectKey(key, true);
        }
        return stepFrame_ + 1 >= step.frames;
    }
    if (step.action == "random") {
        // 一定間隔で、候補のキーからいくつかを選んで押し直す（同時押しや連打の組み合わせを試す）
        const int holdFrames = SecondsToFrames(kDefaultRandomHoldSeconds);
        if (stepFrame_ % holdFrames == 0 && !step.keys.empty()) {
            std::bernoulli_distribution pressed(0.35);
            for (const uint8_t key : step.keys) {
                input_->InjectKey(key, pressed(rng_));
            }
        }
        return stepFrame_ + 1 >= step.frames;
    }
    if (step.action == "wait") {
        return stepFrame_ + 1 >= step.frames;
    }
    if (step.action == "changeScene") {
        scenes->ChangeScene(step.scene);
        return true;
    }
    if (step.action == "mark") {
        Logger::LogInfo("[SmokeTest] " + step.label);
        return true;
    }
    Fail("知らない手順です: " + step.label + "（smoke_test.json の action を確認してください）");
    return false;
}

/** @brief 今の画面が scene で、切り替えも落ち着いているか */
bool SmokeTestRunner::IsSettledOn(const std::string& scene) const {
    const SceneManager* scenes = SceneManager::GetInstance();
    return scenes->GetCurrentSceneName() == scene && !scenes->IsChangingScene() && framesSinceSwitch_ >= settleFrames_;
}

/** @brief 押したことにしているキーを全部離す */
void SmokeTestRunner::ReleaseKeys() {
    if (input_ != nullptr) {
        input_->ClearInjectedKeys();
    }
}

/** @brief 失敗として記録して終える */
void SmokeTestRunner::Fail(const std::string& reason) {
    ReleaseKeys();
    log_.push_back("NG  手順 " + std::to_string(stepIndex_ + 1) + ": " + reason);
    WriteReport(false, reason);
    finished_ = true;
}

/** @brief 結果のファイルを書き出す */
void SmokeTestRunner::WriteReport(bool passed, const std::string& reason) const {
    const std::string headline = passed ? "[SmokeTest] 成功: 全部の手順を終えました" : "[SmokeTest] 失敗: " + reason;
    if (passed) {
        Logger::LogInfo(headline);
    } else {
        Logger::LogError(headline);
    }
    CrashBreadcrumbs::Add("smoke", passed ? "自動テスト成功" : "自動テスト失敗");

    std::error_code error;
    std::filesystem::create_directories(kReportDirectory, error);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[kReportPathCapacity];
    std::snprintf(path, sizeof(path), "%s/result_%04d%02d%02d_%02d%02d%02d.txt", kReportDirectory,
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::ofstream report(path);
    if (!report) {
        return;
    }
    report << (passed ? "成功" : "失敗") << '\n';
    if (!passed) {
        report << "理由: " << reason << '\n';
        report << "（F5・F9 のデバッグボタンを使う手順は、game_rules.json の debugClearButton / debugGameOverButton が true でないと進みません）\n";
    }
    report << "かかったフレーム数: " << totalFrames_ << "（約 " << static_cast<int>(static_cast<float>(totalFrames_) * gameConstants::kFrameDeltaTime) << " 秒）\n";
    report << "--- 手順の記録 ---\n";
    for (const std::string& line : log_) {
        report << line << '\n';
    }
    if (!passed) {
        report << "--- 直前の出来事（古い順） ---\n";
        CrashBreadcrumbs::ForEach([&](const CrashBreadcrumbs::Entry& entry) {
            report << "[frame " << entry.frame << "] " << entry.category << ": " << entry.text << '\n';
        });
    }
    Logger::LogInfo(std::string("[SmokeTest] 結果を書き出しました: ") + path);
}

} // namespace engine::game
