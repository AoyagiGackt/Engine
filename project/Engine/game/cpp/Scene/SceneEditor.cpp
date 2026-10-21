/**
 * @file SceneEditor.cpp
 * @brief 実行中のライブパラメータ調整パネル（SceneEditor、Stateパターンによる選択種別ごとの表示切替）の実装
 */
#include "SceneEditor.h"
#include "TextBufferCapacity.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "ParticleManager.h"
#include "RunData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "RevolvedMeshBase.h"
#include <iterator>
#include <string>
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <imgui.h>
#endif
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
// カメラの平滑化に使うフレーム数の範囲
constexpr int kMinSmoothFrames = 1;
constexpr int kMaxSmoothFrames = 60;

#ifdef USE_IMGUI
/** @brief カメラの平滑化のフレーム数のスライダーを出す（変えたら古い平均が混ざらないよう履歴を消す） */
void DrawSmoothFramesSlider(const SceneEditor::EditContext& ctx)
{
    if (ImGui::SliderInt("Smooth Frames", ctx.cameraSmoothFrames, kMinSmoothFrames, kMaxSmoothFrames))
    {
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }
}

/** @brief 回転体メッシュに共通する位置・回転・拡大率を編集し、変化した値を実体へ反映する */
void RenderMeshTransform(RevolvedMeshBase& mesh, Vector3& position, Vector3& rotation, float& scale)
{
    if (ImGui::DragFloat3("Position", &position.x, kDragStepPosition))
    {
        mesh.SetPosition(position);
    }
    if (ImGui::DragFloat3("Rotation", &rotation.x, kDragStepRotation))
    {
        mesh.SetRotation(rotation);
    }
    if (ImGui::DragFloat("Scale", &scale, kDragStepScale, kMinEditableSize, kMaxObjectScale))
    {
        mesh.SetScale(scale);
    }
}
#endif
}

#ifdef USE_IMGUI
using engine::graphics::EditorUI::OpenFileDialog;
using engine::graphics::EditorUI::ToProjectRelativePath;
#endif

// State 遷移

// Hierarchy でオブジェクトが選ばれたとき、対応する State クラスに切り替える
// こうすることで Inspector パネルに表示する内容がオブジェクト種別に応じて変わる
// ══════════════════════════════════════════════════════
// 状態管理とパネル描画
// ══════════════════════════════════════════════════════

/** @brief 選択の種類を切り替え、対応するインスペクタの状態オブジェクトを作り直す */
void SceneEditor::ChangeState(Selection sel)
{
    // 選んだ対象を覚え、UI要素の選択位置は選び直しにする
    selection_ = sel;
    selectionIndex_ = -1;
    // 対象ごとの編集状態の作り方（Selection の並び順と同じ）
    using StateFactory = std::unique_ptr<IEditorState> (*)();
    static constexpr StateFactory kFactories[] = {
        // 何も選んでいない
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<NoneState>());
        },
        // カメラ
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<CameraState>());
        },
        // 画面上のUI要素
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<UIElementState>());
        },
        // リング
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<RingState>());
        },
        // 円柱
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<CylinderState>());
        },
        // 天球
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<SkydomeState>());
        },
        // 人型モデル
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<HumanState>());
        },
        // パーティクル
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<ParticlesState>());
        },
    };
    // 表の外の値が来たら何も選んでいない状態にする
    const size_t index = static_cast<size_t>(sel);
    currentState_ = (index < std::size(kFactories)) ? kFactories[index]() : kFactories[0]();
}

// メインエントリ

// 毎フレーム呼ばれるUSE_IMGUI ビルドのときだけ各パネルを描画する
// リリースビルドでは引数を無視して何もしない
void SceneEditor::Update(const EditContext& ctx, engine::Input* input)
{
#ifdef USE_IMGUI
    if (input && input->TriggerKey(DIK_F3))
    {
        visible_ = !visible_;
    }
    if (!visible_)
    {
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
        if (ImGui::Selectable(label, selection_ == type))
        {
            ChangeState(type);
        }
    };

    // Ring/Cylinder/Human/White Particles はこのシーンのEditContextに配線されておらず
    // 選んでも無効表示にしかならないため、実際に編集できる項目だけを並べる
    selectable("Camera", Selection::Camera);
    selectable("Skydome", Selection::Skydome);

    // UI Elements は可変長リストなのでツリーノードで折りたたみ表示する
    char uiHeader[TextBufferCapacity::kSectionTitle];
    snprintf(uiHeader, sizeof(uiHeader), "UI Elements (%d)", (int)uiElements_.size());
    bool uiOpen = ImGui::TreeNodeEx(uiHeader);
    ImGui::SameLine();
    // + ボタンを押すと新しい UI スプライトを追加する
    if (ImGui::SmallButton("+##addUI"))
    {
        UIEntry entry;
        entry.name = "UI Element " + std::to_string(uiElements_.size() + 1);
        entry.texPath = "";
        entry.sprite = std::make_unique<Sprite>();
        entry.sprite->Initialize(ctx.spriteCommon, entry.texPath);
        entry.sprite->SetPosition({ GameConstants::kScreenCenterX, GameConstants::kScreenCenterY }); // 画面中央に配置
        entry.sprite->SetSize(kNewUIElementSize);
        uiElements_.push_back(std::move(entry));
    }
    if (uiOpen)
    {
        for (int i = 0; i < (int)uiElements_.size(); ++i)
        {
            bool sel = (selection_ == Selection::UIElement && selectionIndex_ == i);
            char label[TextBufferCapacity::kStatus];
            snprintf(label, sizeof(label), "  %s", uiElements_[i].name.c_str());
            if (ImGui::Selectable(label, sel))
            {
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
    if (savedTimer_ > 0.0f)
    {
        // 保存直後は "Saved!" と表示してフェードアウトする
        savedTimer_ -= GameConstants::kFrameDeltaTime;
        ImGui::TextColored(kSavedTextColor, "Saved!");
    }
    else
    {
        if (ImGui::Button("Save", ImVec2(-1, 0)))
        {
            if (selection_ == Selection::Camera)
            {
                SaveCameraParams(ctx);
                savedTimer_ = kSavedMessageSeconds;
            }
            if (selection_ == Selection::UIElement)
            {
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
    if (selection_ == Selection::UIElement && (selectionIndex_ < 0 || selectionIndex_ >= (int)uiElements_.size()))
    {
        ChangeState(Selection::None);
    }

    ImGui::SetNextWindowPos(ImVec2(kInspectorX, kInspectorY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kInspectorW, kInspectorH), ImGuiCond_Once);
    ImGui::Begin("Inspector");

    if (currentState_)
    {
        currentState_->RenderInspector(ctx, *this);
    }
    else
    {
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
    if (ImGui::CollapsingHeader("Score"))
    {
        ImGui::Text("Current : %d", RunData::GetInstance()->GetScore());
        const auto ranking = ScoreManager::GetInstance()->GetRankingSnapshot();
        if (ranking.empty())
        {
            ImGui::TextDisabled("  (no records)");
        }
        for (int i = 0; i < (int)ranking.size(); ++i)
        {
            ImGui::Text("  %2d. %d", i + 1, ranking[i]);
        }
        if (ImGui::Button("Reset All Scores"))
        {
            ScoreManager::GetInstance()->ResetAllScores();
        }
    }

    // シーン切り替えボタン（折りたたみ）
    if (ImGui::CollapsingHeader("Actions"))
    {
        if (ImGui::Button("Game Clear"))
        {
            if (ctx.requestClear)
            {
                *ctx.requestClear = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Game Over"))
        {
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

    DrawSmoothFramesSlider(ctx);

    ImGui::Spacing();

    // 原点にリセットするボタン
    if (ImGui::Button("Center"))
    {
        *ctx.cameraTargetPos = { 0.0f, 0.0f, 0.0f };
        *ctx.cameraTargetRot = { 0.0f, 0.0f, 0.0f };
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save##cam"))
    {
        SaveCameraParams(ctx);
    } // JSON に現在値を保存
    ImGui::SameLine();
    if (ImGui::Button("Load##cam"))
    {
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

// IEditorState 実装（各オブジェクト種別ごとの Inspector 描画）

// 何も選んでいないときの Inspector 表示
// ══════════════════════════════════════════════════════
// 選択状態別の詳細設定
// ══════════════════════════════════════════════════════

/** @brief 何も選んでいないときのインスペクタに案内文を表示する */
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
    DrawSmoothFramesSlider(ctx);
    ImGui::Separator();
    if (ImGui::Button("Save##inspCam"))
    {
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
    if (!ctx.ring || !ctx.ringPosition)
    {
        ImGui::TextDisabled("Ring is disabled.");
        return;
    }
    RenderMeshTransform(*ctx.ring, *ctx.ringPosition, *ctx.ringRotation, *ctx.ringScale);
    ImGui::Separator();
    if (ImGui::DragFloat("Inner Radius", ctx.ringInnerRadius, kDragStepRadius, kMinEditableSize, *ctx.ringOuterRadius - kRadiusGap))
    {
        ctx.ring->SetInnerRadius(*ctx.ringInnerRadius);
    }
    if (ImGui::DragFloat("Outer Radius", ctx.ringOuterRadius, kDragStepRadius, *ctx.ringInnerRadius + kRadiusGap, kMaxRadius))
    {
        ctx.ring->SetOuterRadius(*ctx.ringOuterRadius);
    }
    ImGui::Separator();
    if (ImGui::ColorEdit4("Color", &ctx.ringColor->x))
    {
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
    if (!ctx.cylinder || !ctx.cylinderPosition)
    {
        ImGui::TextDisabled("Cylinder is disabled.");
        return;
    }
    RenderMeshTransform(*ctx.cylinder, *ctx.cylinderPosition, *ctx.cylinderRotation, *ctx.cylinderScale);
    ImGui::Separator();
    if (ImGui::DragFloat("Top Radius", ctx.cylinderTopRadius, kDragStepRadius, kMinEditableSize, kMaxRadius))
    {
        ctx.cylinder->SetTopRadius(*ctx.cylinderTopRadius);
    }
    if (ImGui::DragFloat("Bottom Radius", ctx.cylinderBottomRadius, kDragStepRadius, kMinEditableSize, kMaxRadius))
    {
        ctx.cylinder->SetBottomRadius(*ctx.cylinderBottomRadius);
    }
    if (ImGui::DragFloat("Height", ctx.cylinderHeight, kDragStepRadius, kMinEditableSize, kMaxRadius))
    {
        ctx.cylinder->SetHeight(*ctx.cylinderHeight);
    }
    ImGui::Separator();
    if (ImGui::ColorEdit4("Color", &ctx.cylinderColor->x))
    {
        ctx.cylinder->SetColor(*ctx.cylinderColor);
    }
    if (ImGui::SliderFloat("Alpha Reference", ctx.cylinderAlphaRef, 0.0f, 1.0f))
    {
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
    if (ctx.skydome)
    {
        if (ImGui::ColorEdit4("Sky Color", &ctx.skyColor->x))
        {
            ctx.skydome->SetSkyColor(*ctx.skyColor);
        }
        // 天球を Y 軸回転させることで、朝夕方夜のような方角の変化を演出できる
        if (ImGui::SliderFloat("Rotation Offset Y", ctx.skyRotOffsetY, -GameConstants::kPi, GameConstants::kPi))
        {
            ctx.skydome->SetRotationOffsetY(*ctx.skyRotOffsetY);
        }
        if (ImGui::Button("Reset Offset"))
        {
            *ctx.skyRotOffsetY = 0.0f;
            ctx.skydome->SetRotationOffsetY(0.0f);
        }
    }
    else
    {
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
    if (!ctx.human || !ctx.humanPosition)
    {
        ImGui::TextDisabled("Human is disabled.");
        return;
    }
    if (ImGui::DragFloat3("Position", &ctx.humanPosition->x, kDragStepFinePosition))
    {
        ctx.human->SetPosition(*ctx.humanPosition);
    }
    if (ImGui::DragFloat3("Rotation", &ctx.humanRotation->x, kDragStepRotation))
    {
        ctx.human->SetRotation(*ctx.humanRotation);
    }
    if (ImGui::DragFloat3("Scale", &ctx.humanScale->x, kDragStepScale, kMinEditableHumanScale, kMaxHumanScale))
    {
        ctx.human->SetScale(*ctx.humanScale);
    }
    ImGui::Separator();
    if (ImGui::SliderFloat("Anim Speed", ctx.humanAnimSpeed, 0.0f, kMaxAnimSpeed))
    {
        ctx.human->SetAnimSpeed(*ctx.humanAnimSpeed);
    }
    ImGui::Separator();
    if (ImGui::Button("Reset"))
    {
        constexpr Vector3 kDefaultHumanPosition = { 5.0f, 0.0f, 0.0f };
        *ctx.humanPosition = kDefaultHumanPosition;
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
    if (!ctx.whiteParticlePos)
    {
        ImGui::TextDisabled("White Particles are disabled.");
        return;
    }
    ImGui::DragFloat3("Position", &ctx.whiteParticlePos->x, kDragStepPosition);
    ImGui::ColorEdit4("Color", &ctx.whiteParticleColor->x);
    ImGui::DragFloat("Scale", ctx.whiteParticleScale, kDragStepScale, kMinEditableSize, kMaxParticleScale);
    ImGui::SliderInt("Count", ctx.whiteParticleCount, 1, kMaxParticleCount);
    ImGui::Separator();
    if (ImGui::Button("Re-emit", ImVec2(-1, 0)))
    {
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
    // 選択中の番号が一覧の範囲外なら何もしない
    const int idx = editor.selectionIndex_;
    if (idx < 0 || idx >= static_cast<int>(editor.uiElements_.size()))
    {
        return;
    }
    // 編集はスプライトの値を直接読み書きする
    UIEntry& entry = editor.uiElements_[idx];
    Sprite* sp = entry.sprite.get();
    // 何を編集中か分かるよう、色付きの見出しを出す
    constexpr ImVec4 kHeadingColor = { 1.0f, 0.5f, 1.0f, 1.0f };
    ImGui::TextColored(kHeadingColor, "[UI Element]");
    ImGui::Separator();
    // 選択が変わったときだけ名前の入力欄を読み直す
    if (editor.uiNameEditIndex_ != idx)
    {
        editor.uiNameEditIndex_ = idx;
        strncpy_s(editor.uiNameBuffer_, entry.name.c_str(), sizeof(editor.uiNameBuffer_) - 1);
    }
    // 入力した名前はその場で一覧の名前に反映する
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##uiname", editor.uiNameBuffer_, sizeof(editor.uiNameBuffer_)))
    {
        entry.name = editor.uiNameBuffer_;
    }
    ImGui::Separator();
    // 位置・サイズ・回転・色を編集する（変わった値だけスプライトへ書き戻す）
    Vector2 pos = sp->GetPosition();
    if (ImGui::DragFloat2("Position", &pos.x, kDragStepPixel))
    {
        sp->SetPosition(pos);
    }
    // サイズは小さすぎて見えなくならないよう上下限を付ける
    Vector2 sz = sp->GetSize();
    if (ImGui::DragFloat2("Size", &sz.x, kDragStepPixel, kMinSpritePixels, kMaxSpritePixels))
    {
        sp->SetSize(sz);
    }
    float rot = sp->GetRotation();
    if (ImGui::DragFloat("Rotation", &rot, kDragStepRotation))
    {
        sp->SetRotation(rot);
    }
    Vector4 col = sp->GetColor();
    if (ImGui::ColorEdit4("Color", &col.x))
    {
        sp->SetColor(col);
    }
    ImGui::Separator();
    // テクスチャファイルのパスを表示し、Browse ボタンでファイル選択ダイアログを開く
    ImGui::TextDisabled("%.40s", entry.texPath.empty() ? "(no texture)" : entry.texPath.c_str());
    if (ImGui::Button("Browse##uitex"))
    {
        const std::string selected = OpenFileDialog("PNG Files\0*.png\0All Files\0*.*\0\0", "Resources");
        // キャンセルされたら何もしない
        if (!selected.empty())
        {
            // 保存するレイアウトに絶対パスを残さない
            const std::string p = ToProjectRelativePath(selected);
            entry.texPath = p;
            sp->SetTexture(p);
        }
    }
    ImGui::Separator();
    // Delete で一覧から削除、Save/Load で JSON に保存・復元する
    if (ImGui::Button("Delete##ui"))
    {
        // 消した要素を指したままにならないよう、選択を外して名前の編集状態も戻す
        editor.uiElements_.erase(editor.uiElements_.begin() + idx);
        editor.ChangeState(Selection::None);
        editor.uiNameEditIndex_ = kNoEditedUiIndex;
        editor.SaveUILayout();
    }
    else
    {
        ImGui::SameLine();
        if (ImGui::Button("Save##inspUI"))
        {
            editor.SaveUILayout();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load##inspUI"))
        {
            editor.LoadUILayout(ctx);
        }
    }
#endif
}
/** @brief 編集対象のUIを一覧の所有権を公開せずに描画する */
void SceneEditor::DrawUIElements()
{
    for (const UIEntry& entry : uiElements_)
    {
        entry.sprite->Update();
        entry.sprite->Draw();
    }
}
} // namespace engine::game
