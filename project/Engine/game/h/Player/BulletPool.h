/**
 * @file BulletPool.h
 * @brief プレイヤーの射撃弾丸をプールして管理するファイル
 */
#pragma once
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include <array>
#include <memory>
namespace engine::game {
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;

/**
 * @brief 弾丸オブジェクトを固定数プールし、生成・移動・破棄を管理するクラス
 * @note 衝突判定は呼び出し側で行い、命中したら Kill() でスロットを解放する
 */
class BulletPool {
public:
    static constexpr int kMaxBullets = 48;

    void Initialize(ModelCommon* modelCommon, Model* model);
    void Spawn(const Vector3& pos, const Vector3& vel);
    // 移動・境界チェックのみ衝突判定は呼び出し側で行い Kill() で無効化する
    /**
     * @brief Update に対応する状態を更新する
     * @return なし
     */
    void Update();
    /** @brief 弾を消して、使い回せるように空きにする */
    void Kill(int index) {
        slots_[index].active = false;
    }
    /**
     * @brief Draw に対応する内容を描画する
     * @return なし
     */
    void Draw();

    /** @brief 弾を動かさず、現在のカメラで行列だけ再計算する（ステージエディタ中のカメラ移動用） */
    void RefreshVisualTransforms();

    /** @brief 指定した弾が飛んでいるかを返す */
    bool IsActive(int i) const {
        return slots_[i].active;
    }
    /** @brief 指定した弾の位置を返す */
    const Vector3& GetPos(int i) const {
        return slots_[i].pos;
    }
    /** @brief 指定した弾の速度を返す */
    const Vector3& GetVel(int i) const {
        return slots_[i].vel;
    }

private:
    /** @brief プールまたはHUDの1枠が保持する表示資源と状態 */
    struct Slot {
        Vector3 pos = { };
        Vector3 vel = { };
        float life = 0.0f;
        bool active = false;
        std::unique_ptr<Object3d> obj;
    };

    std::array<Slot, kMaxBullets> slots_;
};

} // namespace engine::game
