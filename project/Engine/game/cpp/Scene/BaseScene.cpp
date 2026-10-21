/**
 * @file BaseScene.cpp
 * @brief 全シーン共通の初期化・更新・描画の入口処理とStageEditor連携（BaseScene）の実装
 */
#include "BaseScene.h"
#include "Camera.h"
#include "GameConstants.h"
#include "JsonHelper.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "ShadowManager.h"
#include "SkinnedObject3d.h"
#include "Player.h"
#include "SpriteCommon.h"
#include "StageEditor.h"

// コンストラクタ／デストラクタの実体をここに1箇所だけ置く
// StageEditor.hをフルインクルードするのはこの.cppだけでよく、BaseScene.hはforward宣言のみで済む
namespace engine::game {
namespace {
constexpr int kNoEditorPlayerVisualPreset = -1; // プレイヤーがいないシーンで返す見た目の種類
constexpr const char* kSceneCameraConfigPath = "Resources/Config/scene_cameras.json"; // シーンごとのカメラの初期位置
constexpr size_t kCameraStartComponents = 2; // カメラの初期位置は [x, y] の2要素
constexpr Vector3 kDefaultCameraStart = { 19.0f, 6.0f, 0.0f }; // scene_cameras.json に無い時のカメラの初期位置（z は使わない）
} // namespace

/** @brief ステージエディタを生成して初期化する */
BaseScene::BaseScene()
    : stageEditor_(std::make_unique<StageEditor>())
{
}

/** @brief unique_ptr で持つ不完全型のメンバを破棄するため、定義を cpp に置く */
BaseScene::~BaseScene() = default;

/** @brief エディタのトリガー判定に使うプレイヤー位置（プレイヤーがいないシーンは原点） */
Vector3 BaseScene::GetEditorPlayerPos() const
{
    const Player* player = GetEditorPlayer();
    return player ? player->GetPosition() : Vector3 { };
}

/** @brief Init()時にPlayerとしてRegisterExternalEntity()する対象の位置操作（プレイヤーがいないシーンは nullptr） */
IPositionTarget* BaseScene::GetEditorPlayerPositionTarget() const
{
    return GetEditorPlayer();
}

/** @brief エディタで選んだプレイヤーの見た目の種類を返す（プレイヤーがいないシーンは -1） */
int BaseScene::GetEditorPlayerVisualPreset() const
{
    const Player* player = GetEditorPlayer();
    return player ? player->GetVisualPreset() : kNoEditorPlayerVisualPreset;
}

/** @brief プレイヤーの見た目の種類を設定する（プレイヤーがいないシーンでは何もしない） */
void BaseScene::SetEditorPlayerVisualPreset(int preset)
{
    if (Player* player = GetEditorPlayer())
    {
        player->SetVisualPreset(preset);
    }
}

/** @brief プレイヤーの固定の見た目（モデルとテクスチャ）を設定する（プレイヤーがいないシーンでは何もしない） */
void BaseScene::SetEditorPlayerStaticVisual(const std::string& model, const std::string& texture)
{
    if (Player* player = GetEditorPlayer())
    {
        player->SetStaticVisualModel(model, texture);
    }
}

/** @brief プレイヤーの固定の見た目のモデルのパスを返す（プレイヤーがいないシーンは空） */
std::string BaseScene::GetEditorPlayerStaticVisualModel() const
{
    const Player* player = GetEditorPlayer();
    return player ? player->GetStaticVisualModelPath() : std::string { };
}

/** @brief プレイヤーの固定の見た目のテクスチャのパスを返す（プレイヤーがいないシーンは空） */
std::string BaseScene::GetEditorPlayerStaticVisualTexture() const
{
    const Player* player = GetEditorPlayer();
    return player ? player->GetStaticVisualTexturePath() : std::string { };
}

/** @brief 派生シーンと共通エディタを決められた順序で終了する */
void BaseScene::Shutdown()
{
    // エディタが参照するModelCommonとカメラをシーン側が破棄する前に編集リソースを解放する
    if (stageEditor_)
    {
        stageEditor_->Finalize();
    }
    Finalize();
}

/** @brief SceneManagerが呼ぶ初期化の入口（非virtual） */
void BaseScene::Init(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    Initialize(dxCommon, input, audio);

    // フックが既定値のまま（レベルパス空・ModelCommon/Camera無し）ならOpen()自体をスキップする
    std::string levelPath = GetEditorLevelPath();
    auto* modelCommon = GetEditorModelCommon();
    auto* camera = GetEditorCamera();
    if (!levelPath.empty() && modelCommon && camera)
    {
        GetStageEditor().Open(levelPath, modelCommon, camera);
        OnEditorLevelLoaded();
    }

    if (IPositionTarget* player = GetEditorPlayerPositionTarget())
    {
        GetStageEditor().RegisterExternalEntity("Player", player,
            [this] {
                return GetEditorPlayerVisualPreset();
            },
            [this](int preset) {
                SetEditorPlayerVisualPreset(preset);
            },
            [this](const std::string& model, const std::string& tex) {
                SetEditorPlayerStaticVisual(model, tex);
            },
            [this] {
                return GetEditorPlayerStaticVisualModel();
            },
            [this] {
                return GetEditorPlayerStaticVisualTexture();
            });
    }
}

/** @brief SceneManagerが毎フレーム呼ぶ更新の入口（非virtual） */
void BaseScene::Tick()
{
    if (GetStageEditor().ShouldPauseGame())
    {
        // エディタ表示中はゲームプレイを丸ごと止め、配置物/enemy系の見た目だけ追従させる
        GetStageEditor().UpdateObjects(GetEditorParticleManager(), GetEditorPlayerPos());
        RefreshVisualTransformsForEditor();
        return;
    }

    Update();
    // 親子追従やenemy系のAI/重力は表示状態に関係なく毎フレーム進める
    GetStageEditor().UpdateObjects(GetEditorParticleManager(), GetEditorPlayerPos());
}

/** @brief 各シーンのInitialize()冒頭で重複しがちな共通初期化をまとめて行う */
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

/** @brief シーン設定の初期位置を使って3D描画の土台を構築する */
void BaseScene::BuildRenderFoundation(DirectXCommon* dxCommon, engine::graphics::SrvManager* srvManager,
    const char* cameraKey, const RenderFoundationTargets& targets)
{
    BuildRenderFoundation(dxCommon, srvManager, targets);

    // カメラを scene_cameras.json の初期位置（奥行きは共通のカメラ距離）に置く
    const nlohmann::json cameras = JsonHelper::Load(kSceneCameraConfigPath);
    const nlohmann::json& start = cameras.contains(cameraKey) ? cameras[cameraKey] : nlohmann::json::array();
    const float startX = start.is_array() && start.size() >= kCameraStartComponents ? start[0].get<float>() : kDefaultCameraStart.x;
    const float startY = start.is_array() && start.size() >= kCameraStartComponents ? start[1].get<float>() : kDefaultCameraStart.y;
    targets.camera->SetTranslate({ startX, startY, GameConstants::kCameraDistanceZ });
}

/** @brief モデル・ライト・影とカメラを作り、3Dオブジェクトの共通参照を登録する */
void BaseScene::BuildRenderFoundation(DirectXCommon* dxCommon, engine::graphics::SrvManager* srvManager,
    const RenderFoundationTargets& targets)
{
    using namespace engine::graphics;
    // 後から作るゲームの実体が安全にモデルを生成できるよう、描画の共通設定を先に作る
    targets.modelCommon = std::make_unique<ModelCommon>();
    targets.modelCommon->Initialize(dxCommon);
    targets.objectCommon = std::make_unique<Object3dCommon>();
    targets.objectCommon->Initialize(dxCommon);
    targets.shadowManager = std::make_unique<ShadowManager>();
    targets.shadowManager->Initialize(dxCommon, srvManager);

    // OutlineEffect等でルートシグネチャを切り替えた後にライト/シャドウマップを再バインドできるようにする
    Object3d::SetCommonObjectCommon(targets.objectCommon.get());
    Object3d::SetCommonShadowManager(targets.shadowManager.get());
    SkinnedObject3d::SetCommonObjectCommon(targets.objectCommon.get());
    SkinnedObject3d::SetCommonShadowManager(targets.shadowManager.get());

    // カメラ位置は各シーンの設定から適用する
    targets.camera = std::make_unique<Camera>();
    Object3d::SetCommonCamera(targets.camera.get());
}

/** @brief SceneManagerが呼ぶ描画の入口（非virtual） */
void BaseScene::Render()
{
    Draw();
    // Draw()内でHUDより前に自分でDrawObjects()を呼んだシーンは、ここでの二重描画をスキップする
    // （呼んでいなければ、これまで通りフレーム最後に上乗せ描画する＝自己完結・PSO状態に依存しない）
    if (!GetStageEditor().WasObjectsDrawnThisFrame())
    {
        GetStageEditor().DrawObjects();
    }
}

} // namespace engine::game
