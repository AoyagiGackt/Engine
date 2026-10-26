/**
 * @file EnemyRegistry.cpp
 * @brief ID から敵を引ける登録簿（EnemyRegistry）の実装
 */
#include "EnemyRegistry.h"

namespace engine::game {
/** @brief 唯一のEnemyRegistryインスタンスを取得する（未生成なら生成する） */
EnemyRegistry* EnemyRegistry::GetInstance() {
    static EnemyRegistry sInstance;
    return &sInstance;
}

/** @brief idで敵を登録する（同じidが既にあれば上書き） */
void EnemyRegistry::Register(const std::string& id, EnemyEntity* enemy) {
    enemies_[id] = enemy;
}

/** @brief 登録を解除する（敵を破棄する前に呼ぶこと） */
void EnemyRegistry::Unregister(const std::string& id) {
    enemies_.erase(id);
}

/** @brief idから敵を検索する未登録なら nullptr */
EnemyEntity* EnemyRegistry::Find(const std::string& id) const {
    auto it = enemies_.find(id);
    return (it != enemies_.end()) ? it->second : nullptr;
}

/** @brief 全登録を消去する（シーン遷移時などに使う想定） */
void EnemyRegistry::Clear() {
    enemies_.clear();
}

} // namespace engine::game
