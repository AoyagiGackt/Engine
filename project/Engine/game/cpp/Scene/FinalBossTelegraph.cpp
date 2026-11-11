/**
 * @file FinalBossTelegraph.cpp
 * @brief ラスボスの攻撃の予兆（床の円と帯）を、床に寝かせた板で描く
 */
#include "FinalBossTelegraph.h"
#include "GameConstants.h"
#include "ModelManager.h"

using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kPlaneModel = "Resources/plane/plane.obj"; ///< XY 面に置いた -1〜1 の板（表は +Z）
constexpr const char* kCircleTexture = "Resources/Effects/circle2.png";
constexpr const char* kLineTexture = "Resources/white.png";
constexpr size_t kCircleCount = 6; ///< 同時に出せる円の数（範囲斬り3つ・跳び斬り・撃ち出す剣の狙い）
constexpr float kFloorLift = 0.03f; ///< 床と重なってちらつかないよう浮かせる高さ
constexpr float kLayFlat = -gameConstants::kHalfPi; ///< 板を X 軸回りに倒して表を上へ向ける角度

/** @brief 板の実体を作る（光を当てず、色と画像だけで見せる） */
std::unique_ptr<Object3d> CreateMark(ModelCommon* modelCommon, const char* texture) {
    auto object = std::make_unique<Object3d>();
    object->Initialize(modelCommon);
    object->SetModel(ModelManager::GetInstance()->GetOrLoad(modelCommon, kPlaneModel, texture));
    object->SetEnableLighting(false);
    object->SetRotation({ kLayFlat, 0.0f, 0.0f });
    return object;
}
} // namespace

/** @brief 印のモデルを読み込み、使い回す実体を作る */
void FinalBossTelegraph::Initialize(ModelCommon* modelCommon) {
    circles_.resize(kCircleCount);
    for (Mark& mark : circles_) {
        mark.object = CreateMark(modelCommon, kCircleTexture);
    }
    line_.object = CreateMark(modelCommon, kLineTexture);
}

/** @brief このフレームの印を全部消す */
void FinalBossTelegraph::BeginFrame() {
    for (Mark& mark : circles_) {
        mark.active = false;
    }
    line_.active = false;
    usedCircles_ = 0;
}

/** @brief 当たる範囲の円を床に置く（数を超えた分は出さない） */
void FinalBossTelegraph::Circle(float x, float floorY, float radius, const Vector4& color) {
    if (usedCircles_ >= circles_.size()) {
        return;
    }
    Place(circles_[usedCircles_++], { x, floorY + kFloorLift, 0.0f }, radius, radius, color);
}

/** @brief 通り道の帯を床に置く */
void FinalBossTelegraph::Line(float fromX, float toX, float floorY, float width, const Vector4& color) {
    const float halfLength = (toX > fromX ? toX - fromX : fromX - toX) * 0.5f;
    Place(line_, { (fromX + toX) * 0.5f, floorY + kFloorLift, 0.0f }, halfLength, width * 0.5f, color);
}

/** @brief 床に寝かせた印を置く（板は -1〜1 なので、半分の幅をそのまま倍率にする） */
void FinalBossTelegraph::Place(Mark& mark, const Vector3& center, float halfWidth, float halfDepth, const Vector4& color) {
    mark.active = true;
    mark.object->SetPosition(center);
    mark.object->SetScale({ halfWidth, halfDepth, 1.0f });
    mark.object->SetColor(color);
}

/** @brief 置いた印を描く（カメラが動いた後の行列で描く） */
void FinalBossTelegraph::Draw() {
    for (Mark& mark : circles_) {
        if (mark.active) {
            mark.object->Update();
            mark.object->Draw();
        }
    }
    if (line_.active) {
        line_.object->Update();
        line_.object->Draw();
    }
}

} // namespace engine::game
