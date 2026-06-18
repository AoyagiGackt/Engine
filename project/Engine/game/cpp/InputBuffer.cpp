#include "InputBuffer.h"
#include <algorithm>
#include <cstring>

InputBuffer* InputBuffer::GetInstance()
{
    static InputBuffer instance;
    return &instance;
}

void InputBuffer::Update(Input* input)
{
    // head_ を 1 つ進めてリングバッファの最新スロットへ移動
    head_  = (head_ + 1) % kHistorySize;
    // 有効フレーム数をインクリメント（最大 kHistorySize でクランプ）
    count_ = std::min(count_ + 1, kHistorySize);

    FrameState& frame = history_[head_];
    frame.valid = true;

    // キーボード: 全 256 キーの押下状態を記録
    for (int i = 0; i < 256; ++i) {
        frame.keys[i] = input->PushKey(static_cast<BYTE>(i)) ? 1 : 0;
    }

    // コントローラー: よく使うボタンを OR で合成してビットフラグとして保存
    // 注: Input::PushButton() はビットマスクを受け取り、そのビットが立っているか返す
    frame.buttons = 0;
    static const WORD kAllButtons[] = {
        XINPUT_GAMEPAD_A,              // ×（PS）/ A（Xbox）
        XINPUT_GAMEPAD_B,              // ○（PS）/ B（Xbox）
        XINPUT_GAMEPAD_X,              // □（PS）/ X（Xbox）
        XINPUT_GAMEPAD_Y,              // △（PS）/ Y（Xbox）
        XINPUT_GAMEPAD_START,          // スタート / オプション
        XINPUT_GAMEPAD_BACK,           // セレクト / シェア
        XINPUT_GAMEPAD_LEFT_THUMB,     // L3
        XINPUT_GAMEPAD_RIGHT_THUMB,    // R3
        XINPUT_GAMEPAD_LEFT_SHOULDER,  // L1 / LB
        XINPUT_GAMEPAD_RIGHT_SHOULDER, // R1 / RB
        XINPUT_GAMEPAD_DPAD_UP,        // 十字キー上
        XINPUT_GAMEPAD_DPAD_DOWN,      // 十字キー下
        XINPUT_GAMEPAD_DPAD_LEFT,      // 十字キー左
        XINPUT_GAMEPAD_DPAD_RIGHT,     // 十字キー右
    };
    for (WORD btn : kAllButtons) {
        if (input->PushButton(btn)) {
            frame.buttons |= btn; // 押されているボタンのビットを立てる
        }
    }
}

bool InputBuffer::WasKeyPressed(BYTE keyCode, int withinFrames) const
{
    // 実際に保持しているフレーム数を超えて調べることはできない
    withinFrames = std::min(withinFrames, count_);

    // head_ から過去に向かって withinFrames 個のスロットを調べる
    for (int i = 0; i < withinFrames; ++i) {
        // リングバッファのインデックス計算（負になっても正しくラップする）
        int idx = (head_ - i + kHistorySize) % kHistorySize;
        if (history_[idx].valid && history_[idx].keys[keyCode]) {
            return true; // 1 フレームでも押されていたら true を返す
        }
    }
    return false;
}

bool InputBuffer::WasButtonPressed(WORD button, int withinFrames) const
{
    withinFrames = std::min(withinFrames, count_);

    for (int i = 0; i < withinFrames; ++i) {
        int idx = (head_ - i + kHistorySize) % kHistorySize;
        // ビットマスクで指定ボタンが押されていたか確認
        if (history_[idx].valid && (history_[idx].buttons & button)) {
            return true;
        }
    }
    return false;
}
