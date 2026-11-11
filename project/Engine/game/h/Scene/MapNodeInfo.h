/**
 * @file MapNodeInfo.h
 * @brief マップのマスの種類ごとの見た目（色・名前・説明）と、戦闘へ進むか（MapScene の更新と画面表示で共有する）
 */
#pragma once
#include "RunData.h"
#include "Vector4.h"
#include <iterator>

namespace engine::game::mapNodeInfo {

// ノード種別ごとの色
inline constexpr Vector4 kNodeCombatColor = { 0.75f, 0.18f, 0.18f, 1.0f };
inline constexpr Vector4 kNodeEliteColor = { 0.85f, 0.45f, 0.08f, 1.0f };
inline constexpr Vector4 kNodeShopColor = { 0.18f, 0.65f, 0.28f, 1.0f };
inline constexpr Vector4 kNodeRestColor = { 0.18f, 0.38f, 0.85f, 1.0f };
inline constexpr Vector4 kNodeBossColor = { 0.55f, 0.08f, 0.75f, 1.0f };
inline constexpr Vector4 kNodeDefaultColor = { 0.5f, 0.5f, 0.5f, 1.0f };

/** @brief ノード種別ごとの見た目と遷移先 */
struct NodeInfo {
    Vector4 color; ///< マップ上の表示色
    const wchar_t* label; ///< 日本語ラベル
    const wchar_t* desc; ///< 右下に出す説明
    bool entersCombat; ///< 決定時に戦闘シーンへ遷移するか
};

// RunData::NodeType の並び順と一致させる
inline constexpr NodeInfo kNodeInfos[] = {
    { kNodeCombatColor, L"戦 闘", L"ステージを進み、ボスの武器を奪ってクリア　スタイルが高いほど評価UP", true },
    { kNodeEliteColor, L"強敵", L"手強い敵が待ち構えるステージ", true },
    { kNodeShopColor, L"ショップ", L"スキルを1つ選んで取得できる　スキルは永続効果", false },
    { kNodeRestColor, L"休 憩", L"HP を10回復する　のんびり休もう", false },
    { kNodeBossColor, L"ボ ス", L"最終決戦  倒せばクリア! 全力で挑め", true },
};
inline constexpr NodeInfo kUnknownNodeInfo = { kNodeDefaultColor, L"？？？", L"", false };

/** @brief マップのマスの種類に対応する表示情報を返す */
inline const NodeInfo& GetNodeInfo(RunData::NodeType t) {
    const size_t index = static_cast<size_t>(t);
    return index < std::size(kNodeInfos) ? kNodeInfos[index] : kUnknownNodeInfo;
}

} // namespace engine::game::mapNodeInfo
