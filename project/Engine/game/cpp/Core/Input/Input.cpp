/**
 * @file Input.cpp
 * @brief DirectInput/XInputによるキーボード・ゲームパッド・マウス入力の取得とアクション判定（Input）の実装
 */
#include "Input.h"
#include "EngineAssert.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <cmath>
#include <dinput.h>
#include <iterator>
#include <utility>
using namespace engine;

namespace {

/** @brief input_bindings.jsonで使うキー名と、画面の操作説明に出す表記 */
struct KeyName {
    const char* name;
    BYTE key;
    const wchar_t* label;
};
constexpr KeyName kKeyNames[] = {
    { "A", DIK_A, L"A" },
    { "D", DIK_D, L"D" },
    { "W", DIK_W, L"W" },
    { "S", DIK_S, L"S" },
    { "F", DIK_F, L"F" },
    { "G", DIK_G, L"G" },
    { "J", DIK_J, L"J" },
    { "K", DIK_K, L"K" },
    { "L", DIK_L, L"L" },
    { "R", DIK_R, L"R" },
    { "E", DIK_E, L"E" },
    { "I", DIK_I, L"I" },
    { "Q", DIK_Q, L"Q" },
    { "C", DIK_C, L"C" },
    { "Left", DIK_LEFT, L"←" },
    { "Right", DIK_RIGHT, L"→" },
    { "Up", DIK_UP, L"↑" },
    { "Down", DIK_DOWN, L"↓" },
    { "Space", DIK_SPACE, L"Space" },
    { "Escape", DIK_ESCAPE, L"Esc" },
    { "Enter", DIK_RETURN, L"Enter" },
    { "LShift", DIK_LSHIFT, L"Shift" },
    { "LControl", DIK_LCONTROL, L"Ctrl" },
};

/** @brief input_bindings.jsonで使うボタン名と、画面の操作説明に出す表記 */
struct PadButtonName {
    const char* name;
    WORD button;
    const wchar_t* label;
};
constexpr PadButtonName kPadButtonNames[] = {
    { "A", XINPUT_GAMEPAD_A, L"A" },
    { "B", XINPUT_GAMEPAD_B, L"B" },
    { "X", XINPUT_GAMEPAD_X, L"X" },
    { "Y", XINPUT_GAMEPAD_Y, L"Y" },
    { "LB", XINPUT_GAMEPAD_LEFT_SHOULDER, L"LB" },
    { "RB", XINPUT_GAMEPAD_RIGHT_SHOULDER, L"RB" },
    { "LT", Input::kGamepadLeftTrigger, L"LT" },
    { "RT", Input::kGamepadRightTrigger, L"RT" },
    { "LS", XINPUT_GAMEPAD_LEFT_THUMB, L"L3" },
    { "RS", XINPUT_GAMEPAD_RIGHT_THUMB, L"R3" },
    { "Start", XINPUT_GAMEPAD_START, L"START" },
    { "Back", XINPUT_GAMEPAD_BACK, L"BACK" },
    { "DpadUp", XINPUT_GAMEPAD_DPAD_UP, L"十字キー上" },
    { "DpadDown", XINPUT_GAMEPAD_DPAD_DOWN, L"十字キー下" },
    { "DpadLeft", XINPUT_GAMEPAD_DPAD_LEFT, L"十字キー左" },
    { "DpadRight", XINPUT_GAMEPAD_DPAD_RIGHT, L"十字キー右" },
};

/** @brief JSONの操作名とActionの対応（設定読み込みと {操作名} の置き換えで共用する） */
constexpr std::pair<const char*, Input::Action> kActionNames[] = {
    { "MoveLeft", Input::Action::MoveLeft },
    { "MoveRight", Input::Action::MoveRight },
    { "Jump", Input::Action::Jump },
    { "Down", Input::Action::Down },
    { "Attack", Input::Action::Attack },
    { "Shoot", Input::Action::Shoot },
    { "Skill", Input::Action::Skill },
    { "Awaken", Input::Action::Awaken },
    { "Finisher", Input::Action::Finisher },
    { "GunSwitch", Input::Action::GunSwitch },
    { "Dodge", Input::Action::Dodge },
    { "Warp", Input::Action::Warp },
    { "Steal", Input::Action::Steal },
    { "Interact", Input::Action::Interact },
    { "LockOn", Input::Action::LockOn },
    { "Pause", Input::Action::Pause },
};
static_assert(std::size(kActionNames) == static_cast<size_t>(Input::Action::Count), "操作名の表にActionの追加漏れがある");

BYTE ParseKey(const std::string& name)
{
    for (const KeyName& entry : kKeyNames) {
        if (name == entry.name) {
            return entry.key;
        }
    }
    return 0;
}

WORD ParseGamepadButton(const std::string& name)
{
    for (const PadButtonName& entry : kPadButtonNames) {
        if (name == entry.name) {
            return entry.button;
        }
    }
    return 0;
}

std::wstring KeyLabel(BYTE key)
{
    for (const KeyName& entry : kKeyNames) {
        if (entry.key == key) {
            return entry.label;
        }
    }
    return L"?";
}

std::wstring PadButtonLabel(WORD button)
{
    for (const PadButtonName& entry : kPadButtonNames) {
        if (entry.button == button) {
            return entry.label;
        }
    }
    return L"?";
}

constexpr SHORT kStickMenuThreshold = 16000; // メニュー操作でスティックを倒したとみなす量（最大32767）
constexpr float kStickMax = 32767.0f; // XInputのスティック値の最大値

/**
 * @brief 左スティックの倒し方が移動系の操作に当たるか
 * @note 下入力は打ち上げ技の条件になるため、移動中に少し下へ傾いただけで出ないよう深めに倒した時だけにする
 */
bool StickHeld(Input::Action action, const XINPUT_STATE& state, float deadzone)
{
    const float x = state.Gamepad.sThumbLX / kStickMax;
    const float y = state.Gamepad.sThumbLY / kStickMax;
    constexpr float kStickDownThreshold = 0.6f;
    switch (action) {
    case Input::Action::MoveLeft:
        return x <= -deadzone;
    case Input::Action::MoveRight:
        return x >= deadzone;
    case Input::Action::Down:
        return y <= -kStickDownThreshold;
    default:
        return false;
    }
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
    current_ = this;
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
    actionBindings_[static_cast<size_t>(Action::GunSwitch)] = { DIK_G, 0, XINPUT_GAMEPAD_DPAD_LEFT };
    actionBindings_[static_cast<size_t>(Action::Dodge)] = { DIK_I, 0, XINPUT_GAMEPAD_RIGHT_THUMB };
    actionBindings_[static_cast<size_t>(Action::Warp)] = { DIK_C, 0, XINPUT_GAMEPAD_LEFT_THUMB };
    actionBindings_[static_cast<size_t>(Action::Steal)] = { DIK_J, 0, kGamepadLeftTrigger };
    actionBindings_[static_cast<size_t>(Action::Interact)] = { DIK_RETURN, 0, kGamepadLeftTrigger };
    actionBindings_[static_cast<size_t>(Action::LockOn)] = { DIK_LSHIFT, 0, kGamepadRightTrigger };
    actionBindings_[static_cast<size_t>(Action::Pause)] = { DIK_ESCAPE, 0, XINPUT_GAMEPAD_START };

    const nlohmann::json root = JsonHelper::Load("Resources/Config/input_bindings.json");
    const auto actions = root.value("actions", nlohmann::json::object());
    for (const auto& [name, action] : kActionNames) {
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
    const size_t idx = static_cast<size_t>(action);
    if (actionOverrideEnabled_[idx]) {
        return actionOverridePressed_[idx];
    }
    const ActionBinding& binding = actionBindings_[idx];
    const bool keyboard = (binding.primaryKey && key_[binding.primaryKey])
        || (binding.secondaryKey && key_[binding.secondaryKey]);
    return keyboard || (binding.gamepadButton && (state_.Gamepad.wButtons & binding.gamepadButton))
        || StickHeld(action, state_, deadzone_);
}

bool Input::TriggerAction(Action action) const
{
    const size_t idx = static_cast<size_t>(action);
    if (actionOverrideEnabled_[idx]) {
        return actionOverridePressed_[idx] && !actionOverridePressedPrev_[idx];
    }
    const ActionBinding& binding = actionBindings_[idx];
    const bool keyboard = (binding.primaryKey && key_[binding.primaryKey] && !keyPre_[binding.primaryKey])
        || (binding.secondaryKey && key_[binding.secondaryKey] && !keyPre_[binding.secondaryKey]);
    const bool gamepad = binding.gamepadButton
        && (state_.Gamepad.wButtons & binding.gamepadButton)
        && !(previousState_.Gamepad.wButtons & binding.gamepadButton);
    const bool stick = StickHeld(action, state_, deadzone_) && !StickHeld(action, previousState_, deadzone_);
    return keyboard || gamepad || stick;
}

void Input::SetActionOverride(Action action, bool pressed)
{
    actionOverrideEnabled_[static_cast<size_t>(action)] = true;
    actionOverridePressed_[static_cast<size_t>(action)] = pressed;
}

void Input::ClearActionOverrides()
{
    actionOverrideEnabled_.fill(false);
    actionOverridePressed_.fill(false);
    actionOverridePressedPrev_.fill(false);
}

void Input::Update()
{
    // ゲームコントローラー更新
    UpdateGamepad();

    // オーバーライド版のTrigger判定用（実機キーのkey_/keyPre_と同じ前後関係を保つ）
    actionOverridePressedPrev_ = actionOverridePressed_;

    // 前回のキー入力を保存
    memcpy(keyPre_, key_, sizeof(key_));
    // キーボード情報の取得開始
    keyboard_->Acquire();
    // 全キーの入力情報を取得するフォーカス喪失などで失敗したらバッファをクリアして刺さり防止
    if (FAILED(keyboard_->GetDeviceState(sizeof(key_), key_))) {
        ZeroMemory(key_, sizeof(key_));
    }

    // 前回のマウス入力を保存
    mouseStatePre_ = mouseState_;
    // マウス情報の取得開始
    mouse_->Acquire();
    // 全マウスの入力情報を取得する失敗時はクリア
    if (FAILED(mouse_->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState_))) {
        ZeroMemory(&mouseState_, sizeof(DIMOUSESTATE2));
    }

    UpdateLastDevice();
}

void Input::UpdateLastDevice()
{
    if (WasGamepadDisconnected()) {
        usingGamepad_ = false;
        return;
    }
    for (int k = 0; k < 256; ++k) {
        if (key_[k] && !keyPre_[k]) {
            usingGamepad_ = false;
            return;
        }
    }
    const bool padPressed = (state_.Gamepad.wButtons & ~previousState_.Gamepad.wButtons) != 0;
    const bool stickMoved = std::abs(state_.Gamepad.sThumbLX) >= kStickMenuThreshold
        || std::abs(state_.Gamepad.sThumbLY) >= kStickMenuThreshold;
    if (WasGamepadConnected() || padPressed || stickMoved) {
        usingGamepad_ = true;
    }
}

bool Input::TriggerStick(int axisX, int axisY) const
{
    auto pushed = [&](const XINPUT_STATE& s) {
        const int x = s.Gamepad.sThumbLX;
        const int y = s.Gamepad.sThumbLY;
        return (axisX == 0 || x * axisX >= kStickMenuThreshold) && (axisY == 0 || y * axisY >= kStickMenuThreshold);
    };
    return pushed(state_) && !pushed(previousState_);
}

bool Input::TriggerMenuUp() const
{
    return (key_[DIK_W] && !keyPre_[DIK_W]) || (key_[DIK_UP] && !keyPre_[DIK_UP])
        || TriggerButton(XINPUT_GAMEPAD_DPAD_UP) || TriggerStick(0, 1);
}

bool Input::TriggerMenuDown() const
{
    return (key_[DIK_S] && !keyPre_[DIK_S]) || (key_[DIK_DOWN] && !keyPre_[DIK_DOWN])
        || TriggerButton(XINPUT_GAMEPAD_DPAD_DOWN) || TriggerStick(0, -1);
}

bool Input::TriggerMenuLeft() const
{
    return (key_[DIK_A] && !keyPre_[DIK_A]) || (key_[DIK_LEFT] && !keyPre_[DIK_LEFT])
        || TriggerButton(XINPUT_GAMEPAD_DPAD_LEFT) || TriggerStick(-1, 0);
}

bool Input::TriggerMenuRight() const
{
    return (key_[DIK_D] && !keyPre_[DIK_D]) || (key_[DIK_RIGHT] && !keyPre_[DIK_RIGHT])
        || TriggerButton(XINPUT_GAMEPAD_DPAD_RIGHT) || TriggerStick(1, 0);
}

bool Input::TriggerMenuConfirm() const
{
    return (key_[DIK_SPACE] && !keyPre_[DIK_SPACE]) || (key_[DIK_RETURN] && !keyPre_[DIK_RETURN])
        || TriggerButton(XINPUT_GAMEPAD_A);
}

bool Input::TriggerMenuCancel() const
{
    return (key_[DIK_ESCAPE] && !keyPre_[DIK_ESCAPE]) || (key_[DIK_BACK] && !keyPre_[DIK_BACK])
        || TriggerButton(XINPUT_GAMEPAD_B);
}

std::wstring Input::GetActionLabel(Action action) const
{
    const ActionBinding& binding = actionBindings_[static_cast<size_t>(action)];
    if (usingGamepad_ && binding.gamepadButton) {
        return PadButtonLabel(binding.gamepadButton);
    }
    if (usingGamepad_) {
        switch (action) {
        case Action::MoveLeft:
            return L"左スティック←";
        case Action::MoveRight:
            return L"左スティック→";
        case Action::Down:
            return L"左スティック↓";
        default:
            break;
        }
    }
    return binding.primaryKey ? KeyLabel(binding.primaryKey) : L"?";
}

std::wstring Input::ExpandPrompts(const std::wstring& text) const
{
    auto resolve = [&](const std::wstring& name, std::wstring& out) {
        if (name == L"Move") {
            out = usingGamepad_ ? L"左スティック" : GetActionLabel(Action::MoveLeft) + L" " + GetActionLabel(Action::MoveRight);
            return true;
        }
        if (name == L"Slot") {
            out = usingGamepad_ ? L"十字キー" : L"1-3";
            return true;
        }
        if (name == L"Confirm") {
            out = usingGamepad_ ? L"A" : L"Enter";
            return true;
        }
        if (name == L"Back") {
            out = usingGamepad_ ? L"B" : L"Esc";
            return true;
        }
        for (const auto& [actionName, action] : kActionNames) {
            const std::string narrow(actionName);
            if (name == std::wstring(narrow.begin(), narrow.end())) {
                out = GetActionLabel(action);
                return true;
            }
        }
        return false;
    };

    std::wstring result;
    result.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t open = text.find(L'{', pos);
        const size_t close = (open == std::wstring::npos) ? std::wstring::npos : text.find(L'}', open);
        if (close == std::wstring::npos) {
            result.append(text, pos, std::wstring::npos);
            break;
        }
        result.append(text, pos, open - pos);
        std::wstring label;
        if (resolve(text.substr(open + 1, close - open - 1), label)) {
            result += label;
        } else {
            result.append(text, open, close - open + 1);
        }
        pos = close + 1;
    }
    return result;
}

bool Input::PushKey(BYTE keyNumber)
{
    // 指定キーを押していればtrueを返す
    if (key_[keyNumber]) {
        return true;
    }

    // そうでなければfalseを返す
    return false;
}

bool Input::TriggerKey(BYTE keyNumber)
{
    return key_[keyNumber] && !keyPre_[keyNumber];
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

    // アナログのトリガーを空きビットへ載せ、ボタンと同じPush/Trigger判定で扱えるようにする
    if (state_.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        state_.Gamepad.wButtons |= kGamepadLeftTrigger;
    }
    if (state_.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        state_.Gamepad.wButtons |= kGamepadRightTrigger;
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
    float x = (float)state_.Gamepad.sThumbLX / kStickMax;
    float y = (float)state_.Gamepad.sThumbLY / kStickMax;

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
