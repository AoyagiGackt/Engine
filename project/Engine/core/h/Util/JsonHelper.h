/**
 * @file JsonHelper.h
 * @brief nlohmann::jsonを使ったJSONファイルの読み込み・保存ユーティリティ
 */
#pragma once
#include "json.hpp"
#include "Vector4.h"
#include <string>
#include <cstddef>
#include <cmath>
#include <limits>

// JSON ファイルの読み書きユーティリティ
namespace engine {
/** @brief JSONファイルの読み込みと書き込みを提供する */
class JsonHelper {
public:
    /**
     * @brief 指定節のレコードを記載順にIDで検索し、一致した値または未知IDをコールバックへ渡す
     * @note 対応表は変更せず、同じIDの複数レコードも順番どおり渡す。複数対象への適用は呼び出し側が行う
     */
    template<class Targets, class Apply, class Unknown>
    static void VisitNamedOverrides(const nlohmann::json& root, const char* section,
        const Targets& targets, const Apply& apply, const Unknown& unknown) {
        for (const auto& record : root.value(section, nlohmann::json::array())) {
            const std::string id = record.value("id", "");
            const auto found = targets.find(id);
            if (found == targets.end()) {
                unknown(id);
                continue;
            }
            apply(found->second, record);
        }
    }

    /** @brief RGBAの4要素配列を読み、要素数が違う場合は既定色を返す */
    static Vector4 ReadColor(const nlohmann::json& value, const Vector4& fallback) {
        constexpr std::size_t kColorChannels = 4;
        if (!value.is_array() || value.size() != kColorChannels) {
            return fallback;
        }
        float channels[kColorChannels] = {};
        for (std::size_t i = 0; i < kColorChannels; ++i) {
            if (!value[i].is_number()) {
                return fallback;
            }
            const double channel = value[i].get<double>();
            const double limit = (std::numeric_limits<float>::max)();
            if (!std::isfinite(channel) || channel < -limit || channel > limit) {
                return fallback;
            }
            channels[i] = static_cast<float>(channel);
        }
        return { channels[0], channels[1], channels[2], channels[3] };
    }

    // ファイルから JSON を読み込む（ファイルが存在しない・破損の場合は空オブジェクトを返す）
    /**
     * @brief JSONファイルを読み込んでパースする
     * @param path 読み込むファイルのパス
     * @return パース結果ファイルが無い・パースに失敗した場合は空オブジェクト
     */
    static nlohmann::json Load(const std::string& path);

    // JSON をファイルへ保存する（親ディレクトリがなければ作成する）
    /**
     * @brief JSONをファイルへ書き出す（親ディレクトリが無ければ作成する）
     * @param path 書き出し先のファイルパス
     * @param j 書き出すJSONデータ
     * @param indent インデントの空白数（0以下ならnlohmann::jsonのdump()の仕様に従う）
     */
    static bool Save(const std::string& path, const nlohmann::json& j, int indent = 2);
};
} // namespace engine
