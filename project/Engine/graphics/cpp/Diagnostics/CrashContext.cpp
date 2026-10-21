/**
 * @file CrashContext.cpp
 * @brief クラッシュレポート用の実行状況を同期して管理する
 */
#include "CrashContext.h"

namespace engine {

/** @brief プロセス内で共有する実行状況管理を返す */
CrashContext& CrashContext::GetInstance()
{
    // 関数ローカルstaticを利用し、初回アクセス時の生成をC++ランタイムへ委ねる
    static CrashContext instance;
    return instance;
}

/** @brief クラッシュレポートへ記録する現在の処理名を設定する */
void CrashContext::Set(const std::string& context)
{
    // 更新中に例外処理スレッドから不完全な文字列を参照しないよう排他制御する
    std::scoped_lock lock(mutex_);
    context_ = context;
}

/** @brief 現在の処理名を安全に複製して返す */
std::string CrashContext::Get() const
{
    // ロック保持期間を短くするため、呼び出し元へ値の複製を返す
    std::scoped_lock lock(mutex_);
    return context_;
}

} // namespace engine
