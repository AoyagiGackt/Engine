/**
 * @file SceneEffectBridge.h
 * @brief シーンが所有する演出（カメラシェイク等）をノードグラフから呼ぶための橋渡し
 * @note CameraShakerは各シーンのメンバなので、シーンがInitializeでハンドラを登録し、Finalizeで解除する。
 * 登録が無い間に呼ばれても何もしない（グラフ側はシーンの種類を知らなくてよい）
 */
#pragma once
#include <functional>
#include <string>
namespace engine::game {

/**
 * @brief シーン側の演出ハンドラを保持し、グラフのノードから呼び出すシングルトン
 */
class SceneEffectBridge {
public:
    using CameraShakeHandler = std::function<void(float amount, float seconds)>;
    using MessageHandler = std::function<void(const std::string& text, float seconds)>;
    using CameraFocusHandler = std::function<void(float x, float y, float seconds)>;
    using ControlLockHandler = std::function<void(bool locked)>;

    /**
     * @brief 唯一のSceneEffectBridgeインスタンスを取得する（未生成なら生成する）
     * @return SceneEffectBridgeのインスタンス
     */
    static SceneEffectBridge* GetInstance();

    /** @brief カメラシェイクの実体を登録する（空のstd::functionで解除） */
    void SetCameraShakeHandler(CameraShakeHandler handler) {
        cameraShake_ = std::move(handler);
    }

    /**
     * @brief カメラシェイクを要求する
     * @param amount 揺れの強さ（ワールド単位）
     * @param seconds 揺れの継続秒数
     */
    void RequestCameraShake(float amount, float seconds) const {
        if (cameraShake_) {
            cameraShake_(amount, seconds);
        }
    }

    /** @brief 画面にメッセージを出す実体を登録する（空のstd::functionで解除） */
    void SetMessageHandler(MessageHandler handler) {
        message_ = std::move(handler);
    }

    /**
     * @brief 画面にメッセージを出す
     * @param text 出す文字（UTF-8）
     * @param seconds 出しておく秒数
     */
    void RequestMessage(const std::string& text, float seconds) const {
        if (message_) {
            message_(text, seconds);
        }
    }

    /** @brief カメラを指定の場所へ向ける実体を登録する（空のstd::functionで解除） */
    void SetCameraFocusHandler(CameraFocusHandler handler) {
        cameraFocus_ = std::move(handler);
    }

    /**
     * @brief カメラを指定の場所へ向ける（時間がたつとプレイヤーの追従へ戻る）
     * @param x 映す中心のX座標
     * @param y 映す中心のY座標
     * @param seconds 向けておく秒数
     */
    void RequestCameraFocus(float x, float y, float seconds) const {
        if (cameraFocus_) {
            cameraFocus_(x, y, seconds);
        }
    }

    /** @brief プレイヤーの操作を止める実体を登録する（空のstd::functionで解除） */
    void SetControlLockHandler(ControlLockHandler handler) {
        controlLock_ = std::move(handler);
    }

    /** @brief プレイヤーの操作を止める（true）か戻す（false） */
    void RequestControlLock(bool locked) const {
        if (controlLock_) {
            controlLock_(locked);
        }
    }

private:
    /** @brief 演出先が未接続の橋渡しを作る */
    SceneEffectBridge() = default;
    SceneEffectBridge(const SceneEffectBridge&) = delete;
    SceneEffectBridge& operator=(const SceneEffectBridge&) = delete;

    CameraShakeHandler cameraShake_;
    MessageHandler message_; ///< ShowMessage ノードの出し先
    CameraFocusHandler cameraFocus_; ///< FocusCamera ノードの出し先
    ControlLockHandler controlLock_; ///< LockPlayerControl ノードの出し先
};

} // namespace engine::game
