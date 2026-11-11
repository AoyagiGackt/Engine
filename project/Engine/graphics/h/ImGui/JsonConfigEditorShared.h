/**
 * @file JsonConfigEditorShared.h
 * @brief 調整値エディタ（JsonConfigEditor）の本体と見た目の補助で共有する色・寸法と、保存時の値を引く関数
 */
#pragma once
#ifdef USE_IMGUI
#include "json.hpp"
#include <imgui.h>
#include <string>

namespace engine::graphics::jsonConfigEditorShared {

constexpr float kLabelWidthRatio = 0.45f; ///< 項目名に使う横幅の割合
constexpr ImVec4 kLiveColor = { 0.45f, 0.9f, 0.5f, 1.0f }; ///< すぐ反映される・オンの色
constexpr ImVec4 kNextBootColor = { 0.75f, 0.75f, 0.75f, 1.0f }; ///< 次回起動時に反映・オフの色
constexpr ImVec4 kModifiedColor = { 1.0f, 0.78f, 0.3f, 1.0f }; ///< 保存時から変えた項目の名前の色
constexpr ImU32 kModifiedBarColor = IM_COL32(255, 190, 70, 255); ///< 変えた項目の左端に引く線の色
constexpr ImU32 kStripeColor = IM_COL32(255, 255, 255, 12); ///< 1行おきに敷く背景の色
constexpr float kModifiedBarWidth = 3.0f; ///< 変えた項目の左端の線の太さ

/** @brief 保存時の内容のうち、オブジェクトの子の値（無ければ nullptr） */
inline const nlohmann::json* SavedChild(const nlohmann::json* saved, const std::string& key) {
    if (saved == nullptr || !saved->is_object()) {
        return nullptr;
    }
    const auto found = saved->find(key);
    return found != saved->end() ? &*found : nullptr;
}

/** @brief 保存時の内容のうち、配列の要素（無ければ nullptr） */
inline const nlohmann::json* SavedElement(const nlohmann::json* saved, size_t index) {
    return saved != nullptr && saved->is_array() && index < saved->size() ? &(*saved)[index] : nullptr;
}

} // namespace engine::graphics::jsonConfigEditorShared

#endif // USE_IMGUI
