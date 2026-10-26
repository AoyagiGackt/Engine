/**
 * @file CrashBreadcrumbs.h
 * @brief 落ちる直前に何をしていたか（押したキー・画面の切り替え・ゲームの出来事）を最後の数十件だけ覚えておく記録
 * @note 落ちた時に CrashHandler がこの記録をクラッシュの報告へ書き出す。
 *       例外の中から読むので、記録はヒープを使わない固定の箱に入れ、ロックも取らない
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace engine {

/** @brief 直前の出来事の記録（パンくず） */
class CrashBreadcrumbs {
public:
    static constexpr size_t kCapacity = 64; ///< 覚えておく件数（古いものから上書きする）
    static constexpr size_t kCategorySize = 16; ///< 種類の名前の最大の長さ（終端を含む）
    static constexpr size_t kTextSize = 120; ///< 内容の最大の長さ（終端を含む。長いものは切る）

    /** @brief 1件の記録 */
    struct Entry {
        uint64_t frame; ///< 何フレーム目の出来事か
        char category[kCategorySize]; ///< 種類（key / pad / scene / game など）
        char text[kTextSize]; ///< 内容
    };

    /**
     * @brief 出来事を1件記録する
     * @param category 種類（key / pad / scene / game など）
     * @param text     内容（長すぎる分は切る）
     */
    static void Add(std::string_view category, std::string_view text);

    /** @brief フレームを1つ進める（毎フレームの最初に1回呼ぶ） */
    static void NextFrame();

    /** @brief 今のフレーム番号を返す */
    static uint64_t Frame();

    /**
     * @brief 覚えている記録を古い順に渡す（例外の中からも呼べる。ヒープを使わない）
     * @param visit 1件ごとに呼ぶ処理
     */
    template <class Visit>
    static void ForEach(const Visit& visit) {
        const uint64_t end = Count();
        const uint64_t begin = end > kCapacity ? end - kCapacity : 0;
        for (uint64_t i = begin; i < end; ++i) {
            visit(At(i));
        }
    }

private:
    /** @brief これまでに記録した件数を返す */
    static uint64_t Count();
    /** @brief 通し番号 index の記録を返す */
    static const Entry& At(uint64_t index);
};

} // namespace engine
