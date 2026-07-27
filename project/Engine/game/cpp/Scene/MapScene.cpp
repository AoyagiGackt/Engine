/**
 * @file MapScene.cpp
 * @brief ローグライトのフロア選択マップ（MapScene）の表示とノード選択・遷移処理の実装
 */
#include "MapScene.h"
#include "GameConstants.h"
#include "SceneManager.h"
#include "SkinnedObject3d.h"
#include "SrvManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

// フロアごとのY座標（上=boss, 下=floor0）
static constexpr float kFloorY[4] = { 530.0f, 410.0f, 290.0f, 150.0f };

// ノードの列X座標
static constexpr float kColX3[3] = { 280.0f, 640.0f, 1000.0f };
static constexpr float kColX2[2] = { 430.0f, 850.0f };
static constexpr float kColX1[1] = { 640.0f };

static constexpr float kNodeW = 130.0f;
static constexpr float kNodeH = 44.0f;
static constexpr int kStageCount = 6;
static constexpr float kStageWorldX[kStageCount] = { 8.0f, 18.0f, 28.0f, 38.0f, 48.0f, 58.0f };

static Vector4 NodeColor(RunData::NodeType t, bool selected, bool completed)
{
    if (completed) {
        return { 0.25f, 0.25f, 0.25f, 1.0f };
    }
    Vector4 c;
    switch (t) {
    case RunData::NodeType::Combat:
        c = { 0.75f, 0.18f, 0.18f, 1.0f };
        break;
    case RunData::NodeType::Elite:
        c = { 0.85f, 0.45f, 0.08f, 1.0f };
        break;
    case RunData::NodeType::Shop:
        c = { 0.18f, 0.65f, 0.28f, 1.0f };
        break;
    case RunData::NodeType::Rest:
        c = { 0.18f, 0.38f, 0.85f, 1.0f };
        break;
    case RunData::NodeType::Boss:
        c = { 0.55f, 0.08f, 0.75f, 1.0f };
        break;
    default:
        c = { 0.5f, 0.5f, 0.5f, 1.0f };
        break;
    }
    if (selected) {
        c.x = (std::min)(c.x + 0.25f, 1.0f);
        c.y = (std::min)(c.y + 0.25f, 1.0f);
        c.z = (std::min)(c.z + 0.25f, 1.0f);
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
        return L"敵を倒してゴールドを獲得　スタイルが高いほど報酬UP";
    case RunData::NodeType::Elite:
        return L"手強い敵　倒せば多くのゴールドを獲得できる";
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

    InitializeUiSprites();
    InitializeRenderFoundationAndPlayer();
    InitializeStageObjects();
    InitializeFloorsAndStartPosition();
}

void MapScene::InitializeUiSprites()
{
    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });
    bgSprite_->SetColor({ 0.05f, 0.05f, 0.08f, 1.0f });

    nodeSprite_ = std::make_unique<Sprite>();
    nodeSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    nodeSprite_->SetSize({ kNodeW, kNodeH });

    groundSprite_ = std::make_unique<Sprite>();
    groundSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    groundSprite_->SetPosition({ 0.0f, 560.0f });
    groundSprite_->SetSize({ GameConstants::kScreenWidth, 160.0f });
    groundSprite_->SetColor({ 0.12f, 0.42f, 0.2f, 1.0f });

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
    camera_->SetTranslate({ 18.0f, 4.8f, -24.0f });
    Object3d::SetCommonCamera(camera_.get());

    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    player_->SetHorizontalBounds(2.5f, 63.5f);
    player_->SetPosition({ 8.0f, 0.4f, 0.0f });
}

void MapScene::InitializeStageObjects()
{
    blockModel_ = std::make_unique<Model>();
    blockModel_->Initialize(modelCommon_.get(),
        "Resources/block/block.obj",
        "Resources/DowntownCityMegaKit[Standard]/Textures/T_Concrete_BaseColor.png");
    for (int x = 0; x <= 66; ++x) {
        auto block = std::make_unique<Object3d>();
        block->Initialize(modelCommon_.get());
        block->SetModel(blockModel_.get());
        block->SetPosition({ static_cast<float>(x), -0.6f, 0.0f });
        block->SetEnableLighting(false);
        block->Update();
        groundBlocks_.push_back(std::move(block));
    }

    for (int i = 0; i < kStageCount; ++i) {
        auto portal = std::make_unique<Object3d>();
        portal->Initialize(modelCommon_.get());
        portal->SetModel(blockModel_.get());
        portal->SetScale({ 2.5f, 4.0f, 1.0f });
        portal->SetEnableLighting(false);
        portalObjects_.push_back(std::move(portal));
    }

    cityModel_ = std::make_unique<Model>();
    cityModel_->Initialize(modelCommon_.get(),
        "Resources/DowntownCityMegaKit[Standard]/Exports/glTF (Godot)/Building_Small_1.gltf",
        "Resources/DowntownCityMegaKit[Standard]/Textures/T_RedBrick_BaseColor.png");
    for (float x : { 5.0f, 19.0f, 33.0f, 47.0f, 61.0f }) {
        auto city = std::make_unique<Object3d>();
        city->Initialize(modelCommon_.get());
        city->SetModel(cityModel_.get());
        city->SetPosition({ x, -0.6f, 6.0f });
        city->SetScale({ 0.45f, 0.45f, 0.45f });
        city->Update();
        cityObjects_.push_back(std::move(city));
    }
}

void MapScene::InitializeFloorsAndStartPosition()
{
    floors_ = {
        { RunData::NodeType::Combat },
        { RunData::NodeType::Combat },
        { RunData::NodeType::Combat },
        { RunData::NodeType::Combat },
        { RunData::NodeType::Elite },
        { RunData::NodeType::Boss },
    };

    selectedCol_ = -1;

    auto* rd = RunData::GetInstance();
    if (rd->GetFloor() >= static_cast<int>(floors_.size())) {
        SceneManager::GetInstance()->ChangeScene("CLEAR");
    } else {
        player_->SetPosition({ kStageWorldX[rd->GetFloor()], 0.4f, 0.0f });
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
    player_->Update(input_, { currentPos.x + player_->GetLastDirX() * 8.0f, currentPos.y, 0.0f });
    Vector3& playerPos = player_->GetPositionRef();
    playerPos.x = std::clamp(playerPos.x, 2.5f, 63.5f);
    camera_->SetTranslate({ std::clamp(playerPos.x, 12.0f, 54.0f), 4.8f, -24.0f });
    player_->RefreshVisualTransforms();

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    SkinnedObject3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());

    selectedCol_ = -1;
    float nearestDistance = 2.5f;
    for (int i = 0; i < kStageCount; ++i) {
        const float distance = std::abs(playerPos.x - kStageWorldX[i]);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            selectedCol_ = i;
        }
    }

    for (auto& block : groundBlocks_) {
        block->Update();
    }
    for (auto& city : cityObjects_) {
        city->Update();
    }
    for (int i = 0; i < kStageCount; ++i) {
        portalObjects_[i]->SetPosition({ kStageWorldX[i], 1.4f, 1.0f });
        Vector4 color = NodeColor(floors_[i][0], i == selectedCol_, i < curFloor);
        if (i > curFloor) {
            color = { 0.12f, 0.12f, 0.16f, 1.0f };
        }
        portalObjects_[i]->SetColor(color);
        portalObjects_[i]->Update();
    }

    if (input_->TriggerKey(DIK_T)) {
        SceneManager::GetInstance()->ChangeScene("TRAINING");
        return;
    }

    if (selectedCol_ == curFloor && (input_->TriggerKey(DIK_RETURN) || input_->TriggerButton(XINPUT_GAMEPAD_A))) {
        RunData::NodeType chosen = floors_[selectedCol_][0];
        rd->SetCurrentNode(chosen);

        switch (chosen) {
        case RunData::NodeType::Combat:
        case RunData::NodeType::Elite:
        case RunData::NodeType::Boss:
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
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

    spriteCommon_->CommonDrawSettings();
    fontRenderer_.Reset();

    DrawHeader(rd);
    const int floor = rd->GetFloor();
    if (floor < static_cast<int>(floors_.size())) {
        DrawStagePortalLabels(floor);
    }

    DrawSkillList(rd);

    fontRenderer_.DrawStringW(L"A Dまたは左スティックで移動  入口の前でEnterまたはAボタン  Tでトレーニング",
        20.0f, 690.0f, 1.1f, { 0.88f, 0.90f, 1.0f, 1.0f });

    fontRenderer_.Draw();
}

void MapScene::DrawShadowPass()
{
    // シャドウマップを描画可能状態からシェーダー参照状態へ遷移する
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    shadowManager_->BeginShadowPass(commandList);
    modelCommon_->BeginShadowPass();
    for (auto& city : cityObjects_) {
        city->DrawShadow();
    }
    for (auto& block : groundBlocks_) {
        block->DrawShadow();
    }
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
    for (auto& city : cityObjects_) {
        city->Draw();
    }
    for (auto& block : groundBlocks_) {
        block->Draw();
    }

    const int floor = RunData::GetInstance()->GetFloor();
    if (floor < static_cast<int>(floors_.size())) {
        const int count = kStageCount;
        for (int i = 0; i < count; ++i) {
            portalObjects_[i]->Draw();
        }
    }
    player_->Draw();
}

void MapScene::DrawStagePortalLabels(int floor)
{
    const int count = kStageCount;
    const float* portalXs = kStageWorldX;
    const Vector3& cameraPos = camera_->GetTranslate();
    for (int i = 0; i < count; ++i) {
        const bool isNear = i == selectedCol_;
        const float screenX = (portalXs[i] - cameraPos.x)
                / GameConstants::kCameraHalfW * GameConstants::kScreenCenterX
            + GameConstants::kScreenCenterX;

        wchar_t stageLabel[32];
        swprintf_s(stageLabel, L"ステージ %d", i + 1);
        const wchar_t* label = stageLabel;
        fontRenderer_.DrawStringW(label, screenX - 40.0f, 188.0f, 1.55f,
            { 0.02f, 0.02f, 0.04f, 0.95f });
        fontRenderer_.DrawStringW(label, screenX - 42.0f, 185.0f, 1.55f,
            isNear ? Vector4 { 1.0f, 1.0f, 0.2f, 1.0f }
                   : Vector4 { 1.0f, 1.0f, 1.0f, 1.0f });
        if (isNear) {
            fontRenderer_.DrawStringW(L"ENTERで入る", screenX - 62.0f, 225.0f, 1.2f,
                { 1.0f, 0.9f, 0.3f, 0.0f });
            fontRenderer_.DrawString("ENTER / A", screenX - 41.0f, 228.0f, 1.35f,
                { 0.02f, 0.02f, 0.03f, 0.95f });
            fontRenderer_.DrawString("ENTER / A", screenX - 43.0f, 225.0f, 1.35f,
                { 1.0f, 0.92f, 0.22f, 1.0f });
            DrawSelectedNodeInfo(floor, floors_[i][0]);
        }
    }
}

void MapScene::DrawHeader(RunData* rd)
{
    // ── タイトルバー ──
    fontRenderer_.DrawStringW(L"STYLE RUN", 510.0f, 12.0f, 2.2f, { 1.0f, 0.85f, 0.2f, 1.0f });

    // HP / ゴールド
    char buf[64];
    snprintf(buf, sizeof(buf), "HP:%d/%d", rd->GetHp(), rd->GetMaxHp());
    fontRenderer_.DrawString(buf, 20.0f, 18.0f, 1.6f, { 0.3f, 1.0f, 0.4f, 1.0f });
    snprintf(buf, sizeof(buf), "Gold: %dG", rd->GetGold());
    fontRenderer_.DrawString(buf, 20.0f, 44.0f, 1.6f, { 1.0f, 0.85f, 0.2f, 1.0f });
}

RunData::NodeType MapScene::DrawFloorNodes(int curFloor)
{
    RunData::NodeType hoveredNode = RunData::NodeType::Combat;

    for (int f = 0; f < static_cast<int>(floors_.size()); ++f) {
        const auto& row = floors_[f];
        int nCols = static_cast<int>(row.size());
        const float* colXs = (nCols == 3) ? kColX3 : (nCols == 2) ? kColX2
                                                                  : kColX1;
        float rowY = kFloorY[f];

        bool isActive = (f == curFloor);
        bool isCompleted = (f < curFloor);

        // フロアラベル
        {
            wchar_t lbl[16];
            if (f == 3) {
                swprintf_s(lbl, L"BOSS");
            } else {
                swprintf_s(lbl, L"フロア %d", f + 1);
            }
            Vector4 lblCol = isActive ? Vector4 { 1.0f, 1.0f, 1.0f, 1.0f }
                : isCompleted         ? Vector4 { 0.35f, 0.35f, 0.35f, 1.0f }
                                      : Vector4 { 0.5f, 0.5f, 0.5f, 1.0f };
            fontRenderer_.DrawStringW(lbl, 30.0f, rowY + 8.0f, 1.2f, lblCol);
        }

        for (int c = 0; c < nCols; ++c) {
            float nx = colXs[c] - kNodeW * 0.5f;
            float ny = rowY;
            bool selected = isActive && (c == selectedCol_);

            if (selected) {
                hoveredNode = row[c];
            }

            Vector4 col = NodeColor(row[c], selected, isCompleted);
            nodeSprite_->SetPosition({ nx, ny });
            nodeSprite_->SetColor(col);
            nodeSprite_->Update();
            nodeSprite_->Draw();

            // 日本語ノードラベル
            const wchar_t* wlbl = NodeLabelW(row[c]);
            float charW = FontRenderer::kJpCharW * 1.3f;
            float textW = static_cast<float>(wcslen(wlbl)) * charW;
            float tx = nx + (kNodeW - textW) * 0.5f;
            float ty = ny + (kNodeH - FontRenderer::kJpCharH * 1.3f) * 0.5f;

            Vector4 textCol = isCompleted ? Vector4 { 0.45f, 0.45f, 0.45f, 1.0f }
                : selected                ? Vector4 { 1.0f, 1.0f, 0.15f, 1.0f }
                                          : Vector4 { 1.0f, 1.0f, 1.0f, 1.0f };
            fontRenderer_.DrawStringW(wlbl, tx, ty, 1.3f, textCol);

            // 選択カーソル
            if (selected) {
                fontRenderer_.DrawString(">", nx - 16.0f, ty + 4.0f, 1.4f, { 1.0f, 1.0f, 0.2f, 1.0f });
                fontRenderer_.DrawString("<", nx + kNodeW + 2.0f, ty + 4.0f, 1.4f, { 1.0f, 1.0f, 0.2f, 1.0f });
            }
        }
    }

    return hoveredNode;
}

void MapScene::DrawSelectedNodeInfo(int curFloor, RunData::NodeType hoveredNode)
{
    nodeSprite_->SetPosition({ 875.0f, 350.0f });
    nodeSprite_->SetSize({ 365.0f, 118.0f });
    nodeSprite_->SetColor({ 0.015f, 0.02f, 0.04f, 0.84f });
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
        900.0f, 370.0f, 1.8f, { 1.0f, 0.85f, 0.2f, 1.0f });
    // 説明を2行に折り返して表示
    std::wstring descStr(desc);
    size_t br = descStr.find(L'　'); // 全角スペースで折り返しポイントを探す
    if (br != std::wstring::npos && br > 10) {
        fontRenderer_.DrawStringW(descStr.substr(0, br).c_str(), 900.0f, 410.0f, 1.15f, { 1.0f, 1.0f, 1.0f, 1.0f });
        fontRenderer_.DrawStringW(descStr.substr(br + 1).c_str(), 900.0f, 435.0f, 1.15f, { 1.0f, 1.0f, 1.0f, 1.0f });
    } else {
        fontRenderer_.DrawStringW(desc, 900.0f, 410.0f, 1.15f, { 1.0f, 1.0f, 1.0f, 1.0f });
    }
}

void MapScene::DrawSkillList(RunData* rd)
{
    // ── スキル一覧 ──
    fontRenderer_.DrawStringW(L"取得スキル:", 20.0f, 648.0f, 1.2f, { 0.7f, 0.9f, 1.0f, 1.0f });
    if (rd->GetSkills().empty()) {
        fontRenderer_.DrawStringW(L"なし", 200.0f, 648.0f, 1.2f, { 0.5f, 0.5f, 0.5f, 1.0f });
        return;
    }

    float sx = 200.0f;
    for (auto sk : rd->GetSkills()) {
        const char* name = RunData::SkillName(sk);
        // 最初の単語だけ（スペース前まで）
        std::string n(name);
        auto p = n.find(' ');
        std::string short_n = (p != std::string::npos) ? n.substr(0, p) : n;
        fontRenderer_.DrawString((short_n + "  ").c_str(), sx, 650.0f, 1.2f, { 0.9f, 0.85f, 0.3f, 1.0f });
        sx += static_cast<float>(short_n.size() + 2) * FontRenderer::kCharW * 1.2f;
    }
}
