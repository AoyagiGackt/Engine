/**
 * @file JsonFieldTable.h
 * @brief 設定構造体のメンバとJSONのキーの対応表から、まとめて値を読み書きする仕組み
 * @note 調整値を足す時は、構造体にメンバを足して対応表に1行足すだけでJSONから読めるようになる。
 *       JSONに無い項目は構造体の既定値のまま残る
 */
#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include "json.hpp"
#include <cstddef>
#include <string>
#include <type_traits>
#include <variant>

namespace engine::game {

/** @brief JSONの1項目と、それを書き込むメンバの対応 */
template <class Data>
struct JsonField {
    const char* section; ///< ルート直下のオブジェクト名（nullptr ならルート直下のキー）
    const char* key;
    std::variant<int Data::*, long long Data::*, float Data::*, bool Data::*, std::string Data::*, Vector3 Data::*, Vector4 Data::*> member;
};

namespace json_field_detail {
    inline constexpr std::size_t kColorChannels = 4;
    inline constexpr std::size_t kVector3Size = 3;

    template <class Value>
    void Assign(Value& target, const nlohmann::json& value)
    {
        if constexpr (std::is_same_v<Value, Vector3>) {
            if (value.is_array() && value.size() >= kVector3Size) {
                target = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>() };
            }
        } else if constexpr (std::is_same_v<Value, Vector4>) {
            // 色は [R, G, B, A] の配列で書く
            if (value.is_array() && value.size() >= kColorChannels) {
                target = { value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>() };
            }
        } else if constexpr (std::is_same_v<Value, std::string>) {
            if (value.is_string()) {
                target = value.get<std::string>();
            }
        } else if constexpr (std::is_same_v<Value, bool>) {
            if (value.is_boolean()) {
                target = value.get<bool>();
            }
        } else {
            if (value.is_number()) {
                target = value.get<Value>();
            }
        }
    }

    template <class Value>
    nlohmann::json ToJson(const Value& value)
    {
        if constexpr (std::is_same_v<Value, Vector3>) {
            return nlohmann::json::array({ value.x, value.y, value.z });
        } else if constexpr (std::is_same_v<Value, Vector4>) {
            return nlohmann::json::array({ value.x, value.y, value.z, value.w });
        } else {
            return nlohmann::json(value);
        }
    }
} // namespace json_field_detail

/**
 * @brief 対応表のとおりに root から data へ値を読み込む
 * @note 型の合わない値は無視して既定値を残す
 */
template <class Data, std::size_t N>
void ReadJsonFields(const nlohmann::json& root, const JsonField<Data> (&fields)[N], Data& data)
{
    if (!root.is_object()) {
        return;
    }
    for (const JsonField<Data>& field : fields) {
        const nlohmann::json* scope = &root;
        if (field.section != nullptr) {
            const auto section = root.find(field.section);
            if (section == root.end() || !section->is_object()) {
                continue;
            }
            scope = &*section;
        }
        const auto value = scope->find(field.key);
        if (value == scope->end()) {
            continue;
        }
        std::visit([&](auto member) { json_field_detail::Assign(data.*member, *value); }, field.member);
    }
}

/** @brief 対応表のとおりに data の値を root へ書き出す（ReadJsonFields と対になる） */
template <class Data, std::size_t N>
void WriteJsonFields(nlohmann::json& root, const JsonField<Data> (&fields)[N], const Data& data)
{
    if (!root.is_object()) {
        root = nlohmann::json::object();
    }
    for (const JsonField<Data>& field : fields) {
        nlohmann::json& scope = field.section != nullptr ? root[field.section] : root;
        std::visit([&](auto member) { scope[field.key] = json_field_detail::ToJson(data.*member); }, field.member);
    }
}

} // namespace engine::game
