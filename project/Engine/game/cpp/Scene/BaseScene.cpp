/**
 * @file BaseScene.cpp
 * @brief BaseSceneが担当する処理を実装するファイル
 */
#include "BaseScene.h"
#include "SpriteCommon.h"
#include "StageEditor.h"
using namespace engine::game;

// コンストラクタ／デストラクタの実体をここに1箇所だけ置く
// StageEditor.hをフルインクルードするのはこの.cppだけでよく、BaseScene.hはforward宣言のみで済む
// （StageEditor内部のunique_ptr<KnightEnemy>/<EnemyEntity>絡みで、他の翻訳単位が
//   EnemyEntity/KnightEnemyの完全な定義を要求されてしまう問題の対処）
BaseScene::BaseScene()
    : stageEditor_(std::make_unique<StageEditor>())
{
}

BaseScene::~BaseScene() = default;

void BaseScene::Shutdown()
{
    // エディタが参照するModelCommonとカメラをシーン側が破棄する前に編集リソースを解放する
    if (stageEditor_) {
        stageEditor_->Finalize();
    }
    Finalize();
}

void BaseScene::Init(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    Initialize(dxCommon, input, audio);

    // フックが既定値のまま（レベルパス空・ModelCommon/Camera無し）ならOpen()自体をスキップする
    std::string levelPath = GetEditorLevelPath();
    auto* modelCommon = GetEditorModelCommon();
    auto* camera = GetEditorCamera();
    if (!levelPath.empty() && modelCommon && camera) {
        GetStageEditor().Open(levelPath, modelCommon, camera);
    }

    if (Vector3* playerPos = GetEditorPlayerPositionRef()) {
        GetStageEditor().RegisterExternalEntity("Player", playerPos);
    }
}

void BaseScene::Tick()
{
    if (GetStageEditor().ShouldPauseGame()) {
        // エディタ表示中はゲームプレイを丸ごと止め、配置物/enemy系の見た目だけ追従させる
        GetStageEditor().UpdateObjects(GetEditorParticleManager(), GetEditorPlayerPos());
        RefreshVisualTransformsForEditor();
        return;
    }

    Update();
    // 親子追従やenemy系のAI/重力は表示状態に関係なく毎フレーム進める
    GetStageEditor().UpdateObjects(GetEditorParticleManager(), GetEditorPlayerPos());
}

std::unique_ptr<engine::graphics::SpriteCommon> BaseScene::InitializeCommonResources(
    DirectXCommon* dxCommon, Input* input, Audio* audio,
    DirectXCommon*& outDxCommon, Input*& outInput, Audio*& outAudio)
{
    outDxCommon = dxCommon;
    outInput = input;
    outAudio = audio;

    auto spriteCommon = std::make_unique<engine::graphics::SpriteCommon>();
    spriteCommon->Initialize(outDxCommon);
    return spriteCommon;
}

void BaseScene::Render()
{
    Draw();
    // Draw()内でHUDより前に自分でDrawObjects()を呼んだシーンは、ここでの二重描画をスキップする
    // （呼んでいなければ、これまで通りフレーム最後に上乗せ描画する＝自己完結・PSO状態に依存しない）
    if (!GetStageEditor().WasObjectsDrawnThisFrame()) {
        GetStageEditor().DrawObjects();
    }
}
