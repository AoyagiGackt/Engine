/**
 * @file StageEditorEventConnection.cpp
 * @brief ステージエディタのイベント接続状態と適用処理を実装するファイル
 */
#include "StageEditorEventConnection.h"

using namespace engine::game;

void StageEditorEventConnection::Reset()
{
    sourceIndex_ = -1;
    targetIndex_ = -1;
    actionIndex_ = 0;
    delaySeconds_ = 0.0f;
}

bool StageEditorEventConnection::SupportsTarget(const ObjectDesc& desc)
{
    return desc.kind == "spawn_point" || desc.kind == "gimmick" || desc.kind == "camera_point";
}

bool StageEditorEventConnection::CanConnect(int objectCount, const ObjectDesc* target) const
{
    return sourceIndex_ >= 0 && targetIndex_ >= 0 && targetIndex_ < objectCount
        && target && SupportsTarget(*target);
}

std::string StageEditorEventConnection::Connect(TriggerDesc& source, ObjectDesc& target) const
{
    if (source.flag.empty()) {
        source.flag = "event_" + source.name;
    }
    ApplyTarget(source.flag, target);
    return source.name;
}

std::string StageEditorEventConnection::Connect(const ObjectDesc& source, ObjectDesc& target) const
{
    ApplyTarget("condition_" + source.name, target);
    return source.name;
}

void StageEditorEventConnection::Disconnect(ObjectDesc& target) const
{
    target.activationFlag.clear();
    target.activeWhenFlag = true;
    target.activationDelay = 0.0f;
}

void StageEditorEventConnection::ApplyTarget(const std::string& flag, ObjectDesc& target) const
{
    target.activationFlag = flag;
    target.activeWhenFlag = actionIndex_ == 0;
    target.activationDelay = delaySeconds_;
}
