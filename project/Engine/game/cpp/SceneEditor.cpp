#include "SceneEditor.h"
#include "GameConstants.h"
#include "JsonHelper.h"
#include "ParticleManager.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include <fstream>
#include <sstream>
#include <string>
#ifdef USE_IMGUI
#include <imgui.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")
#endif

// ============================================================
// 内部ヘルパー
// ============================================================

#ifdef USE_IMGUI
static std::string OpenFileDialog(const char* filter, const char* initDir)
{
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.lpstrFilter     = filter;
    ofn.lpstrFile       = path;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = initDir;
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) { return path; }
    return {};
}
#endif

// ============================================================
// State 遷移
// ============================================================

void SceneEditor::ChangeState(Selection sel)
{
    selection_      = sel;
    selectionIndex_ = -1;
    switch (sel) {
    case Selection::Camera:         currentState_ = std::make_unique<CameraState>();    break;
    case Selection::Ring:           currentState_ = std::make_unique<RingState>();      break;
    case Selection::Cylinder:       currentState_ = std::make_unique<CylinderState>(); break;
    case Selection::Skydome:        currentState_ = std::make_unique<SkydomeState>();   break;
    case Selection::Human:          currentState_ = std::make_unique<HumanState>();     break;
    case Selection::WhiteParticles: currentState_ = std::make_unique<ParticlesState>();break;
    case Selection::UIElement:      currentState_ = std::make_unique<UIElementState>();break;
    default:                        currentState_ = std::make_unique<NoneState>();      break;
    }
}

// ============================================================
// メインエントリ
// ============================================================

void SceneEditor::Update(const EditContext& ctx)
{
#ifdef USE_IMGUI
    RenderHierarchy(ctx);
    RenderInspector(ctx);
    RenderSceneControls(ctx);
    RenderCameraControl(ctx);
#else
    (void)ctx;
#endif
}

// ============================================================
// Hierarchy パネル
// ============================================================

void SceneEditor::RenderHierarchy(const EditContext& ctx)
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kHierarchyX, kHierarchyY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kHierarchyW, kHierarchyH), ImGuiCond_Once);
    ImGui::Begin("Hierarchy");

    auto selectable = [&](const char* label, Selection type) {
        if (ImGui::Selectable(label, selection_ == type)) { ChangeState(type); }
    };

    selectable("Camera",          Selection::Camera);
    selectable("Skydome",         Selection::Skydome);
    selectable("Human",           Selection::Human);
    selectable("Ring",            Selection::Ring);
    selectable("Cylinder",        Selection::Cylinder);
    selectable("White Particles", Selection::WhiteParticles);

    // UI Elements
    char uiHeader[48];
    snprintf(uiHeader, sizeof(uiHeader), "UI Elements (%d)", (int)uiElements_.size());
    bool uiOpen = ImGui::TreeNodeEx(uiHeader);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addUI")) {
        UIEntry entry;
        entry.name    = "UI Element " + std::to_string(uiElements_.size() + 1);
        entry.texPath = "";
        entry.sprite  = std::make_unique<Sprite>();
        entry.sprite->Initialize(ctx.spriteCommon, entry.texPath);
        entry.sprite->SetPosition({ GameConstants::kScreenCenterX, GameConstants::kScreenCenterY });
        entry.sprite->SetSize({ 100.0f, 100.0f });
        uiElements_.push_back(std::move(entry));
    }
    if (uiOpen) {
        for (int i = 0; i < (int)uiElements_.size(); ++i) {
            bool sel = (selection_ == Selection::UIElement && selectionIndex_ == i);
            char label[80];
            snprintf(label, sizeof(label), "  %s", uiElements_[i].name.c_str());
            if (ImGui::Selectable(label, sel)) {
                selection_      = Selection::UIElement;
                selectionIndex_ = i;
                currentState_   = std::make_unique<UIElementState>();
            }
        }
        ImGui::TreePop();
    }

    // Save ボタン
    ImGui::Separator();
    if (savedTimer_ > 0.0f) {
        savedTimer_ -= GameConstants::kFrameDeltaTime;
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Saved!");
    } else {
        if (ImGui::Button("Save", ImVec2(-1, 0))) {
            if (selection_ == Selection::Camera)    { SaveCameraParams(ctx); savedTimer_ = 1.5f; }
            if (selection_ == Selection::UIElement) { SaveUILayout();        savedTimer_ = 1.5f; }
        }
    }

    ImGui::End();
#endif
}

// ============================================================
// Inspector パネル（State に委譲）
// ============================================================

void SceneEditor::RenderInspector(const EditContext& ctx)
{
#ifdef USE_IMGUI
    // UIElement の選択インデックスが範囲外になったらリセット
    if (selection_ == Selection::UIElement &&
        (selectionIndex_ < 0 || selectionIndex_ >= (int)uiElements_.size())) {
        ChangeState(Selection::None);
    }

    ImGui::SetNextWindowPos(ImVec2(kInspectorX, kInspectorY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kInspectorW, kInspectorH), ImGuiCond_Once);
    ImGui::Begin("Inspector");

    if (currentState_) {
        currentState_->RenderInspector(ctx, *this);
    } else {
        currentState_ = std::make_unique<NoneState>();
        currentState_->RenderInspector(ctx, *this);
    }

    ImGui::End();
#endif
}

// ============================================================
// Scene Controls パネル
// ============================================================

void SceneEditor::RenderSceneControls(const EditContext& ctx)
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kSceneCtrlX, kSceneCtrlY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kSceneCtrlW, kSceneCtrlH), ImGuiCond_Once);
    ImGui::Begin("Scene Controls");

    if (ImGui::CollapsingHeader("Score")) {
        ImGui::Text("Current : %d", ScoreManager::GetInstance()->GetCurrentScore());
        const auto& ranking = ScoreManager::GetInstance()->GetRanking();
        if (ranking.empty()) { ImGui::TextDisabled("  (no records)"); }
        for (int i = 0; i < (int)ranking.size(); ++i) {
            ImGui::Text("  %2d. %d", i + 1, ranking[i]);
        }
        if (ImGui::Button("Reset All Scores")) {
            ScoreManager::GetInstance()->ResetAllScores();
        }
    }

    if (ImGui::CollapsingHeader("Game Time")) {
        ImGui::Text("Time : %02d:%02d", ctx.gameHour, ctx.gameMinute);
    }

    if (ImGui::CollapsingHeader("Actions")) {
        if (ImGui::Button("Game Clear"))  { SceneManager::GetInstance()->ChangeScene("CLEAR");    }
        ImGui::SameLine();
        if (ImGui::Button("Game Over"))   { SceneManager::GetInstance()->ChangeScene("GAMEOVER"); }
    }

    ImGui::End();
#endif
}

// ============================================================
// Camera Control パネル
// ============================================================

void SceneEditor::RenderCameraControl(const EditContext& ctx)
{
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kCamCtrlX, kCamCtrlY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kCamCtrlW, kCamCtrlH), ImGuiCond_Once);
    ImGui::Begin("Camera Control");

    ImGui::DragFloat3("Pos", &ctx.cameraTargetPos->x, 0.1f);
    ImGui::DragFloat3("Rot", &ctx.cameraTargetRot->x, 0.01f);

    if (ImGui::SliderInt("Smooth Frames", ctx.cameraSmoothFrames, 1, 60)) {
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }

    ImGui::Spacing();
    if (ImGui::Button("Center")) {
        *ctx.cameraTargetPos = { 0.0f, 0.0f, 0.0f };
        *ctx.cameraTargetRot = { 0.0f, 0.0f, 0.0f };
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save##cam")) { SaveCameraParams(ctx); }
    ImGui::SameLine();
    if (ImGui::Button("Load##cam")) { LoadCameraParams(ctx); }

    ImGui::End();
#endif
}

// ============================================================
// JSON 永続化
// ============================================================

void SceneEditor::SaveCameraParams(const EditContext& ctx)
{
    std::ofstream f("Resources/debug_camera.json");
    if (!f) { return; }
    const Vector3& pos = *ctx.cameraTargetPos;
    const Vector3& rot = *ctx.cameraTargetRot;
    f << "{\n"
      << "  \"camera_pos_x\": " << pos.x << ",\n"
      << "  \"camera_pos_y\": " << pos.y << ",\n"
      << "  \"camera_pos_z\": " << pos.z << ",\n"
      << "  \"camera_rot_x\": " << rot.x << ",\n"
      << "  \"camera_rot_y\": " << rot.y << ",\n"
      << "  \"camera_rot_z\": " << rot.z << ",\n"
      << "  \"camera_smooth_frames\": " << *ctx.cameraSmoothFrames << "\n"
      << "}\n";
}

void SceneEditor::LoadCameraParams(const EditContext& ctx)
{
    std::ifstream f("Resources/debug_camera.json");
    if (!f) { return; }
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Vector3& pos = *ctx.cameraTargetPos;
    Vector3& rot = *ctx.cameraTargetRot;
    pos = {
        JsonHelper::ReadFloat(src, "camera_pos_x", pos.x),
        JsonHelper::ReadFloat(src, "camera_pos_y", pos.y),
        JsonHelper::ReadFloat(src, "camera_pos_z", pos.z),
    };
    rot = {
        JsonHelper::ReadFloat(src, "camera_rot_x", rot.x),
        JsonHelper::ReadFloat(src, "camera_rot_y", rot.y),
        JsonHelper::ReadFloat(src, "camera_rot_z", rot.z),
    };
    *ctx.cameraSmoothFrames = JsonHelper::ReadInt(src, "camera_smooth_frames", *ctx.cameraSmoothFrames);
    ctx.camera->SetTranslate(pos);
    ctx.camera->SetRotate(rot);
    ctx.cameraPosHistory->clear();
    ctx.cameraRotHistory->clear();
}

void SceneEditor::SaveUILayout()
{
    std::ofstream f("Resources/debug_ui.json");
    if (!f) { return; }
    f << "{\n  \"count\": " << uiElements_.size();
    for (int i = 0; i < (int)uiElements_.size(); ++i) {
        const UIEntry& e  = uiElements_[i];
        Sprite*        sp = e.sprite.get();
        Vector2 pos = sp->GetPosition();
        Vector2 sz  = sp->GetSize();
        Vector4 col = sp->GetColor();
        float   rot = sp->GetRotation();
        auto    mk  = [&](const char* field) { return "ui_" + std::to_string(i) + field; };
        f << ",\n"
          << "  \"" << mk("_name")  << "\": \"" << e.name     << "\",\n"
          << "  \"" << mk("_tex")   << "\": \"" << e.texPath  << "\",\n"
          << "  \"" << mk("_pos_x") << "\": " << pos.x << ",\n"
          << "  \"" << mk("_pos_y") << "\": " << pos.y << ",\n"
          << "  \"" << mk("_sz_x")  << "\": " << sz.x  << ",\n"
          << "  \"" << mk("_sz_y")  << "\": " << sz.y  << ",\n"
          << "  \"" << mk("_rot")   << "\": " << rot   << ",\n"
          << "  \"" << mk("_col_r") << "\": " << col.x << ",\n"
          << "  \"" << mk("_col_g") << "\": " << col.y << ",\n"
          << "  \"" << mk("_col_b") << "\": " << col.z << ",\n"
          << "  \"" << mk("_col_a") << "\": " << col.w;
    }
    f << "\n}\n";
}

void SceneEditor::LoadAll(const EditContext& ctx)
{
    LoadCameraParams(ctx);
    LoadUILayout(ctx);
}

void SceneEditor::LoadUILayout(const EditContext& ctx)
{
    std::ifstream f("Resources/debug_ui.json");
    if (!f) { return; }
    std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int count = JsonHelper::ReadInt(src, "count", 0);
    uiElements_.clear();
    ChangeState(Selection::None);
    for (int i = 0; i < count; ++i) {
        auto mk = [&](const char* field) { return "ui_" + std::to_string(i) + field; };
        UIEntry entry;
        entry.name    = JsonHelper::ReadString(src, mk("_name"), "UI Element");
        entry.texPath = JsonHelper::ReadString(src, mk("_tex"),  "");
        entry.sprite  = std::make_unique<Sprite>();
        entry.sprite->Initialize(ctx.spriteCommon, entry.texPath);
        entry.sprite->SetPosition({
            JsonHelper::ReadFloat(src, mk("_pos_x"), GameConstants::kScreenCenterX),
            JsonHelper::ReadFloat(src, mk("_pos_y"), GameConstants::kScreenCenterY) });
        entry.sprite->SetSize({
            JsonHelper::ReadFloat(src, mk("_sz_x"), 100.0f),
            JsonHelper::ReadFloat(src, mk("_sz_y"), 100.0f) });
        entry.sprite->SetRotation(JsonHelper::ReadFloat(src, mk("_rot"), 0.0f));
        entry.sprite->SetColor({
            JsonHelper::ReadFloat(src, mk("_col_r"), 1.0f),
            JsonHelper::ReadFloat(src, mk("_col_g"), 1.0f),
            JsonHelper::ReadFloat(src, mk("_col_b"), 1.0f),
            JsonHelper::ReadFloat(src, mk("_col_a"), 1.0f) });
        uiElements_.push_back(std::move(entry));
    }
}

// ============================================================
// IEditorState 実装
// ============================================================

void SceneEditor::NoneState::RenderInspector(const EditContext&, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextDisabled("(Nothing selected)");
    ImGui::TextDisabled("Hierarchy でオブジェクトを");
    ImGui::TextDisabled("クリックしてください");
#endif
}

void SceneEditor::CameraState::RenderInspector(const EditContext& ctx, SceneEditor& editor)
{
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "[Camera]");
    ImGui::Separator();
    ImGui::DragFloat3("Position", &ctx.cameraTargetPos->x, 0.1f);
    ImGui::DragFloat3("Rotation", &ctx.cameraTargetRot->x, 0.01f);
    if (ImGui::SliderInt("Smooth Frames", ctx.cameraSmoothFrames, 1, 60)) {
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }
    ImGui::Separator();
    if (ImGui::Button("Save##inspCam")) { editor.SaveCameraParams(ctx); }
#endif
}

void SceneEditor::RingState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1), "[Ring]");
    ImGui::Separator();
    if (ImGui::DragFloat3("Position", &ctx.ringPosition->x, 0.1f))  { ctx.ring->SetPosition(*ctx.ringPosition); }
    if (ImGui::DragFloat3("Rotation", &ctx.ringRotation->x, 0.01f)) { ctx.ring->SetRotation(*ctx.ringRotation); }
    if (ImGui::DragFloat("Scale", ctx.ringScale, 0.01f, 0.01f, 20.0f)) { ctx.ring->SetScale(*ctx.ringScale); }
    ImGui::Separator();
    if (ImGui::DragFloat("Inner Radius", ctx.ringInnerRadius, 0.05f, 0.01f, *ctx.ringOuterRadius - 0.01f)) {
        ctx.ring->SetInnerRadius(*ctx.ringInnerRadius);
    }
    if (ImGui::DragFloat("Outer Radius", ctx.ringOuterRadius, 0.05f, *ctx.ringInnerRadius + 0.01f, 50.0f)) {
        ctx.ring->SetOuterRadius(*ctx.ringOuterRadius);
    }
    ImGui::Separator();
    if (ImGui::ColorEdit4("Color", &ctx.ringColor->x)) { ctx.ring->SetColor(*ctx.ringColor); }
#endif
}

void SceneEditor::CylinderState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1), "[Cylinder]");
    ImGui::Separator();
    if (ImGui::DragFloat3("Position", &ctx.cylinderPosition->x, 0.1f))  { ctx.cylinder->SetPosition(*ctx.cylinderPosition); }
    if (ImGui::DragFloat3("Rotation", &ctx.cylinderRotation->x, 0.01f)) { ctx.cylinder->SetRotation(*ctx.cylinderRotation); }
    if (ImGui::DragFloat("Scale", ctx.cylinderScale, 0.01f, 0.01f, 20.0f)) { ctx.cylinder->SetScale(*ctx.cylinderScale); }
    ImGui::Separator();
    if (ImGui::DragFloat("Top Radius",    ctx.cylinderTopRadius,    0.05f, 0.01f, 50.0f)) { ctx.cylinder->SetTopRadius(*ctx.cylinderTopRadius); }
    if (ImGui::DragFloat("Bottom Radius", ctx.cylinderBottomRadius, 0.05f, 0.01f, 50.0f)) { ctx.cylinder->SetBottomRadius(*ctx.cylinderBottomRadius); }
    if (ImGui::DragFloat("Height",        ctx.cylinderHeight,       0.05f, 0.01f, 50.0f)) { ctx.cylinder->SetHeight(*ctx.cylinderHeight); }
    ImGui::Separator();
    if (ImGui::ColorEdit4("Color", &ctx.cylinderColor->x)) { ctx.cylinder->SetColor(*ctx.cylinderColor); }
    if (ImGui::SliderFloat("Alpha Reference", ctx.cylinderAlphaRef, 0.0f, 1.0f)) { ctx.cylinder->SetAlphaReference(*ctx.cylinderAlphaRef); }
#endif
}

void SceneEditor::SkydomeState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1), "[Skydome]");
    ImGui::Separator();
    if (ctx.skydome) {
        if (ImGui::ColorEdit4("Sky Color", &ctx.skyColor->x)) { ctx.skydome->SetSkyColor(*ctx.skyColor); }
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

void SceneEditor::HumanState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1), "[Human]");
    ImGui::Separator();
    if (ImGui::DragFloat3("Position", &ctx.humanPosition->x, 0.05f)) { ctx.human->SetPosition(*ctx.humanPosition); }
    if (ImGui::DragFloat3("Rotation", &ctx.humanRotation->x, 0.01f)) { ctx.human->SetRotation(*ctx.humanRotation); }
    if (ImGui::DragFloat3("Scale",    &ctx.humanScale->x, 0.01f, 0.001f, 100.0f)) { ctx.human->SetScale(*ctx.humanScale); }
    ImGui::Separator();
    if (ImGui::SliderFloat("Anim Speed", ctx.humanAnimSpeed, 0.0f, 5.0f)) { ctx.human->SetAnimSpeed(*ctx.humanAnimSpeed); }
    ImGui::Separator();
    if (ImGui::Button("Reset")) {
        *ctx.humanPosition  = { 5.0f, 0.0f, 0.0f };
        *ctx.humanRotation  = {};
        *ctx.humanScale     = { 1.0f, 1.0f, 1.0f };
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

void SceneEditor::ParticlesState::RenderInspector(const EditContext& ctx, SceneEditor&)
{
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1), "[White Particles]");
    ImGui::Separator();
    ImGui::DragFloat3("Position", &ctx.whiteParticlePos->x, 0.1f);
    ImGui::ColorEdit4("Color",    &ctx.whiteParticleColor->x);
    ImGui::DragFloat("Scale",     ctx.whiteParticleScale, 0.01f, 0.01f, 10.0f);
    ImGui::SliderInt("Count",     ctx.whiteParticleCount, 1, 1024);
    ImGui::Separator();
    if (ImGui::Button("Re-emit", ImVec2(-1, 0))) {
        ParticleManager::GetInstance()->EmitScatterLoop(
            "white", *ctx.whiteParticlePos, 20.0f,
            static_cast<uint32_t>(*ctx.whiteParticleCount),
            *ctx.whiteParticleColor, 2.0f, 5.0f, *ctx.whiteParticleScale);
    }
#endif
}

void SceneEditor::UIElementState::RenderInspector(const EditContext& ctx, SceneEditor& editor)
{
#ifdef USE_IMGUI
    int idx = editor.selectionIndex_;
    if (idx < 0 || idx >= (int)editor.uiElements_.size()) { return; }

    UIEntry& entry = editor.uiElements_[idx];
    Sprite*  sp    = entry.sprite.get();

    ImGui::TextColored(ImVec4(1, 0.5f, 1, 1), "[UI Element]");
    ImGui::Separator();

    static int   lastIdx      = -2;
    static char  uiNameBuf[64] = {};
    if (lastIdx != idx) {
        lastIdx = idx;
        strncpy_s(uiNameBuf, entry.name.c_str(), sizeof(uiNameBuf) - 1);
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##uiname", uiNameBuf, sizeof(uiNameBuf))) { entry.name = uiNameBuf; }
    ImGui::Separator();

    Vector2 pos = sp->GetPosition();
    if (ImGui::DragFloat2("Position", &pos.x, 1.0f))          { sp->SetPosition(pos); }
    Vector2 sz  = sp->GetSize();
    if (ImGui::DragFloat2("Size", &sz.x, 1.0f, 1.0f, 4096.0f)) { sp->SetSize(sz); }
    float rot = sp->GetRotation();
    if (ImGui::DragFloat("Rotation", &rot, 0.01f))             { sp->SetRotation(rot); }
    Vector4 col = sp->GetColor();
    if (ImGui::ColorEdit4("Color", &col.x))                    { sp->SetColor(col); }
    ImGui::Separator();

    ImGui::TextDisabled("%.40s", entry.texPath.empty() ? "(no texture)" : entry.texPath.c_str());
    if (ImGui::Button("Browse##uitex")) {
        std::string p = OpenFileDialog("PNG Files\0*.png\0All Files\0*.*\0\0", "Resources");
        if (!p.empty()) { entry.texPath = p; sp->SetTexture(p); }
    }
    ImGui::Separator();

    if (ImGui::Button("Delete##ui")) {
        editor.uiElements_.erase(editor.uiElements_.begin() + idx);
        editor.ChangeState(Selection::None);
        lastIdx = -2;
        editor.SaveUILayout();
    } else {
        ImGui::SameLine();
        if (ImGui::Button("Save##inspUI"))  { editor.SaveUILayout();           }
        ImGui::SameLine();
        if (ImGui::Button("Load##inspUI"))  { editor.LoadUILayout(ctx);        }
    }
#endif
}
