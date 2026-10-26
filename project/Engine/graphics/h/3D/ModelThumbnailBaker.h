/**
 * @file ModelThumbnailBaker.h
 * @brief CPUによるモデル縮小画像の生成とキャッシュ
 */
#pragma once
#include <cstdint>
#include <deque>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::graphics {
/** @brief 生成予約を別スレッドで1件ずつ処理する縮小画像生成器 */
class ModelThumbnailBaker {
public:
    /** @brief 未登録なら予約し、完成済みのテクスチャIDまたは0を返す */
    uint64_t RequestThumbnail(const std::string& model, const std::string& texture);
    /** @brief 1フレームに1回呼び、作り終えた1件をGPUへ送って次の1件を別スレッドで作り始める */
    void Update();
private:
    /** @brief 作る予定の1件 */
    struct Request {
        std::string model;
        std::string texture;
        std::string key; ///< テクスチャの登録名（キャッシュのキーも兼ねる）
    };
    /** @brief 別スレッドで作り終えた1件 */
    struct Result {
        std::string key; ///< テクスチャの登録名
        bool succeeded = false; ///< 画像を作れたか
        std::vector<uint8_t> pixels; ///< RGBA8の画素
    };
    std::deque<Request> pending_; ///< まだ作っていない予約
    std::future<Result> inFlight_; ///< 別スレッドで作っている途中の1件
    std::unordered_map<std::string, uint64_t> cache_; ///< 登録名 → ImGui に渡すテクスチャID（作れなかったものは0のまま残す）
};
} // namespace engine::graphics
