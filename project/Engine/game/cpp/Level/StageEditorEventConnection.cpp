/**
 * @file StageEditorEventConnection.cpp
 * @brief ステージエディタのイベント接続状態と適用処理を実装するファイル
 */
#include "StageEditorEventConnection.h"


namespace engine::game {
/** @brief 選択状態と調整値を初期値へ戻す */
void StageEditorEventConnection::Reset() {
    sourceIndex_ = -1;
    targetIndex_ = -1;
    action_ = Action::kActivate;
    delaySeconds_ = 0.0f;
}

/** @brief イベントの動作対象にできる配置種類か返す */
bool StageEditorEventConnection::SupportsTarget(const ObjectDesc& desc) {
    return ObjectKind::Of(desc.kind).AcceptsActivationFlag();
}

/** @brief 現在の選択内容で接続を作成できるか返す */
bool StageEditorEventConnection::CanConnect(int objectCount, const ObjectDesc* target) const {
    return sourceIndex_ >= 0 && targetIndex_ >= 0 && targetIndex_ < objectCount
        && target && SupportsTarget(*target);
}

/** @brief イベント条件から対象への接続を適用する */
std::string StageEditorEventConnection::Connect(TriggerDesc& source, ObjectDesc& target) const {
    if (source.flag.empty()) {
        source.flag = "event_" + source.name;
    }
    ApplyTarget(source.flag, target);
    return source.name;
}

/** @brief イベント条件から対象への接続を適用する */
std::string StageEditorEventConnection::Connect(const ObjectDesc& source, ObjectDesc& target) const {
    ApplyTarget(ConditionFlagName(source.name), target);
    return source.name;
}

/** @brief 対象に設定されているイベント接続を解除する */
void StageEditorEventConnection::Disconnect(ObjectDesc& target) const {
    target.activationFlag.clear();
    target.activeWhenFlag = true;
    target.activationDelay = 0.0f;
}

/** @brief 解決済みの条件フラグを対象へ設定する */
void StageEditorEventConnection::ApplyTarget(const std::string& flag, ObjectDesc& target) const {
    target.activationFlag = flag;
    target.activeWhenFlag = action_ == Action::kActivate;
    target.activationDelay = delaySeconds_;
}

} // namespace engine::game
