/**
 * @file MapScene.cpp
 * @brief ローグライトのフロア選択マップ（MapScene）の表示とノード選択・遷移処理の実装
 */
#include "MapScene.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "TextBufferCapacity.h"
#include "CrashBreadcrumbs.h"
#include "GameConstants.h"
#include "GameRules.h"
#include "ModelManager.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "StageEditor.h"
#include "SkinnedObject3d.h"
#include "SrvManager.h"
#include "UILayout.h"
#include "WeaponManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iterator>
#include <string>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kPortalMotionConfig = "Resources/Config/map_portal_motion.json";
constexpr float kMinimumMotionSeconds = 0.001f;
constexpr float kMinimumPlayerScale = 0.01f;
/** @brief 入口の出入り演出の調整値 */
struct PortalMotionTuning {
    float alignSeconds = 0.2f;
    float enterSeconds = 0.6f;
    float exitSeconds = 0.7f;
    float depth = 1.0f;
    float smallScale = 0.55f;
    float faintAlpha = 0.0f;
    float glowStrength = 0.8f;
    Vector4 glowColor = { 1.0f, 0.95f, 0.7f, 1.0f };
};
constexpr JsonField<PortalMotionTuning> kPortalMotionFields[] = {
    { "motion", "alignSeconds", &PortalMotionTuning::alignSeconds },
    { "motion", "enterSeconds", &PortalMotionTuning::enterSeconds },
    { "motion", "exitSeconds", &PortalMotionTuning::exitSeconds },
    { "motion", "depth", &PortalMotionTuning::depth },
    { "motion", "smallScale", &PortalMotionTuning::smallScale },
    { "motion", "faintAlpha", &PortalMotionTuning::faintAlpha },
    { "glow", "strength", &PortalMotionTuning::glowStrength },
    { "glow", "color", &PortalMotionTuning::glowColor },
};
/** @brief JSONから入口の調整値を読む */
PortalMotionTuning LoadPortalMotionTuning() {
    PortalMotionTuning tuning;
    const auto root = JsonHelper::Load(kPortalMotionConfig);
    if (root.is_object()) {
        ReadJsonFields(root, kPortalMotionFields, tuning);
    }
    tuning.alignSeconds = (std::max)(tuning.alignSeconds, kMinimumMotionSeconds);
    tuning.enterSeconds = (std::max)(tuning.enterSeconds, kMinimumMotionSeconds);
    tuning.exitSeconds = (std::max)(tuning.exitSeconds, kMinimumMotionSeconds);
    tuning.depth = (std::max)(tuning.depth, 0.0f);
    tuning.smallScale = std::clamp(tuning.smallScale, kMinimumPlayerScale, 1.0f);
    tuning.faintAlpha = std::clamp(tuning.faintAlpha, 0.0f, 1.0f);
    return tuning;
}
/** @brief 読み込み済みの入口の調整値（マップを開くたびに読み直す） */
PortalMotionTuning sPortalTune;
/** @brief 秒数から安全な進行度を求める */
float PortalProgress(float elapsed, float seconds) {
    return std::clamp(elapsed / (std::max)(seconds, kMinimumMotionSeconds), 0.0f, 1.0f);
}
} // namespace

// ステージの並びごとの種類（最初は通常、途中は強敵、最後はボス。先へ進むほどボスのHPが上がる）
static constexpr RunData::NodeType kFirstFloorType = RunData::NodeType::kCombat;
static constexpr RunData::NodeType kMiddleFloorType = RunData::NodeType::kElite;
static constexpr RunData::NodeType kLastFloorType = RunData::NodeType::kBoss;

// ノード種別ごとの色
static constexpr Vector4 kNodeCombatColor = { 0.75f, 0.18f, 0.18f, 1.0f };
static constexpr Vector4 kNodeEliteColor = { 0.85f, 0.45f, 0.08f, 1.0f };
static constexpr Vector4 kNodeShopColor = { 0.18f, 0.65f, 0.28f, 1.0f };
static constexpr Vector4 kNodeRestColor = { 0.18f, 0.38f, 0.85f, 1.0f };
static constexpr Vector4 kNodeBossColor = { 0.55f, 0.08f, 0.75f, 1.0f };
static constexpr Vector4 kNodeDefaultColor = { 0.5f, 0.5f, 0.5f, 1.0f };
static constexpr float kNodeSelectedBrighten = 0.25f;
static constexpr Vector4 kPortalLockedColor = { 0.12f, 0.12f, 0.16f, 1.0f };

// 背景・地面
static constexpr Vector4 kBackgroundColor = { 0.05f, 0.05f, 0.08f, 1.0f };

// 3Dの舞台
static constexpr float kCameraY = 4.8f;
static constexpr float kInitialCameraX = 18.0f;
static constexpr float kCameraMinX = 12.0f;
static constexpr float kCameraMaxX = 54.0f;
static constexpr float kPlayerMinX = 2.5f;
static constexpr float kPlayerMaxX = 63.5f;
static constexpr float kPlayerGroundY = 0.4f;
static constexpr float kPlayerFacingTargetDistance = 8.0f; // 向きを決めるために渡す前方の注視点までの距離
static constexpr Vector3 kPortalScale = { 2.5f, 4.0f, 1.0f };
static constexpr float kPortalY = 1.4f;
static constexpr float kPortalZ = 1.0f;
static constexpr float kPortalSelectDistance = 2.5f; // この距離以内のポータルを選択中にする

// 解放演出の確認用ボタン（評価・確認用。game_rules.json の debugLiberationButton で出し入れする）
static constexpr float kDebugLiberationButtonTop = 48.0f;
static constexpr Vector4 kDebugLiberationHoverColor = { 0.75f, 0.6f, 0.1f, 0.85f };

// 以下は既定の配置（F2のエディタで動かした値は Resources/Config/UI/map.json に保存される）
static constexpr const char* kLayoutName = "map";

// 画面下の操作説明
static constexpr Vector2 kHelpTextPosition = { 20.0f, 610.0f };
static constexpr Vector2 kBackHintPosition = { 20.0f, 570.0f };
static constexpr float kHelpTextScale = 1.1f;
static constexpr Vector4 kHelpTextColor = { 0.88f, 0.90f, 1.0f, 1.0f };
static constexpr float kHintPanelPadding = 8.0f;
static constexpr Vector4 kHintPanelColor = { 0.0f, 0.0f, 0.05f, 0.65f };

// ポータル上のラベル
static constexpr float kLabelOffsetX = 42.0f;
static constexpr float kLabelY = 185.0f;
static constexpr float kLabelScale = 1.55f;
static constexpr Vector2 kLabelShadowOffset = { 2.0f, 3.0f };
static constexpr Vector4 kLabelShadowColor = { 0.02f, 0.02f, 0.04f, 0.95f };
static constexpr Vector4 kLabelNearColor = { 1.0f, 1.0f, 0.2f, 1.0f };
static constexpr Vector4 kLabelColor = { 1.0f, 1.0f, 1.0f, 1.0f };
static constexpr float kEnterHintOffsetX = 43.0f;
static constexpr float kEnterHintY = 225.0f;
static constexpr float kEnterHintScale = 1.35f;
static constexpr Vector4 kEnterHintShadowColor = { 0.02f, 0.02f, 0.03f, 0.95f };
static constexpr Vector4 kEnterHintColor = { 1.0f, 0.92f, 0.22f, 1.0f };

// 選択中ノードの説明パネル
static constexpr Vector2 kInfoPanelPosition = { 875.0f, 350.0f };
static constexpr Vector2 kInfoPanelSize = { 365.0f, 118.0f };
static constexpr Vector4 kInfoPanelColor = { 0.015f, 0.02f, 0.04f, 0.84f };
static constexpr Vector2 kInfoTitleOffset = { 25.0f, 20.0f }; // 説明パネル左上からの位置
static constexpr float kInfoTitleScale = 1.8f;
static constexpr Vector4 kInfoTitleColor = { 1.0f, 0.85f, 0.2f, 1.0f };
static constexpr Vector2 kInfoDescOffset = { 25.0f, 60.0f };
static constexpr float kInfoDescLineHeight = 25.0f;
static constexpr float kInfoDescScale = 1.15f;
static constexpr Vector4 kInfoDescColor = { 1.0f, 1.0f, 1.0f, 1.0f };
static constexpr size_t kInfoDescMinWrapIndex = 10; // これより手前の全角スペースでは折り返さない

/** @brief ノード種別ごとの見た目と遷移先 */
struct NodeInfo {
    Vector4 color; ///< マップ上の表示色
    const wchar_t* label; ///< 日本語ラベル
    const wchar_t* desc; ///< 右下に出す説明
    bool entersCombat; ///< 決定時に戦闘シーンへ遷移するか
};

// RunData::NodeType の並び順と一致させる
static constexpr NodeInfo kNodeInfos[] = {
    { kNodeCombatColor, L"戦 闘", L"ステージを進み、ボスの武器を奪ってクリア　スタイルが高いほど評価UP", true },
    { kNodeEliteColor, L"強敵", L"手強い敵が待ち構えるステージ", true },
    { kNodeShopColor, L"ショップ", L"スキルを1つ選んで取得できる　スキルは永続効果", false },
    { kNodeRestColor, L"休 憩", L"HP を10回復する　のんびり休もう", false },
    { kNodeBossColor, L"ボ ス", L"最終決戦  倒せばクリア! 全力で挑め", true },
};
static constexpr NodeInfo kUnknownNodeInfo = { kNodeDefaultColor, L"？？？", L"", false };

/** @brief マップのマスの種類に対応する表示情報を返す */
static const NodeInfo& GetNodeInfo(RunData::NodeType t) {
    const size_t index = static_cast<size_t>(t);
    return index < std::size(kNodeInfos) ? kNodeInfos[index] : kUnknownNodeInfo;
}

/** @brief マスの種類と、選択中かから表示色を決める（クリア済みの色は StageLiberation::ClearedColor） */
static Vector4 NodeColor(RunData::NodeType t, bool selected) {
    Vector4 c = GetNodeInfo(t).color;
    if (selected) {
        c.x = (std::min)(c.x + kNodeSelectedBrighten, 1.0f);
        c.y = (std::min)(c.y + kNodeSelectedBrighten, 1.0f);
        c.z = (std::min)(c.z + kNodeSelectedBrighten, 1.0f);
    }
    return c;
}

/** @brief シーンの初期化スプライト・フォント・マップ定義を構築する */
void MapScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) {
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    // Title の NEW GAME/CONTINUE を経由せずここへ来た場合（Tabショートカット・デバッグ直行等）に備え、
    // ランが未開始ならここで開始しておく（未開始のままだとHP/スタイルUIがGamePlayScene側で出ない）
    // 武器もトレーニング等で装備したものがシングルトン経由で残ってしまうためあわせてリセットする
    if (!RunData::GetInstance()->IsRunActive()) {
        RunData::GetInstance()->StartNewRun();
        WeaponManager::GetInstance()->ResetForNewRun();
    }

    sPortalTune = LoadPortalMotionTuning();
    portalMotion_ = PortalMotion::kNone;
    playerVisible_ = true;
    stageWorldX_ = GameRules::GetInstance()->Get().stagePortalXs;
    InitializeUiSprites();
    InitializeRenderFoundationAndPlayer();
    InitializeFloorsAndStartPosition(); // ポータルの本数がステージ数に依存するため先に決める
    InitializeStageObjects();
    StartLiberationIfPending();
    audio_->PlaySceneBGM(bgmKey::kStageSelect);
}

/** @brief Initialize()の下請け 背景・ノード・地面のUIスプライトを初期化する */
void MapScene::InitializeUiSprites() {
    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ gameConstants::kScreenWidth, gameConstants::kScreenHeight });
    bgSprite_->SetColor(kBackgroundColor);

    nodeSprite_ = std::make_unique<Sprite>();
    nodeSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");

    fontRenderer_.Initialize(spriteCommon_.get());
    liberation_.Initialize(spriteCommon_.get());
    debugLiberationButton_.Initialize(spriteCommon_.get(), { L"[F5] 解放演出を見る", DIK_F5, "debug_liberation",
        kDebugLiberationButtonTop, kDebugLiberationHoverColor, &GameRulesData::debugLiberationButton });
}

/** @brief Initialize()の下請け 描画基盤(Common類・シャドウ・カメラ)とプレイヤーを初期化する */
void MapScene::InitializeRenderFoundationAndPlayer() {
    BuildRenderFoundation(dxCommon_, SrvManager::GetInstance(),
        { modelCommon_, objectCommon_, shadowManager_, camera_ });

    camera_->SetTranslate({ kInitialCameraX, kCameraY, gameConstants::kCameraDistanceZ });

    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    player_->SetHorizontalBounds(kPlayerMinX, kPlayerMaxX);
    player_->SetPosition({ stageWorldX_[0], kPlayerGroundY, 0.0f });
}

/** @brief Initialize()の下請け 地面ブロック・ステージ入口ポータル・街並み背景を初期化する */
void MapScene::InitializeStageObjects() {
    // 床と背景のビルはレベルJSON（Resources/Levels/map.json）に置き、F2のステージエディタで編集する
    // 入口ポータルはステージ数に追従させるためコード側で並べる
    blockModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/DowntownCityMegaKit[Standard]/Textures/T_Concrete_BaseColor.png");

    for (int i = 0; i < static_cast<int>(floors_.size()); ++i) {
        auto portal = std::make_unique<Object3d>();
        portal->Initialize(modelCommon_.get());
        portal->SetModel(blockModel_);
        portal->SetScale(kPortalScale);
        portal->SetEnableLighting(false);
        portalObjects_.push_back(std::move(portal));
    }
}

/** @brief Initialize()の下請け フロア構成を組み、現在のフロアに応じた開始位置へプレイヤーを置く(クリア済みならCLEARへ遷移) */
void MapScene::InitializeFloorsAndStartPosition() {
    // ステージ数はgame_rules.jsonのlevelPathsに追従する（ローグライク時代のフロア構成は撤回済み）。
    // 最初のステージは通常、途中は強敵、最後はボス扱いにして、先へ進むほどボスのHPを上げる。
    // ポータルの座標はstageWorldX_の数までが上限
    const int stageCount = std::clamp(static_cast<int>(GameRules::GetInstance()->Get().levelPaths.size()), 1, static_cast<int>(stageWorldX_.size()));
    floors_.assign(static_cast<size_t>(stageCount), std::vector<RunData::NodeType> { kMiddleFloorType });
    floors_.front().front() = kFirstFloorType;
    floors_.back().front() = kLastFloorType; // 1ステージだけの時はボス扱いを優先する

    selectedCol_ = -1;

    auto* rd = RunData::GetInstance();
    if (rd->GetFloor() >= static_cast<int>(floors_.size())) {
        SceneFlow::GetInstance()->Transition("MAP", "all_cleared", "CLEAR");
    } else {
        player_->SetPosition({ stageWorldX_[rd->GetFloor()], kPlayerGroundY, 0.0f });
    }
}

/** @brief Initialize()の下請け ステージをクリアして戻ってきた時だけ、クリアした入口の解放演出を始める */
void MapScene::StartLiberationIfPending() {
    // 解放の印は見せるかどうかに関わらずここで消費し、もう一度マップを開いた時に繰り返さない
    const bool returning = RunData::GetInstance()->ConsumePortalReturnPending();
    const int floor = RunData::GetInstance()->GetFloor();
    const int stageCount = static_cast<int>(floors_.size());
    if (!RunData::GetInstance()->ConsumeLiberationPending() || floor <= 0 || floor >= stageCount) {
        const int index = RunData::GetInstance()->GetPlayFloor();
        if (returning && floor < stageCount && index >= 0 && index < stageCount) {
            StartPortalMotion(index, false);
        }
        return;
    }
    StartLiberation(floor - 1);
}

/** @brief クリアした入口の番号を渡して解放演出を始める（次の入口が無ければ光るだけ） */
void MapScene::StartLiberation(int clearedIndex) {
    CrashBreadcrumbs::Add("game", "解放演出を開始（クリアした入口 " + std::to_string(clearedIndex) + "）");
    // 入口の中心の高さを画面の座標へ直し、光の玉をそこへ置く
    const int nextIndex = clearedIndex + 1 < static_cast<int>(floors_.size()) ? clearedIndex + 1 : -1;
    StageLiberation::Setup setup;
    setup.clearedIndex = clearedIndex;
    setup.nextIndex = nextIndex;
    setup.clearedX = stageWorldX_[clearedIndex];
    setup.nextX = nextIndex >= 0 ? stageWorldX_[nextIndex] : setup.clearedX;
    setup.portalScreenY = gameConstants::kScreenCenterY - (kPortalY - kCameraY) / gameConstants::kCameraHalfH * gameConstants::kScreenCenterY;
    StartPortalMotion(clearedIndex, false);
    playerVisible_ = false;
    liberation_.Start(setup);
}

/** @brief リソースを解放する */
void MapScene::Finalize() {
}

/** @brief 入力によるノード選択と遷移判定を更新する */
void MapScene::Update() {
    // 戻る操作でタイトルへ
    if (!liberation_.IsPlaying() && portalMotion_ == PortalMotion::kNone && input_->TriggerMenuCancel()) {
        audio_->PlayMenuSelect();
        SceneFlow::GetInstance()->Transition("MAP", "title", "TITLE");
        return;
    }

    auto* rd = RunData::GetInstance();

    // 全フロアを終えていれば何もしない
    int curFloor = rd->GetFloor();
    if (curFloor >= static_cast<int>(floors_.size())) {
        return;
    }

    // 解放演出 → 入口から出る → 通常操作 → 入口へ入る、の順に進める
    const bool liberating = liberation_.IsPlaying();
    const bool movingThroughPortal = portalMotion_ != PortalMotion::kNone;
    const Vector3 playerPos = liberating ? HoldPlayerForLiberation()
        : movingThroughPortal ? UpdatePortalMotion() : MovePlayer();
    const float cameraX = liberating ? liberation_.CameraX() : playerPos.x;
    camera_->SetTranslate({ std::clamp(cameraX, kCameraMinX, kCameraMaxX), kCameraY, gameConstants::kCameraDistanceZ });
    player_->RefreshVisualTransforms();

    // 影の行列を更新し、通常モデルとスキンモデルの両方へ渡す
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    SkinnedObject3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());

    // 一番近い入口を選択中にし、選択が変わったら音を鳴らす
    const int previousSelectedCol = selectedCol_;
    selectedCol_ = -1;
    float nearestDistance = kPortalSelectDistance;
    for (int i = 0; i < static_cast<int>(floors_.size()); ++i) {
        const float distance = std::abs(playerPos.x - stageWorldX_[i]);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            selectedCol_ = i;
        }
    }

    if (selectedCol_ >= 0 && selectedCol_ != previousSelectedCol) {
        audio_->PlayMenuChoice();
    }
    UpdatePortals(curFloor);
    // 解放演出と出入りの間は操作を受け付けない
    if (liberating || movingThroughPortal) {
        return;
    }
    // 確認用: 今のステージの1つ前（最初のステージなら最初の入口）をクリアした体で解放演出を見せる（進行は変えない）
    if (debugLiberationButton_.Update(input_, dxCommon_)) {
        StartLiberation((std::max)(curFloor - 1, 0));
        return;
    }

    // トレーニングへ
    if (input_->TriggerKey(DIK_T) || input_->TriggerButton(XINPUT_GAMEPAD_Y)) {
        audio_->PlayMenuSelect();
        SceneFlow::GetInstance()->Transition("MAP", "training", "TRAINING");
        return;
    }

    // 今のフロアか、クリア済みのフロアの入口の前で決定したら、そのマスへ進む（戦闘マスなら戦闘シーンへ）
    // クリア済みのステージは遊び直せるが、クリアしても進行度は進まない
    const bool canEnter = selectedCol_ >= 0 && selectedCol_ <= curFloor;
    if (canEnter && (input_->TriggerKey(DIK_RETURN) || input_->TriggerButton(XINPUT_GAMEPAD_A))) {
        audio_->PlayMenuSelect();
        RunData::NodeType chosen = floors_[selectedCol_][0];
        rd->SetCurrentNode(chosen);
        rd->SetPlayFloor(selectedCol_);

        if (GetNodeInfo(chosen).entersCombat) {
            rd->MarkPortalReturnPending();
            StartPortalMotion(selectedCol_, true);
        }
    }
}

/** @brief 入口の出入りを始める */
void MapScene::StartPortalMotion(int index, bool entering) {
    CrashBreadcrumbs::Add("game", std::string(entering ? "入口へ入る " : "入口から出る ") + std::to_string(index));
    motionPortalIndex_ = index;
    portalMotionTimer_ = 0.0f;
    portalMotion_ = entering ? PortalMotion::kEntering : PortalMotion::kExiting;
    portalMotionStart_ = player_->GetPosition();
    playerVisible_ = true;
    if (!entering) {
        player_->SetPosition({ stageWorldX_[index], kPlayerGroundY, sPortalTune.depth });
        player_->SetPortalAppearance(sPortalTune.smallScale, sPortalTune.faintAlpha);
    }
}

/** @brief 入口の出入りを入力なしで進める */
Vector3 MapScene::UpdatePortalMotion() {
    if (portalMotion_ == PortalMotion::kTransitioning) {
        return player_->GetPosition();
    }
    playerVisible_ = true;
    portalMotionTimer_ += gameConstants::kFrameDeltaTime;
    const bool entering = portalMotion_ == PortalMotion::kEntering;
    const float alignSeconds = (std::max)(sPortalTune.alignSeconds, kMinimumMotionSeconds);
    const bool aligning = entering && portalMotionTimer_ < alignSeconds;
    const float t = PortalProgress(portalMotionTimer_ - (entering ? alignSeconds : 0.0f),
        entering ? sPortalTune.enterSeconds : sPortalTune.exitSeconds);
    const float visibility = entering ? 1.0f - t : t;
    Vector3 pos = { stageWorldX_[motionPortalIndex_], kPlayerGroundY, sPortalTune.depth * (1.0f - visibility) };
    if (aligning) {
        const float align = PortalProgress(portalMotionTimer_, alignSeconds);
        pos.x = portalMotionStart_.x + (pos.x - portalMotionStart_.x) * align;
        pos.z = portalMotionStart_.z;
    }
    player_->SetPosition(pos);
    player_->SetPortalAppearance(sPortalTune.smallScale + (1.0f - sPortalTune.smallScale) * visibility,
        sPortalTune.faintAlpha + (1.0f - sPortalTune.faintAlpha) * visibility);
    const float yaw = player_->GetLastDirX() >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    const float targetYaw = aligning ? (pos.x >= portalMotionStart_.x ? gameConstants::kHalfPi : -gameConstants::kHalfPi)
        : entering ? 0.0f : gameConstants::kPi;
    player_->UpdateCinematicPose(false, { 0.0f, targetYaw - yaw, 0.0f }, true);
    if (!aligning && t >= 1.0f) {
        if (entering) {
            playerVisible_ = false;
            portalMotion_ = PortalMotion::kTransitioning;
            SceneFlow::GetInstance()->Transition("MAP", "combat", "GAMEPLAY");
        } else {
            player_->SetPortalAppearance(1.0f, 1.0f);
            portalMotion_ = PortalMotion::kNone;
            player_->UpdateCinematicPose(false);
        }
    }
    return pos;
}

/** @brief Update()の下請け 入力でプレイヤーを動かし、通路の端で止めた位置を返す */
Vector3 MapScene::MovePlayer() {
    // 向きを決める注視点は、今向いている方の少し先に置く
    const Vector3& currentPos = player_->GetPosition();
    player_->Update(input_, { currentPos.x + player_->GetLastDirX() * kPlayerFacingTargetDistance, currentPos.y, 0.0f });
    Vector3 playerPos = player_->GetPosition();
    playerPos.x = std::clamp(playerPos.x, kPlayerMinX, kPlayerMaxX);
    player_->SetPosition(playerPos);
    return playerPos;
}

/** @brief Update()の下請け 解放演出を進め、その間はプレイヤーをその場で待機させた位置を返す */
Vector3 MapScene::HoldPlayerForLiberation() {
    liberation_.Update(input_);
    player_->UpdateCinematicPose(false);
    return player_->GetPosition();
}

/** @brief Update()の下請け 入口の位置・大きさ・色を更新する（選択中・クリア済み・まだ行けない入口、解放演出の光） */
void MapScene::UpdatePortals(int curFloor) {
    for (int i = 0; i < static_cast<int>(floors_.size()); ++i) {
        // 演出が無い時の色: クリア済みは解放後の光の色、今のステージは種類の色、先のステージは閉じた色
        Vector4 color = NodeColor(floors_[i][0], i == selectedCol_);
        if (i < curFloor) {
            color = StageLiberation::ClearedColor();
        } else if (i > curFloor) {
            color = kPortalLockedColor;
        }
        // 解放演出の間は、クリアした入口を光らせ、次の入口を閉じた色から開いていく
        const float scale = liberation_.PortalScale(i);
        portalObjects_[i]->SetPosition({ stageWorldX_[i], kPortalY, kPortalZ });
        portalObjects_[i]->SetScale({ kPortalScale.x * scale, kPortalScale.y * scale, kPortalScale.z });
        color = liberation_.PortalColor(i, color, kPortalLockedColor);
        if (!liberation_.IsPlaying() && portalMotion_ != PortalMotion::kNone && i == motionPortalIndex_) {
            const bool entering = portalMotion_ != PortalMotion::kExiting;
            const float elapsed = portalMotionTimer_ - (entering ? sPortalTune.alignSeconds : 0.0f);
            const float t = PortalProgress(elapsed, entering ? sPortalTune.enterSeconds : sPortalTune.exitSeconds);
            const float glow = std::sin(t * gameConstants::kPi) * std::clamp(sPortalTune.glowStrength, 0.0f, 1.0f);
            color.x += (sPortalTune.glowColor.x - color.x) * glow;
            color.y += (sPortalTune.glowColor.y - color.y) * glow;
            color.z += (sPortalTune.glowColor.z - color.z) * glow;
        }
        portalObjects_[i]->SetColor(color);
        portalObjects_[i]->Update();
    }
}

/** @brief マップノードと現在選択状態を描画する */
void MapScene::Draw() {
    // 今いる階を引くための進行データ
    auto* rd = RunData::GetInstance();

    // 3D（影・ワールド・配置物）を描いてから、文字とパネルを重ねる
    DrawShadowPass();
    DrawWorld();
    GetStageEditor().DrawObjects();

    // 2D描画の設定に切り替え、解放演出の光を重ねてから、前フレームの文字を消す
    spriteCommon_->CommonDrawSettings();
    const float cameraX = camera_->GetTranslate().x;
    liberation_.Draw(cameraX);
    debugLiberationButton_.RefreshLayout();
    if (!liberation_.IsPlaying()) {
        debugLiberationButton_.Draw();
    }
    fontRenderer_.Reset();
    liberation_.QueueText(fontRenderer_, cameraX);
    if (!liberation_.IsPlaying()) {
        debugLiberationButton_.QueueText(fontRenderer_);
    }

    // 入口の上のステージ名と、選択中の入口の説明
    const int floor = rd->GetFloor();
    if (floor < static_cast<int>(floors_.size())) {
        DrawStagePortalLabels(floor);
    }

    // 操作説明と戻り方の案内（パッドとキーボードで文言を変える）
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 helpPosition = layout.Pos("help.pos", kHelpTextPosition);
    const bool pad = input_->IsUsingGamepad();
    const std::wstring helpText = pad
        ? L"左スティックで移動  入口の前でAボタン  Yボタンでトレーニング"
        : L"A Dで移動  入口の前でEnter  Tでトレーニング";
    const std::wstring backText = pad ? L"Bボタンでタイトルへ戻る" : L"Backspaceでタイトルへ戻る";
    const float helpScale = layout.Float("help.scale", kHelpTextScale);
    const float backScale = layout.Float("back_hint.scale", kHelpTextScale);
    const Vector2 backPosition = layout.Pos("back_hint.pos", kBackHintPosition);
    DrawHintPanel(helpText, helpPosition, helpScale);
    DrawHintPanel(backText, backPosition, backScale);
    fontRenderer_.DrawStringW(helpText, helpPosition.x, helpPosition.y, helpScale, layout.Color("help.color", kHelpTextColor));
    fontRenderer_.DrawStringW(backText, backPosition.x, backPosition.y, backScale, layout.Color("back_hint.color", kHelpTextColor));

    // ステージに置いた文章UIを足し、溜めた文字をまとめて描く
    GetStageEditor().DrawUIText(fontRenderer_);
    fontRenderer_.Draw();
}

/** @brief エディタ表示中（移動停止中）もカメラ移動に合わせてプレイヤーと入口ポータルの行列だけ更新する */
void MapScene::RefreshVisualTransformsForEditor() {
    player_->RefreshVisualTransforms();
    for (auto& portal : portalObjects_) {
        portal->Update();
    }
}

/** @brief Draw()の下請け 街並み・地面ブロックのシャドウマップパスを描画する */
void MapScene::DrawShadowPass() {
    // シャドウマップを描画可能状態からシェーダー参照状態へ遷移する
    shadowManager_->RunShadowPass(dxCommon_->GetCommandList(), modelCommon_.get(), [this] {
        GetStageEditor().DrawObjectShadows();
    });
}

/** @brief Draw()の下請け 背景・街並み・地面ブロック・入口ポータル・プレイヤーを3D描画する */
void MapScene::DrawWorld() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector4 backgroundColor = layout.Color("background.color", kBackgroundColor);
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // シャドウパスが設定した専用DSVから通常画面のRTVとDSVへ描画先を戻す
    dxCommon_->SetBackBufferRenderTarget();

    // 背景を描画してから本編と同じ3D描画状態へ切り替える
    spriteCommon_->CommonDrawSettings();
    bgSprite_->SetColor(backgroundColor);
    bgSprite_->Update();
    bgSprite_->Draw();

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(commandList);
    shadowManager_->SetShadowMap(commandList, SrvManager::GetInstance());

    const int floor = RunData::GetInstance()->GetFloor();
    if (floor < static_cast<int>(floors_.size())) {
        const int count = static_cast<int>(floors_.size());
        for (int i = 0; i < count; ++i) {
            portalObjects_[i]->Draw();
        }
    }
    if (playerVisible_) {
        player_->Draw();
    }
}

/** @brief 操作説明の後ろに暗い板を敷く（明るい背景でも読めるように） */
void MapScene::DrawHintPanel(const std::wstring& text, const Vector2& position, float scale) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float hintPanelPadding = layout.Float("hint_panel.padding", kHintPanelPadding);
    const Vector4 hintPanelColor = layout.Color("hint_panel.color", kHintPanelColor);
    const Vector2 size = FontRenderer::MeasureStringW(text, scale);
    fontRenderer_.DrawPanel(position.x - hintPanelPadding, position.y - hintPanelPadding,
        size.x + hintPanelPadding * 2.0f, size.y + hintPanelPadding * 2.0f, hintPanelColor);
}

/** @brief Draw()の下請け 各ステージ入口ポータルの上に番号ラベルとENTER案内を描画する */
void MapScene::DrawStagePortalLabels(int floor) {
    // 表示位置・大きさ・色は UI レイアウトから読む
    UILayout& layout = UILayout::Get(kLayoutName);
    // 影の付け方と、決定ボタン案内の位置
    const float labelOffsetX = layout.Float("portal_label.offset_x", kLabelOffsetX);
    const Vector2 labelShadowOffset = layout.Vec2("portal_label.shadow_offset", kLabelShadowOffset);
    const Vector4 labelShadowColor = layout.Color("portal_label.shadow_color", kLabelShadowColor);
    const float enterHintOffsetX = layout.Float("enter_hint.offset_x", kEnterHintOffsetX);
    const Vector4 enterHintShadowColor = layout.Color("enter_hint.shadow_color", kEnterHintShadowColor);
    // 入口の数と各入口のワールドX座標、画面へ直すためのカメラ位置
    const int count = static_cast<int>(floors_.size());
    const float* portalXs = stageWorldX_.data();
    const Vector3& cameraPos = camera_->GetTranslate();
    // ステージ名ラベルの高さ・大きさ・通常色と選択中の色
    const float labelY = layout.Float("portal_label.y", kLabelY);
    const float labelScale = layout.Float("portal_label.scale", kLabelScale);
    const Vector4 labelColor = layout.Color("portal_label.color", kLabelColor);
    const Vector4 labelNearColor = layout.Color("portal_label.near_color", kLabelNearColor);
    // 決定ボタン案内の高さ・大きさ・色。案内の文字はゲームパッドかキーボードかで変える
    const float enterHintY = layout.Float("enter_hint.y", kEnterHintY);
    const float enterHintScale = layout.Float("enter_hint.scale", kEnterHintScale);
    const Vector4 enterHintColor = layout.Color("enter_hint.color", kEnterHintColor);
    const char* enterHint = input_->IsUsingGamepad() ? "A" : "ENTER";
    for (int i = 0; i < count; ++i) {
        const bool isNear = i == selectedCol_;
        // 入口のワールド座標を画面のX座標へ直す
        const float screenX = (portalXs[i] - cameraPos.x)
                / gameConstants::kCameraHalfW * gameConstants::kScreenCenterX
            + gameConstants::kScreenCenterX;

        // ステージ名は影を少しずらして重ね、選択中は色を変える
        wchar_t stageLabel[textBufferCapacity::kShortText];
        swprintf_s(stageLabel, L"ステージ %d", i + 1);
        const wchar_t* label = stageLabel;
        fontRenderer_.DrawStringW(label, screenX - labelOffsetX + labelShadowOffset.x, labelY + labelShadowOffset.y,
            labelScale, labelShadowColor);
        fontRenderer_.DrawStringW(label, screenX - labelOffsetX, labelY, labelScale,
            isNear ? labelNearColor : labelColor);
        // 選択中の入口だけ、決定ボタンの案内と説明パネルを出す
        if (isNear) {
            // 案内もラベルと同じく影を先に描いてから本体を重ねる
            fontRenderer_.DrawString(enterHint, screenX - enterHintOffsetX + labelShadowOffset.x,
                enterHintY + labelShadowOffset.y, enterHintScale, enterHintShadowColor);
            fontRenderer_.DrawString(enterHint, screenX - enterHintOffsetX, enterHintY, enterHintScale,
                enterHintColor);
            DrawSelectedNodeInfo(floor, floors_[i][0]);
        }
    }
}

/** @brief 選択中ノードの説明パネル（右側）を描画する */
void MapScene::DrawSelectedNodeInfo(int curFloor, RunData::NodeType hoveredNode) {
    // 説明パネルの下地を置いてから、見出しと説明文を重ねる
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 infoTitleOffset = layout.Vec2("info_title.offset", kInfoTitleOffset);
    const float infoTitleScale = layout.Float("info_title.scale", kInfoTitleScale);
    const Vector4 infoTitleColor = layout.Color("info_title.color", kInfoTitleColor);
    const Vector2 infoDescOffset = layout.Vec2("info_desc.offset", kInfoDescOffset);
    const float infoDescLineHeight = layout.Float("info_desc.line_height", kInfoDescLineHeight);
    const float infoDescScale = layout.Float("info_desc.scale", kInfoDescScale);
    const Vector4 infoDescColor = layout.Color("info_desc.color", kInfoDescColor);
    // 折り返した本文と見出しを測り、枠の左右に余白を確保する。
    constexpr float kRightMargin = 16.0f;
    constexpr size_t kSeparatorLength = 1;
    const NodeInfo& panelInfo = GetNodeInfo(hoveredNode);
    const std::wstring panelTitle = panelInfo.entersCombat ? L"ステージ入口" : panelInfo.label;
    const std::wstring panelDesc(panelInfo.desc);
    const size_t panelBreak = panelDesc.find(L'　');
    const bool panelWraps = panelBreak != std::wstring::npos && panelBreak > kInfoDescMinWrapIndex;
    const std::wstring firstLine = panelWraps ? panelDesc.substr(0, panelBreak) : panelDesc;
    const std::wstring secondLine = panelWraps ? panelDesc.substr(panelBreak + kSeparatorLength) : L"";
    const float descWidth = (std::max)(FontRenderer::MeasureStringW(firstLine, infoDescScale).x,
        FontRenderer::MeasureStringW(secondLine, infoDescScale).x);
    Vector2 panelSize = layout.Vec2("info_panel.size", kInfoPanelSize);
    panelSize.x = (std::max)(panelSize.x, (std::max)(
        infoTitleOffset.x + FontRenderer::MeasureStringW(panelTitle, infoTitleScale).x + infoTitleOffset.x,
        infoDescOffset.x + descWidth + infoDescOffset.x));
    // 枠全体を画面右端の余白より内側に配置する。
    Vector2 panelPosition = layout.Pos("info_panel.pos", kInfoPanelPosition);
    panelPosition.x = (std::min)(panelPosition.x, gameConstants::kScreenWidth - kRightMargin - panelSize.x);
    nodeSprite_->SetPosition(panelPosition);
    nodeSprite_->SetSize(panelSize);
    nodeSprite_->SetColor(layout.Color("info_panel.color", kInfoPanelColor));
    nodeSprite_->Update();
    nodeSprite_->Draw();
    // ── 選択中ノードの説明（右側）──
    if (curFloor >= static_cast<int>(floors_.size())) {
        return;
    }

    // 戦闘に入るマスはステージの入口として、それ以外はマスの名前で見出しを出す
    const NodeInfo& info = GetNodeInfo(hoveredNode);
    const wchar_t* desc = info.desc;
    fontRenderer_.DrawStringW(info.entersCombat ? L"ステージ入口" : info.label,
        panelPosition.x + infoTitleOffset.x, panelPosition.y + infoTitleOffset.y, infoTitleScale, infoTitleColor);
    const float descX = panelPosition.x + infoDescOffset.x;
    const float descY = panelPosition.y + infoDescOffset.y;
    // 説明を2行に折り返して表示
    std::wstring descStr(desc);
    size_t br = descStr.find(L'　'); // 全角スペースで折り返しポイントを探す
    if (br != std::wstring::npos && br > kInfoDescMinWrapIndex) {
        fontRenderer_.DrawStringW(descStr.substr(0, br).c_str(), descX, descY, infoDescScale, infoDescColor);
        fontRenderer_.DrawStringW(descStr.substr(br + 1).c_str(), descX, descY + infoDescLineHeight,
            infoDescScale, infoDescColor);
    } else {
        fontRenderer_.DrawStringW(desc, descX, descY, infoDescScale, infoDescColor);
    }
}

} // namespace engine::game
