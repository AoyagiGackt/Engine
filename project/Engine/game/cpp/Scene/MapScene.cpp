/**
 * @file MapScene.cpp
 * @brief ローグライトのフロア選択マップ（MapScene）の表示とノード選択・遷移処理の実装
 */
#include "MapScene.h"
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
#include <string>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

static constexpr int kStageCount = 6;
static constexpr float kStageWorldX[kStageCount] = { 8.0f, 18.0f, 28.0f, 38.0f, 48.0f, 58.0f };

// ノード種別ごとの色
static constexpr Vector4 kNodeCompletedColor = { 0.25f, 0.25f, 0.25f, 1.0f };
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

// 以下は既定の配置（F2のエディタで動かした値は Resources/Config/UI/map.json に保存される）
static constexpr const char* kLayoutName = "map";

// 画面下の操作説明
static constexpr Vector2 kHelpTextPosition = { 20.0f, 690.0f };
static constexpr float kHelpTextScale = 1.1f;
static constexpr Vector4 kHelpTextColor = { 0.88f, 0.90f, 1.0f, 1.0f };

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

static Vector4 NodeColor(RunData::NodeType t, bool selected, bool completed)
{
    if (completed) {
        return kNodeCompletedColor;
    }
    Vector4 c;
    switch (t) {
    case RunData::NodeType::Combat:
        c = kNodeCombatColor;
        break;
    case RunData::NodeType::Elite:
        c = kNodeEliteColor;
        break;
    case RunData::NodeType::Shop:
        c = kNodeShopColor;
        break;
    case RunData::NodeType::Rest:
        c = kNodeRestColor;
        break;
    case RunData::NodeType::Boss:
        c = kNodeBossColor;
        break;
    default:
        c = kNodeDefaultColor;
        break;
    }
    if (selected) {
        c.x = (std::min)(c.x + kNodeSelectedBrighten, 1.0f);
        c.y = (std::min)(c.y + kNodeSelectedBrighten, 1.0f);
        c.z = (std::min)(c.z + kNodeSelectedBrighten, 1.0f);
    }
    return c;
}

// ノードの日本語ラベル（表示用）
static const wchar_t* NodeLabelW(RunData::NodeType t)
{
    switch (t) {
    case RunData::NodeType::Combat:
        return L"戦 闘";
    case RunData::NodeType::Elite:
        return L"強敵";
    case RunData::NodeType::Shop:
        return L"ショップ";
    case RunData::NodeType::Rest:
        return L"休 憩";
    case RunData::NodeType::Boss:
        return L"ボ ス";
    default:
        return L"？？？";
    }
}

// ノードの説明（右下に表示）
static const wchar_t* NodeDesc(RunData::NodeType t)
{
    switch (t) {
    case RunData::NodeType::Combat:
        return L"ステージを進み、ボスの武器を奪ってクリア　スタイルが高いほど評価UP";
    case RunData::NodeType::Elite:
        return L"手強い敵が待ち構えるステージ";
    case RunData::NodeType::Shop:
        return L"スキルを1つ選んで取得できる　スキルは永続効果";
    case RunData::NodeType::Rest:
        return L"HP を10回復する　のんびり休もう";
    case RunData::NodeType::Boss:
        return L"最終決戦  倒せばクリア! 全力で挑め";
    default:
        return L"";
    }
}

void MapScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    // Title の NEW GAME/CONTINUE を経由せずここへ来た場合（Tabショートカット・デバッグ直行等）に備え、
    // ランが未開始ならここで開始しておく（未開始のままだとHP/スタイルUIがGamePlayScene側で出ない）
    // 武器もトレーニング等で装備したものがシングルトン経由で残ってしまうためあわせてリセットする
    if (!RunData::GetInstance()->IsRunActive()) {
        RunData::GetInstance()->StartNewRun();
        WeaponManager::GetInstance()->Reset();
    }

    InitializeUiSprites();
    InitializeRenderFoundationAndPlayer();
    InitializeFloorsAndStartPosition(); // ポータルの本数がステージ数に依存するため先に決める
    InitializeStageObjects();
}

void MapScene::InitializeUiSprites()
{
    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });
    bgSprite_->SetColor(kBackgroundColor);

    nodeSprite_ = std::make_unique<Sprite>();
    nodeSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");

    fontRenderer_.Initialize(spriteCommon_.get());
}

void MapScene::InitializeRenderFoundationAndPlayer()
{
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);
    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    // 3D共通シェーダーが必ず参照するシャドウマップをマップ画面でも用意する
    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, SrvManager::GetInstance());
    Object3d::SetCommonObjectCommon(objectCommon_.get());
    Object3d::SetCommonShadowManager(shadowManager_.get());
    SkinnedObject3d::SetCommonObjectCommon(objectCommon_.get());
    SkinnedObject3d::SetCommonShadowManager(shadowManager_.get());

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ kInitialCameraX, kCameraY, GameConstants::kCameraDistanceZ });
    Object3d::SetCommonCamera(camera_.get());

    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    player_->SetHorizontalBounds(kPlayerMinX, kPlayerMaxX);
    player_->SetPosition({ kStageWorldX[0], kPlayerGroundY, 0.0f });
}

void MapScene::InitializeStageObjects()
{
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

void MapScene::InitializeFloorsAndStartPosition()
{
    // ステージ数はgame_rules.jsonのlevelPathsに追従する（ローグライク時代のフロア構成は撤回済み）。
    // 最後のステージだけボス扱いにしてHPを上げる。ポータルの座標はkStageWorldXの数までが上限
    const int stageCount = std::clamp(static_cast<int>(GameRules::GetInstance()->Get().levelPaths.size()), 1, kStageCount);
    floors_.clear();
    for (int i = 0; i < stageCount; ++i) {
        floors_.push_back({ i == stageCount - 1 ? RunData::NodeType::Boss : RunData::NodeType::Combat });
    }

    selectedCol_ = -1;

    auto* rd = RunData::GetInstance();
    if (rd->GetFloor() >= static_cast<int>(floors_.size())) {
        SceneFlow::GetInstance()->Transition("MAP", "all_cleared", "CLEAR");
    } else {
        player_->SetPosition({ kStageWorldX[rd->GetFloor()], kPlayerGroundY, 0.0f });
    }
}

void MapScene::Finalize()
{
}

void MapScene::Update()
{
    auto* rd = RunData::GetInstance();

    int curFloor = rd->GetFloor();
    if (curFloor >= static_cast<int>(floors_.size())) {
        return;
    }

    const Vector3& currentPos = player_->GetPosition();
    player_->Update(input_, { currentPos.x + player_->GetLastDirX() * kPlayerFacingTargetDistance, currentPos.y, 0.0f });
    Vector3& playerPos = player_->GetPositionRef();
    playerPos.x = std::clamp(playerPos.x, kPlayerMinX, kPlayerMaxX);
    camera_->SetTranslate({ std::clamp(playerPos.x, kCameraMinX, kCameraMaxX), kCameraY, GameConstants::kCameraDistanceZ });
    player_->RefreshVisualTransforms();

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    SkinnedObject3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());

    const int previousSelectedCol = selectedCol_;
    selectedCol_ = -1;
    float nearestDistance = kPortalSelectDistance;
    for (int i = 0; i < static_cast<int>(floors_.size()); ++i) {
        const float distance = std::abs(playerPos.x - kStageWorldX[i]);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            selectedCol_ = i;
        }
    }

    if (selectedCol_ >= 0 && selectedCol_ != previousSelectedCol) {
        audio_->PlayMenuChoice();
    }
    for (int i = 0; i < static_cast<int>(floors_.size()); ++i) {
        portalObjects_[i]->SetPosition({ kStageWorldX[i], kPortalY, kPortalZ });
        Vector4 color = NodeColor(floors_[i][0], i == selectedCol_, i < curFloor);
        if (i > curFloor) {
            color = kPortalLockedColor;
        }
        portalObjects_[i]->SetColor(color);
        portalObjects_[i]->Update();
    }

    if (input_->TriggerKey(DIK_T)) {
        audio_->PlayMenuSelect();
        SceneFlow::GetInstance()->Transition("MAP", "training", "TRAINING");
        return;
    }

    if (selectedCol_ == curFloor && (input_->TriggerKey(DIK_RETURN) || input_->TriggerButton(XINPUT_GAMEPAD_A))) {
        audio_->PlayMenuSelect();
        RunData::NodeType chosen = floors_[selectedCol_][0];
        rd->SetCurrentNode(chosen);

        switch (chosen) {
        case RunData::NodeType::Combat:
        case RunData::NodeType::Elite:
        case RunData::NodeType::Boss:
            SceneFlow::GetInstance()->Transition("MAP", "combat", "GAMEPLAY");
            break;
        case RunData::NodeType::Shop:
        case RunData::NodeType::Rest:
            break;
        }
    }
}

void MapScene::Draw()
{
    auto* rd = RunData::GetInstance();

    DrawShadowPass();
    DrawWorld();
    GetStageEditor().DrawObjects();

    spriteCommon_->CommonDrawSettings();
    fontRenderer_.Reset();

    const int floor = rd->GetFloor();
    if (floor < static_cast<int>(floors_.size())) {
        DrawStagePortalLabels(floor);
    }

    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 helpPosition = layout.Pos("help.pos", kHelpTextPosition);
    fontRenderer_.DrawStringW(L"A Dまたは左スティックで移動  入口の前でEnterまたはAボタン  Tでトレーニング",
        helpPosition.x, helpPosition.y, layout.Float("help.scale", kHelpTextScale), layout.Color("help.color", kHelpTextColor));

    GetStageEditor().DrawUIText(fontRenderer_);
    fontRenderer_.Draw();
}

void MapScene::RefreshVisualTransformsForEditor()
{
    player_->RefreshVisualTransforms();
    for (auto& portal : portalObjects_) {
        portal->Update();
    }
}

void MapScene::DrawShadowPass()
{
    // シャドウマップを描画可能状態からシェーダー参照状態へ遷移する
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    shadowManager_->BeginShadowPass(commandList);
    modelCommon_->BeginShadowPass();
    GetStageEditor().DrawObjectShadows();
    shadowManager_->EndShadowPass(commandList);
}

void MapScene::DrawWorld()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    // シャドウパスが設定した専用DSVから通常画面のRTVとDSVへ描画先を戻す
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon_->GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT viewport = dxCommon_->GetCenteredClientViewport();
    D3D12_RECT scissor = dxCommon_->GetCenteredClientScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    // 背景を描画してから本編と同じ3D描画状態へ切り替える
    spriteCommon_->CommonDrawSettings();
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
    player_->Draw();
}

void MapScene::DrawStagePortalLabels(int floor)
{
    const int count = static_cast<int>(floors_.size());
    const float* portalXs = kStageWorldX;
    const Vector3& cameraPos = camera_->GetTranslate();
    UILayout& layout = UILayout::Get(kLayoutName);
    const float labelY = layout.Float("portal_label.y", kLabelY);
    const float labelScale = layout.Float("portal_label.scale", kLabelScale);
    const Vector4 labelColor = layout.Color("portal_label.color", kLabelColor);
    const Vector4 labelNearColor = layout.Color("portal_label.near_color", kLabelNearColor);
    const float enterHintY = layout.Float("enter_hint.y", kEnterHintY);
    const float enterHintScale = layout.Float("enter_hint.scale", kEnterHintScale);
    const Vector4 enterHintColor = layout.Color("enter_hint.color", kEnterHintColor);
    for (int i = 0; i < count; ++i) {
        const bool isNear = i == selectedCol_;
        const float screenX = (portalXs[i] - cameraPos.x)
                / GameConstants::kCameraHalfW * GameConstants::kScreenCenterX
            + GameConstants::kScreenCenterX;

        wchar_t stageLabel[32];
        swprintf_s(stageLabel, L"ステージ %d", i + 1);
        const wchar_t* label = stageLabel;
        fontRenderer_.DrawStringW(label, screenX - kLabelOffsetX + kLabelShadowOffset.x, labelY + kLabelShadowOffset.y,
            labelScale, kLabelShadowColor);
        fontRenderer_.DrawStringW(label, screenX - kLabelOffsetX, labelY, labelScale,
            isNear ? labelNearColor : labelColor);
        if (isNear) {
            fontRenderer_.DrawString("ENTER / A", screenX - kEnterHintOffsetX + kLabelShadowOffset.x,
                enterHintY + kLabelShadowOffset.y, enterHintScale, kEnterHintShadowColor);
            fontRenderer_.DrawString("ENTER / A", screenX - kEnterHintOffsetX, enterHintY, enterHintScale,
                enterHintColor);
            DrawSelectedNodeInfo(floor, floors_[i][0]);
        }
    }
}

void MapScene::DrawSelectedNodeInfo(int curFloor, RunData::NodeType hoveredNode)
{
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 panelPosition = layout.Pos("info_panel.pos", kInfoPanelPosition);
    nodeSprite_->SetPosition(panelPosition);
    nodeSprite_->SetSize(layout.Vec2("info_panel.size", kInfoPanelSize));
    nodeSprite_->SetColor(layout.Color("info_panel.color", kInfoPanelColor));
    nodeSprite_->Update();
    nodeSprite_->Draw();
    // ── 選択中ノードの説明（右側）──
    if (curFloor >= static_cast<int>(floors_.size())) {
        return;
    }

    const wchar_t* desc = NodeDesc(hoveredNode);
    const bool isStage = hoveredNode == RunData::NodeType::Combat
        || hoveredNode == RunData::NodeType::Elite || hoveredNode == RunData::NodeType::Boss;
    fontRenderer_.DrawStringW(isStage ? L"ステージ入口" : NodeLabelW(hoveredNode),
        panelPosition.x + kInfoTitleOffset.x, panelPosition.y + kInfoTitleOffset.y, kInfoTitleScale, kInfoTitleColor);
    const float descX = panelPosition.x + kInfoDescOffset.x;
    const float descY = panelPosition.y + kInfoDescOffset.y;
    // 説明を2行に折り返して表示
    std::wstring descStr(desc);
    size_t br = descStr.find(L'　'); // 全角スペースで折り返しポイントを探す
    if (br != std::wstring::npos && br > kInfoDescMinWrapIndex) {
        fontRenderer_.DrawStringW(descStr.substr(0, br).c_str(), descX, descY, kInfoDescScale, kInfoDescColor);
        fontRenderer_.DrawStringW(descStr.substr(br + 1).c_str(), descX, descY + kInfoDescLineHeight,
            kInfoDescScale, kInfoDescColor);
    } else {
        fontRenderer_.DrawStringW(desc, descX, descY, kInfoDescScale, kInfoDescColor);
    }
}
