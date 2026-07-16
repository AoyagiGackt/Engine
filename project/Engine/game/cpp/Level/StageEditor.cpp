#include "StageEditor.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "GameFlags.h"
#include "Input.h"
#include "KnightEnemy.h"
#include "Matrix4x4.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "ParticleManager.h"
#include "TimeManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <commdlg.h>
#include <imgui.h>
#pragma comment(lib, "comdlg32.lib")
#endif
using namespace engine::game;
using namespace engine;
using namespace engine::graphics;

#ifdef USE_IMGUI
namespace {
// Windows のファイル選択ダイアログを開き、ユーザーが選んだファイルのパスを返す（キャンセル時は空文字列）
std::string OpenFileDialog(const char* filter, const char* initDir)
{
    char path[MAX_PATH] = { };
    OPENFILENAMEA ofn = { };
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initDir;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        return path;
    }
    return { };
}

// ダイアログが返す絶対パスを、JSON側の表記（"Resources/..."、区切りは/）に正規化する
std::string ToProjectRelativePath(const std::string& absPath)
{
    std::string p = absPath;
    for (char& ch : p) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    size_t pos = p.find("Resources/");
    return (pos != std::string::npos) ? p.substr(pos) : p;
}

// アセットパレットに並べる配置物プリセット（モデル+テクスチャが対応済みの組み合わせのみ収録）
struct AssetPreset {
    const char* label;
    const char* model;
    const char* texture;
};
// Shift+ドラッグのZ移動: マウス垂直1pxあたりの移動量（カメラ距離1.0基準720p想定の見かけ等速係数）
constexpr float kZDragPerPixel = 0.0015f;

constexpr AssetPreset kAssetPresets[] = {
    { "ブロック", "Resources/block/block.obj", "Resources/block/block.png" },
    { "剣（Sword）", "Resources/Knight/OBJ/Sword.obj", "Resources/Knight/OBJ/SwordPalette.png" },
    { "刀（Katana）", "Resources/Knight/OBJ/Katana.obj", "Resources/Knight/OBJ/KatanaPalette.png" },
    { "ナイト像", "Resources/Knight/OBJ/KnightCharacter.obj", "Resources/Knight/OBJ/KnightCharacterPalette.png" },
    { "短剣（Dagger）", "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png" },
    { "大槌（Hammer）", "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png" },
    { "槍（Spear）", "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png" },
    { "大剣（Claymore）", "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png" },
    { "大鎌（Scythe）", "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png" },
    { "両手斧（Axe）", "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png" },
};
} // namespace
#endif

StageEditor::StageEditor() = default;

void StageEditor::Open(const std::string& levelPath, ModelCommon* modelCommon, Camera* camera)
{
    levelPath_ = levelPath;
    modelCommon_ = modelCommon;
    camera_ = camera;

    LevelData data = LevelLoader::Load(levelPath);
    playerSpawn_ = data.playerSpawn;
    enemySpawn_ = data.enemySpawn;

    // 破棄されるenemy_basic配置分をEnemyRegistryから外してからclearする（ダングリングポインタ防止）
    for (auto& entry : objects_) {
        UnregisterEnemyEntity(entry);
    }
    objects_.clear();
    modelStorage_.clear();
    modelCache_.clear();
    for (auto& desc : data.objects) {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        objects_.push_back(std::move(entry));
    }

    // EnemyRegistryへの登録キーになるため、実体を生成する前に名前を確定させる
    EnsureUniqueNames(); // 手書きJSON等で名前が無い/重複しているエントリに自動命名する（親子参照に必要）

    for (auto& entry : objects_) {
        RegenerateInstances(entry);
    }

    triggers_.clear();
    for (const auto& desc : data.triggers) {
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
    }

    selKind_ = SelKind::None;
    selIndex_ = -1;
#ifdef USE_IMGUI
    // 別ファイルを開いたら、直前のレベルに対するUndo/Redo履歴は無関係になるため破棄する
    undoStack_.clear();
    redoStack_.clear();
    hasPendingUndo_ = false;
    dirty_ = false;
#endif
    statusMessage_ = "読み込みました: " + levelPath_;
    statusTimer_ = 2.0f;
}

StageEditor::~StageEditor()
{
    for (auto& entry : objects_) {
        UnregisterEnemyEntity(entry);
    }
}

void StageEditor::UnregisterEnemyEntity(const ObjectEntry& entry)
{
    if (entry.desc.kind == "enemy_basic" && entry.enemy) {
        EnemyRegistry::GetInstance()->Unregister(entry.desc.name);
    }
}

void StageEditor::EnsureUniqueNames()
{
    std::set<std::string> used;
    for (auto& entry : objects_) {
        std::string& name = entry.desc.name;
        if (name.empty() || used.count(name)) {
            std::string candidate;
            do {
                candidate = "obj_" + std::to_string(nextSerial_++);
            } while (used.count(candidate));
            name = candidate;
        }
        used.insert(name);
    }
}

Vector3 StageEditor::ParentWorldPositionOf(const ObjectDesc& desc) const
{
    Vector3 pos = { };
    const ObjectDesc* cur = &desc;
    for (int guard = 0; guard < 16 && !cur->parent.empty(); ++guard) {
        const ObjectDesc* parent = nullptr;
        for (const auto& entry : objects_) {
            if (entry.desc.name == cur->parent) {
                parent = &entry.desc;
                break;
            }
        }
        if (!parent) {
            break;
        }
        pos = pos + parent->position;
        cur = parent;
    }
    return pos;
}

Vector3 StageEditor::WorldPositionOf(const ObjectDesc& desc) const
{
    return ParentWorldPositionOf(desc) + desc.position;
}

bool StageEditor::IsDescendantOf(const std::string& candidateName, const std::string& selfName) const
{
    // candidate から親を辿って self に行き着くなら子孫（親に設定すると循環する）
    const ObjectDesc* cur = nullptr;
    for (const auto& entry : objects_) {
        if (entry.desc.name == candidateName) {
            cur = &entry.desc;
            break;
        }
    }
    for (int guard = 0; guard < 16 && cur; ++guard) {
        if (cur->parent.empty()) {
            return false;
        }
        if (cur->parent == selfName) {
            return true;
        }
        const ObjectDesc* next = nullptr;
        for (const auto& entry : objects_) {
            if (entry.desc.name == cur->parent) {
                next = &entry.desc;
                break;
            }
        }
        cur = next;
    }
    return false;
}

void StageEditor::Save()
{
    LevelData data;
    data.playerSpawn = playerSpawn_;
    data.enemySpawn = enemySpawn_;
    for (const auto& entry : objects_) {
        data.objects.push_back(entry.desc);
    }
    for (const auto& trg : triggers_) {
        data.triggers.push_back(trg.GetDesc());
    }

    LevelLoader::Save(levelPath_, data);
#ifdef USE_IMGUI
    dirty_ = false;
#endif
    statusMessage_ = "保存しました: " + levelPath_;
    statusTimer_ = 2.0f;
}

Model* StageEditor::GetOrLoadModel(const std::string& modelPath, const std::string& texPath)
{
    std::string key = modelPath + '|' + texPath;
    auto it = modelCache_.find(key);
    if (it != modelCache_.end()) {
        return it->second;
    }

    auto model = std::make_unique<Model>();
    model->Initialize(modelCommon_, modelPath, texPath);
    Model* ptr = model.get();
    modelStorage_.push_back(std::move(model));
    modelCache_[key] = ptr;
    return ptr;
}

void StageEditor::RegenerateInstances(ObjectEntry& entry)
{
    entry.instances.clear();
    const ObjectDesc& desc = entry.desc;

    if (desc.kind == "enemy_knight") {
        if (!entry.knight) {
            entry.knight = std::make_unique<KnightEnemy>();
            entry.knight->Initialize(modelCommon_, WorldPositionOf(desc));
        }
        return;
    }
    if (desc.kind == "enemy_basic") {
        if (!entry.enemy) {
            entry.enemy = std::make_unique<EnemyEntity>();
            entry.enemy->Initialize(modelCommon_, WorldPositionOf(desc));
            entry.enemy->SetId(desc.name);
            EnemyRegistry::GetInstance()->Register(desc.name, entry.enemy.get());
        }
        return;
    }

    if (desc.model.empty()) {
        return;
    }

    Model* model = GetOrLoadModel(desc.model, desc.texture);

    auto spawnOne = [&](const Vector3& pos) {
        auto obj = std::make_unique<Object3d>();
        obj->Initialize(modelCommon_);
        obj->SetModel(model);
        obj->SetPosition(pos);
        obj->SetRotation(desc.rotation);
        obj->SetScale(desc.scale);
        obj->SetEnableLighting(desc.lighting);
        obj->Update();
        entry.instances.push_back(std::move(obj));
    };

    // 位置はRefreshTransforms()が毎フレーム上書きするため、ここでは個数分の生成だけが本質
    int instanceCount = (desc.type == "row") ? (std::max)(1, desc.count) : 1;
    for (int i = 0; i < instanceCount; ++i) {
        spawnOne(desc.position);
    }
    RefreshTransforms(entry);
}

void StageEditor::RefreshTransforms(ObjectEntry& entry)
{
    const ObjectDesc& desc = entry.desc;
    Vector3 basePos = WorldPositionOf(desc); // 親がいる場合は親のワールド位置＋ローカルオフセット

    for (int i = 0; i < static_cast<int>(entry.instances.size()); ++i) {
        Vector3 pos = basePos;
        if (desc.type == "row") {
            float offset = desc.step * static_cast<float>(i);
            if (desc.axis == 'y') {
                pos.y += offset;
            } else if (desc.axis == 'z') {
                pos.z += offset;
            } else {
                pos.x += offset;
            }
        }
        entry.instances[i]->SetPosition(pos);
        entry.instances[i]->SetRotation(desc.rotation);
        entry.instances[i]->SetScale(desc.scale);
        entry.instances[i]->SetEnableLighting(desc.lighting);
    }
}

void StageEditor::AddPropAtScreenCenter(const std::string& model, const std::string& texture)
{
    // 見えている画面の中央（z=0平面上）に置くカメラをどこへ動かしていても手元に出る
    Vector3 center = playerSpawn_;
    MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);

#ifdef USE_IMGUI
    RecordUndoSnapshotNow();
    center.x = SnapValue(center.x);
    center.y = SnapValue(center.y);
#endif

    ObjectEntry entry;
    entry.desc.name = "obj_" + std::to_string(nextSerial_++);
    entry.desc.kind = "prop";
    entry.desc.type = "static";
    entry.desc.position = center;
    entry.desc.model = model;
    entry.desc.texture = texture;
    objects_.push_back(std::move(entry));
    RegenerateInstances(objects_.back());
    selKind_ = SelKind::Object;
    selIndex_ = static_cast<int>(objects_.size()) - 1;
}

void StageEditor::RegisterExternalEntity(const std::string& name, Vector3* position)
{
    for (auto& ref : externalEntities_) {
        if (ref.name == name) {
            ref.position = position; // 同名なら上書き（Scene再初期化等での再登録に備える）
            return;
        }
    }
    externalEntities_.push_back({ name, position });
}

std::vector<engine::AABB> StageEditor::GetSolidColliders() const
{
    std::vector<engine::AABB> result;
    for (const auto& entry : objects_) {
        if (!entry.desc.solid) {
            continue;
        }
        const ObjectDesc& desc = entry.desc;
        Vector3 basePos = WorldPositionOf(desc);
        Vector3 half = { 0.5f * desc.scale.x, 0.5f * desc.scale.y, 0.5f * desc.scale.z };

        int instanceCount = static_cast<int>(entry.instances.size());
        for (int i = 0; i < instanceCount; ++i) {
            Vector3 pos = basePos;
            if (desc.type == "row") {
                float offset = desc.step * static_cast<float>(i);
                if (desc.axis == 'y') {
                    pos.y += offset;
                } else if (desc.axis == 'z') {
                    pos.z += offset;
                } else {
                    pos.x += offset;
                }
            }
            result.push_back({ { pos.x - half.x, pos.y - half.y, pos.z - half.z },
                { pos.x + half.x, pos.y + half.y, pos.z + half.z } });
        }
    }
    return result;
}

void StageEditor::UpdateObjects(ParticleManager* pm, const Vector3& playerPos)
{
    objectsDrawnThisFrame_ = false; // 毎フレームUpdateObjects()が先に呼ばれるのでここでリセットする

    // 親を動かしたら子も追従するよう、毎フレーム親子チェーンを解決してから反映する
    for (auto& entry : objects_) {
        if (entry.knight || entry.enemy) {
            UpdateEnemyEntry(entry, pm, playerPos);
            continue;
        }
        RefreshTransforms(entry);
        for (auto& obj : entry.instances) {
            obj->Update();
        }
    }
}

void StageEditor::UpdateEnemyEntry(ObjectEntry& entry, ParticleManager* pm, const Vector3& playerPos)
{
    Vector3 worldPos = WorldPositionOf(entry.desc);
    if (entry.knight) {
        if (visible_) {
            // 編集中はAI/重力を進めず、desc.positionへドラッグされた位置だけ反映する
            entry.knight->GetPositionRef() = worldPos;
            entry.knight->RefreshVisualTransforms();
        } else {
            entry.knight->Update(pm, playerPos);
            // Inspector表示・親子追従の基準にするため現在地を書き戻す（JSON保存はしない）
            entry.desc.position = entry.knight->GetPosition();
        }
    } else if (entry.enemy) {
        if (visible_) {
            entry.enemy->GetPositionRef() = worldPos;
            entry.enemy->RefreshVisualTransforms();
        } else {
            entry.enemy->Update();
            entry.desc.position = entry.enemy->GetPosition();
        }
    }
}

void StageEditor::DrawObjects()
{
    // BaseScene::Render()がシーンのDraw()の直後に自動で呼ぶため自己完結させる：
    // 呼び出し側が既にモデル用PSO/ルートシグネチャを設定済みである前提を置かず、ここで自分で設定する
    // （その代わり配置物は毎フレーム最後に上乗せ描画される＝シャドウ/ポストエフェクトの対象外になる）
    // Scene::Draw()内でHUDより前に自分で呼んだ場合は、その旨をフラグで記録する
    // （BaseScene::Render()側の自動呼び出しを止め、配置ブロックがHUDテキストの上に重なるのを防ぐ）
    objectsDrawnThisFrame_ = true;

    if (!modelCommon_ || objects_.empty()) {
        return;
    }
    modelCommon_->CommonDrawSettings();

    for (auto& entry : objects_) {
        for (auto& obj : entry.instances) {
            obj->Draw();
        }
        if (entry.knight) {
            entry.knight->Draw();
        }
        if (entry.enemy) {
            entry.enemy->Draw();
        }
    }
}

std::vector<KnightEnemy*> StageEditor::GetKnights()
{
    std::vector<KnightEnemy*> result;
    for (auto& entry : objects_) {
        if (entry.knight) {
            result.push_back(entry.knight.get());
        }
    }
    return result;
}

void StageEditor::Update(Input* input, const Vector3& playerPos)
{
    // トリガー判定はエディタの表示状態に関係なく常に行う（普段のプレイ中でも成立させるため）
    for (auto& trg : triggers_) {
        trg.Update(playerPos);
    }

#ifdef USE_IMGUI
    bool wasVisible = visible_;
    if (input && input->TriggerKey(DIK_F2)) {
        visible_ = !visible_;
    }
    if (visible_ != wasVisible) {
        if (visible_) {
            savedTimeScale_ = TimeManager::GetInstance()->GetTimeScale();
            TimeManager::GetInstance()->SetTimeScale(0.0f);
        } else {
            TimeManager::GetInstance()->SetTimeScale(savedTimeScale_);
        }
    }
    if (!visible_) {
        return;
    }

    const float realDt = ImGui::GetIO().DeltaTime;
    if (statusTimer_ > 0.0f) {
        statusTimer_ -= realDt;
    }

    UpdateFreeCamera(input, realDt);

    if (camera_) {
        DebugDraw::SetCamera(camera_->GetViewProjectionMatrix(),
            static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight));
    }

    UpdateViewportInteraction();

    // キーボードショートカット（テキスト入力欄にフォーカスがある間はImGui自身の入力欄内編集に譲る）
    {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
                Undo();
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
                Redo();
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                Save();
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
                DuplicateSelected();
            } else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                DeleteSelected();
            }
        }
    }

    RenderHierarchy();
    RenderInspector();
    RenderAssetPalette();
    RenderFlagsPanel();
    DrawGizmos();
#else
    (void)input;
#endif
}

#ifdef USE_IMGUI
StageEditor::LevelSnapshot StageEditor::MakeSnapshot() const
{
    LevelSnapshot snap;
    snap.objects.reserve(objects_.size());
    for (const auto& entry : objects_) {
        snap.objects.push_back(entry.desc);
    }
    snap.triggers.reserve(triggers_.size());
    for (const auto& trg : triggers_) {
        snap.triggers.push_back(trg.GetDesc());
    }
    snap.playerSpawn = playerSpawn_;
    snap.enemySpawn = enemySpawn_;
    return snap;
}

void StageEditor::ApplySnapshot(const LevelSnapshot& snap)
{
    // Open()のファイル読み込み抜き版。実体はdescから作り直す
    for (auto& entry : objects_) {
        UnregisterEnemyEntity(entry);
    }
    objects_.clear();
    for (const auto& desc : snap.objects) {
        ObjectEntry entry;
        entry.desc = desc;
        objects_.push_back(std::move(entry));
    }
    for (auto& entry : objects_) {
        RegenerateInstances(entry);
    }

    triggers_.clear();
    for (const auto& desc : snap.triggers) {
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
    }

    playerSpawn_ = snap.playerSpawn;
    enemySpawn_ = snap.enemySpawn;

    selKind_ = SelKind::None;
    selIndex_ = -1;
    viewportDragging_ = false;
}

void StageEditor::PushUndo(LevelSnapshot snapshot)
{
    undoStack_.push_back(std::move(snapshot));
    if (undoStack_.size() > kMaxUndoHistory) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear(); // 新しい操作をしたら、それ以前のRedo履歴は無効にする
    dirty_ = true; // 記録される変更 = 保存すべき変更（全ての編集操作がここを通る）
}

void StageEditor::RecordUndoSnapshotNow()
{
    PushUndo(MakeSnapshot());
}

void StageEditor::BeginUndoCapture()
{
    if (!hasPendingUndo_) {
        pendingUndoSnapshot_ = MakeSnapshot();
        hasPendingUndo_ = true;
        pendingUndoDirtied_ = false;
    }
}

void StageEditor::MarkUndoDirty()
{
    pendingUndoDirtied_ = true;
}

void StageEditor::CommitUndoCapture()
{
    if (hasPendingUndo_) {
        if (pendingUndoDirtied_) {
            PushUndo(std::move(pendingUndoSnapshot_));
        }
        hasPendingUndo_ = false;
        pendingUndoDirtied_ = false;
    }
}

void StageEditor::Undo()
{
    if (undoStack_.empty()) {
        return;
    }
    redoStack_.push_back(MakeSnapshot());
    LevelSnapshot snap = std::move(undoStack_.back());
    undoStack_.pop_back();
    ApplySnapshot(snap);
    dirty_ = true;
    statusMessage_ = "元に戻しました";
    statusTimer_ = 1.5f;
}

void StageEditor::Redo()
{
    if (redoStack_.empty()) {
        return;
    }
    undoStack_.push_back(MakeSnapshot());
    LevelSnapshot snap = std::move(redoStack_.back());
    redoStack_.pop_back();
    ApplySnapshot(snap);
    dirty_ = true;
    statusMessage_ = "やり直しました";
    statusTimer_ = 1.5f;
}

void StageEditor::DeleteSelected()
{
    if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
        RecordUndoSnapshotNow();
        // 子の親参照を外してから消す（子はワールド位置を保ったまま独立させる）
        const std::string deletedName = objects_[selIndex_].desc.name;
        for (auto& other : objects_) {
            if (other.desc.parent == deletedName) {
                other.desc.position = WorldPositionOf(other.desc);
                other.desc.parent.clear();
            }
        }
        UnregisterEnemyEntity(objects_[selIndex_]); // EnemyRegistryに登録済みならダングリング防止に解除する
        objects_.erase(objects_.begin() + selIndex_);
    } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
        RecordUndoSnapshotNow();
        triggers_.erase(triggers_.begin() + selIndex_);
    } else {
        // エンティティ(Player/Enemy等)はエディタが生成したものではないため削除の対象外
        return;
    }
    selKind_ = SelKind::None;
    selIndex_ = -1;
}

void StageEditor::DuplicateSelected()
{
    if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
        RecordUndoSnapshotNow();
        ObjectEntry entry;
        entry.desc = objects_[selIndex_].desc;
        // 名前はEnemyRegistryの登録キーや親子参照のキーにもなるため、必ず新規で振り直す
        entry.desc.name = "obj_" + std::to_string(nextSerial_++);
        entry.desc.position.x += snapEnabled_ ? snapStep_ : 1.0f; // 元と重ならないよう横にずらす
        objects_.push_back(std::move(entry));
        RegenerateInstances(objects_.back());
        selKind_ = SelKind::Object;
        selIndex_ = static_cast<int>(objects_.size()) - 1;
        statusMessage_ = "複製しました";
        statusTimer_ = 1.5f;
    } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
        RecordUndoSnapshotNow();
        TriggerDesc desc = triggers_[selIndex_].GetDesc();
        desc.name = "trigger_" + std::to_string(nextSerial_++);
        desc.position.x += snapEnabled_ ? snapStep_ : 1.0f;
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
        selKind_ = SelKind::Trigger;
        selIndex_ = static_cast<int>(triggers_.size()) - 1;
        statusMessage_ = "複製しました";
        statusTimer_ = 1.5f;
    }
}

float StageEditor::SnapValue(float v) const
{
    if (!snapEnabled_ || snapStep_ <= 0.0f) {
        return v;
    }
    return std::round(v / snapStep_) * snapStep_;
}

void StageEditor::DrawHierarchyEntry(int index, int depthLevel)
{
    if (depthLevel > 8) {
        return;
    } // 循環参照の安全弁

    const ObjectDesc& desc = objects_[index].desc;
    bool sel = (selKind_ == SelKind::Object && selIndex_ == index);

    // 種類が一目で分かるようタグを付ける（配置物はタグ無し）
    const char* kindTag = (desc.kind == "enemy_knight") ? "[ナイト] "
        : (desc.kind == "enemy_basic")                  ? "[エネミー] "
                                                        : "";

    // 深さぶんインデントして親子関係を視覚化する
    char label[128];
    std::string indent(static_cast<size_t>(depthLevel) * 2, ' ');
    snprintf(label, sizeof(label), "%s%s%s%s##obj%d",
        indent.c_str(), (depthLevel > 0) ? "└ " : "", kindTag, desc.name.c_str(), index);
    if (ImGui::Selectable(label, sel)) {
        selKind_ = SelKind::Object;
        selIndex_ = index;
    }

    // このエントリを親にしている子を直下に描く
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (i != index && objects_[i].desc.parent == desc.name) {
            DrawHierarchyEntry(i, depthLevel + 1);
        }
    }
}

void StageEditor::RenderHierarchy()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 480.0f), ImGuiCond_Once);
    ImGui::Begin("ステージエディタ");

    ImGui::TextDisabled("F2: 表示/非表示    WASD/QE: カメラ移動    ホイール: ズーム");
    if (ImGui::CollapsingHeader("使い方")) {
        ImGui::BulletText("WASD: カメラ移動    Q/E・マウスホイール: 奥/手前へズーム");
        ImGui::BulletText("画面上のオブジェクトを左クリック: 選択");
        ImGui::BulletText("そのまま左ドラッグ: つかんで移動（Shift+ドラッグ: 奥行き(Z)移動）");
        ImGui::BulletText("Ctrl+Z: 元に戻す  Ctrl+Y: やり直す  Ctrl+S: 保存");
        ImGui::BulletText("Ctrl+D・複製ボタン: 選択中の物を複製    Deleteキー: 削除");
        ImGui::BulletText("スナップをONにすると、移動・配置・複製の座標が指定間隔の倍数に揃う");
        ImGui::BulletText("[+]ボタン: 配置物/敵(ナイト・汎用エネミー)を選んで画面中央に新規追加");
        ImGui::BulletText("敵は実際にHPを持って湧く本物の敵配置数・種類は自由に増減できる");
        ImGui::BulletText("左下のアセットパレット: モード切替で新規配置/選択物への差し替えを選べる");
        ImGui::BulletText("右の詳細設定で数値・モデル・親子関係を編集");
        ImGui::BulletText("親を設定すると、親を動かしたとき子も一緒に動く");
        ImGui::BulletText("トリガー(水色の球): プレイヤーが入るとフラグON");
        ImGui::BulletText("　フラグはノードエディタ(F1)のGetFlagで参照できる");
        ImGui::BulletText("エンティティ(マゼンタの十字): Player/Enemy等も同様に選択・ドラッグ移動できる");
        ImGui::BulletText("　ただし削除不可・JSONに保存されない（位置はシーン起動時の初期値に戻る）");
        ImGui::BulletText("保存ボタンでJSONへ書き出しゲーム本編に即反映");
    }
    char pathBuf[256];
    strncpy_s(pathBuf, levelPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) {
        levelPath_ = pathBuf;
    }
    if (ImGui::Button("開く", ImVec2(80, 0))) {
        // 未保存の編集がある時は黙って破棄せず、確認モーダルを挟む
        if (dirty_) {
            ImGui::OpenPopup("開くの確認");
        } else {
            Open(levelPath_, modelCommon_, camera_);
        }
    }
    if (EditorUI::ConfirmModal("開くの確認",
            "未保存の変更があります。\n変更を破棄して読み込み直しますか？",
            "破棄して開く")
        == EditorUI::ConfirmResult::Ok) {
        Open(levelPath_, modelCommon_, camera_);
    }
    ImGui::SameLine();
    if (ImGui::Button("保存", ImVec2(80, 0))) {
        Save();
    }
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "未保存");
    }

    ImGui::BeginDisabled(undoStack_.empty());
    if (ImGui::Button("元に戻す", ImVec2(80, 0))) {
        Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(redoStack_.empty());
    if (ImGui::Button("やり直す", ImVec2(80, 0))) {
        Redo();
    }
    ImGui::EndDisabled();

    ImGui::Checkbox("スナップ", &snapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("##snapStep", &snapStep_, 0.1f, 0.1f, 10.0f, "%.1f");
    EditorUI::HelpMarker("ドラッグ移動・新規配置・複製の座標を、この間隔の倍数に揃えます");

    if (statusTimer_ > 0.0f) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    ImGui::Separator();

    char objHeader[48];
    snprintf(objHeader, sizeof(objHeader), "オブジェクト (%d)", static_cast<int>(objects_.size()));
    bool objOpen = ImGui::TreeNodeEx(objHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addObj")) {
        ImGui::OpenPopup("AddObjectPopup");
    }
    if (ImGui::BeginPopup("AddObjectPopup")) {
        // 見えている画面の中央（z=0平面上）に置くカメラをどこへ動かしていても手元に出る
        Vector3 center = playerSpawn_;
        MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);

        auto addEnemyEntry = [&](const std::string& namePrefix, const std::string& kind) {
            RecordUndoSnapshotNow();
            ObjectEntry entry;
            entry.desc.name = namePrefix + "_" + std::to_string(nextSerial_++);
            entry.desc.kind = kind;
            entry.desc.position = center;
            entry.desc.position.x = SnapValue(entry.desc.position.x);
            entry.desc.position.y = SnapValue(entry.desc.position.y);
            objects_.push_back(std::move(entry));
            RegenerateInstances(objects_.back());
            selKind_ = SelKind::Object;
            selIndex_ = static_cast<int>(objects_.size()) - 1;
        };

        if (ImGui::MenuItem("配置物（ブロック）")) {
            AddPropAtScreenCenter("Resources/block/block.obj", "Resources/block/block.png");
        }
        if (ImGui::MenuItem("敵：ナイト")) {
            addEnemyEntry("knight", "enemy_knight");
        }
        if (ImGui::MenuItem("敵：汎用エネミー")) {
            addEnemyEntry("enemy", "enemy_basic");
        }
        ImGui::EndPopup();
    }
    if (objOpen) {
        // 親を持たない（または親が見つからない）ルートから再帰的にツリー表示する
        auto parentExists = [&](const std::string& parentName) {
            if (parentName.empty()) {
                return false;
            }
            for (const auto& e : objects_) {
                if (e.desc.name == parentName) {
                    return true;
                }
            }
            return false;
        };
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            if (!parentExists(objects_[i].desc.parent)) {
                DrawHierarchyEntry(i, 0);
            }
        }
        ImGui::TreePop();
    }

    if (!externalEntities_.empty()) {
        char entHeader[48];
        snprintf(entHeader, sizeof(entHeader), "エンティティ (%d)", static_cast<int>(externalEntities_.size()));
        bool entOpen = ImGui::TreeNodeEx(entHeader, ImGuiTreeNodeFlags_DefaultOpen);
        if (entOpen) {
            for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
                bool sel = (selKind_ == SelKind::External && selIndex_ == i);
                char label[96];
                snprintf(label, sizeof(label), "  %s##ent%d", externalEntities_[i].name.c_str(), i);
                if (ImGui::Selectable(label, sel)) {
                    selKind_ = SelKind::External;
                    selIndex_ = i;
                }
            }
            ImGui::TreePop();
        }
    }

    char trgHeader[48];
    snprintf(trgHeader, sizeof(trgHeader), "トリガー (%d)", static_cast<int>(triggers_.size()));
    bool trgOpen = ImGui::TreeNodeEx(trgHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addTrg")) {
        RecordUndoSnapshotNow();
        TriggerDesc desc;
        desc.name = "trigger_" + std::to_string(nextSerial_++);
        desc.position = playerSpawn_;
        desc.flag = desc.name;
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
        selKind_ = SelKind::Trigger;
        selIndex_ = static_cast<int>(triggers_.size()) - 1;
    }
    if (trgOpen) {
        for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
            bool sel = (selKind_ == SelKind::Trigger && selIndex_ == i);
            const TriggerDesc& d = triggers_[i].GetDesc();
            char label[96];
            snprintf(label, sizeof(label), "  %s -> %s=%s", d.name.c_str(), d.flag.c_str(), d.value ? "true" : "false");
            if (ImGui::Selectable(label, sel)) {
                selKind_ = SelKind::Trigger;
                selIndex_ = i;
            }
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    // エンティティ(Player/Enemy等)はエディタが生成したものではないため削除・複製の対象外
    bool hasSel = (selKind_ == SelKind::Object || selKind_ == SelKind::Trigger);
    float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(!hasSel);
    if (ImGui::Button("複製 (Ctrl+D)", ImVec2(halfWidth, 0))) {
        DuplicateSelected();
    }
    ImGui::SameLine();
    if (ImGui::Button("選択を削除 (Del)", ImVec2(halfWidth, 0))) {
        DeleteSelected();
    }
    ImGui::EndDisabled();

    ImGui::End();
}

void StageEditor::RenderInspector()
{
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(WinApp::kClientWidth) - 300.0f, 0.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 340.0f), ImGuiCond_Once);
    ImGui::Begin("詳細設定");

    if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
        ObjectEntry& entry = objects_[selIndex_];
        ObjectDesc& desc = entry.desc;
        bool structuralDirty = false;
        bool transformDirty = false;

        // 名前（親子参照のキーなので、変更時は子の親参照も追従させる）
        {
            char nameBuf[96];
            strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
            bool changed = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (changed) {
                std::string newName = nameBuf;
                if (newName != desc.name && !newName.empty()) {
                    MarkUndoDirty();
                    for (auto& other : objects_) {
                        if (other.desc.parent == desc.name) {
                            other.desc.parent = newName;
                        }
                    }
                    desc.name = newName;
                }
            }
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
        }

        // 親の選択（自分自身と自分の子孫は循環になるため選択肢から除外する）
        {
            std::string currentParent = desc.parent.empty() ? "(なし)" : desc.parent;
            if (ImGui::BeginCombo("親", currentParent.c_str())) {
                if (ImGui::Selectable("(なし)", desc.parent.empty())) {
                    if (!desc.parent.empty()) {
                        RecordUndoSnapshotNow();
                        // 親を外しても見た目の位置が変わらないよう、ワールド座標をローカルへ引き継ぐ
                        desc.position = WorldPositionOf(desc);
                        desc.parent.clear();
                    }
                }
                for (const auto& other : objects_) {
                    const std::string& name = other.desc.name;
                    if (name == desc.name || IsDescendantOf(name, desc.name)) {
                        continue;
                    }
                    if (ImGui::Selectable(name.c_str(), desc.parent == name)) {
                        if (desc.parent != name) {
                            RecordUndoSnapshotNow();
                            // 付け替えても見た目の位置が変わらないよう、新しい親基準のローカル座標へ変換する
                            Vector3 world = WorldPositionOf(desc);
                            desc.parent = name;
                            Vector3 parentW = ParentWorldPositionOf(desc);
                            desc.position = Subtract(world, parentW);
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (desc.kind == "prop") {
            char modelBuf[256];
            strncpy_s(modelBuf, desc.model.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            bool modelChanged = ImGui::InputText("モデル", modelBuf, sizeof(modelBuf));
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (modelChanged) {
                MarkUndoDirty();
                desc.model = modelBuf;
            }
            structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("参照##model")) {
                std::string p = OpenFileDialog("OBJファイル\0*.obj\0すべてのファイル\0*.*\0\0", "Resources");
                if (!p.empty()) {
                    RecordUndoSnapshotNow();
                    desc.model = ToProjectRelativePath(p);
                    structuralDirty = true;
                }
            }

            char texBuf[256];
            strncpy_s(texBuf, desc.texture.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(180.0f);
            bool texChanged = ImGui::InputText("テクスチャ", texBuf, sizeof(texBuf));
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (texChanged) {
                MarkUndoDirty();
                desc.texture = texBuf;
            }
            structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("参照##tex")) {
                std::string p = OpenFileDialog("画像ファイル\0*.png;*.jpg;*.jpeg\0すべてのファイル\0*.*\0\0", "Resources");
                if (!p.empty()) {
                    RecordUndoSnapshotNow();
                    desc.texture = ToProjectRelativePath(p);
                    structuralDirty = true;
                }
            }

            const char* kTypes[] = { "static", "row" };
            const char* kTypeLabels[] = { "単体配置(static)", "並べて配置(row)" };
            int typeIdx = (desc.type == "row") ? 1 : 0;
            if (ImGui::Combo("種類", &typeIdx, kTypeLabels, 2)) {
                RecordUndoSnapshotNow();
                desc.type = kTypes[typeIdx];
                structuralDirty = true;
            }
            EditorUI::HelpMarker("単体配置: 1つだけ置く\n並べて配置: 同じモデルを一定間隔で複数並べる（階段や壁に便利）");
        } else {
            const char* kindLabel = (desc.kind == "enemy_knight") ? "ナイト（本物の敵として湧く）" : "汎用エネミー（本物の敵として湧く）";
            ImGui::TextDisabled("%s", kindLabel);
            if (entry.knight) {
                ImGui::Text("HP: %d / %d", entry.knight->GetHp(), entry.knight->GetMaxHp());
            } else if (entry.enemy) {
                ImGui::Text("HP: %d / %d", entry.enemy->GetHp(), entry.enemy->GetMaxHp());
            }
        }

        if (!desc.parent.empty()) {
            ImGui::TextDisabled("※位置は親からの相対値");
        }
        // ドラッグ系ウィジェットは操作開始で変更前を控え、離した時に1回分のUndoとして確定する
        auto captureItemUndo = [&](bool changed) {
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (changed) {
                MarkUndoDirty();
            }
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
            return changed;
        };
        transformDirty |= captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, 0.1f));

        if (desc.kind == "prop") {
            transformDirty |= captureItemUndo(ImGui::DragFloat3("回転", &desc.rotation.x, 0.01f));
            transformDirty |= captureItemUndo(ImGui::DragFloat3("スケール", &desc.scale.x, 0.05f));
            {
                bool lighting = desc.lighting;
                if (ImGui::Checkbox("ライティング", &lighting)) {
                    RecordUndoSnapshotNow();
                    desc.lighting = lighting;
                    transformDirty = true;
                }
                bool solid = desc.solid;
                if (ImGui::Checkbox("当たり判定あり(solid)", &solid)) {
                    RecordUndoSnapshotNow();
                    desc.solid = solid;
                }
                EditorUI::HelpMarker("ONにするとプレイヤーや敵が乗れる・ぶつかる足場になります（オレンジの枠で表示）");
            }

            if (desc.type == "row") {
                const char* kAxes[] = { "x", "y", "z" };
                int axisIdx = (desc.axis == 'y') ? 1 : (desc.axis == 'z') ? 2
                                                                          : 0;
                if (ImGui::Combo("並べる軸", &axisIdx, kAxes, 3)) {
                    RecordUndoSnapshotNow();
                    desc.axis = kAxes[axisIdx][0];
                    structuralDirty = true;
                }
                int count = desc.count;
                if (ImGui::InputInt("個数", &count)) {
                    RecordUndoSnapshotNow();
                    desc.count = (std::max)(1, count);
                    structuralDirty = true;
                }
                transformDirty |= captureItemUndo(ImGui::DragFloat("間隔", &desc.step, 0.05f));
            }
        }

        if (structuralDirty) {
            RegenerateInstances(entry);
        } else if (transformDirty && desc.kind == "prop") {
            // enemy系はStageEditor::UpdateObjects()側が毎フレームdesc.position⇔実体位置を同期するので、ここでは不要
            RefreshTransforms(entry);
        }
    } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
        TriggerDesc& desc = triggers_[selIndex_].GetDesc();

        auto captureItemUndo = [&](bool changed) {
            if (ImGui::IsItemActivated()) {
                BeginUndoCapture();
            }
            if (changed) {
                MarkUndoDirty();
            }
            if (ImGui::IsItemDeactivated()) {
                CommitUndoCapture();
            }
            return changed;
        };

        char nameBuf[96];
        strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
        bool nameChanged = ImGui::InputText("名前", nameBuf, sizeof(nameBuf));
        if (captureItemUndo(nameChanged)) {
            desc.name = nameBuf;
        }

        char flagBuf[96];
        strncpy_s(flagBuf, desc.flag.c_str(), _TRUNCATE);
        bool flagChanged = ImGui::InputText("フラグ名", flagBuf, sizeof(flagBuf));
        if (captureItemUndo(flagChanged)) {
            desc.flag = flagBuf;
        }
        EditorUI::HelpMarker("プレイヤーが球に入ると、この名前のフラグが立ちます。\nノードエディタ(F1)のGetFlagノードで参照できます");

        captureItemUndo(ImGui::DragFloat3("位置", &desc.position.x, 0.1f));
        captureItemUndo(ImGui::DragFloat("半径", &desc.radius, 0.05f, 0.1f, 50.0f));
        {
            bool value = desc.value;
            if (ImGui::Checkbox("進入時に設定する値", &value)) {
                RecordUndoSnapshotNow();
                desc.value = value;
            }
            bool once = desc.once;
            if (ImGui::Checkbox("一度だけ成立させる", &once)) {
                RecordUndoSnapshotNow();
                desc.once = once;
            }
        }
        ImGui::TextDisabled(triggers_[selIndex_].IsInside() ? "プレイヤーは範囲内にいます" : "プレイヤーは範囲外です");
    } else if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())) {
        ExternalEntityRef& ref = externalEntities_[selIndex_];
        ImGui::Text("%s", ref.name.c_str());
        ImGui::TextDisabled("ランタイム実体（JSONには保存されません）");
        if (ref.position) {
            ImGui::DragFloat3("位置", &ref.position->x, 0.1f);
        }
    } else {
        ImGui::TextDisabled("左のステージエディタでオブジェクト/トリガーを選択してください");
    }

    ImGui::End();
}

void StageEditor::RenderAssetPalette()
{
    ImGui::SetNextWindowPos(ImVec2(0.0f, 484.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 240.0f), ImGuiCond_Once);
    ImGui::Begin("アセットパレット");

    // どちらの動作になるかを隠れた自動判定にせず、ラジオボタンで明示的に選ばせる
    bool hasPropSel = (selKind_ == SelKind::Object && selIndex_ >= 0
        && selIndex_ < static_cast<int>(objects_.size())
        && objects_[selIndex_].desc.kind == "prop");

    ImGui::RadioButton("新規配置", &paletteMode_, 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasPropSel);
    ImGui::RadioButton("選択へ差し替え", &paletteMode_, 1);
    ImGui::EndDisabled();
    EditorUI::HelpMarker("新規配置: クリックしたプリセットを画面中央に追加します\n選択へ差し替え: 選択中の配置物のモデルを置き換えます（配置物を選択中のみ有効）");

    bool applyToSelection = (paletteMode_ == 1 && hasPropSel);
    ImGui::TextDisabled(applyToSelection ? "クリックで選択中の配置物のモデルを差し替え" : "クリックで画面中央に新規配置");

    for (const auto& preset : kAssetPresets) {
        if (ImGui::Button(preset.label, ImVec2(-1, 0))) {
            if (applyToSelection) {
                RecordUndoSnapshotNow();
                ObjectEntry& entry = objects_[selIndex_];
                entry.desc.model = preset.model;
                entry.desc.texture = preset.texture;
                RegenerateInstances(entry);
            } else {
                AddPropAtScreenCenter(preset.model, preset.texture);
            }
        }
    }

    ImGui::End();
}

void StageEditor::RenderFlagsPanel()
{
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(WinApp::kClientWidth) - 300.0f, 350.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f), ImGuiCond_Once);
    ImGui::Begin("フラグ一覧");
    for (const auto& [name, value] : GameFlags::GetInstance()->GetAll()) {
        ImGui::TextColored(value ? ImVec4(0.5f, 1.0f, 0.6f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "%s = %s", name.c_str(), value ? "true" : "false");
    }
    ImGui::End();
}

void StageEditor::DrawGizmos()
{
    for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
        const TriggerDesc& d = triggers_[i].GetDesc();
        ImU32 color = triggers_[i].IsInside()                  ? DebugDraw::kColorGreen
            : (selKind_ == SelKind::Trigger && selIndex_ == i) ? DebugDraw::kColorYellow
                                                               : DebugDraw::kColorCyan;
        DebugDraw::DrawSphere({ d.position, d.radius }, color);
        DebugDraw::DrawCross(d.position, 0.3f, color);
    }
    // 全オブジェクトに小さな十字マーカーを出し、どこをクリックすれば掴めるか分かるようにする
    // solid=trueのものは実際の当たり判定AABBをオレンジのワイヤーフレームで重ねて表示する
    // 敵配置（enemy_knight/enemy_basic）は赤い十字にして配置物(白)と一目で区別できるようにする
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        bool sel = (selKind_ == SelKind::Object && selIndex_ == i);
        const ObjectDesc& d = objects_[i].desc;
        bool isEnemy = (d.kind != "prop");
        Vector3 world = WorldPositionOf(d);
        ImU32 baseColor = isEnemy ? DebugDraw::kColorRed : DebugDraw::kColorWhite;
        DebugDraw::DrawCross(world, sel ? 0.6f : (isEnemy ? 0.35f : 0.25f), sel ? DebugDraw::kColorYellow : baseColor);

        if (d.solid) {
            Vector3 half = { 0.5f * d.scale.x, 0.5f * d.scale.y, 0.5f * d.scale.z };
            DebugDraw::DrawAABB({ { world.x - half.x, world.y - half.y, world.z - half.z },
                                    { world.x + half.x, world.y + half.y, world.z + half.z } },
                DebugDraw::kColorOrange);
        }

        // 親子関係を白線で可視化する（親→子）
        const std::string& parentName = objects_[i].desc.parent;
        if (!parentName.empty()) {
            for (const auto& other : objects_) {
                if (other.desc.name == parentName) {
                    DebugDraw::DrawLine(WorldPositionOf(other.desc), world, IM_COL32(255, 255, 255, 90));
                    break;
                }
            }
        }
    }

    // Player/Enemy等のランタイム実体（マゼンタの十字。オブジェクトの白・トリガーのシアンと区別する）
    for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
        const ExternalEntityRef& ref = externalEntities_[i];
        if (!ref.position) {
            continue;
        }
        bool sel = (selKind_ == SelKind::External && selIndex_ == i);
        DebugDraw::DrawCross(*ref.position, sel ? 0.6f : 0.3f, sel ? DebugDraw::kColorYellow : DebugDraw::kColorMagenta);
    }
}

void StageEditor::UpdateFreeCamera(Input* input, float dt)
{
    if (!camera_ || !input) {
        return;
    }
    // ImGuiのテキスト入力欄にフォーカスがある間は、"s"等のタイプ入力がカメラ移動と衝突しないようにする
    if (ImGui::GetIO().WantCaptureKeyboard) {
        return;
    }

    constexpr float kSpeed = 8.0f;
    Vector3& pos = camera_->GetTranslate();
    if (input->PushKey(DIK_A)) {
        pos.x -= kSpeed * dt;
    }
    if (input->PushKey(DIK_D)) {
        pos.x += kSpeed * dt;
    }
    if (input->PushKey(DIK_W)) {
        pos.y += kSpeed * dt;
    }
    if (input->PushKey(DIK_S)) {
        pos.y -= kSpeed * dt;
    }
    if (input->PushKey(DIK_Q)) {
        pos.z -= kSpeed * dt;
    }
    if (input->PushKey(DIK_E)) {
        pos.z += kSpeed * dt;
    }
}

bool StageEditor::MouseToGround(float mouseX, float mouseY, Vector3& outWorld) const
{
    if (!camera_) {
        return false;
    }

    // スクリーン座標→NDC→（逆VP行列で）ワールドのレイに戻し、ゲーム平面(z=0)との交点を取る
    Matrix4x4 inv = Inverse(camera_->GetViewProjectionMatrix());
    float ndcX = (mouseX / static_cast<float>(WinApp::kClientWidth)) * 2.0f - 1.0f;
    float ndcY = 1.0f - (mouseY / static_cast<float>(WinApp::kClientHeight)) * 2.0f;

    auto unproject = [&](float ndcZ) -> Vector3 {
        float x = ndcX * inv.m[0][0] + ndcY * inv.m[1][0] + ndcZ * inv.m[2][0] + inv.m[3][0];
        float y = ndcX * inv.m[0][1] + ndcY * inv.m[1][1] + ndcZ * inv.m[2][1] + inv.m[3][1];
        float z = ndcX * inv.m[0][2] + ndcY * inv.m[1][2] + ndcZ * inv.m[2][2] + inv.m[3][2];
        float w = ndcX * inv.m[0][3] + ndcY * inv.m[1][3] + ndcZ * inv.m[2][3] + inv.m[3][3];
        if (std::abs(w) < 1e-8f) {
            w = 1e-8f;
        }
        return { x / w, y / w, z / w };
    };

    Vector3 nearPt = unproject(0.0f); // DirectXのNDC zは[0,1]
    Vector3 farPt = unproject(1.0f);
    float dz = farPt.z - nearPt.z;
    if (std::abs(dz) < 1e-6f) {
        return false;
    } // レイが平面と平行

    float t = -nearPt.z / dz;
    if (t < 0.0f) {
        return false;
    } // 交点がカメラ後方
    outWorld = { nearPt.x + (farPt.x - nearPt.x) * t, nearPt.y + (farPt.y - nearPt.y) * t, 0.0f };
    return true;
}

void StageEditor::UpdateViewportInteraction()
{
    ImGuiIO& io = ImGui::GetIO();
    // ImGuiパネル上のマウス操作はビューポート操作として扱わない
    if (io.WantCaptureMouse) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (viewportDragging_) {
                CommitUndoCapture(); // パネル上で離した場合もドラッグ分をここで確定する
            }
            viewportDragging_ = false;
        }
        return;
    }

    const ImVec2 m = io.MousePos;

    // マウスホイール: カメラを奥/手前へ移動（Q/Eと同じ軸、手前に回すと近づく）
    if (camera_ && io.MouseWheel != 0.0f) {
        constexpr float kWheelSpeed = 2.0f;
        camera_->GetTranslate().z += io.MouseWheel * kWheelSpeed;
    }

    // 左クリック: 画面上で一番近いオブジェクト/トリガーを選択（40px以内）
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float bestDist = 40.0f;
        SelKind bestKind = SelKind::None;
        int bestIdx = -1;

        auto consider = [&](const Vector3& worldPos, SelKind kind, int index) {
            ImVec2 s;
            if (!DebugDraw::WorldToScreen(worldPos, s)) {
                return;
            }
            float dx = s.x - m.x;
            float dy = s.y - m.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < bestDist) {
                bestDist = dist;
                bestKind = kind;
                bestIdx = index;
            }
        };

        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            consider(WorldPositionOf(objects_[i].desc), SelKind::Object, i);
        }
        for (int i = 0; i < static_cast<int>(triggers_.size()); ++i) {
            consider(triggers_[i].GetDesc().position, SelKind::Trigger, i);
        }
        for (int i = 0; i < static_cast<int>(externalEntities_.size()); ++i) {
            if (externalEntities_[i].position) {
                consider(*externalEntities_[i].position, SelKind::External, i);
            }
        }

        if (bestIdx >= 0) {
            selKind_ = bestKind;
            selIndex_ = bestIdx;
            viewportDragging_ = true;

            // ドラッグ1回ぶんを1つのUndoにまとめるため、変更前をここで控える
            // （エンティティはスナップショット対象外なので、動かしても確定時に捨てられる）
            BeginUndoCapture();

            // Shift+ドラッグ(Z移動)用に、選択物の現在のZをスナップ前の生値として持つ
            if (bestKind == SelKind::Object) {
                dragRawZ_ = objects_[bestIdx].desc.position.z;
            } else if (bestKind == SelKind::Trigger) {
                dragRawZ_ = triggers_[bestIdx].GetDesc().position.z;
            } else if (externalEntities_[bestIdx].position) {
                dragRawZ_ = externalEntities_[bestIdx].position->z;
            }
        }
    }

    // ドラッグ中: 通常はマウス位置（z=0平面上）へXY移動（Zは保持）、Shift中は垂直マウス移動をZ移動にする
    if (viewportDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const bool mouseMoved = (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f);
        if (io.KeyShift) {
            // カメラが遠いほど1pxあたりの移動量を増やし、近くでも遠くでも同じ操作感にする
            float camDist = 10.0f;
            if (camera_) {
                camDist = (std::max)(1.0f, std::abs(camera_->GetTranslate().z));
            }
            dragRawZ_ += -io.MouseDelta.y * camDist * kZDragPerPixel;
            const float snappedZ = SnapValue(dragRawZ_);
            if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
                objects_[selIndex_].desc.position.z = snappedZ;
                if (mouseMoved) {
                    MarkUndoDirty();
                }
            } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
                triggers_[selIndex_].GetDesc().position.z = snappedZ;
                if (mouseMoved) {
                    MarkUndoDirty();
                }
            } else if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())) {
                Vector3* pos = externalEntities_[selIndex_].position;
                if (pos) {
                    pos->z = snappedZ;
                }
            }
        } else {
            Vector3 ground;
            if (MouseToGround(m.x, m.y, ground)) {
                // スナップはワールド座標側で丸めてから、親がいる場合はローカル座標へ逆算する
                const float wx = SnapValue(ground.x);
                const float wy = SnapValue(ground.y);
                if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
                    ObjectDesc& desc = objects_[selIndex_].desc;
                    Vector3 parentW = ParentWorldPositionOf(desc);
                    desc.position.x = wx - parentW.x;
                    desc.position.y = wy - parentW.y;
                    if (mouseMoved) {
                        MarkUndoDirty();
                    }
                } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
                    TriggerDesc& desc = triggers_[selIndex_].GetDesc();
                    desc.position.x = wx;
                    desc.position.y = wy;
                    if (mouseMoved) {
                        MarkUndoDirty();
                    }
                } else if (selKind_ == SelKind::External && selIndex_ >= 0 && selIndex_ < static_cast<int>(externalEntities_.size())) {
                    Vector3* pos = externalEntities_[selIndex_].position;
                    if (pos) {
                        pos->x = wx;
                        pos->y = wy;
                    }
                }
            }
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (viewportDragging_) {
            CommitUndoCapture(); // 実際に動かしていた場合だけ1回分のUndoとして確定する
        }
        viewportDragging_ = false;
    }
}
#else
void StageEditor::RenderHierarchy() { }
void StageEditor::RenderInspector() { }
void StageEditor::RenderAssetPalette() { }
void StageEditor::RenderFlagsPanel() { }
void StageEditor::DrawGizmos() { }
void StageEditor::UpdateFreeCamera(Input*, float) { }
void StageEditor::UpdateViewportInteraction() { }
void StageEditor::DrawHierarchyEntry(int, int) { }
bool StageEditor::MouseToGround(float, float, Vector3&) const { return false; }
#endif
