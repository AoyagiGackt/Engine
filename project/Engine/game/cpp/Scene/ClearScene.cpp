/**
 * @file ClearScene.cpp
 * @brief エンディング画面の演出・スコア確定・タイトルへの遷移（ClearScene）の実装
 */
#include "ClearScene.h"
#include "AssetPack.h"
#include "GameConstants.h"
#include "ImGuiManager.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "SaveData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "StageEditor.h"
#include "StyleMeter.h"
#include "UILayout.h"
#include <algorithm>
#include <cmath>
#include <string>
using namespace engine::graphics;

// 既定の配置（F2のエディタで動かした値は Resources/Config/UI/clear.json に保存される）
namespace engine::game {
namespace {
constexpr const char* kLayoutName = "clear";
constexpr const char* kConfigPath = "Resources/Config/clear_scene.json";
constexpr float kHalf = 0.5f;
constexpr float kScreenCenterX = gameConstants::kScreenWidth * kHalf;

// 見出し
constexpr Vector2 kTitlePosition = { kScreenCenterX, 42.0f }; // 文字の中央上
constexpr float kTitleScale = 4.5f;
constexpr Vector4 kTitleColor = { 1.0f, 0.95f, 0.75f, 1.0f };
constexpr Vector2 kTitleShadowOffset = { 4.0f, 4.0f };
constexpr Vector4 kTitleShadowColor = { 0.0f, 0.0f, 0.0f, 0.75f };
constexpr Vector2 kSubtitlePosition = { kScreenCenterX, 136.0f };
constexpr float kSubtitleScale = 1.4f;
constexpr Vector4 kSubtitleColor = { 0.8f, 0.85f, 0.95f, 1.0f };
constexpr float kTitleLineY = 126.0f;
constexpr float kTitleLineWidth = 860.0f;
constexpr float kTitleLineThickness = 3.0f;
constexpr Vector4 kTitleLineColor = { 1.0f, 0.8f, 0.35f, 1.0f };

// 左の戦績の板
constexpr Vector2 kStagePanelPosition = { 70.0f, 180.0f };
constexpr Vector2 kStagePanelSize = { 660.0f, 450.0f };
constexpr Vector2 kRowsPosition = { 100.0f, 240.0f }; // 最初の行の左上
constexpr float kRowSpacing = 42.0f;
constexpr float kRowScale = 1.6f;
constexpr float kHeaderScale = 1.1f;
constexpr float kHeaderOffsetY = -36.0f; // 最初の行から見た列名の高さ
constexpr float kRankColumnX = 190.0f; // 行の左端から見た各列の位置
constexpr float kTimeColumnX = 310.0f;
constexpr float kScoreColumnX = 450.0f;
constexpr int kMaxStageRows = 6; // 板に収まる行数
constexpr Vector4 kRowColor = { 0.92f, 0.94f, 1.0f, 1.0f };
constexpr Vector4 kHeaderColor = { 0.6f, 0.65f, 0.75f, 1.0f };

// 合計スコア
constexpr Vector2 kTotalPosition = { 100.0f, 500.0f };
constexpr float kTotalLabelScale = 1.3f;
constexpr float kTotalValueScale = 3.2f;
constexpr float kTotalValueOffsetY = 28.0f;
constexpr float kTotalDetailOffsetX = 340.0f; // 合計時間と最高ランクを置く横位置
constexpr float kTotalDetailSpacing = 34.0f;
constexpr float kTotalDetailScale = 1.3f;
constexpr Vector4 kTotalColor = { 1.0f, 0.9f, 0.45f, 1.0f };
constexpr Vector4 kNewRecordColor = { 1.0f, 0.45f, 0.3f, 1.0f };
constexpr float kNewRecordOffsetY = 68.0f; // 合計スコアのラベルから見た高さ（合計時間と最高ランクの下）
constexpr float kDividerOffsetY = -14.0f;
constexpr float kDividerWidth = 600.0f;
constexpr float kDividerThickness = 2.0f;

// 右のランキングの板
constexpr Vector2 kRankingPanelPosition = { 770.0f, 180.0f };
constexpr Vector2 kRankingPanelSize = { 440.0f, 450.0f };
constexpr Vector2 kRankingPosition = { 800.0f, 240.0f };
constexpr float kRankingSpacing = 38.0f;
constexpr float kRankingScale = 1.6f;
constexpr int kRankingRows = 5;
constexpr Vector4 kRankingColor = { 0.85f, 0.88f, 0.95f, 1.0f };
constexpr Vector4 kRankingHighlightColor = { 1.0f, 0.9f, 0.45f, 1.0f };
constexpr Vector2 kRecordsPosition = { 800.0f, 480.0f };
constexpr float kRecordsScale = 1.2f;
constexpr float kRecordsSpacing = 34.0f;
constexpr Vector4 kRecordsColor = { 0.65f, 0.7f, 0.8f, 1.0f };

constexpr Vector4 kPanelColor = { 0.0f, 0.0f, 0.0f, 0.5f };

// タイトルへ戻る案内
constexpr float kPromptY = 655.0f;
constexpr float kPromptScale = 1.4f;
constexpr Vector4 kPromptColor = { 1.0f, 1.0f, 1.0f, 1.0f };

constexpr int kSecondsPerMinute = 60;
constexpr int kTwoDigitMin = 10; // これ未満の秒は0を補って2桁にする

/** @brief エンディングの見せ方の調整値 */
struct ClearSceneTuning {
    float titleDelay = 0.3f; ///< 見出しが出るまでの間（秒）
    float titleSeconds = 0.9f; ///< 見出しが現れきるまでの時間（秒）
    float rowInterval = 0.35f; ///< 戦績の行が順に出る間隔（秒）
    float rowSlideSeconds = 0.35f; ///< 戦績の行が滑り込む時間（秒）
    float rowSlideDistance = 60.0f; ///< 戦績の行が滑り込む距離（ピクセル）
    float totalDelay = 0.3f; ///< 最後の行から合計スコアまでの間（秒）
    float countUpSeconds = 1.2f; ///< 合計スコアを数え上げる時間（秒）
    float rankingDelay = 0.3f; ///< 数え上げの後、ランキングまでの間（秒）
    float rankingInterval = 0.12f; ///< ランキングの行が順に出る間隔（秒）
    float promptDelay = 0.4f; ///< ランキングの後、案内までの間（秒）
    float promptBlinkSpeed = 2.5f; ///< 案内の明滅の速さ（1秒あたりのラジアン）
    float promptBlinkDepth = 0.45f; ///< 案内の明滅で濃さが変わる割合
};

constexpr JsonField<ClearSceneTuning> kTuningFields[] = {
    { "title", "delay", &ClearSceneTuning::titleDelay },
    { "title", "seconds", &ClearSceneTuning::titleSeconds },
    { "rows", "interval", &ClearSceneTuning::rowInterval },
    { "rows", "slideSeconds", &ClearSceneTuning::rowSlideSeconds },
    { "rows", "slideDistance", &ClearSceneTuning::rowSlideDistance },
    { "total", "delay", &ClearSceneTuning::totalDelay },
    { "total", "countUpSeconds", &ClearSceneTuning::countUpSeconds },
    { "ranking", "delay", &ClearSceneTuning::rankingDelay },
    { "ranking", "interval", &ClearSceneTuning::rankingInterval },
    { "prompt", "delay", &ClearSceneTuning::promptDelay },
    { "prompt", "blinkSpeed", &ClearSceneTuning::promptBlinkSpeed },
    { "prompt", "blinkDepth", &ClearSceneTuning::promptBlinkDepth },
};

/** @brief clear_scene.json から調整値を読む */
ClearSceneTuning LoadTuning() {
    return ReadJsonFields(JsonHelper::Load(kConfigPath), kTuningFields);
}

/** @brief 読み込み済みの調整値の置き場（初回に読み、調整値エディタで保存した時に読み直す） */
ClearSceneTuning& MutableTune() {
    static ClearSceneTuning sTuning = LoadTuning();
    return sTuning;
}

/** @brief 読み込み済みの調整値 */
const ClearSceneTuning& Tune() {
    return MutableTune();
}

/** @brief start から seconds かけて 0 から 1 へ進み、終わり際をなめらかにした値を返す */
float RevealAmount(float time, float start, float seconds) {
    if (seconds <= 0.0f) {
        return time >= start ? 1.0f : 0.0f;
    }
    const float remaining = 1.0f - std::clamp((time - start) / seconds, 0.0f, 1.0f);
    return 1.0f - remaining * remaining * remaining;
}

/** @brief 色の不透明度に amount を掛けた色を返す */
Vector4 Faded(const Vector4& color, float amount) {
    return { color.x, color.y, color.z, color.w * amount };
}

/** @brief 秒数を 分:秒 の文字列にする */
std::wstring FormatTime(int seconds) {
    const int remainder = seconds % kSecondsPerMinute;
    return std::to_wstring(seconds / kSecondsPerMinute) + L":"
        + (remainder < kTwoDigitMin ? L"0" : L"") + std::to_wstring(remainder);
}

/** @brief 文字列の中央が centerX に来るように積む */
void QueueCentered(FontRenderer& font, const std::wstring& text, float centerX, float y, float scale, const Vector4& color) {
    const float width = FontRenderer::MeasureStringW(text, scale).x;
    font.DrawStringW(text, centerX - width * kHalf, y, scale, color);
}
} // namespace

// 初期化

/** @brief 背景・文字を初期化し、このランの戦績をランキングと通算記録へ反映する */
void ClearScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) {
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);
    fontRenderer_.Initialize(spriteCommon_.get());
    backdrop_.Initialize(spriteCommon_.get());
    // 左右に自前の板を敷くので、結果画面用の中央の板は出さない
    backdrop_.SetPanelVisible(false);

    // ローグライトのランを完走した場合のみスコアと戦績を扱う（サンドボックステスト経由は対象外）
    auto* rd = RunData::GetInstance();
    const bool runActive = rd->IsRunActive();
    runScore_ = runActive ? rd->GetScore() : 0;
    if (runActive) {
        stageRecords_ = rd->GetStageRecordSnapshot();
    }

    // 背景の色と透かしは、このランで一番良かったランクから決める
    int bestPermille = -1;
    for (const RunData::StageRecord& record : stageRecords_) {
        totalSeconds_ += record.seconds;
        bestPermille = (std::max)(bestPermille, record.stylePermille);
    }
    if (bestPermille >= 0) {
        bestRank_ = RunData::CalcRank(static_cast<float>(bestPermille) / RunData::kStylePermilleScale);
    }
    backdrop_.Start(bestRank_.empty() ? ResultBackdrop::NeutralColor() : StyleMeter::ColorForRank(bestRank_));

    ranking_ = ScoreManager::GetInstance()->GetRankingSnapshot();

    // cook中（素材をpakへ記録するために画面を開いているだけ）はスコアや通算記録を書き換えない
    if (AssetPack::GetInstance()->IsCooking()) {
        return;
    }

    audio_->PlaySceneBGM(bgmKey::kGameClear, false);
    if (runActive) {
        isNewRecord_ = runScore_ > 0 && (ranking_.empty() || runScore_ > ranking_.front());
        ScoreManager::GetInstance()->SubmitAndSave(runScore_);
        ranking_ = ScoreManager::GetInstance()->GetRankingSnapshot();
        SaveDataManager::GetInstance()->RecordRunResult(true, rd->GetFloor());
    }
}

// 終了

/** @brief 終了処理（所有する資源は unique_ptr が解放するので何もしない） */
void ClearScene::Finalize() {
}

// 更新

/** @brief 演出を進める。決定キーで演出を飛ばし、すべて出ていればタイトルへ戻る */
void ClearScene::Update() {
    time_ += gameConstants::kFrameDeltaTime;
    backdrop_.Update(gameConstants::kFrameDeltaTime);

    if (input_->TriggerMenuConfirm()) {
        if (time_ < RevealEndTime()) {
            time_ = RevealEndTime();
        } else {
            SceneFlow::GetInstance()->Transition("CLEAR", "title", "TITLE");
        }
    }

    DrawScoreUI();
}

// 演出の時刻

/** @brief ステージの戦績の行が出始める時刻（秒） */
float ClearScene::RowsStartTime() const {
    return Tune().titleDelay + Tune().titleSeconds;
}

/** @brief 合計スコアが出始める時刻（秒） */
float ClearScene::TotalStartTime() const {
    return RowsStartTime() + static_cast<float>(stageRecords_.size()) * Tune().rowInterval + Tune().totalDelay;
}

/** @brief ランキングが出始める時刻（秒） */
float ClearScene::RankingStartTime() const {
    return TotalStartTime() + Tune().countUpSeconds + Tune().rankingDelay;
}

/** @brief すべてを出し終える時刻（秒） */
float ClearScene::RevealEndTime() const {
    return RankingStartTime() + static_cast<float>(kRankingRows) * Tune().rankingInterval + Tune().promptDelay;
}

// 描画

/** @brief 背景の上に、演出の進み具合に合わせて文字と板を描く */
void ClearScene::Draw() {
    UILayout& layout = UILayout::Get(kLayoutName);
    // エディタ表示中はUpdate()が止まるので、配置を確かめられるよう全部出した状態で描く
    const float time = GetStageEditor().IsVisible() ? RevealEndTime() : time_;

    spriteCommon_->CommonDrawSettings();
    backdrop_.Draw();

    fontRenderer_.Reset();
    if (!bestRank_.empty()) {
        backdrop_.QueueWatermark(fontRenderer_, bestRank_);
    }
    QueueTitle(layout, time);
    QueueStageRows(layout, time);
    QueueTotal(layout, time);
    QueueRanking(layout, time);
    QueuePrompt(layout, time);
    fontRenderer_.Draw();
}

/** @brief 全ステージクリアの見出しと、広がる線を積む */
void ClearScene::QueueTitle(UILayout& layout, float time) {
    const ClearSceneTuning& tune = Tune();
    const float amount = RevealAmount(time, tune.titleDelay, tune.titleSeconds);

    // 線が中央から左右へ伸び、その上下に文字が浮かび上がる
    const float lineY = layout.Float("title.line_y", kTitleLineY);
    const float lineWidth = layout.Float("title.line_width", kTitleLineWidth) * amount;
    fontRenderer_.DrawPanel(kScreenCenterX - lineWidth * kHalf, lineY, lineWidth, kTitleLineThickness,
        Faded(layout.Color("title.line_color", kTitleLineColor), amount));

    const Vector2 titlePosition = layout.Pos("title.pos", kTitlePosition);
    const float titleScale = layout.Float("title.scale", kTitleScale);
    const std::wstring title = L"全ステージクリア";
    QueueCentered(fontRenderer_, title, titlePosition.x + kTitleShadowOffset.x, titlePosition.y + kTitleShadowOffset.y,
        titleScale, Faded(kTitleShadowColor, amount));
    QueueCentered(fontRenderer_, title, titlePosition.x, titlePosition.y, titleScale,
        Faded(layout.Color("title.color", kTitleColor), amount));

    const Vector2 subtitlePosition = layout.Pos("subtitle.pos", kSubtitlePosition);
    QueueCentered(fontRenderer_, L"ALL STAGES CLEAR", subtitlePosition.x, subtitlePosition.y,
        layout.Float("subtitle.scale", kSubtitleScale), Faded(layout.Color("subtitle.color", kSubtitleColor), amount));
}

/** @brief ステージごとの戦績を1行ずつ積む */
void ClearScene::QueueStageRows(UILayout& layout, float time) {
    const ClearSceneTuning& tune = Tune();
    const float rowsStart = RowsStartTime();
    const Vector2 panelPosition = layout.Pos("stage_panel.pos", kStagePanelPosition);
    const Vector2 panelSize = layout.Vec2("stage_panel.size", kStagePanelSize);
    const Vector2 rows = layout.Pos("rows.pos", kRowsPosition);
    const float spacing = layout.Float("rows.spacing", kRowSpacing);
    const float scale = layout.Float("rows.scale", kRowScale);
    const Vector4 rowColor = layout.Color("rows.color", kRowColor);

    const float panelAmount = RevealAmount(time, rowsStart - tune.rowSlideSeconds, tune.rowSlideSeconds);
    fontRenderer_.DrawPanel(panelPosition.x, panelPosition.y, panelSize.x, panelSize.y, Faded(kPanelColor, panelAmount));

    // ランを経由していなければ戦績が無いので、その旨だけ出す
    if (stageRecords_.empty()) {
        fontRenderer_.DrawStringW(L"このプレイの戦績はありません", rows.x, rows.y, scale, Faded(rowColor, panelAmount));
        return;
    }

    // 列の見出し
    const Vector4 headerColor = Faded(kHeaderColor, panelAmount);
    const float headerY = rows.y + kHeaderOffsetY;
    fontRenderer_.DrawStringW(L"STAGE", rows.x, headerY, kHeaderScale, headerColor);
    fontRenderer_.DrawStringW(L"RANK", rows.x + kRankColumnX, headerY, kHeaderScale, headerColor);
    fontRenderer_.DrawStringW(L"TIME", rows.x + kTimeColumnX, headerY, kHeaderScale, headerColor);
    fontRenderer_.DrawStringW(L"SCORE", rows.x + kScoreColumnX, headerY, kHeaderScale, headerColor);

    const int rowCount = (std::min)(static_cast<int>(stageRecords_.size()), kMaxStageRows);
    for (int i = 0; i < rowCount; ++i) {
        const RunData::StageRecord& record = stageRecords_[static_cast<size_t>(i)];
        const float amount = RevealAmount(time, rowsStart + static_cast<float>(i) * tune.rowInterval, tune.rowSlideSeconds);
        if (amount <= 0.0f) {
            continue;
        }
        // 左から滑り込ませる
        const float x = rows.x - tune.rowSlideDistance * (1.0f - amount);
        const float y = rows.y + spacing * static_cast<float>(i);
        const Vector4 color = Faded(rowColor, amount);
        const std::string rank = RunData::CalcRank(static_cast<float>(record.stylePermille) / RunData::kStylePermilleScale);
        fontRenderer_.DrawStringW(L"STAGE " + std::to_wstring(record.floor + 1), x, y, scale, color);
        fontRenderer_.DrawString(rank, x + kRankColumnX, y, scale, Faded(StyleMeter::ColorForRank(rank), amount));
        fontRenderer_.DrawStringW(FormatTime(record.seconds), x + kTimeColumnX, y, scale, color);
        fontRenderer_.DrawStringW(L"+" + std::to_wstring(record.score), x + kScoreColumnX, y, scale, color);
    }
}

/** @brief 数え上がる合計スコアと、合計時間・最高ランクを積む */
void ClearScene::QueueTotal(UILayout& layout, float time) {
    const ClearSceneTuning& tune = Tune();
    const float start = TotalStartTime();
    const Vector2 position = layout.Pos("total.pos", kTotalPosition);
    if (stageRecords_.empty()) {
        return;
    }
    const float amount = RevealAmount(time, start, tune.rowSlideSeconds);
    const Vector4 totalColor = layout.Color("total.color", kTotalColor);

    fontRenderer_.DrawPanel(position.x, position.y + kDividerOffsetY, kDividerWidth, kDividerThickness, Faded(kHeaderColor, amount));
    fontRenderer_.DrawStringW(L"TOTAL SCORE", position.x, position.y, kTotalLabelScale, Faded(kHeaderColor, amount));

    const float counted = RevealAmount(time, start, tune.countUpSeconds);
    const int shownScore = static_cast<int>(std::lround(static_cast<float>(runScore_) * counted));
    fontRenderer_.DrawStringW(std::to_wstring(shownScore), position.x, position.y + kTotalValueOffsetY,
        layout.Float("total.scale", kTotalValueScale), Faded(totalColor, amount));

    // 合計時間と最高ランクは数え上げと一緒に出す
    const float detailX = position.x + kTotalDetailOffsetX;
    const Vector4 detailColor = Faded(kRowColor, amount);
    fontRenderer_.DrawStringW(L"TOTAL TIME  " + FormatTime(totalSeconds_), detailX, position.y, kTotalDetailScale, detailColor);
    fontRenderer_.DrawStringW(L"BEST RANK", detailX, position.y + kTotalDetailSpacing, kTotalDetailScale, detailColor);
    const float rankOffsetX = FontRenderer::MeasureStringW(L"BEST RANK  ", kTotalDetailScale).x;
    fontRenderer_.DrawString(bestRank_, detailX + rankOffsetX, position.y + kTotalDetailSpacing, kTotalDetailScale,
        Faded(StyleMeter::ColorForRank(bestRank_), amount));

    // 数え終わった所で1位の更新を知らせる
    if (isNewRecord_) {
        const float recordAmount = RevealAmount(time, start + tune.countUpSeconds, tune.rowSlideSeconds);
        fontRenderer_.DrawStringW(L"NEW RECORD!", detailX, position.y + kNewRecordOffsetY, kTotalDetailScale,
            Faded(kNewRecordColor, recordAmount));
    }
}

/** @brief ランキングと通算記録を積む */
void ClearScene::QueueRanking(UILayout& layout, float time) {
    const ClearSceneTuning& tune = Tune();
    const float start = RankingStartTime();
    const Vector2 panelPosition = layout.Pos("ranking_panel.pos", kRankingPanelPosition);
    const Vector2 panelSize = layout.Vec2("ranking_panel.size", kRankingPanelSize);
    const Vector2 position = layout.Pos("ranking.pos", kRankingPosition);
    const float spacing = layout.Float("ranking.spacing", kRankingSpacing);
    const float scale = layout.Float("ranking.scale", kRankingScale);
    const Vector4 rankingColor = layout.Color("ranking.color", kRankingColor);

    const float panelAmount = RevealAmount(time, start, tune.rowSlideSeconds);
    fontRenderer_.DrawPanel(panelPosition.x, panelPosition.y, panelSize.x, panelSize.y, Faded(kPanelColor, panelAmount));
    fontRenderer_.DrawStringW(L"RANKING", position.x, position.y + kHeaderOffsetY, kHeaderScale, Faded(kHeaderColor, panelAmount));

    // 上位から順に出し、今回のスコアの行だけ色を変える
    bool highlighted = false;
    const int rowCount = (std::min)(static_cast<int>(ranking_.size()), kRankingRows);
    for (int i = 0; i < rowCount; ++i) {
        const int score = ranking_[static_cast<size_t>(i)];
        if (score <= 0) {
            break;
        }
        const bool isThisRun = !highlighted && !stageRecords_.empty() && score == runScore_;
        highlighted = highlighted || isThisRun;
        const float amount = RevealAmount(time, start + static_cast<float>(i) * tune.rankingInterval, tune.rowSlideSeconds);
        const Vector4 color = isThisRun ? layout.Color("ranking.highlight_color", kRankingHighlightColor) : rankingColor;
        fontRenderer_.DrawStringW(std::to_wstring(i + 1) + L".  " + std::to_wstring(score),
            position.x, position.y + spacing * static_cast<float>(i), scale, Faded(color, amount));
    }

    // 通算記録
    const SaveRecords records = SaveDataManager::GetInstance()->GetRecordSnapshot();
    const std::wstring lines[] = {
        L"プレイ回数  " + std::to_wstring(records.totalRuns),
        L"クリア回数  " + std::to_wstring(records.totalClears),
    };
    const Vector2 recordsPosition = layout.Pos("records.pos", kRecordsPosition);
    const float recordsScale = layout.Float("records.scale", kRecordsScale);
    const Vector4 recordsColor = Faded(layout.Color("records.color", kRecordsColor), panelAmount);
    float y = recordsPosition.y;
    for (const std::wstring& line : lines) {
        fontRenderer_.DrawStringW(line, recordsPosition.x, y, recordsScale, recordsColor);
        y += kRecordsSpacing;
    }
}

/** @brief タイトルへ戻る案内を積む */
void ClearScene::QueuePrompt(UILayout& layout, float time) {
    const ClearSceneTuning& tune = Tune();
    const float y = layout.Float("prompt.y", kPromptY);
    if (time < RevealEndTime()) {
        return;
    }
    // ゆっくり明滅させて、押せる状態だと分かるようにする
    const float wave = (1.0f + std::sin(time_ * tune.promptBlinkSpeed)) * kHalf;
    const float alpha = 1.0f - tune.promptBlinkDepth * wave;
    QueueCentered(fontRenderer_, L"決定でタイトルへ", kScreenCenterX, y, layout.Float("prompt.scale", kPromptScale),
        Faded(layout.Color("prompt.color", kPromptColor), alpha));
}

/** @brief 調整値をファイルから読み直す */
void ClearScene::ReloadTuning() {
    MutableTune() = LoadTuning();
}

// デバッグ UI（ImGui）

/** @brief このシーンのスコアを ImGui のウィンドウに表示する（画面を隠さないよう最初は畳んでおく） */
void ClearScene::DrawScoreUI() {
#ifdef USE_IMGUI
    if (!imguiManager_) {
        return;
    }

    constexpr ImVec2 kDiagnosticsWindowSize = { 300.0f, 120.0f };
    constexpr ImVec2 kDiagnosticsWindowPos = { 10.0f, 10.0f };
    ImGui::SetNextWindowSize(kDiagnosticsWindowSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(kDiagnosticsWindowPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin("Diagnostics");
    ImGui::Text("Score : %d", runScore_);
    ImGui::Separator();
    if (ImGui::Button("Reset All Scores")) {
        ScoreManager::GetInstance()->ResetAllScores();
        ranking_ = ScoreManager::GetInstance()->GetRankingSnapshot();
    }
    ImGui::End();
#endif
}

} // namespace engine::game
