#include "MapScene.h"
#include "GameConstants.h"
#include "SceneManager.h"
#include <algorithm>
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
static constexpr float kNodeH =  44.0f;

static Vector4 NodeColor(RunData::NodeType t, bool selected, bool completed)
{
    if (completed) { return { 0.25f, 0.25f, 0.25f, 1.0f }; }
    Vector4 c;
    switch (t) {
    case RunData::NodeType::Combat: c = { 0.75f, 0.18f, 0.18f, 1.0f }; break;
    case RunData::NodeType::Elite:  c = { 0.85f, 0.45f, 0.08f, 1.0f }; break;
    case RunData::NodeType::Shop:   c = { 0.18f, 0.65f, 0.28f, 1.0f }; break;
    case RunData::NodeType::Rest:   c = { 0.18f, 0.38f, 0.85f, 1.0f }; break;
    case RunData::NodeType::Boss:   c = { 0.55f, 0.08f, 0.75f, 1.0f }; break;
    default:                         c = { 0.5f,  0.5f,  0.5f,  1.0f }; break;
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
    case RunData::NodeType::Combat: return L"戦 闘";
    case RunData::NodeType::Elite:  return L"強敵";
    case RunData::NodeType::Shop:   return L"ショップ";
    case RunData::NodeType::Rest:   return L"休 憩";
    case RunData::NodeType::Boss:   return L"ボ ス";
    default:                         return L"？？？";
    }
}

// ノードの説明（右下に表示）
static const wchar_t* NodeDesc(RunData::NodeType t)
{
    switch (t) {
    case RunData::NodeType::Combat: return L"敵を倒してゴールドを獲得　スタイルが高いほど報酬UP";
    case RunData::NodeType::Elite:  return L"手強い敵　倒せば多くのゴールドを獲得できる";
    case RunData::NodeType::Shop:   return L"スキルを1つ選んで取得できる　スキルは永続効果";
    case RunData::NodeType::Rest:   return L"HP を10回復する　のんびり休もう";
    case RunData::NodeType::Boss:   return L"最終決戦  倒せばクリア! 全力で挑め";
    default:                         return L"";
    }
}

void MapScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    dxCommon_ = dxCommon;
    input_    = input;
    audio_    = audio;

    spriteCommon_ = std::make_unique<SpriteCommon>();
    spriteCommon_->Initialize(dxCommon_);

    bgSprite_ = std::make_unique<Sprite>();
    bgSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    bgSprite_->SetPosition({ 0.0f, 0.0f });
    bgSprite_->SetSize({ GameConstants::kScreenWidth, GameConstants::kScreenHeight });
    bgSprite_->SetColor({ 0.05f, 0.05f, 0.08f, 1.0f });

    nodeSprite_ = std::make_unique<Sprite>();
    nodeSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    nodeSprite_->SetSize({ kNodeW, kNodeH });

    fontRenderer_.Initialize(spriteCommon_.get());

    floors_ = {
        { RunData::NodeType::Combat, RunData::NodeType::Combat, RunData::NodeType::Shop  },
        { RunData::NodeType::Combat, RunData::NodeType::Rest,   RunData::NodeType::Combat },
        { RunData::NodeType::Elite,  RunData::NodeType::Shop                              },
        { RunData::NodeType::Boss                                                          },
    };

    selectedCol_   = 0;
    waitingResult_ = false;

    auto* rd = RunData::GetInstance();
    if (rd->GetFloor() >= static_cast<int>(floors_.size())) {
        SceneManager::GetInstance()->ChangeScene("CLEAR");
    }
}

void MapScene::Finalize()
{
}

void MapScene::Update()
{
    auto* rd = RunData::GetInstance();

    if (waitingResult_) {
        waitTimer_ -= GameConstants::kFrameDeltaTime;
        if (waitTimer_ <= 0.0f) {
            waitingResult_ = false;
            rd->AdvanceFloor();
            if (rd->GetFloor() >= static_cast<int>(floors_.size())) {
                SceneManager::GetInstance()->ChangeScene("CLEAR");
            }
        }
        return;
    }

    int curFloor = rd->GetFloor();
    if (curFloor >= static_cast<int>(floors_.size())) { return; }

    const auto& row = floors_[curFloor];
    int maxCol = static_cast<int>(row.size()) - 1;
    selectedCol_ = std::clamp(selectedCol_, 0, maxCol);

    if (input_->TriggerKey(DIK_A) || input_->TriggerKey(DIK_LEFT)) {
        selectedCol_ = (std::max)(0, selectedCol_ - 1);
    }
    if (input_->TriggerKey(DIK_D) || input_->TriggerKey(DIK_RIGHT)) {
        selectedCol_ = (std::min)(maxCol, selectedCol_ + 1);
    }

    if (input_->TriggerKey(DIK_T)) {
        SceneManager::GetInstance()->ChangeScene("TRAINING");
        return;
    }

    if (input_->TriggerKey(DIK_RETURN) || input_->TriggerKey(DIK_SPACE)) {
        RunData::NodeType chosen = row[selectedCol_];
        rd->SetCurrentNode(chosen);

        switch (chosen) {
        case RunData::NodeType::Combat:
        case RunData::NodeType::Elite:
        case RunData::NodeType::Boss:
            SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            break;
        case RunData::NodeType::Shop:
            SceneManager::GetInstance()->ChangeScene("SHOP");
            break;
        case RunData::NodeType::Rest:
            restHealAmount_ = 10;
            rd->Heal(restHealAmount_);
            waitingResult_ = true;
            waitTimer_     = 1.5f;
            break;
        }
    }
}

void MapScene::Draw()
{
    auto* rd = RunData::GetInstance();

    spriteCommon_->CommonDrawSettings();
    bgSprite_->Update();
    bgSprite_->Draw();

    fontRenderer_.Reset();

    DrawHeader(rd);
    RunData::NodeType hoveredNode = DrawFloorNodes(rd->GetFloor());
    DrawSelectedNodeInfo(rd->GetFloor(), hoveredNode);
    DrawRestMessage();
    DrawSkillList(rd);

    // ── 操作説明 ──
    fontRenderer_.DrawStringW(L"A/Dキー:選択  Enterキー:決定  Tキー:トレーニング (Backspaceで戻る)",
        20.0f, 690.0f, 1.0f, { 0.45f, 0.45f, 0.45f, 1.0f });

    fontRenderer_.Draw();
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
        const float* colXs = (nCols == 3) ? kColX3 : (nCols == 2) ? kColX2 : kColX1;
        float rowY = kFloorY[f];

        bool isActive    = (f == curFloor) && !waitingResult_;
        bool isCompleted = (f < curFloor);

        // フロアラベル
        {
            wchar_t lbl[16];
            if (f == 3) { swprintf_s(lbl, L"BOSS"); }
            else        { swprintf_s(lbl, L"フロア %d", f + 1); }
            Vector4 lblCol = isActive    ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f }
                           : isCompleted ? Vector4{ 0.35f, 0.35f, 0.35f, 1.0f }
                           :               Vector4{ 0.5f, 0.5f, 0.5f, 1.0f };
            fontRenderer_.DrawStringW(lbl, 30.0f, rowY + 8.0f, 1.2f, lblCol);
        }

        for (int c = 0; c < nCols; ++c) {
            float nx = colXs[c] - kNodeW * 0.5f;
            float ny = rowY;
            bool selected = isActive && (c == selectedCol_);

            if (selected) { hoveredNode = row[c]; }

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

            Vector4 textCol = isCompleted ? Vector4{ 0.45f, 0.45f, 0.45f, 1.0f }
                            : selected    ? Vector4{ 1.0f, 1.0f, 0.15f, 1.0f }
                            :               Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
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
    // ── 選択中ノードの説明（右側）──
    if (waitingResult_ || curFloor >= static_cast<int>(floors_.size())) { return; }

    const wchar_t* desc = NodeDesc(hoveredNode);
    fontRenderer_.DrawStringW(NodeLabelW(hoveredNode), 900.0f, 370.0f, 1.8f, { 1.0f, 0.85f, 0.2f, 1.0f });
    // 説明を2行に折り返して表示
    std::wstring descStr(desc);
    size_t br = descStr.find(L'　');  // 全角スペースで折り返しポイントを探す
    if (br != std::wstring::npos && br > 10) {
        fontRenderer_.DrawStringW(descStr.substr(0, br).c_str(),   900.0f, 410.0f, 1.1f, { 0.8f, 0.8f, 0.8f, 1.0f });
        fontRenderer_.DrawStringW(descStr.substr(br + 1).c_str(),  900.0f, 435.0f, 1.1f, { 0.8f, 0.8f, 0.8f, 1.0f });
    } else {
        fontRenderer_.DrawStringW(desc, 900.0f, 410.0f, 1.1f, { 0.8f, 0.8f, 0.8f, 1.0f });
    }
}

void MapScene::DrawRestMessage()
{
    // ── REST待機メッセージ ──
    if (!waitingResult_) { return; }

    wchar_t buf[32];
    swprintf_s(buf, L"HP が %d 回復した!", restHealAmount_);
    fontRenderer_.DrawStringW(buf, 440.0f, 330.0f, 2.2f, { 0.3f, 0.8f, 1.0f, 1.0f });
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
