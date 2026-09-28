/**
 * @file SceneEffectBridge.h
 * @brief シーンが所有する演出（カメラシェイク等）をノードグラフから呼ぶための橋渡し
 * @note CameraShakerは各シーンのメンバなので、シーンがInitializeでハンドラを登録し、Finalizeで解除する。
 * 登録が無い間に呼ばれても何もしない（グラフ側はシーンの種類を知らなくてよい）
 */
#pragma once
#include <functional>
namespace engine::game {

/**
 * @brief シーン側の演出ハンドラを保持し、グラフのノードから呼び出すシングルトン
 */
class SceneEffectBridge {
public:
    using CameraShakeHandler = std::function<void(float amount, float seconds)>;

    /**
     * @brief 唯一のSceneEffectBridgeインスタンスを取得する（未生成なら生成する）
     * @return SceneEffectBridgeのインスタンス
     */
    static SceneEffectBridge* GetInstance();

    /** @brief カメラシェイクの実体を登録する（空のstd::functionで解除） */
    void SetCameraShakeHandler(CameraShakeHandler handler) { cameraShake_ = std::move(handler); }

    /**
     * @brief カメラシェイクを要求する
     * @param amount 揺れの強さ（ワールド単位）
     * @param seconds 揺れの継続秒数
     */
    void RequestCameraShake(float amount, float seconds) const
    {
        if (cameraShake_) {
            cameraShake_(amount, seconds);
        }
    }

private:
    SceneEffectBridge() = default;
    SceneEffectBridge(const SceneEffectBridge&) = delete;
    SceneEffectBridge& operator=(const SceneEffectBridge&) = delete;

    CameraShakeHandler cameraShake_;
};

} // namespace engine::game
