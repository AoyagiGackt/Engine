#include "PlayerBridge.h"
using namespace engine::game;

PlayerBridge* PlayerBridge::GetInstance()
{
    static PlayerBridge instance;
    return &instance;
}
