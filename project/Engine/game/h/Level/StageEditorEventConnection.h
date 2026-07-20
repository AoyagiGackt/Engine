/**
 * @file StageEditorEventConnection.h
 * @brief ステージエディタのイベント接続状態と適用処理を管理するファイル
 */
#pragma once
#include "LevelLoader.h"
#include <string>

namespace engine::game {

/**
 * @brief 条件、動作対象、実行内容を保持してイベント接続を構築するクラス
 */
class StageEditorEventConnection {
public:
    /** @brief 選択状態と調整値を初期値へ戻す */
    void Reset();

    /** @brief イベントの動作対象にできる配置種類か返す */
    static bool SupportsTarget(const ObjectDesc& desc);

    /** @brief 現在の選択内容で接続を作成できるか返す */
    bool CanConnect(int objectCount, const ObjectDesc* target) const;

    /**
     * @brief 進入トリガーから対象への接続を適用する
     * @param source 接続元の進入トリガー
     * @param target 接続先の配置物
     * @return 状態表示に使用する接続元の名前を返す
     */
    std::string Connect(TriggerDesc& source, ObjectDesc& target) const;

    /**
     * @brief イベント条件から対象への接続を適用する
     * @param source 接続元のイベント条件
     * @param target 接続先の配置物
     * @return 状態表示に使用する接続元の名前を返す
     */
    std::string Connect(const ObjectDesc& source, ObjectDesc& target) const;

    /** @brief 対象に設定されているイベント接続を解除する */
    void Disconnect(ObjectDesc& target) const;

    int& SourceIndex() { return sourceIndex_; }
    int SourceIndex() const { return sourceIndex_; }
    int& TargetIndex() { return targetIndex_; }
    int TargetIndex() const { return targetIndex_; }
    int& ActionIndex() { return actionIndex_; }
    float& DelaySeconds() { return delaySeconds_; }

private:
    /** @brief 解決済みの条件フラグを対象へ設定する */
    void ApplyTarget(const std::string& flag, ObjectDesc& target) const;

    int sourceIndex_ = -1;
    int targetIndex_ = -1;
    int actionIndex_ = 0;
    float delaySeconds_ = 0.0f;
};

} // namespace engine::game
