/**
 * @file EnemyRegistry.cpp
 * @brief EnemyRegistryが担当する処理を実装するファイル
 */
#include "EnemyRegistry.h"
using namespace engine::game;

EnemyRegistry* EnemyRegistry::GetInstance()
{
    static EnemyRegistry instance;
    return &instance;
}

void EnemyRegistry::Register(const std::string& id, EnemyEntity* enemy)
{
    enemies_[id] = enemy;
}

void EnemyRegistry::Unregister(const std::string& id)
{
    enemies_.erase(id);
}

EnemyEntity* EnemyRegistry::Find(const std::string& id) const
{
    auto it = enemies_.find(id);
    return (it != enemies_.end()) ? it->second : nullptr;
}

void EnemyRegistry::Clear()
{
    enemies_.clear();
}
