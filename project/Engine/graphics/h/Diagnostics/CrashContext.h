/**
 * @file CrashContext.h
 * @brief クラッシュレポートへ記録する実行状況を同期して保持するファイル
 */
#pragma once

#include <mutex>
#include <string>

namespace engine {

/**
 * @brief クラッシュ発生時に参照する実行状況をスレッドセーフに管理する
 *
 * CrashHandlerから可変なグローバル状態を分離し、状態の更新と取得に必要な
 * 排他制御を一か所へ集約する。
 */
class CrashContext {
public:
    /**
     * @brief プロセス内で共有する実行状況管理を返す
     * @return 実行状況管理の参照
     */
    static CrashContext& GetInstance();

    /**
     * @brief クラッシュレポートへ記録する現在の処理名を設定する
     * @param context シーン名やロード処理名
     */
    void Set(const std::string& context);

    /**
     * @brief 現在の処理名を安全に複製して返す
     * @return クラッシュレポートへ記録する処理名
     */
    std::string Get() const;

private:
    CrashContext() = default;

    mutable std::mutex mutex_;
    std::string context_ = "Engine initialization";
};

} // namespace engine
