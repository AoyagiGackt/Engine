/**
 * @file PlayerBridge.h
 * @brief ノードグラフから現在のPlayerを操作するための橋渡しシングルトン
 * @note EnemyRegistry/AudioBridgeと同じパターン。Scene::Initialize()がPlayer*を登録する
 */
#pragma once
namespace engine::game {

class Player;

class PlayerBridge {
public:
    /**
     * @brief 唯一のPlayerBridgeインスタンスを取得する（未生成なら生成する）
     * @return PlayerBridgeのインスタンス
     */
    static PlayerBridge* GetInstance();

    /** @brief 現在のシーンのPlayerを登録する（Scene::Initialize()から呼ぶ） */
    void SetPlayer(Player* player) { player_ = player; }

    /** @brief 登録されていなければnullptr */
    Player* Get() const { return player_; }

private:
    PlayerBridge() = default;
    PlayerBridge(const PlayerBridge&) = delete;
    PlayerBridge& operator=(const PlayerBridge&) = delete;

    Player* player_ = nullptr;
};

} // namespace engine::game
