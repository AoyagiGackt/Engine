/**
 * @file Input.cpp
 * @brief DirectInput/XInputによるキーボード・ゲームパッド・マウス入力の取得とアクション判定（Input）の実装
 */
#include "Input.h"
#include "CrashBreadcrumbs.h"
#include "EngineAssert.h"
#include "JsonHelper.h"
#include "Logger.h"
#include <cmath>
#include <dinput.h>
#include <iterator>
#include <utility>

namespace engine {
namespace {

constexpr int kMouseButtonCount = 3; // 左・右・中だけ記録する
constexpr const char* kMouseButtonNames[kMouseButtonCount] = { "左", "右", "中" };
constexpr size_t kKeyNameCapacity = 32;
constexpr LONG kScanCodeShift = 16; // GetKeyNameText に渡すスキャンコードの位置
constexpr LONG kExtendedKeyFlag = 1 << 24; // 拡張キー（矢印キーなど）の印
constexpr BYTE kDikExtendedBit = 0x80; // DIK の値で拡張キーを表すビット
constexpr BYTE kDikScanCodeMask = 0x7F;

/** @brief DIK のキー番号から、キーボードに書かれている名前を返す（取れなければ番号） */
std::string DikKeyName(int key) {
    const BYTE dik = static_cast<BYTE>(key);
    LONG param = static_cast<LONG>(dik & kDikScanCodeMask) << kScanCodeShift;
    if ((dik & kDikExtendedBit) != 0) {
        param |= kExtendedKeyFlag;
    }
    char name[kKeyNameCapacity] = {};
    if (GetKeyNameTextA(param, name, static_cast<int>(sizeof(name))) > 0) {
        return name;
    }
    return "DIK " + std::to_string(key);
}

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
    { "P", DIK_P, L"P" },
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
    { "MoveLeft", Input::Action::kMoveLeft },
    { "MoveRight", Input::Action::kMoveRight },
    { "Jump", Input::Action::kJump },
    { "Down", Input::Action::kDown },
    { "Attack", Input::Action::kAttack },
    { "Shoot", Input::Action::kShoot },
    { "Skill", Input::Action::kSkill },
    { "Awaken", Input::Action::kAwaken },
    { "Finisher", Input::Action::kFinisher },
    { "GunSwitch", Input::Action::kGunSwitch },
    { "Dodge", Input::Action::kDodge },
    { "Warp", Input::Action::kWarp },
    { "Steal", Input::Action::kSteal },
    { "Interact", Input::Action::kInteract },
    { "LockOn", Input::Action::kLockOn },
    { "Pause", Input::Action::kPause },
};
static_assert(std::size(kActionNames) == static_cast<size_t>(Input::Action::kCount), "操作名の表にActionの追加漏れがある");

/** @brief 操作の割り当てに依らず表記が決まっている {名前}（パッド時とキーボード時の表記） */
struct FixedPrompt {
    const wchar_t* name;
    const wchar_t* padLabel;
    const wchar_t* keyLabel;
};
constexpr FixedPrompt kFixedPrompts[] = {
    { L"Slot", L"十字キー", L"1-3" },
    { L"Confirm", L"A", L"Enter" },
    { L"Back", L"B", L"Backspace" },
};

/** @brief 操作の割り当てには使わないが、自動テストで押したいキーの名前 */
constexpr std::pair<const char*, BYTE> kExtraKeyNames[] = {
    { "F1", DIK_F1 }, { "F2", DIK_F2 }, { "F3", DIK_F3 }, { "F4", DIK_F4 }, { "F5", DIK_F5 }, { "F6", DIK_F6 },
    { "F7", DIK_F7 }, { "F8", DIK_F8 }, { "F9", DIK_F9 }, { "F10", DIK_F10 }, { "F11", DIK_F11 }, { "F12", DIK_F12 },
    { "T", DIK_T }, { "Backspace", DIK_BACK }, { "Tab", DIK_TAB },
    { "1", DIK_1 }, { "2", DIK_2 }, { "3", DIK_3 },
};

/** @brief 設定ファイルのキー名を仮想キーコードへ変換する */
BYTE ParseKey(const std::string& name) {
    for (const KeyName& entry : kKeyNames) {
        if (name == entry.name) {
            return entry.key;
        }
    }
    return 0;
}

/** @brief 設定ファイルのボタン名をゲームパッドのボタンのビットへ変換する */
WORD ParseGamepadButton(const std::string& name) {
    for (const PadButtonName& entry : kPadButtonNames) {
        if (name == entry.name) {
            return entry.button;
        }
    }
    return 0;
}

/** @brief 画面に表示するキーボードのキー名を返す */
std::wstring KeyLabel(BYTE key) {
    for (const KeyName& entry : kKeyNames) {
        if (entry.key == key) {
            return entry.label;
        }
    }
    return L"?";
}

/** @brief 画面に表示するゲームパッドのボタン名を返す */
std::wstring PadButtonLabel(WORD button) {
    for (const PadButtonName& entry : kPadButtonNames) {
        if (entry.button == button) {
            return entry.label;
        }
    }
    return L"?";
}

constexpr SHORT kStickMenuThreshold = 16000; // メニュー操作でスティックを倒したとみなす量（最大32767）
constexpr float kStickMax = 32767.0f; // XInputのスティック値の最大値
// 下入力は打ち上げ技の条件になるため、移動中に少し下へ傾いただけで出ないよう深めに倒した時だけにする
constexpr float kStickDownThreshold = 0.6f;

/** @brief 左スティックでも入力できる操作と、その判定・操作説明の表記 */
struct StickAction {
    Input::Action action;
    const wchar_t* label;
    bool (*held)(float x, float y, float deadzone); ///< 正規化したスティック値で倒れているか
};
constexpr StickAction kStickActions[] = {
    {
        Input::Action::kMoveLeft, L"左スティック←", [](float x, float, float deadzone) {
            return x <= -deadzone;
        }
    },
    {
        Input::Action::kMoveRight, L"左スティック→", [](float x, float, float deadzone) {
            return x >= deadzone;
        }
    },
    {
        Input::Action::kDown, L"左スティック↓", [](float, float y, float) {
            return y <= -kStickDownThreshold;
        }
    },
};

/** @brief アクションに割り当てたスティックの方向を探す（割り当てが無ければ nullptr） */
const StickAction* FindStickAction(Input::Action action) {
    for (const StickAction& entry : kStickActions) {
        if (entry.action == action) {
            return &entry;
        }
    }
    return nullptr;
}

/** @brief 左スティックの倒し方が移動系の操作に当たるか */
bool StickHeld(Input::Action action, const XINPUT_STATE& state, float deadzone) {
    const StickAction* entry = FindStickAction(action);
    if (entry == nullptr) {
        return false;
    }
    return entry->held(state.Gamepad.sThumbLX / kStickMax, state.Gamepad.sThumbLY / kStickMax, deadzone);
}

} // namespace

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

/** @brief 入力システムの初期化 */
void Input::Initialize(WinApp* winApp) {
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
    sCurrent = this;
}

/** @brief Resources/Config/input_bindings.jsonから操作割り当てを読み込む */
void Input::LoadActionBindings() {
    // 設定ファイルが無い場合も操作不能にならない既定割り当てを先に設定する
    // 値の並びは { 主キー, 副キー, ゲームパッドのボタン }。0 は割り当て無し
    // 移動・ジャンプ・しゃがみは矢印キーでも操作できるよう副キーを持たせる
    actionBindings_[static_cast<size_t>(Action::kMoveLeft)] = { DIK_A, DIK_LEFT, 0 };
    actionBindings_[static_cast<size_t>(Action::kMoveRight)] = { DIK_D, DIK_RIGHT, 0 };
    actionBindings_[static_cast<size_t>(Action::kJump)] = { DIK_W, DIK_UP, XINPUT_GAMEPAD_A };
    actionBindings_[static_cast<size_t>(Action::kDown)] = { DIK_S, DIK_DOWN, 0 };
    // 攻撃系（近接・射撃・スキル・覚醒・とどめ・銃の切り替え）
    actionBindings_[static_cast<size_t>(Action::kAttack)] = { DIK_L, 0, XINPUT_GAMEPAD_X };
    actionBindings_[static_cast<size_t>(Action::kShoot)] = { DIK_K, 0, XINPUT_GAMEPAD_Y };
    actionBindings_[static_cast<size_t>(Action::kSkill)] = { DIK_SPACE, 0, XINPUT_GAMEPAD_B };
    actionBindings_[static_cast<size_t>(Action::kAwaken)] = { DIK_R, 0, XINPUT_GAMEPAD_RIGHT_SHOULDER };
    actionBindings_[static_cast<size_t>(Action::kFinisher)] = { DIK_F, 0, XINPUT_GAMEPAD_LEFT_SHOULDER };
    actionBindings_[static_cast<size_t>(Action::kGunSwitch)] = { DIK_G, 0, XINPUT_GAMEPAD_DPAD_LEFT };
    // 回避・ワープ・武器奪取・調べる・ロックオン
    actionBindings_[static_cast<size_t>(Action::kDodge)] = { DIK_I, 0, XINPUT_GAMEPAD_RIGHT_THUMB };
    actionBindings_[static_cast<size_t>(Action::kWarp)] = { DIK_C, 0, XINPUT_GAMEPAD_LEFT_THUMB };
    actionBindings_[static_cast<size_t>(Action::kSteal)] = { DIK_J, 0, kGamepadLeftTrigger };
    actionBindings_[static_cast<size_t>(Action::kInteract)] = { DIK_RETURN, 0, kGamepadLeftTrigger };
    actionBindings_[static_cast<size_t>(Action::kLockOn)] = { DIK_LSHIFT, 0, kGamepadRightTrigger };
    // ポーズ
    actionBindings_[static_cast<size_t>(Action::kPause)] = { DIK_P, 0, XINPUT_GAMEPAD_START };

    // input_bindings.json に書かれたアクションだけ、キー名・ボタン名を読み取って上書きする
    const nlohmann::json root = JsonHelper::Load("Resources/Config/input_bindings.json");
    const auto actions = root.value("actions", nlohmann::json::object());
    for (const auto& [name, action] : kActionNames) {
        // 書かれていないアクションは既定の割り当てのまま残す
        const auto data = actions.value(name, nlohmann::json::object());
        if (!data.is_object() || data.empty()) {
            continue;
        }
        // キー名・ボタン名を内部のコードに変換して上書きする（空や未知の名前は割り当て無しになる）
        ActionBinding& binding = actionBindings_[static_cast<size_t>(action)];
        binding.primaryKey = ParseKey(data.value("primary", ""));
        binding.secondaryKey = ParseKey(data.value("secondary", ""));
        binding.gamepadButton = ParseGamepadButton(data.value("gamepad", ""));
    }
}

/** @brief 指定アクションが押されているかを返す */
bool Input::PushAction(Action action) const {
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

/** @brief 指定アクションが押された瞬間かを返す */
bool Input::TriggerAction(Action action) const {
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

/** @brief 指定アクションの押下状態を実機入力の代わりに強制する（タイトルデモ等の自動操作用） */
void Input::SetActionOverride(Action action, bool pressed) {
    actionOverrideEnabled_[static_cast<size_t>(action)] = true;
    actionOverridePressed_[static_cast<size_t>(action)] = pressed;
}

/** @brief 全アクションのオーバーライドを解除し、実機入力の参照に戻す */
void Input::ClearActionOverrides() {
    actionOverrideEnabled_.fill(false);
    actionOverridePressed_.fill(false);
    actionOverridePressedPrev_.fill(false);
}

/** @brief キー状態の更新 */
void Input::Update() {
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
    // 自動テストが押したことにしているキーを重ねる（ウィンドウが裏にあって実機の入力が取れない時も効く）
    constexpr BYTE kKeyDown = 0x80;
    for (int k = 0; k < kKeyCount; ++k) {
        if (injectedKeys_[k]) {
            key_[k] = kKeyDown;
        }
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
    RecordInputBreadcrumbs();
}

/** @brief 自動テスト用に、キーを押したことにする */
void Input::InjectKey(BYTE keyNumber, bool pressed) {
    injectedKeys_[keyNumber] = pressed;
}

/** @brief 押したことにしていたキーをすべて離す */
void Input::ClearInjectedKeys() {
    injectedKeys_.fill(false);
}

/** @brief キーの名前から DIK のキー番号を返す（知らない名前なら 0） */
BYTE Input::KeyFromName(const std::string& name) {
    if (const BYTE key = ParseKey(name); key != 0) {
        return key;
    }
    for (const auto& [extraName, key] : kExtraKeyNames) {
        if (name == extraName) {
            return key;
        }
    }
    return 0;
}

/** @brief このフレームに押されたキー・マウス・パッドのボタンを、落ちた時の調査用に直前の出来事へ記録する */
void Input::RecordInputBreadcrumbs() const {
    // 押しっぱなしは記録せず、押した瞬間だけ残す（記録の数には限りがあるため）
    for (int k = 0; k < kKeyCount; ++k) {
        if (key_[k] && !keyPre_[k]) {
            CrashBreadcrumbs::Add("key", DikKeyName(k));
        }
    }
    for (int button = 0; button < kMouseButtonCount; ++button) {
        if (mouseState_.rgbButtons[button] && !mouseStatePre_.rgbButtons[button]) {
            CrashBreadcrumbs::Add("mouse", kMouseButtonNames[button]);
        }
    }
    const WORD pressed = static_cast<WORD>(state_.Gamepad.wButtons & ~previousState_.Gamepad.wButtons);
    for (const PadButtonName& entry : kPadButtonNames) {
        if ((pressed & entry.button) != 0) {
            CrashBreadcrumbs::Add("pad", entry.name);
        }
    }
}

/** @brief このフレームの入力から、最後に使った機器を更新する */
void Input::UpdateLastDevice() {
    if (WasGamepadDisconnected()) {
        usingGamepad_ = false;
        return;
    }
    for (int k = 0; k < kKeyCount; ++k) {
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

/** @brief 左スティックが指定方向へこのフレームに倒されたか（倒しっぱなしでは連続しない） */
bool Input::TriggerStick(int axisX, int axisY) const {
    auto pushed = [&](const XINPUT_STATE& s) {
        const int x = s.Gamepad.sThumbLX;
        const int y = s.Gamepad.sThumbLY;
        return (axisX == 0 || x * axisX >= kStickMenuThreshold) && (axisY == 0 || y * axisY >= kStickMenuThreshold);
    };
    return pushed(state_) && !pushed(previousState_);
}

/** @brief メニューの上入力がこのフレームで押されたかを返す */
bool Input::TriggerMenuUp() const {
    return (key_[DIK_W] && !keyPre_[DIK_W]) || (key_[DIK_UP] && !keyPre_[DIK_UP])
        || TriggerButton(XINPUT_GAMEPAD_DPAD_UP) || TriggerStick(0, 1);
}

/** @brief メニューの下入力がこのフレームで押されたかを返す */
bool Input::TriggerMenuDown() const {
    return (key_[DIK_S] && !keyPre_[DIK_S]) || (key_[DIK_DOWN] && !keyPre_[DIK_DOWN])
        || TriggerButton(XINPUT_GAMEPAD_DPAD_DOWN) || TriggerStick(0, -1);
}

/** @brief メニューの左入力がこのフレームで押されたかを返す */
bool Input::TriggerMenuLeft() const {
    return (key_[DIK_A] && !keyPre_[DIK_A]) || (key_[DIK_LEFT] && !keyPre_[DIK_LEFT])
        || TriggerButton(XINPUT_GAMEPAD_DPAD_LEFT) || TriggerStick(-1, 0);
}

/** @brief メニューの右入力がこのフレームで押されたかを返す */
bool Input::TriggerMenuRight() const {
    return (key_[DIK_D] && !keyPre_[DIK_D]) || (key_[DIK_RIGHT] && !keyPre_[DIK_RIGHT])
        || TriggerButton(XINPUT_GAMEPAD_DPAD_RIGHT) || TriggerStick(1, 0);
}

/** @brief 決定（Space/Enter/Aボタン） */
bool Input::TriggerMenuConfirm() const {
    return (key_[DIK_SPACE] && !keyPre_[DIK_SPACE]) || (key_[DIK_RETURN] && !keyPre_[DIK_RETURN])
        || TriggerButton(XINPUT_GAMEPAD_A);
}

/** @brief 戻る（Backspace/Bボタン。Escはアプリ終了なので含めない） */
bool Input::TriggerMenuCancel() const {
    return (key_[DIK_BACK] && !keyPre_[DIK_BACK]) || TriggerButton(XINPUT_GAMEPAD_B);
}

/** @brief 演出を飛ばす（決定・攻撃・ジャンプのどれか） */
bool Input::TriggerCinematicSkip() const {
    return TriggerMenuConfirm() || TriggerAction(Action::kAttack) || TriggerAction(Action::kJump);
}

/** @brief 操作の表示名を、最後に使った機器に合わせて返す（例: キーボードなら "J"、パッドなら "LT"） */
std::wstring Input::GetActionLabel(Action action) const {
    const ActionBinding& binding = actionBindings_[static_cast<size_t>(action)];
    if (usingGamepad_ && binding.gamepadButton) {
        return PadButtonLabel(binding.gamepadButton);
    }
    if (usingGamepad_) {
        if (const StickAction* stick = FindStickAction(action)) {
            return stick->label;
        }
    }
    return binding.primaryKey ? KeyLabel(binding.primaryKey) : L"?";
}

/** @brief 文章中の {操作名} を、最後に使った機器のボタン名へ置き換える */
std::wstring Input::ExpandPrompts(const std::wstring& text) const {
    // {名前} の名前を表示用の文字に置き換える。移動→固定の案内→アクション名の順に探す
    auto resolve = [&](const std::wstring& name, std::wstring& out) {
        if (name == L"Move") {
            out = usingGamepad_ ? L"左スティック" : GetActionLabel(Action::kMoveLeft) + L" " + GetActionLabel(Action::kMoveRight);
            return true;
        }
        for (const FixedPrompt& prompt : kFixedPrompts) {
            if (name == prompt.name) {
                out = usingGamepad_ ? prompt.padLabel : prompt.keyLabel;
                return true;
            }
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

    // 文字列を先頭から見て、{ } で囲まれた部分だけを置き換える
    std::wstring result;
    result.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t open = text.find(L'{', pos);
        const size_t close = (open == std::wstring::npos) ? std::wstring::npos : text.find(L'}', open);
        // 閉じていない { は置き換えず、残りをそのまま付ける
        if (close == std::wstring::npos) {
            result.append(text, pos, std::wstring::npos);
            break;
        }
        result.append(text, pos, open - pos);
        // 知らない名前は { } ごと元の文字のまま残す
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

/** @brief キーが押されている（押しっぱなし）かチェック */
bool Input::PushKey(BYTE keyNumber) {
    // 指定キーを押していればtrueを返す
    if (key_[keyNumber]) {
        return true;
    }

    // そうでなければfalseを返す
    return false;
}

/** @brief キーが押された瞬間かチェック */
bool Input::TriggerKey(BYTE keyNumber) {
    return key_[keyNumber] && !keyPre_[keyNumber];
}

/**
 * @brief ゲームパッドの状態更新
 */
void Input::UpdateGamepad() {
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
Input::Stick Input::GetLeftStick() const {
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

/** @brief マウスボタンがこのフレームで押されたかを返す */
bool Input::TriggerMouseButton(int32_t buttonNumber) {
    // 今回押されていて、前回押されていないかチェック
    if (mouseState_.rgbButtons[buttonNumber] && !mouseStatePre_.rgbButtons[buttonNumber]) {
        return true;
    }

    return false;
}

} // namespace engine
