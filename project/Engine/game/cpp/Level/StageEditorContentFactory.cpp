/**
 * @file StageEditorContentFactory.cpp
 * @brief ステージエディタで使用する配置テンプレートを実装するファイル
 */
#include "StageEditorContentFactory.h"

using namespace engine::game;

StageEditorGeneratedContent StageEditorContentFactory::CreateBattleRoom(const Vector3& center, int& nextSerial)
{
    StageEditorGeneratedContent result;
    const std::string serial = std::to_string(nextSerial++);
    const std::string startFlag = "room_start_" + serial;
    const std::string enemyGroup = "room_wave_" + serial;

    TriggerDesc entryTrigger;
    entryTrigger.name = "room_entry_" + serial;
    entryTrigger.position = center;
    entryTrigger.flag = startFlag;
    result.triggers.push_back(std::move(entryTrigger));

    for (int i = 0; i < 2; ++i) {
        ObjectDesc spawn;
        spawn.name = "room_spawn_" + serial + "_" + std::to_string(i + 1);
        spawn.kind = "spawn_point";
        spawn.position = center + Vector3 { 3.0f + i * 2.0f, 0.0f, 0.0f };
        spawn.activationFlag = startFlag;
        spawn.enemyGroup = enemyGroup;
        result.objects.push_back(std::move(spawn));
    }

    ObjectDesc clearCondition;
    clearCondition.name = "room_clear_" + serial;
    clearCondition.kind = "event_condition";
    clearCondition.conditionType = "enemy_group_defeated";
    clearCondition.enemyGroup = enemyGroup;
    clearCondition.position = center;
    const std::string clearConditionName = clearCondition.name;
    result.objects.push_back(std::move(clearCondition));

    ObjectDesc exitDoor;
    exitDoor.name = "room_exit_" + serial;
    exitDoor.kind = "gimmick";
    exitDoor.model = "Resources/block/block.obj";
    exitDoor.texture = "Resources/block/block.png";
    exitDoor.position = center + Vector3 { 8.0f, 2.0f, 0.0f };
    exitDoor.scale = { 1.0f, 5.0f, 1.0f };
    exitDoor.solid = true;
    exitDoor.activationFlag = "condition_" + clearConditionName;
    exitDoor.activeWhenFlag = false;
    result.objects.push_back(std::move(exitDoor));

    ObjectDesc cameraPoint;
    cameraPoint.name = "room_camera_" + serial;
    cameraPoint.kind = "camera_point";
    cameraPoint.position = center + Vector3 { 4.0f, 4.0f, -24.0f };
    cameraPoint.activationFlag = startFlag;
    cameraPoint.cameraHoldSeconds = 2.0f;
    result.objects.push_back(std::move(cameraPoint));
    return result;
}

StageEditorGeneratedContent StageEditorContentFactory::CreateWave(const StageEditorWaveConfig& config, int& nextSerial)
{
    StageEditorGeneratedContent result;
    result.objects.reserve(config.enemyCount);
    for (int i = 0; i < config.enemyCount; ++i) {
        ObjectDesc spawn;
        spawn.name = config.groupName + "_spawn_" + std::to_string(nextSerial++);
        spawn.kind = "spawn_point";
        spawn.spawnType = config.spawnType;
        spawn.enemyGroup = config.groupName;
        spawn.activationFlag = config.activationFlag;
        spawn.position = config.center;
        spawn.position.x += (static_cast<float>(i) - (config.enemyCount - 1) * 0.5f) * config.spacing;
        result.objects.push_back(std::move(spawn));
    }
    return result;
}
