/**
 * @file StageEditorEventConnection.h
 * @brief ステージエディタのイベント接続状態と適用処理を管理するファイル
 */
#pragma once
#include "LevelLoader.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace engine::game {

/**
 * @brief 条件、動作対象、実行内容を保持してイベント接続を構築するクラス
 */
class StageEditorEventConnection {
public:
    /** @brief 条件成立時に対象を表示・起動するか、非表示・停止するか */
    enum class Action { Activate, Deactivate };

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

    /** @brief 接続元の番号を指定する。負の番号は未選択として扱う */
    void SetSourceIndex(int index) {
        sourceIndex_ = (std::max)(index, -1);
    }
    /** @brief 選択中の接続元の番号を返す */
    int SourceIndex() const {
        return sourceIndex_;
    }
    /** @brief 接続先の番号を指定する。負の番号は未選択として扱う */
    void SetTargetIndex(int index) {
        targetIndex_ = (std::max)(index, -1);
    }
    /** @brief 選択中の接続先の番号を返す */
    int TargetIndex() const {
        return targetIndex_;
    }
    /** @brief 選択中の接続動作を返す */
    Action GetAction() const {
        return action_;
    }
    /** @brief 接続動作を有効化または無効化の範囲内で指定する */
    void SetAction(Action action) {
        action_ = action == Action::Deactivate ? Action::Deactivate : Action::Activate;
    }
    /** @brief 接続動作を始めるまでの遅れ（秒）を返す */
    float DelaySeconds() const {
        return delaySeconds_;
    }
    /** @brief 接続動作の遅れを非負の有限秒数で指定する。不正値は待機なしに戻す */
    void SetDelaySeconds(float seconds) {
        delaySeconds_ = std::isfinite(seconds) ? (std::max)(seconds, 0.0f) : 0.0f;
    }

private:
    /** @brief 解決済みの条件フラグを対象へ設定する */
    void ApplyTarget(const std::string& flag, ObjectDesc& target) const;

    int sourceIndex_ = -1;
    int targetIndex_ = -1;
    Action action_ = Action::Activate;
    float delaySeconds_ = 0.0f;
};

} // namespace engine::game
