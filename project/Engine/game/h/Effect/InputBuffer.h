/**
 * @file InputBuffer.h
 * @brief 入力バッファ（N フレーム分の入力履歴を保持するシングルトン）
 *
 * 【概要】
 *   格闘ゲームや素早い操作が必要なアクションゲームでよく使われる仕組み
 *   「N フレーム以内にボタンが押されていたか」を判定できるため、
 *   操作の猶予（バッファリング）やコマンド入力の検出に使える
 *
 * 【使い方】
 *   // 毎フレーム（Input::Update() の後、Game::Update() などで）
 *   InputBuffer::GetInstance()->Update(input_);
 *
 *   // 10 フレーム以内にジャンプキーが押されていたか（接地直前の入力受け付け）
 *   if (isGrounded_ && InputBuffer::GetInstance()->WasKeyPressed(DIK_SPACE, 10)) {
 *       Jump();
 *   }
 *
 *   // 8 フレーム以内に A ボタンが押されていたか（コントローラー）
 *   if (InputBuffer::GetInstance()->WasButtonPressed(XINPUT_GAMEPAD_A, 8)) {
 *       Attack();
 *   }
 */
#pragma once
#include "Input.h"
#include <array>
#include <cstdint>
namespace engine::game {
using engine::Input;

class InputBuffer {
public:
    /// @brief 保持する最大フレーム数（60 フレーム = 約 1 秒分）
    static constexpr int kHistorySize = 60;

    /// @brief シングルトンインスタンスを取得する
    static InputBuffer* GetInstance();

    /**
     * @brief 毎フレーム入力状態を記録する
     * @param input Input クラスのポインタ（Input::Update() の後に呼ぶこと）
     */
    void Update(Input* input);

    /**
     * @brief 指定キーが直近 N フレーム以内に押されていたかを判定する
     * @param keyCode      DIK_xxx 定数（例: DIK_SPACE）
     * @param withinFrames 何フレーム以内を調べるか（1〜kHistorySize）
     * @return 指定フレーム以内に一度でも押されていれば true
     */
    bool WasKeyPressed(BYTE keyCode, int withinFrames) const;

    /**
     * @brief 指定ボタンが直近 N フレーム以内に押されていたかを判定する
     * @param button       XINPUT_GAMEPAD_xxx 定数（例: XINPUT_GAMEPAD_A）
     * @param withinFrames 何フレーム以内を調べるか（1〜kHistorySize）
     * @return 指定フレーム以内に一度でも押されていれば true
     */
    bool WasButtonPressed(WORD button, int withinFrames) const;

private:
    InputBuffer() = default;

    /// @brief 1 フレーム分の入力状態スナップショット
    struct FrameState {
        BYTE keys[256] = { }; ///< キーボード全キーの押下状態（1=押してる, 0=押してない）
        WORD buttons = 0; ///< コントローラーボタンのビットフラグ
        bool valid = false; ///< このスロットに有効なデータが入っているか
    };

    /// @brief リングバッファ形式の入力履歴（古いフレームを上書きしながら使う）
    std::array<FrameState, kHistorySize> history_ = { };
    int head_ = 0; ///< 最新フレームが格納されているインデックス
    int count_ = 0; ///< バッファ内の有効フレーム数（最大 kHistorySize）
};

} // namespace engine::game
