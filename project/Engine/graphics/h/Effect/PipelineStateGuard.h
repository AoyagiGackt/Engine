/**
 * @file PipelineStateGuard.h
 * @brief 自前のルートシグネチャ/PSO/レンダーターゲットを使うエフェクトの描画後、
 *        呼び出し側が必ず状態を復帰できるようにするスコープガード
 * @note 自前PSOエフェクト（GlassShatterEffect等）の描画直後に CommonDrawSettings() /
 * SetupMainRenderTarget() を呼び忘れると、後続の Object3d/Sprite 描画が誤った
 * ルートシグネチャで走り D3D12 クラッシュになる。
 * このガードはスコープを抜けるタイミングで復帰処理を必ず呼ぶことで、
 * 早期returnや将来のコード追加があっても復帰忘れが起きないようにする。
 */
#pragma once
#include <functional>
#include <utility>
namespace engine::graphics {

/**
 * @brief スコープを抜ける際に自前PSO描画からの復帰処理を必ず実行するRAIIガード
 */
class PipelineStateGuard {
public:
    /**
     * @param onRestore スコープを抜ける際に呼ぶ復帰処理
     * （例: SetupMainRenderTarget() + spriteCommon_->CommonDrawSettings() をまとめたラムダ）
     */
    explicit PipelineStateGuard(std::function<void()> onRestore)
        : onRestore_(std::move(onRestore))
    {
    }
    ~PipelineStateGuard()
    {
        if (onRestore_) {
            // コンストラクタで渡された復帰処理（PSO/レンダーターゲットの復帰）を呼ぶ
            onRestore_();
        }
    }

    PipelineStateGuard(const PipelineStateGuard&) = delete;
    PipelineStateGuard& operator=(const PipelineStateGuard&) = delete;

private:
    std::function<void()> onRestore_;
};

} // namespace engine::graphics
