/** @file FinalBossArena.h
 * @brief 最終戦の円形闘技場を管理する。
 */
#pragma once
#include "Object3d.h"
#include <memory>
#include <string>
#include <vector>
namespace engine::graphics { class ParticleManager; }
namespace engine::game {
/** @brief 外部から中心と回転を受け取る闘技場。 */
class FinalBossArena {
public:
    /** @brief 初期化に使う共通の部品。 */
    struct Setup {
        engine::graphics::ModelCommon* modelCommon = nullptr;
        engine::graphics::ParticleManager* particles = nullptr;
    };
    /** @brief モデルと設定を読み全装飾を生成する。 */
    void Initialize(const Setup& setup);
    /** @brief 中心のシーン座標とY軸回転を設定する。 */
    void SetFrame(const Vector3& center, float yaw);
    /** @brief 後半戦の発光色への補間量を設定する。 */
    void SetRage(float amount);
    /** @brief 明滅と粒、全行列を更新する。 */
    void Update(float deltaTime);
    /** @brief 共通描画設定済みのパスで描画する。 */
    void Draw();
    /** @brief 歩けるローカル半径を返す。 */
    float GetRadius() const;
    /** @brief 中心の高さを加えた床上面のシーン座標を返す。 */
    float GetFloorY() const;
private:
    /** @brief ローカル配置と発光の有無を持つ装飾。 */
    struct Piece {
        std::unique_ptr<engine::graphics::Object3d> object;
        Vector3 position {};
        float yaw = 0.0f;
        bool glow = false;
    };
    /** @brief 既存の回転行列でローカル座標を変換する。 */
    Vector3 ToScene(const Vector3& local) const;
    std::vector<Piece> pieces_;
    engine::graphics::ParticleManager* particles_ = nullptr;
    std::string particleGroup_;
    Vector3 center_ {};
    float yaw_ = 0.0f;
    float radius_ = 1.0f;
    float floorY_ = 0.0f;
    float rage_ = 0.0f;
    float time_ = 0.0f;
    float emitClock_ = 0.0f;
    float pulseSpeed_ = 1.0f;
    float pulseDepth_ = 0.0f;
    float particleInterval_ = 1.0f;
    float particleHeight_ = 1.0f;
    float particleSpeed_ = 0.0f;
    float particleLife_ = 1.0f;
    float particleSize_ = 1.0f;
    Vector4 calmColor_ {};
    Vector4 rageColor_ {};
};
} // namespace engine::game
