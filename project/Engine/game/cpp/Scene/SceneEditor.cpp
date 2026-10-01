/**
 * @file SceneEditor.cpp
 * @brief 実行中のライブパラメータ調整パネル（SceneEditor、Stateパターンによる選択種別ごとの表示切替）の実装
 */
#include "SceneEditor.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "ParticleManager.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include <string>
#ifdef USE_IMGUI
#include <commdlg.h>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")
#endif
using namespace engine;
using namespace engine::graphics;

namespace engine::game {

// 内部ヘルパー

namespace {
// ImGuiのドラッグ操作1pxあたりの変化量
constexpr float kDragStepPosition = 0.1f;
constexpr float kDragStepFinePosition = 0.05f;
constexpr float kDragStepRotation = 0.01f;
constexpr float kDragStepScale = 0.01f;
constexpr float kDragStepRadius = 0.05f;
constexpr float kDragStepPixel = 1.0f;
// 編集値の下限・上限
constexpr float kMinEditableSize = 0.01f;
constexpr float kMinEditableHumanScale = 0.001f;
constexpr float kMaxObjectScale = 20.0f;
constexpr float kMaxRadius = 50.0f;
constexpr float kMaxHumanScale = 100.0f;
constexpr float kMaxAnimSpeed = 5.0f;
constexpr float kMaxParticleScale = 10.0f;
constexpr int kMaxParticleCount = 1024;
constexpr float kMinSpritePixels = 1.0f;
constexpr float kMaxSpritePixels = 4096.0f;
constexpr float kRadiusGap = 0.01f; // 内径と外径が重ならないように空ける最小差
#ifdef USE_IMGUI
// 見出し色
constexpr ImVec4 kSavedTextColor = { 0.2f, 1.0f, 0.4f, 1.0f };
constexpr ImVec4 kRingHeaderColor = { 0.4f, 1.0f, 0.8f, 1.0f };
constexpr ImVec4 kCylinderHeaderColor = { 0.8f, 0.6f, 1.0f, 1.0f };
constexpr ImVec4 kSkydomeHeaderColor = { 0.5f, 0.8f, 1.0f, 1.0f };
constexpr ImVec4 kHumanHeaderColor = { 0.4f, 0.8f, 1.0f, 1.0f };
#endif
constexpr float kSavedMessageSeconds = 1.5f;
constexpr Vector2 kNewUIElementSize = { 100.0f, 100.0f };
// 白パーティクルの再放出
constexpr float kWhiteParticleSpread = 20.0f;
constexpr float kWhiteParticleSpeedMin = 2.0f;
constexpr float kWhiteParticleSpeedMax = 5.0f;
}

#ifdef USE_IMGUI
// Windows のファイル選択ダイアログを開き、ユーザーが選んだファイルのパスを返す
// filter  = 表示するファイルの種類の説明（例: "PNG Files\0*.png\0..."）
// initDir = ダイアログを開いたときに最初に表示するフォルダ
static std::string OpenFileDialog(const char* filter, const char* initDir)
{
    char path[MAX_PATH] = { };
    OPENFILENAMEA ofn = { };
    ofn.lStructSize = sizeof(ofn); // 構造体のサイズ（Windows API の決まり）
    ofn.lpstrFilter = filter; // 表示するファイル種別フィルタ
    ofn.lpstrFile = path; // 選択されたパスの書き込み先バッファ
    ofn.nMaxFile = MAX_PATH; // バッファの最大サイズ
    ofn.lpstrInitialDir = initDir; // 最初に表示するフォルダ
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST; // 実在するファイルのみ選択可能
    if (GetOpenFileNameA(&ofn)) {
        return path;
    } // OK を押したらパスを返す
    return { }; // キャンセルしたら空文字列を返す
}
#endif

// State 遷移

// Hierarchy でオブジェクトが選ばれたとき、対応する State クラスに切り替える
// こうすることで Inspector パネルに表示する内容がオブジェクト種別に応じて変わる
// ══════════════════════════════════════════════════════
// 状態管理とパネル描画
// ══════════════════════════════════════════════════════

void SceneEditor::ChangeState(Selection sel)
{
    selection_ = sel;
    selectionIndex_ = -1; // UIElement のインデックスをリセット
    switch (sel) {
    case Selection::Camera:
        currentState_ = std::make_unique<CameraState>();
        break;
    case Selection::Ring:
        currentState_ = std::make_unique<RingState>();
        break;
    case Selection::Cylinder:
        currentState_ = std::make_unique<CylinderState>();
        break;
    case Selection::Skydome:
        currentState_ = std::make_unique<SkydomeState>();
        break;
    case Selection::Human:
        currentState_ = std::make_unique<HumanState>();
        break;
    case Selection::WhiteParticles:
        currentState_ = std::make_unique<ParticlesState>();
        break;
    case Selection::UIElement:
        currentState_ = std::make_unique<UIElementState>();
        break;
    default:
        currentState_ = std::make_unique<NoneState>();
        break;
    }
}

// メインエントリ

// 毎フレーム呼ばれるUSE_IMGUI ビルドのときだけ各パネルを描画する
// リリースビルドでは引数を無視して何もしない
void SceneEditor::Update(const EditContext& ctx, engine::Input* input)
{
#ifdef USE_IMGUI
    if (input && input->TriggerKey(DIK_F3)) {
        visible_ = !visible_;
    }
    if (!visible_) {
        return;
    }
    RenderHierarchy(ctx); // 左 オブジェクト一覧
    RenderInspector(ctx); // 右 選択中オブジェクトのプロパティ
    RenderSceneControls(ctx); // 左下 スコア・ゲーム時刻・シーン操作
    RenderCameraControl(ctx); // 上中央 カメラ位置の簡易操作
#else
    (void)ctx;
    (void)input;
#endif
}

// Hierarchy パネル（画面左）

// シーン内のオブジェクト一覧を表示する
// クリックすると Inspector パネルの内容が切り替わる
void SceneEditor::RenderHierarchy(const EditContext& ctx)
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kHierarchyX, kHierarchyY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kHierarchyW, kHierarchyH), ImGuiCond_Once);
    ImGui::Begin("Hierarchy");

    // Selectable を選んだら ChangeState を呼んで Inspector を切り替える
    auto selectable = [&](const char* label, Selection type) {
        if (ImGui::Selectable(label, selection_ == type)) {
            ChangeState(type);
        }
    };

    // Ring/Cylinder/Human/White Particles はこのシーンのEditContextに配線されておらず
    // 選んでも無効表示にしかならないため、実際に編集できる項目だけを並べる
    selectable("Camera", Selection::Camera);
    selectable("Skydome", Selection::Skydome);

    // UI Elements は可変長リストなのでツリーノードで折りたたみ表示する
    char uiHeader[48];
    snprintf(uiHeader, sizeof(uiHeader), "UI Elements (%d)", (int)uiElements_.size());
    bool uiOpen = ImGui::TreeNodeEx(uiHeader);
    ImGui::SameLine();
    // + ボタンを押すと新しい UI スプライトを追加する
    if (ImGui::SmallButton("+##addUI")) {
        UIEntry entry;
        entry.name = "UI Element " + std::to_string(uiElements_.size() + 1);
        entry.texPath = "";
        entry.sprite = std::make_unique<Sprite>();
        entry.sprite->Initialize(ctx.spriteCommon, entry.texPath);
        entry.sprite->SetPosition({ GameConstants::kScreenCenterX, GameConstants::kScreenCenterY }); // 画面中央に配置
        entry.sprite->SetSize(kNewUIElementSize);
        uiElements_.push_back(std::move(entry));
    }
    if (uiOpen) {
        for (int i = 0; i < (int)uiElements_.size(); ++i) {
            bool sel = (selection_ == Selection::UIElement && selectionIndex_ == i);
            char label[80];
            snprintf(label, sizeof(label), "  %s", uiElements_[i].name.c_str());
            if (ImGui::Selectable(label, sel)) {
                // UIElement を選択したときはインデックスも一緒に保存する
                selection_ = Selection::UIElement;
                selectionIndex_ = i;
                currentState_ = std::make_unique<UIElementState>();
            }
        }
        ImGui::TreePop();
    }

    // Save ボタン 選択中オブジェクトのパラメータを JSON に保存する
    ImGui::Separator();
    if (savedTimer_ > 0.0f) {
        // 保存直後は "Saved!" と表示してフェードアウトする
        savedTimer_ -= GameConstants::kFrameDeltaTime;
        ImGui::TextColored(kSavedTextColor, "Saved!");
    } else {
        if (ImGui::Button("Save", ImVec2(-1, 0))) {
            if (selection_ == Selection::Camera) {
                SaveCameraParams(ctx);
                savedTimer_ = kSavedMessageSeconds;
            }
            if (selection_ == Selection::UIElement) {
                SaveUILayout();
                savedTimer_ = kSavedMessageSeconds;
            }
        }
    }

    ImGui::End();
#endif
}

// Inspector パネル（画面右）

// 現在選ばれているオブジェクトの詳細プロパティを表示する
// 描画の実体は currentState_（各 State クラスの RenderInspector）に委譲する
void SceneEditor::RenderInspector(const EditContext& ctx)
{
#ifdef USE_IMGUI
    // UIElement を選んでいるのにインデックスが範囲外なら選択を解除する
    if (selection_ == Selection::UIElement && (selectionIndex_ < 0 || selectionIndex_ >= (int)uiElements_.size())) {
        ChangeState(Selection::None);
    }

    ImGui::SetNextWindowPos(ImVec2(kInspectorX, kInspectorY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kInspectorW, kInspectorH), ImGuiCond_Once);
    ImGui::Begin("Inspector");

    if (currentState_) {
        currentState_->RenderInspector(ctx, *this);
    } else {
        // State が初期化されていない場合の安全処理
        currentState_ = std::make_unique<NoneState>();
        currentState_->RenderInspector(ctx, *this);
    }

    ImGui::End();
#endif
}

// Scene Controls パネル（画面左下）

// スコアの確認・ゲーム時刻の表示・シーン切り替えボタンをまとめたパネル
void SceneEditor::RenderSceneControls(const EditContext& ctx)
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kSceneCtrlX, kSceneCtrlY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kSceneCtrlW, kSceneCtrlH), ImGuiCond_Once);
    ImGui::Begin("Scene Controls");

    // スコアランキングの表示（折りたたみ）
    if (ImGui::CollapsingHeader("Score")) {
        ImGui::Text("Current : %d", ScoreManager::GetInstance()->GetCurrentScore());
        const auto& ranking = ScoreManager::GetInstance()->GetRanking();
        if (ranking.empty()) {
            ImGui::TextDisabled("  (no records)");
        }
        for (int i = 0; i < (int)ranking.size(); ++i) {
            ImGui::Text("  %2d. %d", i + 1, ranking[i]);
        }
        if (ImGui::Button("Reset All Scores")) {
            ScoreManager::GetInstance()->ResetAllScores();
        }
    }

    // ゲーム内時刻の表示（折りたたみ）
    if (ImGui::CollapsingHeader("Game Time")) {
        ImGui::Text("Time : %02d:%02d", ctx.gameHour, ctx.gameMinute);
    }

    // シーン切り替えボタン（折りたたみ）
    if (ImGui::CollapsingHeader("Actions")) {
        if (ImGui::Button("Game Clear")) {
            if (ctx.requestClear) {
                *ctx.requestClear = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Game Over")) {
            SceneFlow::GetInstance()->Transition("GAMEPLAY", "gameover", "GAMEOVER");
        }
    }

    ImGui::End();
#endif
}

// Camera Control パネル（画面上中央）

// カメラの目標位置・角度・スムージングフレーム数を素早く調整するための簡易パネル
// Inspector の Camera よりも手軽に操作できるよう別パネルとして用意している
void SceneEditor::RenderCameraControl(const EditContext& ctx)
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kCamCtrlX, kCamCtrlY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kCamCtrlW, kCamCtrlH), ImGuiCond_Once);
    ImGui::Begin("Camera Control");

    // ドラッグスライダーで目標座標・角度を変更する
    ImGui::DragFloat3("Pos", &ctx.cameraTargetPos->x, kDragStepPosition);
    ImGui::DragFloat3("Rot", &ctx.cameraTargetRot->x, kDragStepRotation);

    // スムージングのフレーム数を変えたら履歴をクリアする（古い平均が混入しないように）
    if (ImGui::SliderInt("Smooth Frames", ctx.cameraSmoothFrames, 1, 60)) {
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }

    ImGui::Spacing();

    // 原点にリセットするボタン
    if (ImGui::Button("Center")) {
        *ctx.cameraTargetPos = { 0.0f, 0.0f, 0.0f };
        *ctx.cameraTargetRot = { 0.0f, 0.0f, 0.0f };
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save##cam")) {
        SaveCameraParams(ctx);
    } // JSON に現在値を保存
    ImGui::SameLine();
    if (ImGui::Button("Load##cam")) {
        LoadCameraParams(ctx);
    } // JSON から前回値を復元

    ImGui::End();
#endif
}

// JSON 永続化

// カメラの位置・角度・スムージングフレーム数を JSON ファイルに書き出す
// ══════════════════════════════════════════════════════
// 設定の保存と読み込み
// ══════════════════════════════════════════════════════

void SceneEditor::SaveCameraParams(const EditContext& ctx)
{
    const Vector3& pos = *ctx.cameraTargetPos;
    const Vector3& rot = *ctx.cameraTargetRot;
    nlohmann::json j;
    j["camera_pos"] = { pos.x, pos.y, pos.z };
    j["camera_rot"] = { rot.x, rot.y, rot.z };
    j["camera_smooth_frames"] = *ctx.cameraSmoothFrames;
    JsonHelper::Save("Resources/editor_camera.json", j);
}

// カメラの位置・角度・スムージングフレーム数を JSON ファイルから読み込む
void SceneEditor::LoadCameraParams(const EditContext& ctx)
{
    auto j = JsonHelper::Load("Resources/editor_camera.json");
    if (j.empty()) {
        return;
    }

    Vector3& pos = *ctx.cameraTargetPos;
    Vector3& rot = *ctx.cameraTargetRot;

    if (j.contains("camera_pos") && j["camera_pos"].is_array()) {
        pos = { j["camera_pos"][0], j["camera_pos"][1], j["camera_pos"][2] };
    }
    if (j.contains("camera_rot") && j["camera_rot"].is_array()) {
        rot = { j["camera_rot"][0], j["camera_rot"][1], j["camera_rot"][2] };
    }
    *ctx.cameraSmoothFrames = j.value("camera_smooth_frames", *ctx.cameraSmoothFrames);

    ctx.camera->SetTranslate(pos);
    ctx.camera->SetRotate(rot);
    ctx.cameraPosHistory->clear();
    ctx.cameraRotHistory->clear();
}

// UI スプライトのレイアウト（位置・サイズ・色・テクスチャパスなど）を JSON に書き出す
void SceneEditor::SaveUILayout()
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : uiElements_) {
        Sprite* sp = e.sprite.get();
        Vector2 pos = sp->GetPosition();
        Vector2 sz = sp->GetSize();
        Vector4 col = sp->GetColor();
        float rot = sp->GetRotation();
        arr.push_back({ { "name", e.name },
            { "tex", e.texPath },
            { "pos", { pos.x, pos.y } },
            { "size", { sz.x, sz.y } },
            { "rot", rot },
            { "color", { col.x, col.y, col.z, col.w } } });
    }
    JsonHelper::Save("Resources/Config/editor_ui.json", arr);
}

// 起動時に1度だけ呼ばれ、カメラパラメータと UI レイアウトを一括で読み込む
void SceneEditor::LoadAll(const EditContext& ctx)
{
    LoadCameraParams(ctx);
    LoadUILayout(ctx);
}

// UI スプライトのレイアウトを JSON ファイルから復元する
void SceneEditor::LoadUILayout(const EditContext& ctx)
{
    auto j = JsonHelper::Load("Resources/Config/editor_ui.json");
    if (!j.is_array()) {
        return;
    }

    uiElements_.clear();
    ChangeState(Selection::None);

    for (const auto& item : j) {
        UIEntry entry;
        entry.name = item.value("name", std::string("UI Element"));
        entry.texPath = item.value("tex", std::string(""));

        entry.sprite = std::make_unique<Sprite>();
        entry.sprite->Initialize(ctx.spriteCommon, entry.texPath);

        if (item.contains("pos") && item["pos"].is_array()) {
            entry.sprite->SetPosition({ item["pos"][0], item["pos"][1] });
        }
        if (item.contains("size") && item["size"].is_array()) {
            entry.sprite->SetSize({ item["size"][0], item["size"][1] });
        }
        entry.sprite->SetRotation(item.value("rot", 0.0f));
        if (item.contains("color") && item["color"].is_array()) {
            const auto& c = item["color"];
            entry.sprite->SetColor({ c[0], c[1], c[2], c[3] });
        }

        uiElements_.push_back(std::move(entry));
    }
}

// IEditorState 実装（各オブジェクト種別ごとの Inspector 描画）

// 何も選んでいないときの Inspector 表示
// ══════════════════════════════════════════════════════
// 選択状態別の詳細設定
// ══════════════════════════════════════════════════════

void SceneEditor::NoneState::RenderInspector(const EditContext&, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextDisabled("(Nothing selected)");
    ImGui::TextDisabled("Hierarchy でオブジェクトを");
    ImGui::TextDisabled("クリックしてください");
#endif
}

// Camera を選んでいるときの Inspector 表示
void SceneEditor::CameraState::RenderInspector(const EditContext& ctx, SceneEditor& editor)
{
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "[Camera]");
    ImGui::Separator();
    ImGui::DragFloat3("Position", &ctx.cameraTargetPos->x, kDragStepPosition); // ドラッグで位置を変更
    ImGui::DragFloat3("Rotation", &ctx.cameraTargetRot->x, kDragStepRotation); // ドラッグで角度を変更
    // スムージングフレーム数を変えたら履歴をリセットしないと古い平均が残ってしまう
    if (ImGui::SliderInt("Smooth Frames", ctx.cameraSmoothFrames, 1, 60)) {
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }
    ImGui::Separator();
    if (ImGui::Button("Save##inspCam")) {
        editor.SaveCameraParams(ctx);
    }
#endif
}

// Ring を選んでいるときの Inspector 表示
void SceneEditor::RingState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(kRingHeaderColor, "[Ring]");
    ImGui::Separator();
    if (!ctx.ring || !ctx.ringPosition) {
        ImGui::TextDisabled("Ring is disabled.");
        return;
    }
    if (ImGui::DragFloat3("Position", &ctx.ringPosition->x, kDragStepPosition)) {
        ctx.ring->SetPosition(*ctx.ringPosition);
    }
    if (ImGui::DragFloat3("Rotation", &ctx.ringRotation->x, kDragStepRotation)) {
        ctx.ring->SetRotation(*ctx.ringRotation);
    }
    if (ImGui::DragFloat("Scale", ctx.ringScale, kDragStepScale, kMinEditableSize, kMaxObjectScale)) {
        ctx.ring->SetScale(*ctx.ringScale);
    }
    ImGui::Separator();
    if (ImGui::DragFloat("Inner Radius", ctx.ringInnerRadius, kDragStepRadius, kMinEditableSize, *ctx.ringOuterRadius - kRadiusGap)) {
        ctx.ring->SetInnerRadius(*ctx.ringInnerRadius);
    }
    if (ImGui::DragFloat("Outer Radius", ctx.ringOuterRadius, kDragStepRadius, *ctx.ringInnerRadius + kRadiusGap, kMaxRadius)) {
        ctx.ring->SetOuterRadius(*ctx.ringOuterRadius);
    }
    ImGui::Separator();
    if (ImGui::ColorEdit4("Color", &ctx.ringColor->x)) {
        ctx.ring->SetColor(*ctx.ringColor);
    }
#endif
}

// Cylinder を選んでいるときの Inspector 表示
void SceneEditor::CylinderState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(kCylinderHeaderColor, "[Cylinder]");
    ImGui::Separator();
    if (!ctx.cylinder || !ctx.cylinderPosition) {
        ImGui::TextDisabled("Cylinder is disabled.");
        return;
    }
    if (ImGui::DragFloat3("Position", &ctx.cylinderPosition->x, kDragStepPosition)) {
        ctx.cylinder->SetPosition(*ctx.cylinderPosition);
    }
    if (ImGui::DragFloat3("Rotation", &ctx.cylinderRotation->x, kDragStepRotation)) {
        ctx.cylinder->SetRotation(*ctx.cylinderRotation);
    }
    if (ImGui::DragFloat("Scale", ctx.cylinderScale, kDragStepScale, kMinEditableSize, kMaxObjectScale)) {
        ctx.cylinder->SetScale(*ctx.cylinderScale);
    }
    ImGui::Separator();
    if (ImGui::DragFloat("Top Radius", ctx.cylinderTopRadius, kDragStepRadius, kMinEditableSize, kMaxRadius)) {
        ctx.cylinder->SetTopRadius(*ctx.cylinderTopRadius);
    }
    if (ImGui::DragFloat("Bottom Radius", ctx.cylinderBottomRadius, kDragStepRadius, kMinEditableSize, kMaxRadius)) {
        ctx.cylinder->SetBottomRadius(*ctx.cylinderBottomRadius);
    }
    if (ImGui::DragFloat("Height", ctx.cylinderHeight, kDragStepRadius, kMinEditableSize, kMaxRadius)) {
        ctx.cylinder->SetHeight(*ctx.cylinderHeight);
    }
    ImGui::Separator();
    if (ImGui::ColorEdit4("Color", &ctx.cylinderColor->x)) {
        ctx.cylinder->SetColor(*ctx.cylinderColor);
    }
    if (ImGui::SliderFloat("Alpha Reference", ctx.cylinderAlphaRef, 0.0f, 1.0f)) {
        ctx.cylinder->SetAlphaReference(*ctx.cylinderAlphaRef);
    }
#endif
}

// Skydome を選んでいるときの Inspector 表示
void SceneEditor::SkydomeState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(kSkydomeHeaderColor, "[Skydome]");
    ImGui::Separator();
    if (ctx.skydome) {
        if (ImGui::ColorEdit4("Sky Color", &ctx.skyColor->x)) {
            ctx.skydome->SetSkyColor(*ctx.skyColor);
        }
        // 天球を Y 軸回転させることで、朝夕方夜のような方角の変化を演出できる
        if (ImGui::SliderFloat("Rotation Offset Y", ctx.skyRotOffsetY, -GameConstants::kPi, GameConstants::kPi)) {
            ctx.skydome->SetRotationOffsetY(*ctx.skyRotOffsetY);
        }
        if (ImGui::Button("Reset Offset")) {
            *ctx.skyRotOffsetY = 0.0f;
            ctx.skydome->SetRotationOffsetY(0.0f);
        }
    } else {
        ImGui::TextDisabled("Skydome is disabled.");
    }
#endif
}

// Human を選んでいるときの Inspector 表示
void SceneEditor::HumanState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(kHumanHeaderColor, "[Human]");
    ImGui::Separator();
    if (!ctx.human || !ctx.humanPosition) {
        ImGui::TextDisabled("Human is disabled.");
        return;
    }
    if (ImGui::DragFloat3("Position", &ctx.humanPosition->x, kDragStepFinePosition)) {
        ctx.human->SetPosition(*ctx.humanPosition);
    }
    if (ImGui::DragFloat3("Rotation", &ctx.humanRotation->x, kDragStepRotation)) {
        ctx.human->SetRotation(*ctx.humanRotation);
    }
    if (ImGui::DragFloat3("Scale", &ctx.humanScale->x, kDragStepScale, kMinEditableHumanScale, kMaxHumanScale)) {
        ctx.human->SetScale(*ctx.humanScale);
    }
    ImGui::Separator();
    if (ImGui::SliderFloat("Anim Speed", ctx.humanAnimSpeed, 0.0f, kMaxAnimSpeed)) {
        ctx.human->SetAnimSpeed(*ctx.humanAnimSpeed);
    }
    ImGui::Separator();
    if (ImGui::Button("Reset")) {
        *ctx.humanPosition = { 5.0f, 0.0f, 0.0f };
        *ctx.humanRotation = { };
        *ctx.humanScale = { 1.0f, 1.0f, 1.0f };
        *ctx.humanAnimSpeed = 1.0f;
        ctx.human->SetPosition(*ctx.humanPosition);
        ctx.human->SetRotation(*ctx.humanRotation);
        ctx.human->SetScale(*ctx.humanScale);
        ctx.human->SetAnimSpeed(*ctx.humanAnimSpeed);
    }
    ImGui::Separator();
    ImGui::Checkbox("Show Skeleton", ctx.showSkeleton);
#endif
}

// White Particles を選んでいるときの Inspector 表示
void SceneEditor::ParticlesState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1), "[White Particles]");
    ImGui::Separator();
    if (!ctx.whiteParticlePos) {
        ImGui::TextDisabled("White Particles are disabled.");
        return;
    }
    ImGui::DragFloat3("Position", &ctx.whiteParticlePos->x, kDragStepPosition);
    ImGui::ColorEdit4("Color", &ctx.whiteParticleColor->x);
    ImGui::DragFloat("Scale", ctx.whiteParticleScale, kDragStepScale, kMinEditableSize, kMaxParticleScale);
    ImGui::SliderInt("Count", ctx.whiteParticleCount, 1, kMaxParticleCount);
    ImGui::Separator();
    if (ImGui::Button("Re-emit", ImVec2(-1, 0))) {
        ParticleManager::GetInstance()->EmitScatterLoop(
            "white", *ctx.whiteParticlePos, kWhiteParticleSpread,
            static_cast<uint32_t>(*ctx.whiteParticleCount),
            *ctx.whiteParticleColor, kWhiteParticleSpeedMin, kWhiteParticleSpeedMax, *ctx.whiteParticleScale);
    }
#endif
}

// UI Element を選んでいるときの Inspector 表示
void SceneEditor::UIElementState::RenderInspector(const EditContext& ctx, SceneEditor& editor)
{
#ifdef USE_IMGUI
    int idx = editor.selectionIndex_;
    if (idx < 0 || idx >= (int)editor.uiElements_.size()) {
        return;
    } // 範囲外なら何もしない

    UIEntry& entry = editor.uiElements_[idx];
    Sprite* sp = entry.sprite.get();

    ImGui::TextColored(ImVec4(1, 0.5f, 1, 1), "[UI Element]");
    ImGui::Separator();

    // 名前編集用のバッファ選択が変わったときだけバッファを更新する
    static int lastIdx = -2;
    static char uiNameBuf[64] = { };
    if (lastIdx != idx) {
        lastIdx = idx;
        strncpy_s(uiNameBuf, entry.name.c_str(), sizeof(uiNameBuf) - 1);
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##uiname", uiNameBuf, sizeof(uiNameBuf))) {
        entry.name = uiNameBuf;
    }
    ImGui::Separator();

    // 位置・サイズ・回転・色を編集する
    Vector2 pos = sp->GetPosition();
    if (ImGui::DragFloat2("Position", &pos.x, kDragStepPixel)) {
        sp->SetPosition(pos);
    }
    Vector2 sz = sp->GetSize();
    if (ImGui::DragFloat2("Size", &sz.x, kDragStepPixel, kMinSpritePixels, kMaxSpritePixels)) {
        sp->SetSize(sz);
    }
    float rot = sp->GetRotation();
    if (ImGui::DragFloat("Rotation", &rot, kDragStepRotation)) {
        sp->SetRotation(rot);
    }
    Vector4 col = sp->GetColor();
    if (ImGui::ColorEdit4("Color", &col.x)) {
        sp->SetColor(col);
    }
    ImGui::Separator();

    // テクスチャファイルのパスを表示し、Browse ボタンでファイル選択ダイアログを開く
    ImGui::TextDisabled("%.40s", entry.texPath.empty() ? "(no texture)" : entry.texPath.c_str());
    if (ImGui::Button("Browse##uitex")) {
        std::string p = OpenFileDialog("PNG Files\0*.png\0All Files\0*.*\0\0", "Resources");
        if (!p.empty()) {
            entry.texPath = p;
            sp->SetTexture(p);
        }
    }
    ImGui::Separator();

    // Delete で一覧から削除、Save/Load で JSON に保存・復元する
    if (ImGui::Button("Delete##ui")) {
        editor.uiElements_.erase(editor.uiElements_.begin() + idx);
        editor.ChangeState(Selection::None);
        lastIdx = -2;
        editor.SaveUILayout();
    } else {
        ImGui::SameLine();
        if (ImGui::Button("Save##inspUI")) {
            editor.SaveUILayout();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load##inspUI")) {
            editor.LoadUILayout(ctx);
        }
    }
#endif
}

} // namespace engine::game
