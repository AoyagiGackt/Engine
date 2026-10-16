#pragma once
#include "JsonHelper.h"
#include "Vector4.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::graphics {

// 描画演出の設定を初回参照時に読み込む。変更は次回起動時に反映する。
class GraphicsTuning {
public:
    static float Positive(const char* key, float fallback)
    {
        return Float(key, fallback, std::numeric_limits<float>::epsilon());
    }

    static int Int(const char* key, int fallback, int minimum, int maximum)
    {
        const auto& root = Document();
        const auto it = root.find(key);
        if (it == root.end() || !it->is_number_integer()) {
            return fallback;
        }
        const double value = it->get<double>();
        return static_cast<int>(std::clamp(value, static_cast<double>(minimum), static_cast<double>(maximum)));
    }

    static float Float(const char* key, float fallback,
        float minimum = 0.0f, float maximum = (std::numeric_limits<float>::max)())
    {
        const auto& root = Document();
        const auto it = root.find(key);
        if (it == root.end() || !it->is_number()) {
            return fallback;
        }
        const double value = it->get<double>();
        if (!std::isfinite(value)) {
            return fallback;
        }
        return static_cast<float>(std::clamp(value, static_cast<double>(minimum), static_cast<double>(maximum)));
    }

    static engine::Vector4 Color(const char* key, const engine::Vector4& fallback)
    {
        const auto& root = Document();
        const auto it = root.find(key);
        if (it == root.end() || !it->is_array() || it->size() != kColorComponents) {
            return fallback;
        }
        float values[kColorComponents] = {};
        for (size_t i = 0; i < kColorComponents; ++i) {
            if (!(*it)[i].is_number()) {
                return fallback;
            }
            const double value = (*it)[i].get<double>();
            if (!std::isfinite(value)) {
                return fallback;
            }
            values[i] = static_cast<float>(std::clamp(value, 0.0, 1.0));
        }
        return { values[0], values[1], values[2], values[3] };
    }

private:
    static constexpr size_t kColorComponents = 4;

    static const nlohmann::json& Document()
    {
        static const auto root = engine::JsonHelper::Load("Resources/Config/graphics_tuning.json");
        return root;
    }
};

} // namespace engine::graphics
