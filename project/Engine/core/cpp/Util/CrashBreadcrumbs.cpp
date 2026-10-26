/**
 * @file CrashBreadcrumbs.cpp
 * @brief 直前の出来事の記録（固定の箱を輪のように使い回す）を実装するファイル
 */
#include "CrashBreadcrumbs.h"
#include <algorithm>
#include <array>
#include <atomic>

namespace engine {
namespace {
// 記録の箱。プログラムの開始時に確保済みなので、落ちた時に読んでもヒープを触らない
std::array<CrashBreadcrumbs::Entry, CrashBreadcrumbs::kCapacity> sEntries {};
std::atomic<uint64_t> sCount { 0 }; // これまでに記録した件数（次に書く通し番号）
std::atomic<uint64_t> sFrame { 0 }; // 今のフレーム番号

/** @brief 終端付きで、入る分だけ文字をコピーする */
template <size_t N>
void CopyTruncated(char (&destination)[N], std::string_view source) {
    const size_t length = (std::min)(source.size(), N - 1);
    std::copy_n(source.data(), length, destination);
    destination[length] = '\0';
}
} // namespace

/** @brief 出来事を1件記録する */
void CrashBreadcrumbs::Add(std::string_view category, std::string_view text) {
    // 通し番号を先に取り、その番号の箱へ書く（古い記録は輪のように上書きされる）
    const uint64_t index = sCount.fetch_add(1, std::memory_order_relaxed);
    Entry& entry = sEntries[index % kCapacity];
    entry.frame = sFrame.load(std::memory_order_relaxed);
    CopyTruncated(entry.category, category);
    CopyTruncated(entry.text, text);
}

/** @brief フレームを1つ進める */
void CrashBreadcrumbs::NextFrame() {
    sFrame.fetch_add(1, std::memory_order_relaxed);
}

/** @brief 今のフレーム番号を返す */
uint64_t CrashBreadcrumbs::Frame() {
    return sFrame.load(std::memory_order_relaxed);
}

/** @brief これまでに記録した件数を返す */
uint64_t CrashBreadcrumbs::Count() {
    return sCount.load(std::memory_order_relaxed);
}

/** @brief 通し番号 index の記録を返す */
const CrashBreadcrumbs::Entry& CrashBreadcrumbs::At(uint64_t index) {
    return sEntries[index % kCapacity];
}

} // namespace engine
