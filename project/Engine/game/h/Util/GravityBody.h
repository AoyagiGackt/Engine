/**
 * @file GravityBody.h
 * @brief 落下体（重力を受けて地面/天井でクランプされるY軸物理）の共通処理を定義するファイル
 */
#pragma once
namespace engine::game {

/**
 * @brief 重力を適用してY座標を1フレーム進め、地面/天井でクランプする
 * @param[in,out] posY   更新するY座標
 * @param[in,out] velY   更新するY速度
 * @param gravity        1フレームあたりの重力加速度
 * @param groundY        地面のY座標（下限、到達したらvelYは0にクランプされる）
 * @param ceilingY       天井のY座標（上限）
 * @param ceilingBounceFactor 天井到達時にvelYへ掛ける倍率（デフォルト0=完全停止、負値でバウンド）
 * @return 地面に到達（クランプ）したら true
 */
inline bool ApplyGravityAndClampY(float& posY, float& velY, float gravity,
    float groundY, float ceilingY, float ceilingBounceFactor = 0.0f)
{
    velY -= gravity;
    posY += velY;

    bool grounded = false;
    if (posY <= groundY) {
        posY     = groundY;
        velY     = 0.0f;
        grounded = true;
    }
    if (posY > ceilingY) {
        posY = ceilingY;
        velY *= ceilingBounceFactor;
    }
    return grounded;
}

} // namespace engine::game
