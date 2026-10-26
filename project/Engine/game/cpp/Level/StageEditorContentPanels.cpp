/**
 * @file StageEditorContentPanels.cpp
 * @brief ステージエディタの各種ImGuiパネル（ヒエラルキー・ツールバー・インスペクタ・
 * アセットパレット・フラグ・制作ワークフロー・ノーコードイベント・Wave・解析・差分・ヘルプ）を実装するファイル
 * @note StageEditor のパネルの描画だけを StageEditor.cpp から分けて置いている（クラスは StageEditor のまま）
 */
#include "StageEditor.h"
#include "TextBufferCapacity.h"
#ifdef USE_IMGUI
#include "Camera.h"
#include "DiagnosticsDraw.h"
#include "EditorUI.h"
#include "EnemyEntity.h"
#include "GameFlags.h"
#include "GraphEditor.h"
#include "JsonConfigEditor.h"
#include "KnightEnemy.h"
#include "StageEditorPanels.h"
#include "SceneManager.h"
#include "StageEditorPrefabService.h"
#include "StageEditorUiStyle.h"
#include "ModelThumbnailBaker.h"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include "json.hpp"
#include "WinApp.h"
#include <algorithm>
#include <iterator>
#include <imgui.h>
#endif
using namespace engine::graphics;

namespace engine::game {
#ifdef USE_IMGUI
namespace {
namespace editorUi = engine::game::stageEditorUiStyle;

constexpr ImVec2 kContentWindowMinSize = { 320.0f, 240.0f };
constexpr ImVec4 kModeLabelColor = { 0.55f, 0.78f, 1.0f, 1.0f };
constexpr ImVec4 kUnsavedColor = { 1.0f, 0.8f, 0.35f, 1.0f };
constexpr ImVec4 kSavedColor = { 0.5f, 0.8f, 0.65f, 1.0f };
constexpr ImVec4 kActiveToolColor = { 0.2f, 0.46f, 0.75f, 1.0f };
constexpr ImVec4 kStopPlayTestColor = { 0.65f, 0.32f, 0.15f, 1.0f };
constexpr ImVec4 kStartPlayTestColor = { 0.16f, 0.48f, 0.34f, 1.0f };
constexpr float kToolbarSnapStepWidth = 55.0f;
constexpr float kDebugStartSceneComboWidth = 240.0f;
constexpr ImVec2 kPlayTestOverlayPos = { 10.0f, 10.0f };
constexpr float kPlayTestOverlayAlpha = 0.85f;
constexpr float kAssetTileSize = 96.0f;

/** @brief ファイルパスをUTF-8文字列へ揃える */
std::string AssetPathUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string(value.begin(), value.end());
}

/** @brief 比較用パスの区切りと英字の大小を揃える */
std::string AssetPathKey(const std::filesystem::path& path) {
    std::string key = AssetPathUtf8(path.lexically_normal());
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return key;
}
/** @brief 見つけたモデルに合うテクスチャを探す（同名.png → 同名Palette.png → .mtl の map_Kd → glTF の最初の画像 → 白） */
std::string GuessAssetTexture(const std::filesystem::path& model) {
    constexpr const char* kFallbackTexture = "Resources/white.png";
    std::error_code error;
    auto exists = [&](const std::filesystem::path& path) {
        return std::filesystem::is_regular_file(path, error);
    };
    std::filesystem::path candidate = model;
    candidate.replace_extension(".png");
    if (exists(candidate)) {
        return AssetPathUtf8(candidate);
    }
    candidate = model.parent_path() / (model.stem().native() + L"Palette.png");
    if (exists(candidate)) {
        return AssetPathUtf8(candidate);
    }
    // OBJ は同名の .mtl に書かれた拡散テクスチャを使う
    candidate = model;
    candidate.replace_extension(".mtl");
    if (exists(candidate)) {
        std::ifstream mtl(candidate);
        std::string line;
        while (std::getline(mtl, line)) {
            constexpr const char* kDiffuseMap = "map_Kd ";
            if (line.rfind(kDiffuseMap, 0) == 0) {
                std::string name = line.substr(std::strlen(kDiffuseMap));
                name.erase(name.find_last_not_of(" \r\t") + 1);
                const std::filesystem::path texture = model.parent_path() / std::filesystem::path(std::u8string(name.begin(), name.end()));
                if (exists(texture)) {
                    return AssetPathUtf8(texture);
                }
            }
        }
    }
    // glTF は images の最初の画像を使う（埋め込みのデータURIは使えないので飛ばす）
    if (AssetPathKey(model.extension()) == ".gltf") {
        std::ifstream file(model);
        const nlohmann::json gltf = nlohmann::json::parse(file, nullptr, false);
        if (gltf.is_object() && gltf.contains("images") && gltf["images"].is_array()) {
            for (const nlohmann::json& image : gltf["images"]) {
                const std::string uri = image.value("uri", std::string());
                if (uri.empty() || uri.rfind("data:", 0) == 0) {
                    continue;
                }
                const std::filesystem::path texture = model.parent_path() / std::filesystem::path(std::u8string(uri.begin(), uri.end()));
                if (exists(texture)) {
                    return AssetPathUtf8(texture);
                }
            }
        }
    }
    return kFallbackTexture;
}
using stageEditorUiStyle::kAssetPresets;
} // namespace

/** @brief Hierarchyツリーに1エントリ＋その子を再帰的に描く */
void StageEditor::DrawHierarchyEntry(int index, int depthLevel) {
    if (depthLevel > editorUi::kMaxHierarchyDepth) {
        return;
    } // 循環参照の安全弁

    const ObjectDesc& desc = objects_[index].desc;
    bool sel = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), index)
        != selectedObjectIndices_.end();

    // 種類が一目で分かるようタグを付ける（配置物はタグ無し）
    const char* kindTag = ObjectKind::Of(desc.kind).ListTag();

    // 深さぶんインデントして親子関係を視覚化する
    char label[textBufferCapacity::kLabel];
    std::string indent(static_cast<size_t>(depthLevel) * 2, ' ');
    snprintf(label, sizeof(label), "%s%s%s%s##obj%d",
        indent.c_str(), (depthLevel > 0) ? "└ " : "", kindTag, desc.name.c_str(), index);
    if (ImGui::Selectable(label, sel)) {
        selKind_ = SelKind::kObject;
        selIndex_ = index;
        if (!ImGui::GetIO().KeyCtrl) {
            selectedObjectIndices_.clear();
        }
        auto selected = std::find(selectedObjectIndices_.begin(), selectedObjectIndices_.end(), index);
        if (selected == selectedObjectIndices_.end()) {
            selectedObjectIndices_.push_back(index);
        } else if (ImGui::GetIO().KeyCtrl) {
            selectedObjectIndices_.erase(selected);
        }
    }
    // ダブルクリックでその配置物へカメラを寄せる（画面外の物を探しに行く手間を省く）
    const bool screenSpaceObject = ObjectKind::Of(desc.kind).IsScreenSpace(desc);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !screenSpaceObject) {
        FocusCameraOn(WorldPositionOf(desc));
    }

    // 階層内のドラッグ＆ドロップで親子付け（他の項目へ落とすとその子になる。見た目の位置は変わらない）
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("STAGE_OBJECT", &index, sizeof(int));
        ImGui::Text("%s を親にする物の上へ", desc.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("STAGE_OBJECT")) {
            const int childIndex = *static_cast<const int*>(payload->Data);
            SetParentPreservingWorld(childIndex, index);
        }
        ImGui::EndDragDropTarget();
    }

    // このエントリを親にしている子を直下に描く
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (i != index && objects_[i].desc.parent == desc.name) {
            DrawHierarchyEntry(i, depthLevel + 1);
        }
    }
}

/** @brief 階層パネルの実際の編集内容を描画する（StageEditorHierarchyPanel::Renderへ委譲） */
void StageEditor::RenderHierarchy() {
    StageEditorHierarchyPanel::Render(*this);
}

/** @brief ゲーム画面のプレビューを描き、ビューポートでの入力をエディタへ渡す */
void StageEditor::RenderGameViewport() {
    // 前のフレームの表示位置を消してから、今回のゲームビューの位置と大きさを決め直す
    viewport_.ClearImageRect();
    diagnosticsDraw::SetImageViewport();
    const auto* display = ImGui::GetMainViewport();
    // 大きく表示: 画面全体に縦横比を保って中央へ収め、パネルを出さない
    if (viewportFocusMode_) {
        const float scale = (std::min)(display->Size.x / WinApp::kClientWidth,
            display->Size.y / WinApp::kClientHeight);
        const ImVec2 size(WinApp::kClientWidth * scale, WinApp::kClientHeight * scale);
        const ImVec2 origin(display->Pos.x + (display->Size.x - size.x) * 0.5f,
            display->Pos.y + (display->Size.y - size.y) * 0.5f);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool hovered = mouse.x >= origin.x && mouse.x < origin.x + size.x
            && mouse.y >= origin.y && mouse.y < origin.y + size.y;
        viewport_.SetImageRect(origin.x, origin.y, size.x, size.y, hovered);
        diagnosticsDraw::SetImageViewport(origin, size, ImGui::GetForegroundDrawList());
        return;
    }
    // ゲーム画面の取り込みがまだ無ければ何も出さない
    if (previewTextureId_ == 0) {
        return;
    }

    // 左右のパネルとツールバーに挟まれた中央の領域に毎フレーム固定する（補助パネルを開いている間は、下をその領域に譲る）
    const float columnHeight = display->Size.y - kToolbarHeight;
    const float viewHeight = HasOpenAuxPanel() ? columnHeight * (1.0f - editorUi::kAuxPanelHeightRatio) : columnHeight;
    ImGui::SetNextWindowPos(ImVec2(display->Pos.x + kLeftPanelWidth,
        display->Pos.y + kToolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(
        (std::max)(kContentWindowMinSize.x, display->Size.x - kLeftPanelWidth - kRightPanelWidth),
        (std::max)(kContentWindowMinSize.y, viewHeight)), ImGuiCond_Always);
    // ウィンドウの中に、縦横比を保ったまま入る最大の大きさでゲーム画面を中央に置く
    ImGui::Begin("ゲームビュー", nullptr,
        editorUi::kFixedPanelFlags | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextDisabled("F4で大きく表示");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float ratio = static_cast<float>(WinApp::kClientWidth) / WinApp::kClientHeight;
    const float width = (std::max)(1.0f, (std::min)(available.x, available.y * ratio));
    const ImVec2 size(width, width / ratio);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 origin(cursor.x + (std::max)(0.0f, (available.x - size.x) * 0.5f),
        cursor.y + (std::max)(0.0f, (available.y - size.y) * 0.5f));
    ImGui::SetCursorScreenPos(origin);
    ImGui::Image((ImTextureID)previewTextureId_, size, { previewU0_, previewV0_ }, { previewU1_, previewV1_ });
    // マウス判定とデバッグ描画が、表示した画像の位置に合うよう伝える
    viewport_.SetImageRect(origin.x, origin.y, size.x, size.y, ImGui::IsItemHovered());
    diagnosticsDraw::SetImageViewport(origin, size, ImGui::GetWindowDrawList());
    ImGui::End();
}
/** @brief 中央シーンビューの上部に編集モードと補助パネルの操作を表示する */
void StageEditor::RenderEditorToolbar() {
    // 画面上端に固定した、タイトルバー無しの細長いウィンドウにする
    const auto* display = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(display->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(display->Size.x, kToolbarHeight), ImGuiCond_Always);
    const ImGuiWindowFlags flags = editorUi::kFixedPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("シーンビューツールバー", nullptr, flags);

    // 今のモード・保存・未保存の表示・元に戻す/やり直す
    ImGui::TextColored(kModeLabelColor, playTestMode_ ? "テスト中" : "ステージ編集");
    ImGui::SameLine();
    if (ImGui::Button("保存 (Ctrl+S)")) {
        Save();
    }
    ImGui::SameLine();
    // 保存し忘れに気付けるよう、未保存の変更があれば色を変えて出す
    ImGui::TextColored(dirty_ ? kUnsavedColor : kSavedColor,
        dirty_ ? "未保存" : "保存済み");
    ImGui::SameLine();
    // 戻せる履歴・やり直せる履歴が無い間はボタンを押せなくする
    ImGui::BeginDisabled(!history_.CanUndo());
    if (ImGui::Button("戻す")) {
        Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!history_.CanRedo());
    if (ImGui::Button("やり直す")) {
        Redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // 選択中の変形ツールのボタンだけ色を変えて並べる
    auto toolButton = [&](const char* label, TransformTool tool) {
        const bool active = transformTool_ == tool;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, kActiveToolColor);
        }
        if (ImGui::Button(label)) {
            transformTool_ = tool;
        }
        if (active) {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
    };
    // 変形ツールとスナップはテストプレイ中は触らせない
    ImGui::BeginDisabled(playTestMode_);
    toolButton("移動 W", TransformTool::kMove);
    toolButton("回転 E", TransformTool::kRotate);
    toolButton("拡縮 R", TransformTool::kScale);
    // スナップのオンオフと、揃える間隔
    ImGui::Checkbox("座標を揃える", &snapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kToolbarSnapStepWidth);
    ImGui::DragFloat("##toolbarSnapStep", &snapStep_, editorUi::kDragStepPosition, editorUi::kMinSnapStep, editorUi::kMaxSnapStep, "%.1f");
    ImGui::EndDisabled();
    ImGui::SameLine();
    // 編集とテストプレイの切り替えボタン
    ImGui::PushStyleColor(ImGuiCol_Button, playTestMode_ ? kStopPlayTestColor : kStartPlayTestColor);
    if (ImGui::Button(playTestMode_ ? "編集へ戻る" : "遊んで確認")) {
        SetPlayTestMode(!playTestMode_);
    }
    ImGui::PopStyleColor();
    ImGui::Separator();
    // 2段目: 追加ボタンと、補助パネルを開くメニュー
    ImGui::BeginDisabled(playTestMode_);
    if (ImGui::Button("文章UIを追加")) {
        AddUITextAt(editorUi::kScreenCenterPosition);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // プレイヤー・敵・演出などの数値を、JSONを手で書かずに直す画面を開く
    if (ImGui::Button("調整値")) {
        engine::graphics::JsonConfigEditor::GetInstance()->Toggle();
    }
    ImGui::SameLine();
    // 動き方・見た目・武器・強さを選んで新しい敵の種類を作る画面を開く
    if (ImGui::Button("敵を作る")) {
        showEnemyTypeEditor_ = !showEnemyTypeEditor_;
    }
    ImGui::SameLine();
    if (ImGui::Button("追加機能")) {
        ImGui::OpenPopup("編集追加機能");
    }
    if (ImGui::BeginPopup("編集追加機能")) {
#ifdef _DEBUG
        ImGui::SeparatorText("Debugの開始位置");
        // 開始シーンの一覧は SceneManager の表から読む
        auto* manager = SceneManager::GetInstance();
        const int sceneCount = SceneManager::GetDebugStartSceneCount();
        // 今の開始シーンが一覧の何番目かを探して、初期の選択にする
        int selected = 0;
        for (int i = 0; i < sceneCount; ++i) {
            if (manager->GetDebugStartScene() == SceneManager::GetDebugStartSceneName(i)) {
                selected = i;
            }
        }
        ImGui::SetNextItemWidth(kDebugStartSceneComboWidth);
        const auto labelOf = [](void*, int index) {
            return SceneManager::GetDebugStartSceneLabel(index);
        };
        if (ImGui::Combo("開始画面", &selected, labelOf, nullptr, sceneCount)) {
            // 選んだ時点で設定ファイルへ保存し、結果を右端の案内に出す
            const bool saved = manager->SetDebugStartScene(SceneManager::GetDebugStartSceneName(selected));
            statusMessage_ = saved ? std::string("次回のDebug起動は「") + SceneManager::GetDebugStartSceneLabel(selected) + "」から開始します"
                                   : "Debugの開始位置を保存できませんでした";
            statusTimer_ = StageEditor::kStatusLongSeconds;
        }
        ImGui::TextDisabled("自動保存され、次回のDebug起動に反映します");
        ImGui::Separator();
#endif
        // 補助パネルの表示切り替え（チェックの付いたパネルが開く）
        ImGui::MenuItem("制作・保存前チェック", nullptr, &showWorkflowPanel_);
        ImGui::MenuItem("イベントをつなぐ", nullptr, &showNoCodeEventPanel_);
        ImGui::MenuItem("敵の出現を作る", nullptr, &showWavePanel_);
        ImGui::Separator();
        // 状態の確認と表示の切り替え
        ImGui::MenuItem("フラグを確認", nullptr, &showFlagsPanel_);
        ImGui::MenuItem("グラフを編集", nullptr, &showGraphPanel_);
        ImGui::MenuItem("配置したテキストを表示", nullptr, &showUIText_);
        ImGui::MenuItem("グリッド線を表示", nullptr, &showGrid_);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    // 使い方パネルと、ゲームビューを広く見る切り替え
    if (ImGui::Button("? 操作・使い方")) {
        showEditorHelp_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("広く見る F4")) {
        viewportFocusMode_ = true;
    }

    ImGui::SameLine();
    // 右端には直前の操作結果を出し、無ければ今の状態に合った操作の案内を出す
    if (statusTimer_ > 0.0f) {
        ImGui::TextUnformatted(statusMessage_.c_str());
    } else if (playTestMode_) {
        ImGui::TextUnformatted("ゲーム操作で確認 / 「編集へ戻る」で調整を再開");
    } else if (selKind_ == SelKind::kNone) {
        ImGui::TextUnformatted("素材で配置 / クリックで選択 / 右ボタン+WASDでカメラ / ホイールでズーム");
    } else {
        ImGui::TextUnformatted("つかんで移動 / Fでカメラ / Ctrl+Dで複製 / Ctrl+Zで戻す");
    }
    ImGui::End();
}
/** @brief ゲーム画面を広く確認するための最小操作バーを表示する */
void StageEditor::RenderViewportFocusBar() {
    // ゲーム画面を遮る範囲を抑えつつ、通常レイアウトへ戻る操作だけを残す
    ImGui::SetNextWindowPos(kPlayTestOverlayPos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(kPlayTestOverlayAlpha);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("画面優先モード", nullptr, flags);
    ImGui::TextDisabled("編集状態とギズモを維持してパネルを隠している");
    if (ImGui::Button("編集パネルを表示 (F4)")) {
        viewportFocusMode_ = false;
    }
    ImGui::End();
}

/** @brief 詳細パネルの実際の編集内容を描画する（StageEditorInspectorPanel::Renderへ委譲） */
void StageEditor::RenderInspector() {
    StageEditorInspectorPanel::Render(*this);
}

/** @brief モデル/テクスチャをプリセットから選んで置ける一覧パネル選択中の配置物があればそれに適用、無ければ新規追加する */
void StageEditor::RenderAssetPalette() {
    // 階層パネルの下、左端の残りの高さいっぱいに置く
    const auto* display = ImGui::GetMainViewport();
    const float availableHeight = (std::max)(1.0f, display->Size.y - kToolbarHeight);
    const float hierarchyHeight = availableHeight * editorUi::kHierarchyHeightRatio;
    ImGui::SetNextWindowPos(ImVec2(display->Pos.x, display->Pos.y + kToolbarHeight + hierarchyHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kLeftPanelWidth, availableHeight - hierarchyHeight), ImGuiCond_Always);
    ImGui::Begin("素材を配置", nullptr, editorUi::kFixedPanelFlags);

    // どちらの動作になるかを隠れた自動判定にせず、ラジオボタンで明示的に選ばせる
    // 見た目を差し替えられるのは、装飾の配置物を1つ選んでいる時だけ
    bool hasPropSel = (selKind_ == SelKind::kObject && selIndex_ >= 0
        && selIndex_ < static_cast<int>(objects_.size())
        && ObjectKind::Of(objects_[selIndex_].desc.kind).IsDecoration());

    if (ImGui::RadioButton("新規配置", paletteMode_ == PaletteMode::kPlaceNew)) {
        paletteMode_ = PaletteMode::kPlaceNew;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasPropSel);
    if (ImGui::RadioButton("見た目を変更", paletteMode_ == PaletteMode::kReplaceSelection)) {
        paletteMode_ = PaletteMode::kReplaceSelection;
    }
    ImGui::EndDisabled();
    editorUI::HelpMarker("新規配置: クリックしたプリセットを画面中央に追加します\n選択へ差し替え: 選択中の配置物のモデルを置き換えます（配置物を選択中のみ有効）");

    // 差し替え対象がなくなった時に、意図せず新規配置しないようモードを戻す。
    if (!hasPropSel) {
        paletteMode_ = PaletteMode::kPlaceNew;
    }
    bool applyToSelection = (paletteMode_ == PaletteMode::kReplaceSelection && hasPropSel);
    ImGui::TextDisabled(applyToSelection ? "クリックで選択中の配置物のモデルを差し替え" : "クリックで画面中央に新規配置");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##素材検索", "素材名で検索", paletteSearch_, sizeof(paletteSearch_));
    const bool refresh = ImGui::Button("一覧を更新");
    // Resources を全部たどるのは重いので、最初の1回と更新ボタンを押した時だけにする
    if (refresh || !assetsScanned_) {
        assetsScanned_ = true;
        discoveredAssets_.clear();
        std::unordered_set<std::string> known;
        for (const auto& preset : kAssetPresets) {
            const std::string model = preset.model;
            known.insert(AssetPathKey(std::filesystem::path(std::u8string(model.begin(), model.end()))));
        }
        std::error_code error;
        std::filesystem::recursive_directory_iterator it("Resources",
            std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (it != end) {
            const auto path = it->path();
            if (it->is_regular_file(error) && !error) {
                const std::string extension = AssetPathKey(path.extension());
                if ((extension == ".obj" || extension == ".gltf" || extension == ".glb")
                    && known.insert(AssetPathKey(path)).second) {
                    discoveredAssets_.push_back({ AssetPathUtf8(path.stem()), AssetPathUtf8(path), GuessAssetTexture(path) });
                }
            }
            error.clear();
            it.increment(error);
            if (error) {
                break;
            }
        }
        std::sort(discoveredAssets_.begin(), discoveredAssets_.end(), [](const auto& a, const auto& b) {
            return a.model < b.model;
        });
    }

    // 生成はパレット表示中に1フレーム1件だけ進める
    static ModelThumbnailBaker thumbnailBaker;
    thumbnailBaker.Update();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float frame = ImGui::GetStyle().FramePadding.x;
    const float tileWidth = kAssetTileSize + frame + frame;
    const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (tileWidth + spacing)));
    int visibleCount = 0;
    ImGui::BeginDisabled(playTestMode_);
    /** @brief 検索に一致した素材の画像または名前を描き、従来の配置処理を呼ぶ */
    const auto drawAsset = [&](const char* label, const char* model, const char* texture, int& column) {
        if (paletteSearch_[0] != '\0' && std::string(label).find(paletteSearch_) == std::string::npos) {
            return;
        }
        if (column++ % columns != 0) {
            ImGui::SameLine();
        }
        ++visibleCount;
        ImGui::PushID(model);
        const uint64_t thumbnail = thumbnailBaker.RequestThumbnail(model, texture);
        const bool clicked = thumbnail != 0
            ? ImGui::ImageButton("##thumbnail", (ImTextureID)thumbnail, ImVec2(kAssetTileSize, kAssetTileSize))
            : ImGui::Button(label, ImVec2(tileWidth, kAssetTileSize + ImGui::GetStyle().FramePadding.y * 2.0f));
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(label);
            ImGui::TextUnformatted(model);
            ImGui::EndTooltip();
        }
        ImGui::PopID();
        if (clicked) {
            if (applyToSelection) {
                RecordUndoSnapshotNow();
                ObjectEntry& entry = objects_[selIndex_];
                entry.desc.model = model;
                entry.desc.texture = texture;
                RegenerateInstances(entry);
            } else {
                AddPropAtScreenCenter(model, texture);
            }
        }
    };
    ImGui::SeparatorText("プリセット");
    int column = 0;
    for (const auto& preset : kAssetPresets) {
        drawAsset(preset.label, preset.model, preset.texture, column);
    }
    ImGui::SeparatorText("見つけた素材");
    column = 0;
    for (const auto& asset : discoveredAssets_) {
        drawAsset(asset.label.c_str(), asset.model.c_str(), asset.texture.c_str(), column);
    }
    ImGui::EndDisabled();
    if (visibleCount == 0) {
        ImGui::TextWrapped("一致する素材がありません。検索語を短くしてみてください。");
    }

    ImGui::End();
}

/** @brief 選択中の配置物を名前付きプレハブへ保存する */
void StageEditor::SaveSelectedPrefab() {
    if (selKind_ != SelKind::kObject || selIndex_ < 0 || selIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }
    const std::string path = StageEditorPrefabService::Save(prefabName_, objects_[selIndex_].desc);
    statusMessage_ = "プレハブを保存しました: " + path;
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

/** @brief 名前付きプレハブを画面中央へ生成する */
void StageEditor::InstantiatePrefab() {
    const std::string path = StageEditorPrefabService::MakePath(prefabName_);
    std::vector<ObjectDesc> prefabObjects = StageEditorPrefabService::Load(prefabName_);
    if (prefabObjects.empty()) {
        statusMessage_ = "プレハブが見つかりません: " + path;
        statusTimer_ = StageEditor::kStatusShortSeconds;
        return;
    }

    RecordUndoSnapshotNow();
    Vector3 center = playerSpawn_;
    center = ViewCenterOnGround();
    for (ObjectDesc desc : prefabObjects) {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        entry.desc.name = "prefab_" + std::to_string(nextSerial_++);
        entry.desc.parent.clear();
        entry.desc.position = center + entry.desc.position;
        objects_.push_back(std::move(entry));
        RegenerateInstances(objects_.back());
    }
    SelectLastObject();
    statusMessage_ = "プレハブを配置しました";
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

#else

/** @brief プレハブ、検証、自動保存、編集とテストの切り替えをまとめて表示する */
void StageEditor::RenderWorkflowPanel() { }
/** @brief 右クリックメニューを描く（RenderEditorPanelsから毎フレーム呼ぶ） */
void StageEditor::RenderViewportContextMenu() { }
/** @brief 配置・接続・到達性の問題を解析して一覧表示する */
void StageEditor::RenderStageAnalysisPanel() { }
/** @brief 最後に保存した状態との差分を一覧表示する */
void StageEditor::RenderDiffPanel() { }
/** @brief 制作手順と確認項目をエディタ内に表示する */
void StageEditor::RenderEditorHelpPanel() { }
/** @brief トリガーと配置対象を選ぶだけでイベント接続を構築する */
void StageEditor::RenderNoCodeEventPanel() { }
/** @brief レベルに紐付ける常駐グラフとフラグ起動グラフの一覧を編集する */
void StageEditor::RenderGraphPanel() { }
/** @brief 敵Wave用のSpawnPoint群を表形式の設定から生成する */
void StageEditor::RenderWavePanel() { }
/** @brief 階層パネルの実際の編集内容を描画する（StageEditorHierarchyPanel::Renderへ委譲） */
void StageEditor::RenderHierarchy() { }
/** @brief 中央シーンビューの上部に編集モードと補助パネルの操作を表示する */
void StageEditor::RenderEditorToolbar() { }
/** @brief ゲーム画面を広く確認するための最小操作バーを表示する */
void StageEditor::RenderViewportFocusBar() { }
/** @brief ImGui を使わない構成では何もしない */
void StageEditor::RenderGameViewport() { }
/** @brief 詳細パネルの実際の編集内容を描画する（StageEditorInspectorPanel::Renderへ委譲） */
void StageEditor::RenderInspector() { }
/** @brief モデル/テクスチャをプリセットから選んで置ける一覧パネル選択中の配置物があればそれに適用、無ければ新規追加する */
void StageEditor::RenderAssetPalette() { }

/** @brief Hierarchyツリーに1エントリ＋その子を再帰的に描く */
void StageEditor::DrawHierarchyEntry(int, int) { }

#endif

} // namespace engine::game
