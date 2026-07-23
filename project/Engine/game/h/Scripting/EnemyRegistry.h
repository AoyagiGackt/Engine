/**
 * @file EnemyRegistry.h
 * @brief idからEnemyEntityを引けるグローバル登録簿ノードグラフの対象敵指定に使う
 * @note 敵の生成・破棄はScene側の責務のままここは今どのidがどのポインタを指すかだけを持つ
 * Scene側はInitializeで登録、Finalizeで登録解除すること（解除を忘れるとダングリングポインタになる）
 */
#pragma once
#include <map>
#include <string>
namespace engine::game {

class EnemyEntity;

/**
 * @brief idからEnemyEntity*を引けるグローバル登録簿
 * @details 敵の生成・破棄そのものはScene側が行い、ここは現在生存している敵のidとポインタの対応だけを持つ
 */
class EnemyRegistry {
public:
    /**
     * @brief 唯一のEnemyRegistryインスタンスを取得する（未生成なら生成する）
     * @return EnemyRegistryのインスタンス
     */
    static EnemyRegistry* GetInstance();

    /** @brief idで敵を登録する（同じidが既にあれば上書き） */
    void Register(const std::string& id, EnemyEntity* enemy);

    /** @brief 登録を解除する（敵を破棄する前に呼ぶこと） */
    void Unregister(const std::string& id);

    /** @brief idから敵を検索する未登録なら nullptr */
    EnemyEntity* Find(const std::string& id) const;

    /** @brief 全登録を消去する（シーン遷移時などに使う想定） */
    void Clear();

private:
    EnemyRegistry() = default;
    EnemyRegistry(const EnemyRegistry&) = delete;
    EnemyRegistry& operator=(const EnemyRegistry&) = delete;

    std::map<std::string, EnemyEntity*> enemies_;
};

} // namespace engine::game
