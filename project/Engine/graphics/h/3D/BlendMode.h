/**
 * @file BlendMode.h
 * @brief BlendModeの描画資源とGPU処理の管理に関する公開型と操作インターフェースを定義するファイル
 */
#pragma once
namespace engine::graphics {
enum class BlendMode {
    None, // ブレンドなし（不透明）
    Alpha, // 通常の半透明（アルファブレンド）
    Add, // 加算合成（エフェクト等）
    Subtract, // 減算合成
    Multiply, // 乗算合成
    Count // 総数
};

} // namespace engine::graphics
