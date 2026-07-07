/**
 * @file LightingMode.h
 * @brief 3Dオブジェクトのライティング計算方式を定義するファイル
 */
#pragma once
namespace engine {
/**
 * @brief ライティングの計算方式を指定する列挙型
 * @note マテリアル設定（Material構造体）の shadingType 等に使用します
 */
enum class LightingMode {

    /** @brief ライティングなしテクスチャの色をそのまま出力します（Unlit） */
    None = 0,

    /** * @brief ランバート反射（Lambert Reflection）
     * @note 最も標準的な拡散反射モデル光の当たらない部分は真っ暗になります
     */
    Lambert = 1,

    /** * @brief ハーフランバート（Half-Lambert）
     * @note 影の範囲を 0.5~1.0 に補正する方式影が真っ暗にならず、柔らかい印象になります
     */
    HalfLambert = 2,

    /** @brief Lambert 拡散 + Phong 鏡面反射 */
    LambertPhong = 3,

    /** @brief HalfLambert 拡散 + Phong 鏡面反射 */
    HalfLambertPhong = 4,

    /** @brief Cook-Torrance PBR（GGX NDF + Smith-Schlick 幾何 + Fresnel-Schlick）*/
    PBR = 5
};

} // namespace engine
