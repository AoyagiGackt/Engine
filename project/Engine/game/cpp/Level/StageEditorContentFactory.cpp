/**
 * @file StageEditorContentFactory.cpp
 * @brief ステージエディタで使用する配置テンプレートを実装するファイル
 */
#include "StageEditorContentFactory.h"


namespace engine::game {
namespace {
// 戦闘部屋テンプレートの配置（部屋の中心からの相対位置）
constexpr int kRoomSpawnCount = 2;
constexpr float kRoomFirstSpawnX = 3.0f;
constexpr float kRoomSpawnSpacing = 2.0f;
constexpr engine::Vector3 kRoomExitDoorOffset = { 8.0f, 1.5f, 0.0f };
constexpr engine::Vector3 kRoomCameraOffset = { 4.0f, 4.0f, -24.0f };
constexpr float kRoomCameraHoldSeconds = 2.0f;
constexpr engine::Vector4 kBreakableWallColor = { 0.7f, 0.7f, 0.75f, 1.0f };
constexpr engine::Vector4 kGuideTextColor = { 0.85f, 0.95f, 1.0f, 0.95f };
}

/** @brief 進入、敵出現、全滅条件、出口、カメラを含む戦闘部屋を生成する */
StageEditorGeneratedContent StageEditorContentFactory::CreateBattleRoom(const Vector3& center, int& nextSerial)
{
    // 通し番号を名前に付け、同じテンプレートを何度置いても名前とフラグがぶつからないようにする
    StageEditorGeneratedContent result;
    const std::string serial = std::to_string(nextSerial++);
    const std::string startFlag = "room_start_" + serial;
    const std::string enemyGroup = "room_wave_" + serial;

    // 部屋に入ると開始フラグを立てる進入トリガー
    TriggerDesc entryTrigger;
    entryTrigger.name = "room_entry_" + serial;
    entryTrigger.position = center;
    entryTrigger.flag = startFlag;
    result.triggers.push_back(std::move(entryTrigger));

    // 開始フラグで出現する敵の出現地点を横一列に並べ、全員を同じグループにする
    for (int i = 0; i < kRoomSpawnCount; ++i)
    {
        ObjectDesc spawn;
        spawn.name = "room_spawn_" + serial + "_" + std::to_string(i + 1);
        spawn.kind = "spawn_point";
        spawn.position = center + Vector3 { kRoomFirstSpawnX + i * kRoomSpawnSpacing, 0.0f, 0.0f };
        spawn.activationFlag = startFlag;
        spawn.enemyGroup = enemyGroup;
        result.objects.push_back(std::move(spawn));
    }

    // グループが全滅したら成立するイベント条件
    ObjectDesc clearCondition;
    clearCondition.name = "room_clear_" + serial;
    clearCondition.kind = "event_condition";
    clearCondition.conditionType = "enemy_group_defeated";
    clearCondition.enemyGroup = enemyGroup;
    clearCondition.position = center;
    const std::string clearConditionName = clearCondition.name;
    result.objects.push_back(std::move(clearCondition));

    // 出口の扉は全滅条件でせり上がって消えるスライド扉にする
    StageEditorGeneratedContent door = CreateSlidingDoor(center + kRoomExitDoorOffset, ConditionFlagName(clearConditionName), nextSerial);
    for (ObjectDesc& desc : door.objects)
    {
        result.objects.push_back(std::move(desc));
    }

    // 戦闘開始時に部屋全体を映すカメラ地点
    ObjectDesc cameraPoint;
    cameraPoint.name = "room_camera_" + serial;
    cameraPoint.kind = "camera_point";
    cameraPoint.position = center + kRoomCameraOffset;
    cameraPoint.activationFlag = startFlag;
    cameraPoint.cameraHoldSeconds = kRoomCameraHoldSeconds;
    result.objects.push_back(std::move(cameraPoint));
    return result;
}

/** @brief 条件フラグが立つとせり上がって消えるスライド扉を生成する */
StageEditorGeneratedContent StageEditorContentFactory::CreateSlidingDoor(const Vector3& center, const std::string& conditionFlag, int& nextSerial)
{
    // フラグが立つとタイマーが進み、1秒で上へ抜けて1.2秒後に非表示・当たり判定消滅
    constexpr float kDoorHeight = 5.0f;
    constexpr float kOpenDelaySeconds = 1.2f;
    constexpr float kSlideAmount = 5.5f;
    constexpr float kSlideSpeed = 1.0f;

    StageEditorGeneratedContent result;
    // ブロックを縦に伸ばした当たり判定つきの扉
    ObjectDesc door;
    door.name = "door_" + std::to_string(nextSerial++);
    door.kind = "gimmick";
    door.model = "Resources/block/block.obj";
    door.texture = "Resources/block/block.png";
    door.position = center;
    door.scale = { 1.0f, kDoorHeight, 1.0f };
    door.solid = true;
    door.lighting = true;
    // 条件フラグが立つと、遅延ののち上方向へ1回だけ滑らかに動いて消える
    door.activationFlag = conditionFlag;
    door.activeWhenFlag = false;
    door.activationDelay = kOpenDelaySeconds;
    door.gimmickMotion = "custom";
    door.motionMode = MotionMode::Once;
    door.motionEase = MotionEase::Smooth;
    door.motionAxis = { 0.0f, 1.0f, 0.0f };
    door.motionRotation = { };
    door.motionAmount = kSlideAmount;
    door.motionSpeed = kSlideSpeed;
    result.objects.push_back(std::move(door));
    return result;
}

/** @brief 指定武器の近接攻撃でしか壊れない壁を生成する（solid、爆風なし） */
StageEditorGeneratedContent StageEditorContentFactory::CreateBreakableWall(const Vector3& center, const std::string& weaponType, int& nextSerial)
{
    constexpr float kWallHeight = 4.0f;
    constexpr int kWallHp = 3;

    StageEditorGeneratedContent result;
    ObjectDesc wall;
    wall.name = "wall_" + std::to_string(nextSerial++);
    wall.kind = "breakable";
    wall.model = "Resources/block/block.obj";
    wall.texture = "Resources/DowntownCityMegaKit[Standard]/Textures/T_Concrete_BaseColor.png";
    wall.position = center;
    wall.scale = { 1.0f, kWallHeight, 1.0f };
    wall.solid = true;
    wall.lighting = true;
    wall.breakableHp = kWallHp;
    wall.breakableRadius = 0.0f;
    wall.breakablePlayerDamage = 0;
    wall.breakableEnemyDamage = 0;
    wall.breakableWeapon = weaponType;
    wall.breakableColor = kBreakableWallColor;
    result.objects.push_back(std::move(wall));
    return result;
}

/** @brief 区画トリガーと、そのフラグで表示される画面下の案内文をセットで生成する */
StageEditorGeneratedContent StageEditorContentFactory::CreateZoneGuide(const Vector3& center, const std::string& text, int& nextSerial)
{
    constexpr float kTriggerRadius = 1.5f;
    constexpr float kGuideScreenX = 24.0f;
    constexpr float kGuideScreenY = 575.0f;
    constexpr float kGuideScale = 1.35f;

    StageEditorGeneratedContent result;
    const std::string serial = std::to_string(nextSerial++);

    TriggerDesc trigger;
    trigger.name = "zone_" + serial;
    trigger.position = center;
    trigger.radius = kTriggerRadius;
    trigger.flag = "zone_" + serial;
    result.triggers.push_back(std::move(trigger));

    ObjectDesc guide;
    guide.name = "guide_" + serial;
    guide.kind = "ui_text";
    guide.type = PlacementType::Static;
    guide.position = { kGuideScreenX, kGuideScreenY, 0.0f };
    guide.textSpace = TextSpace::Screen;
    guide.textScale = kGuideScale;
    guide.textColor = kGuideTextColor;
    guide.text = text;
    guide.activationFlag = "zone_" + serial;
    result.objects.push_back(std::move(guide));
    return result;
}

/** @brief 収集物を横一列に生成する */
StageEditorGeneratedContent StageEditorContentFactory::CreatePickupRow(const Vector3& center, int count, float spacing, int& nextSerial)
{
    constexpr float kPickupScale = 0.35f;

    StageEditorGeneratedContent result;
    const int clampedCount = count < 1 ? 1 : count;
    for (int i = 0; i < clampedCount; ++i)
    {
        ObjectDesc pickup;
        pickup.name = "pickup_" + std::to_string(nextSerial++);
        pickup.kind = "pickup";
        pickup.model = "Resources/block/block.obj";
        pickup.texture = "Resources/Effects/circle2.png";
        pickup.position = center;
        pickup.position.x += (static_cast<float>(i) - (clampedCount - 1) * 0.5f) * spacing;
        pickup.scale = { kPickupScale, kPickupScale, kPickupScale };
        pickup.lighting = false;
        pickup.solid = false;
        result.objects.push_back(std::move(pickup));
    }
    return result;
}

/** @brief 指定条件に沿った敵出現地点をまとめて生成する */
StageEditorGeneratedContent StageEditorContentFactory::CreateWave(const StageEditorWaveConfig& config, int& nextSerial)
{
    StageEditorGeneratedContent result;
    result.objects.reserve(config.enemyCount);
    for (int i = 0; i < config.enemyCount; ++i)
    {
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

} // namespace engine::game
