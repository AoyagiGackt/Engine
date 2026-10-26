/** @file ResourceAlignment.h
 * @brief DirectX定数バッファの配置境界とサイズ計算
 */
#pragma once
#include <cstddef>
#include <d3d12.h>
namespace engine::graphics {
/** @brief 定数バッファのGPUアドレスとサイズに要求される境界 */
inline constexpr std::size_t kConstantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
/** @brief 必要バイト数をDirectX定数バッファの配置境界へ切り上げる */
constexpr std::size_t AlignConstantBufferSize(std::size_t bytes) {
    return (bytes + kConstantBufferAlignment - 1) & ~(kConstantBufferAlignment - 1);
}
} // namespace engine::graphics
