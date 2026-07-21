/**
 * @file GameFlags.cpp
 * @brief GameFlagsのイベントグラフのデータ、編集、実行に関する具体的な処理を実装するファイル
 */
#include "GameFlags.h"
using namespace engine::game;

GameFlags* GameFlags::GetInstance()
{
    static GameFlags instance;
    return &instance;
}

void GameFlags::SetFlag(const std::string& name, bool value)
{
    flags_[name] = value;
}

bool GameFlags::GetFlag(const std::string& name) const
{
    auto it = flags_.find(name);
    return (it != flags_.end()) ? it->second : false;
}

bool GameFlags::HasFlag(const std::string& name) const
{
    return flags_.find(name) != flags_.end();
}

void GameFlags::Clear()
{
    flags_.clear();
}
