/**
 * @file CrashContext.cpp
 * @brief クラッシュレポート用の実行状況を同期して管理する
 */
#include "CrashContext.h"

namespace engine {

CrashContext& CrashContext::GetInstance()
{
    // 関数ローカルstaticを利用し、初回アクセス時の生成をC++ランタイムへ委ねる
    static CrashContext instance;
    return instance;
}

void CrashContext::Set(const std::string& context)
{
    // 更新中に例外処理スレッドから不完全な文字列を参照しないよう排他制御する
    std::scoped_lock lock(mutex_);
    context_ = context;
}

std::string CrashContext::Get() const
{
    // ロック保持期間を短くするため、呼び出し元へ値の複製を返す
    std::scoped_lock lock(mutex_);
    return context_;
}

} // namespace engine
