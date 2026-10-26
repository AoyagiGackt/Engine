/**
 * @file GameFlags.cpp
 * @brief グラフ・トリガー・シーンが共有するフラグと数値（GameFlags）の実装
 */
#include "GameFlags.h"

namespace engine::game {
/** @brief 唯一のGameFlagsインスタンスを取得する（未生成なら生成する） */
GameFlags* GameFlags::GetInstance() {
    static GameFlags sInstance;
    return &sInstance;
}

/** @brief フラグを設定する（無ければ新規作成） */
void GameFlags::SetFlag(const std::string& name, bool value) {
    flags_[name] = value;
}

/** @brief フラグの値を取得する未設定なら false を返す */
bool GameFlags::GetFlag(const std::string& name) const {
    auto it = flags_.find(name);
    return (it != flags_.end()) ? it->second : false;
}

/** @brief フラグが一度でも設定されたことがあるか */
bool GameFlags::HasFlag(const std::string& name) const {
    return flags_.find(name) != flags_.end();
}

/** @brief 全フラグを消去する（ステージ再読み込み時などに使う想定） */
void GameFlags::Clear() {
    flags_.clear();
}

} // namespace engine::game
