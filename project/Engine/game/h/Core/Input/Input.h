/**
 * @file Input.h
 * @brief DirectInput 8 を使用したキーボード入力管理を行うファイル
 */
#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <XInput.h>
#include <dinput.h>

#include "WinApp.h"
#include <array>
#include <string>
#include <wrl/client.h>

#pragma comment(lib, "xinput.lib")
namespace engine {
/**
 * @brief キーボード入力を一括管理するクラス
 * @note DirectInput 8 を使用して、全256キーの状態を取得・管理します
 * 押しっぱなし判定（PushKey）と、押した瞬間判定（TriggerKey）を提供します
 */
class Input {
public: // メンバ関数
    /** @brief ゲームコードが物理キーへ依存しないための操作名 */
    enum class Action {
        MoveLeft,
        MoveRight,
        Jump,
        Down,
        Attack,
        Shoot,
        Skill,
        Awaken,
        Finisher,
        GunSwitch,
        Dodge,
        Warp,
        Steal,
        Interact,
        LockOn,
        Pause,
        Count
    };

    /** @brief XInputが使っていないビットへ割り当てたトリガー（wButtonsと同じ扱いで押下判定できる） */
    static constexpr WORD kGamepadLeftTrigger = 0x0400;
    static constexpr WORD kGamepadRightTrigger = 0x0800;
    // namespace省略
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    /**
     * @brief 入力システムの初期化
     * @param winApp ウィンドウ管理クラスのポインタ
     * @note DirectInput インスタンスの生成、キーボードデバイスの初期化、データ形式の設定などを行います
     */
    void Initialize(WinApp* winApp);

    /**
     * @brief キー状態の更新
     * @note 毎フレーム呼び出すことで、現在のキー状態を前回の状態にコピーし、
     * デバイスから最新のキー状態を取得して更新します
     */
    void Update();

    /**
     * @brief キーが押されている（押しっぱなし）かチェック
     * @param keyNumber キーの番号（例: DIK_SPACE）
     * @return bool 押されていれば true
     */
    bool PushKey(BYTE keyNumber);

    /**
     * @brief キーが押された瞬間かチェック
     * @param keyNumber キーの番号（例: DIK_RETURN）
     * @return bool このフレームで押された瞬間なら true
     * @note 1フレーム前が離されていたかつ現在が押されている場合に true を返します
     */
    bool TriggerKey(BYTE keyNumber);

    /** @brief 毎フレームの更新（コントローラー用） */
    void UpdateGamepad();

    /** @brief ボタンが押されているか */
    bool PushButton(WORD button) const { return (state_.Gamepad.wButtons & button); }

    /** @brief ボタンが押された瞬間か */
    bool TriggerButton(WORD button) const
    {
        return (state_.Gamepad.wButtons & button) && !(previousState_.Gamepad.wButtons & button);
    }

    /** @brief 左スティックの値を 0.0 ~ 1.0 の範囲で取得 */
    struct Stick {
        float x, y;
    };

    Stick GetLeftStick() const;

    /** @brief 指定アクションが押されているかを返す */
    bool PushAction(Action action) const;

    /** @brief 指定アクションが押された瞬間かを返す */
    bool TriggerAction(Action action) const;

    /**
     * @brief 指定アクションの押下状態を実機入力の代わりに強制する（タイトルデモ等の自動操作用）
     * @note 呼んだアクションだけ実機入力を無視するようになる（PushKey/TriggerKey等の生キー参照や、
     * オーバーライドしていない他のActionには一切影響しない）。毎フレーム希望する状態で呼び直すこと
     * （1フレームだけtrueにしてTriggerAction()へパルスを起こしたい場合、次フレームは明示的にfalseで呼ぶ）
     */
    void SetActionOverride(Action action, bool pressed);

    /** @brief 全アクションのオーバーライドを解除し、実機入力の参照に戻す */
    void ClearActionOverrides();

    /** @brief ゲームパッドが現在接続されているかを返す */
    bool IsGamepadConnected() const { return gamepadConnected_; }

    /** @brief このフレームでゲームパッドが接続されたかを返す */
    bool WasGamepadConnected() const { return gamepadConnected_ && !gamepadConnectedPrevious_; }

    /** @brief このフレームでゲームパッドが切断されたかを返す */
    bool WasGamepadDisconnected() const { return !gamepadConnected_ && gamepadConnectedPrevious_; }

    /** @brief 最後に操作した機器がゲームパッドか（操作説明の表記切り替えに使う） */
    bool IsUsingGamepad() const { return usingGamepad_; }

    // メニュー操作（キーボード・十字キー・左スティックのどれでも反応する）
    bool TriggerMenuUp() const;
    bool TriggerMenuDown() const;
    bool TriggerMenuLeft() const;
    bool TriggerMenuRight() const;
    /** @brief 決定（Space/Enter/Aボタン） */
    bool TriggerMenuConfirm() const;
    /** @brief 戻る（Esc/Backspace/Bボタン） */
    bool TriggerMenuCancel() const;

    /** @brief 操作の表示名を、最後に使った機器に合わせて返す（例: キーボードなら "J"、パッドなら "LT"） */
    std::wstring GetActionLabel(Action action) const;

    /**
     * @brief 文章中の {操作名} を、最後に使った機器のボタン名へ置き換える
     * @param text {Steal} や {Dodge} などを含む文章。Action名のほかに {Move} {Slot} {Confirm} {Back} が使える
     * @return 置き換え後の文章（知らない名前はそのまま残す）
     */
    std::wstring ExpandPrompts(const std::wstring& text) const;

    /** @brief 最後に初期化した入力（シーンから入力を渡されないHUD等が操作説明を作るために使う） */
    static const Input* GetCurrent() { return current_; }

    // マウス関連
    bool TriggerMouseButton(int32_t buttonNumber);
    /** @brief マウスボタンが押されている間trueを返す */
    bool PushMouseButton(int32_t buttonNumber) const
    {
        return buttonNumber >= 0 && buttonNumber < 8 && (mouseState_.rgbButtons[buttonNumber] & 0x80) != 0;
    }
    /** @brief クライアント領域を基準にしたマウス座標を取得する */
    POINT GetMouseClientPosition() const
    {
        POINT point = {};
        GetCursorPos(&point);
        if (winApp_) { ScreenToClient(winApp_->GetHwnd(), &point); }
        return point;
    }

    /** @brief マウスのホイールスクロール量を取得する */
    int32_t GetWheel() const { return mouseState_.lZ; }

private:
    struct ActionBinding {
        BYTE primaryKey = 0;
        BYTE secondaryKey = 0;
        WORD gamepadButton = 0;
    };

    /** @brief Resources/Config/input_bindings.jsonから操作割り当てを読み込む */
    void LoadActionBindings();
    /** @brief DirectInput 8 の本体ポインタ */
    ComPtr<IDirectInput8> directInput_;

    /** @brief キーボードデバイスのポインタ */
    ComPtr<IDirectInputDevice8> keyboard_;

    // キー状態管理用バッファ

    /** @brief 最新のキー状態（256個のキー分） */
    BYTE key_[256] = { };

    /** @brief 1フレーム前のキー状態（256個のキー分） */
    BYTE keyPre_[256] = { };

    /** @brief ウィンドウ管理のポインタ */
    WinApp* winApp_ = nullptr;

    // コントローラー状態管理用

    XINPUT_STATE state_ { }; /// 現在のコントローラー状態
    XINPUT_STATE previousState_ { }; /// 前回のコントローラー状態
    const float deadzone_ = 0.2f; /// デッドゾーン
    bool gamepadConnected_ = false; ///< 現在の接続状態
    bool gamepadConnectedPrevious_ = false; ///< 前フレームの接続状態
    bool usingGamepad_ = false; ///< 最後に操作した機器がゲームパッドか
    std::array<ActionBinding, static_cast<size_t>(Action::Count)> actionBindings_ { };

    static inline const Input* current_ = nullptr;

    /** @brief 左スティックが指定方向へこのフレームに倒されたか（倒しっぱなしでは連続しない） */
    bool TriggerStick(int axisX, int axisY) const;
    /** @brief このフレームの入力から、最後に使った機器を更新する */
    void UpdateLastDevice();

    // アクションオーバーライド（SetActionOverride参照）。有効なアクションだけ実機入力より優先する
    std::array<bool, static_cast<size_t>(Action::Count)> actionOverrideEnabled_ { };
    std::array<bool, static_cast<size_t>(Action::Count)> actionOverridePressed_ { };
    std::array<bool, static_cast<size_t>(Action::Count)> actionOverridePressedPrev_ { };

    // マウス状態管理用

    /** @brief マウスデバイスのポインタ */
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;
    /** @brief マウスの現在の状態 */
    DIMOUSESTATE2 mouseState_ = { };
    /** @brief マウスの前回の状態 */
    DIMOUSESTATE2 mouseStatePre_ = { };
};

} // namespace engine
