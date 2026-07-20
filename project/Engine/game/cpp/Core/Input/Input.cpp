#include "Input.h"
#include "EngineAssert.h"
#include "Logger.h"
#include "JsonHelper.h"
#include <cmath>
#include <dinput.h>
using namespace engine;

namespace {

BYTE ParseKey(const std::string& name)
{
    if (name == "A") return DIK_A;
    if (name == "D") return DIK_D;
    if (name == "W") return DIK_W;
    if (name == "S") return DIK_S;
    if (name == "F") return DIK_F;
    if (name == "G") return DIK_G;
    if (name == "K") return DIK_K;
    if (name == "L") return DIK_L;
    if (name == "R") return DIK_R;
    if (name == "Left") return DIK_LEFT;
    if (name == "Right") return DIK_RIGHT;
    if (name == "Up") return DIK_UP;
    if (name == "Down") return DIK_DOWN;
    if (name == "Space") return DIK_SPACE;
    return 0;
}

WORD ParseGamepadButton(const std::string& name)
{
    if (name == "A") return XINPUT_GAMEPAD_A;
    if (name == "B") return XINPUT_GAMEPAD_B;
    if (name == "X") return XINPUT_GAMEPAD_X;
    if (name == "Y") return XINPUT_GAMEPAD_Y;
    if (name == "LB") return XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (name == "RB") return XINPUT_GAMEPAD_RIGHT_SHOULDER;
    return 0;
}

} // namespace

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

void Input::Initialize(WinApp* winApp)
{
    HRESULT result;

    // 借りてきたWinAppのインスタンスを記録
    this->winApp_ = winApp;

    // DirectInputのインスタンス生成
    result = DirectInput8Create(winApp->GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput_, nullptr);
    ENGINE_ASSERT(SUCCEEDED(result));
    // キーボードデバイス生成
    result = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);
    ENGINE_ASSERT(SUCCEEDED(result));
    // 入力データ形式のセット
    result = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    ENGINE_ASSERT(SUCCEEDED(result));
    // 排他制御レベルのセット
    result = keyboard_->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    ENGINE_ASSERT(SUCCEEDED(result));

    // マウスデバイス生成
    result = directInput_->CreateDevice(GUID_SysMouse, &mouse_, NULL);
    ENGINE_ASSERT(SUCCEEDED(result));
    // 入力データ形式のセット
    result = mouse_->SetDataFormat(&c_dfDIMouse2);
    ENGINE_ASSERT(SUCCEEDED(result));
    // 排他制御レベルのセット
    result = mouse_->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    ENGINE_ASSERT(SUCCEEDED(result));

    LoadActionBindings();
}

void Input::LoadActionBindings()
{
    // 設定ファイルが無い場合も操作不能にならない既定割り当てを先に設定する
    actionBindings_[static_cast<size_t>(Action::MoveLeft)] = { DIK_A, DIK_LEFT, 0 };
    actionBindings_[static_cast<size_t>(Action::MoveRight)] = { DIK_D, DIK_RIGHT, 0 };
    actionBindings_[static_cast<size_t>(Action::Jump)] = { DIK_W, DIK_UP, XINPUT_GAMEPAD_A };
    actionBindings_[static_cast<size_t>(Action::Down)] = { DIK_S, DIK_DOWN, 0 };
    actionBindings_[static_cast<size_t>(Action::Attack)] = { DIK_L, 0, XINPUT_GAMEPAD_X };
    actionBindings_[static_cast<size_t>(Action::Shoot)] = { DIK_K, 0, XINPUT_GAMEPAD_Y };
    actionBindings_[static_cast<size_t>(Action::Skill)] = { DIK_SPACE, 0, XINPUT_GAMEPAD_B };
    actionBindings_[static_cast<size_t>(Action::Awaken)] = { DIK_R, 0, XINPUT_GAMEPAD_RIGHT_SHOULDER };
    actionBindings_[static_cast<size_t>(Action::Finisher)] = { DIK_F, 0, XINPUT_GAMEPAD_LEFT_SHOULDER };
    actionBindings_[static_cast<size_t>(Action::GunSwitch)] = { DIK_G, 0, 0 };

    const nlohmann::json root = JsonHelper::Load("Resources/input_bindings.json");
    const auto actions = root.value("actions", nlohmann::json::object());
    const std::array<std::pair<const char*, Action>, static_cast<size_t>(Action::Count)> names = { {
        { "MoveLeft", Action::MoveLeft }, { "MoveRight", Action::MoveRight },
        { "Jump", Action::Jump }, { "Down", Action::Down }, { "Attack", Action::Attack },
        { "Shoot", Action::Shoot }, { "Skill", Action::Skill }, { "Awaken", Action::Awaken },
        { "Finisher", Action::Finisher }, { "GunSwitch", Action::GunSwitch }
    } };

    for (const auto& [name, action] : names) {
        const auto data = actions.value(name, nlohmann::json::object());
        if (!data.is_object() || data.empty()) {
            continue;
        }
        ActionBinding& binding = actionBindings_[static_cast<size_t>(action)];
        binding.primaryKey = ParseKey(data.value("primary", ""));
        binding.secondaryKey = ParseKey(data.value("secondary", ""));
        binding.gamepadButton = ParseGamepadButton(data.value("gamepad", ""));
    }
}

bool Input::PushAction(Action action) const
{
    const ActionBinding& binding = actionBindings_[static_cast<size_t>(action)];
    const bool keyboard = (binding.primaryKey && key[binding.primaryKey])
        || (binding.secondaryKey && key[binding.secondaryKey]);
    return keyboard || (binding.gamepadButton && (state_.Gamepad.wButtons & binding.gamepadButton));
}

bool Input::TriggerAction(Action action) const
{
    const ActionBinding& binding = actionBindings_[static_cast<size_t>(action)];
    const bool keyboard = (binding.primaryKey && key[binding.primaryKey] && !keyPre[binding.primaryKey])
        || (binding.secondaryKey && key[binding.secondaryKey] && !keyPre[binding.secondaryKey]);
    const bool gamepad = binding.gamepadButton
        && (state_.Gamepad.wButtons & binding.gamepadButton)
        && !(previousState_.Gamepad.wButtons & binding.gamepadButton);
    return keyboard || gamepad;
}

void Input::Update()
{
    // ゲームコントローラー更新
    UpdateGamepad();

    // 前回のキー入力を保存
    memcpy(keyPre, key, sizeof(key));
    // キーボード情報の取得開始
    keyboard_->Acquire();
    // 全キーの入力情報を取得するフォーカス喪失などで失敗したらバッファをクリアして刺さり防止
    if (FAILED(keyboard_->GetDeviceState(sizeof(key), key))) {
        ZeroMemory(key, sizeof(key));
    }

    // 前回のマウス入力を保存
    mouseStatePre_ = mouseState_;
    // マウス情報の取得開始
    mouse_->Acquire();
    // 全マウスの入力情報を取得する失敗時はクリア
    if (FAILED(mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_))) {
        ZeroMemory(&mouseState_, sizeof(DIMOUSESTATE2));
    }
}

bool Input::PushKey(BYTE keyNumber)
{
    // 指定キーを押していればtrueを返す
    if (key[keyNumber]) {
        return true;
    }

    // そうでなければfalseを返す
    return false;
}

bool Input::TriggerKey(BYTE keyNumber)
{
    return key[keyNumber] && !keyPre[keyNumber];
}

/**
 * @brief ゲームパッドの状態更新
 */
void Input::UpdateGamepad()
{
    previousState_ = state_; // 前回の状態を保存
    gamepadConnectedPrevious_ = gamepadConnected_;

    // 0番目のコントローラーを取得
    DWORD result = XInputGetState(0, &state_);
    gamepadConnected_ = result == ERROR_SUCCESS;

    if (!gamepadConnected_) {
        // コントローラーが接続されていない場合はデータをゼロにする
        ZeroMemory(&state_, sizeof(XINPUT_STATE));
    }

    if (WasGamepadConnected()) {
        Logger::LogInfo("Gamepad connected");
    } else if (WasGamepadDisconnected()) {
        Logger::LogWarning("Gamepad disconnected. Keyboard input remains available.");
    }
}

/**
 * @brief スティックの正規化（遊びを考慮して 0.0 ~ 1.0 に変換）
 */
Input::Stick Input::GetLeftStick() const
{
    float x = (float)state_.Gamepad.sThumbLX / 32767.0f;
    float y = (float)state_.Gamepad.sThumbLY / 32767.0f;

    // デッドゾーンの処理
    if (std::abs(x) < deadzone_) {
        x = 0.0f;
    }

    if (std::abs(y) < deadzone_) {
        y = 0.0f;
    }

    return { x, y };
}

bool Input::TriggerMouseButton(int32_t buttonNumber)
{
    // 今回押されていて、前回押されていないかチェック
    if (mouseState_.rgbButtons[buttonNumber] && !mouseStatePre_.rgbButtons[buttonNumber]) {
        return true;
    }

    return false;
}
