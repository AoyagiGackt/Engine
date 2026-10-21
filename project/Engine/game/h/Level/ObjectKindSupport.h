/** @file ObjectKindSupport.h
 * @brief 配置物の種類実装が共有する編集入力とマーカーの取得関数
 */
#pragma once
#include "ObjectKind.h"
#include <string>
#ifdef USE_IMGUI
#include <imgui.h>
#include <cstring>
#endif
namespace engine::game::ObjectKindDetail {
#ifdef USE_IMGUI
constexpr size_t kTextFieldSize = 96;

/** @brief 文字列の設定項目を1行の入力欄で編集する */
inline bool InputString(const char* label, std::string& value, const ObjectKind::UndoCapture& capture)
{
    char buffer[kTextFieldSize] = { };
    strncpy_s(buffer, value.c_str(), _TRUNCATE);
    if (capture(ImGui::InputText(label, buffer, sizeof(buffer))))
    {
        value = buffer;
        return true;
    }
    return false;
}

/**
 * @brief 文字列の選択肢から1つ選ぶコンボ（先頭の選択肢は空文字として扱う）
 * @param selected 選び直された時だけ新しい値を書き込む（Undoの記録を値の書き換えより先に行えるよう、元の値は変えない）
 * @return 選び直されたか
 */
/** @brief コンボボックスで文字列を選ぶ（先頭の項目は空として扱う） */
template <size_t N>
bool ComboWithEmptyFirst(const char* label, const std::string& current, const char* const (&items)[N], std::string& selected)
{
    int index = 0;
    for (int i = 1; i < static_cast<int>(N); ++i)
    {
        if (current == items[i])
        {
            index = i;
            break;
        }
    }
    if (!ImGui::Combo(label, &index, items, static_cast<int>(N)))
    {
        return false;
    }
    selected = index == 0 ? "" : items[index];
    return true;
}
#endif
/** @brief CameraPointの共有定義を取得する */
const ObjectKind& CameraPoint();
/** @brief PatrolPointの共有定義を取得する */
const ObjectKind& PatrolPoint();
/** @brief EventConditionの共有定義を取得する */
const ObjectKind& EventCondition();
/** @brief UITextの共有定義を取得する */
const ObjectKind& UIText();

/**
 * @brief 有効化フラグで働く、実体を描画しない編集用地点の性質を Base に足す（カメラ地点・敵の出現地点）
 * @tparam Base 元にする種類のクラス
 */
template <class Base>
class ActivatableMarker : public Base {
public:
    using Base::Base;
    /** @brief 有効化フラグによる制御を受け付けるか返す */
    bool AcceptsActivationFlag() const override {
        return true;
    }
    /** @brief 実体の描画を持たない編集用地点か返す */
    bool IsMarker() const override {
        return true;
    }
};
}
