/** @file StageEditorObjectPresets.h
 * @brief 新規配置メニューの見た目と座標の初期値を外部設定から適用する */
#pragma once
#include "LevelLoader.h"
#include "JsonHelper.h"
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

namespace engine::game::stageEditorObjectPresets {
inline constexpr const char* kConfigPath = "Resources/Config/editor_object_presets.json";

namespace detail {
/** @brief floatで表現できる有限数だけを読み、不正な値では出力を変更しない */
inline bool ReadFiniteFloat(const nlohmann::json& value, float& result) {
    if (!value.is_number()) {
        return false;
    }
    const double number = value.get<double>();
    const double limit = (std::numeric_limits<float>::max)();
    if (!std::isfinite(number) || number < -limit || number > limit) {
        return false;
    }
    result = static_cast<float>(number);
    return true;
}
}

/**
 * @brief プリセットの見た目と指定された座標成分を適用する。不正な設定では配置データを変更しない
 * @note 名前・種類・有効化フラグは呼び出し側が管理する。座標の未指定成分は現在のビュー中心を保つ
 */
inline bool TryApply(const nlohmann::json& document, const char* key, ObjectDesc& desc) {
    if (!document.is_object()) {
        return false;
    }
    const auto found = document.find(key);
    if (found == document.end() || !found->is_object()) {
        return false;
    }
    const auto& preset = *found;
    if (!preset.contains("model") || !preset["model"].is_string() || preset["model"].get_ref<const std::string&>().empty()
        || !preset.contains("texture") || !preset["texture"].is_string()
        || !preset.contains("lighting") || !preset["lighting"].is_boolean()
        || !preset.contains("solid") || !preset["solid"].is_boolean()) {
        return false;
    }

    // 全項目の検証が終わるまで、呼び出し側の配置データには反映しない。
    ObjectDesc candidate = desc;
    candidate.model = preset["model"].get<std::string>();
    candidate.texture = preset["texture"].get<std::string>();
    candidate.lighting = preset["lighting"].get<bool>();
    candidate.solid = preset["solid"].get<bool>();
    float* scale[] = { &candidate.scale.x, &candidate.scale.y, &candidate.scale.z };
    const auto size = preset.find("scale");
    if (size == preset.end() || !size->is_array() || size->size() != std::size(scale)) {
        return false;
    }
    for (std::size_t axis = 0; axis < std::size(scale); ++axis) {
        if (!detail::ReadFiniteFloat((*size)[axis], *scale[axis]) || *scale[axis] <= 0.0f) {
            return false;
        }
    }
    if (const auto collider = preset.find("meshCollider"); collider != preset.end()) {
        if (!collider->is_boolean()) {
            return false;
        }
        candidate.meshCollider = collider->get<bool>();
    }
    if (const auto position = preset.find("position"); position != preset.end()) {
        if (!position->is_object()) {
            return false;
        }
        const char* names[] = { "x", "y", "z" };
        float* components[] = { &candidate.position.x, &candidate.position.y, &candidate.position.z };
        for (std::size_t axis = 0; axis < std::size(names); ++axis) {
            const auto value = position->find(names[axis]);
            if (value != position->end() && !detail::ReadFiniteFloat(*value, *components[axis])) {
                return false;
            }
        }
    }
    desc = std::move(candidate);
    return true;
}

/** @brief 設定ファイルを初回だけ読み、配置メニューで指定されたプリセットを適用する */
inline bool TryApply(const char* key, ObjectDesc& desc) {
    static const auto document = engine::JsonHelper::Load(kConfigPath);
    return TryApply(document, key, desc);
}
}
