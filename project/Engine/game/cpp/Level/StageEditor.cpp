#include "StageEditor.h"
#include "Camera.h"
#include "DebugDraw.h"
#include "GameFlags.h"
#include "Input.h"
#include "Matrix4x4.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3d.h"
#include "TimeManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#ifdef USE_IMGUI
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
} // namespace
#endif

void StageEditor::Open(const std::string& levelPath, ModelCommon* modelCommon, Camera* camera)
{
    levelPath_ = levelPath;
    modelCommon_ = modelCommon;
    camera_ = camera;

    LevelData data = LevelLoader::Load(levelPath);
    playerSpawn_ = data.playerSpawn;
    enemySpawn_ = data.enemySpawn;

    objects_.clear();
    modelStorage_.clear();
    modelCache_.clear();
    for (auto& desc : data.objects) {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        objects_.push_back(std::move(entry));
    }
    for (auto& entry : objects_) {
        RegenerateInstances(entry);
    }

    triggers_.clear();
    for (const auto& desc : data.triggers) {
        TriggerVolume trg;
        trg.Init(desc);
        triggers_.push_back(std::move(trg));
    }

    EnsureUniqueNames(); // 手書きJSON等で名前が無い/重複しているエントリに自動命名する（親子参照に必要）

    selKind_ = SelKind::None;
    selIndex_ = -1;
    statusMessage_ = "読み込みました: " + levelPath_;
    statusTimer_ = 2.0f;
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

void StageEditor::UpdateObjects()
{
    // 親を動かしたら子も追従するよう、毎フレーム親子チェーンを解決してから反映する
    for (auto& entry : objects_) {
        RefreshTransforms(entry);
        for (auto& obj : entry.instances) {
            obj->Update();
        }
    }
}

void StageEditor::DrawObjects()
{
    for (auto& entry : objects_) {
        for (auto& obj : entry.instances) {
            obj->Draw();
        }
    }
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

    if (camera_) {
        DebugDraw::SetCamera(camera_->GetViewProjectionMatrix(),
            static_cast<float>(WinApp::kClientWidth), static_cast<float>(WinApp::kClientHeight));
    }

    UpdateFreeCamera(input, realDt);
    UpdateViewportInteraction();

    RenderHierarchy();
    RenderInspector();
    RenderFlagsPanel();
    DrawGizmos();
#else
    (void)input;
#endif
}

#ifdef USE_IMGUI
void StageEditor::DrawHierarchyEntry(int index, int depthLevel)
{
    if (depthLevel > 8) {
        return;
    } // 循環参照の安全弁

    const ObjectDesc& desc = objects_[index].desc;
    bool sel = (selKind_ == SelKind::Object && selIndex_ == index);

    // 深さぶんインデントして親子関係を視覚化する
    char label[128];
    std::string indent(static_cast<size_t>(depthLevel) * 2, ' ');
    snprintf(label, sizeof(label), "%s%s%s##obj%d",
        indent.c_str(), (depthLevel > 0) ? "└ " : "", desc.name.c_str(), index);
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

    ImGui::TextDisabled("F2: 閉じる    WASD/QE: カメラ移動    ホイール: ズーム");
    if (ImGui::CollapsingHeader("使い方")) {
        ImGui::BulletText("WASD: カメラ移動    Q/E・マウスホイール: 奥/手前へズーム");
        ImGui::BulletText("画面上のオブジェクトを左クリック: 選択");
        ImGui::BulletText("そのまま左ドラッグ: つかんで移動");
        ImGui::BulletText("[+]ボタン: 画面中央に新規追加");
        ImGui::BulletText("右の「詳細設定」で数値・モデル・親子関係を編集");
        ImGui::BulletText("親を設定すると、親を動かしたとき子も一緒に動く");
        ImGui::BulletText("トリガー(水色の球): プレイヤーが入るとフラグON");
        ImGui::BulletText("　フラグはノードエディタ(F1)のGetFlagで参照できる");
        ImGui::BulletText("「保存」でJSONへ書き出しゲーム本編に即反映");
    }
    char pathBuf[256];
    strncpy_s(pathBuf, levelPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) {
        levelPath_ = pathBuf;
    }
    if (ImGui::Button("開く", ImVec2(80, 0))) {
        Open(levelPath_, modelCommon_, camera_);
    }
    ImGui::SameLine();
    if (ImGui::Button("保存", ImVec2(80, 0))) {
        Save();
    }
    if (statusTimer_ > 0.0f) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    ImGui::Separator();

    char objHeader[48];
    snprintf(objHeader, sizeof(objHeader), "オブジェクト (%d)", static_cast<int>(objects_.size()));
    bool objOpen = ImGui::TreeNodeEx(objHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addObj")) {
        ObjectEntry entry;
        entry.desc.name = "obj_" + std::to_string(nextSerial_++);
        entry.desc.type = "static";
        entry.desc.model = "Resources/block/block.obj";
        entry.desc.texture = "Resources/block/block.png";
        // 見えている画面の中央（z=0平面上）に置くカメラをどこへ動かしていても手元に出る
        Vector3 center = playerSpawn_;
        MouseToGround(WinApp::kClientWidth * 0.5f, WinApp::kClientHeight * 0.5f, center);
        entry.desc.position = center;
        objects_.push_back(std::move(entry));
        RegenerateInstances(objects_.back());
        selKind_ = SelKind::Object;
        selIndex_ = static_cast<int>(objects_.size()) - 1;
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

    char trgHeader[48];
    snprintf(trgHeader, sizeof(trgHeader), "トリガー (%d)", static_cast<int>(triggers_.size()));
    bool trgOpen = ImGui::TreeNodeEx(trgHeader, ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine();
    if (ImGui::SmallButton("+##addTrg")) {
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
    bool hasSel = (selKind_ != SelKind::None);
    ImGui::BeginDisabled(!hasSel);
    if (ImGui::Button("選択を削除", ImVec2(-1, 0))) {
        if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
            // 子の親参照を外してから消す（子はワールド位置を保ったまま独立させる）
            const std::string deletedName = objects_[selIndex_].desc.name;
            for (auto& other : objects_) {
                if (other.desc.parent == deletedName) {
                    other.desc.position = WorldPositionOf(other.desc);
                    other.desc.parent.clear();
                }
            }
            objects_.erase(objects_.begin() + selIndex_);
        } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
            triggers_.erase(triggers_.begin() + selIndex_);
        }
        selKind_ = SelKind::None;
        selIndex_ = -1;
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
            if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf))) {
                std::string newName = nameBuf;
                if (newName != desc.name && !newName.empty()) {
                    for (auto& other : objects_) {
                        if (other.desc.parent == desc.name) {
                            other.desc.parent = newName;
                        }
                    }
                    desc.name = newName;
                }
            }
        }

        // 親の選択（自分自身と自分の子孫は循環になるため選択肢から除外する）
        {
            std::string currentParent = desc.parent.empty() ? "(なし)" : desc.parent;
            if (ImGui::BeginCombo("親", currentParent.c_str())) {
                if (ImGui::Selectable("(なし)", desc.parent.empty())) {
                    if (!desc.parent.empty()) {
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

        char modelBuf[256];
        strncpy_s(modelBuf, desc.model.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("モデル", modelBuf, sizeof(modelBuf))) {
            desc.model = modelBuf;
        }
        structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (ImGui::SmallButton("参照##model")) {
            std::string p = OpenFileDialog("OBJファイル\0*.obj\0すべてのファイル\0*.*\0\0", "Resources");
            if (!p.empty()) {
                desc.model = ToProjectRelativePath(p);
                structuralDirty = true;
            }
        }

        char texBuf[256];
        strncpy_s(texBuf, desc.texture.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("テクスチャ", texBuf, sizeof(texBuf))) {
            desc.texture = texBuf;
        }
        structuralDirty |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (ImGui::SmallButton("参照##tex")) {
            std::string p = OpenFileDialog("画像ファイル\0*.png;*.jpg;*.jpeg\0すべてのファイル\0*.*\0\0", "Resources");
            if (!p.empty()) {
                desc.texture = ToProjectRelativePath(p);
                structuralDirty = true;
            }
        }

        const char* kTypes[] = { "static", "row" };
        const char* kTypeLabels[] = { "単体配置(static)", "並べて配置(row)" };
        int typeIdx = (desc.type == "row") ? 1 : 0;
        if (ImGui::Combo("種類", &typeIdx, kTypeLabels, 2)) {
            desc.type = kTypes[typeIdx];
            structuralDirty = true;
        }

        if (!desc.parent.empty()) {
            ImGui::TextDisabled("※位置は親からの相対値");
        }
        transformDirty |= ImGui::DragFloat3("位置", &desc.position.x, 0.1f);
        transformDirty |= ImGui::DragFloat3("回転", &desc.rotation.x, 0.01f);
        transformDirty |= ImGui::DragFloat3("スケール", &desc.scale.x, 0.05f);
        transformDirty |= ImGui::Checkbox("ライティング", &desc.lighting);
        ImGui::Checkbox("当たり判定あり(solid)", &desc.solid);

        if (desc.type == "row") {
            const char* kAxes[] = { "x", "y", "z" };
            int axisIdx = (desc.axis == 'y') ? 1 : (desc.axis == 'z') ? 2
                                                                      : 0;
            if (ImGui::Combo("並べる軸", &axisIdx, kAxes, 3)) {
                desc.axis = kAxes[axisIdx][0];
                structuralDirty = true;
            }
            if (ImGui::InputInt("個数", &desc.count)) {
                desc.count = (std::max)(1, desc.count);
                structuralDirty = true;
            }
            transformDirty |= ImGui::DragFloat("間隔", &desc.step, 0.05f);
        }

        if (structuralDirty) {
            RegenerateInstances(entry);
        } else if (transformDirty) {
            RefreshTransforms(entry);
        }
    } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
        TriggerDesc& desc = triggers_[selIndex_].GetDesc();

        char nameBuf[96];
        strncpy_s(nameBuf, desc.name.c_str(), _TRUNCATE);
        if (ImGui::InputText("名前", nameBuf, sizeof(nameBuf))) {
            desc.name = nameBuf;
        }

        char flagBuf[96];
        strncpy_s(flagBuf, desc.flag.c_str(), _TRUNCATE);
        if (ImGui::InputText("フラグ名", flagBuf, sizeof(flagBuf))) {
            desc.flag = flagBuf;
        }

        ImGui::DragFloat3("位置", &desc.position.x, 0.1f);
        ImGui::DragFloat("半径", &desc.radius, 0.05f, 0.1f, 50.0f);
        ImGui::Checkbox("進入時に設定する値", &desc.value);
        ImGui::Checkbox("一度だけ成立させる", &desc.once);
        ImGui::TextDisabled(triggers_[selIndex_].IsInside() ? "プレイヤーは範囲内にいます" : "プレイヤーは範囲外です");
    } else {
        ImGui::TextDisabled("左のステージエディタでオブジェクト/トリガーを選択してください");
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
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        bool sel = (selKind_ == SelKind::Object && selIndex_ == i);
        const ObjectDesc& d = objects_[i].desc;
        Vector3 world = WorldPositionOf(d);
        DebugDraw::DrawCross(world, sel ? 0.6f : 0.25f, sel ? DebugDraw::kColorYellow : DebugDraw::kColorWhite);

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

        if (bestIdx >= 0) {
            selKind_ = bestKind;
            selIndex_ = bestIdx;
            viewportDragging_ = true;
        }
    }

    // ドラッグ中: 選択物をマウス位置（z=0平面上）へ移動（zは元の値を保持）
    if (viewportDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        Vector3 ground;
        if (MouseToGround(m.x, m.y, ground)) {
            if (selKind_ == SelKind::Object && selIndex_ >= 0 && selIndex_ < static_cast<int>(objects_.size())) {
                ObjectDesc& desc = objects_[selIndex_].desc;
                Vector3 parentW = ParentWorldPositionOf(desc);
                desc.position.x = ground.x - parentW.x; // 親がいる場合はローカル座標へ逆算する
                desc.position.y = ground.y - parentW.y;
            } else if (selKind_ == SelKind::Trigger && selIndex_ >= 0 && selIndex_ < static_cast<int>(triggers_.size())) {
                TriggerDesc& desc = triggers_[selIndex_].GetDesc();
                desc.position.x = ground.x;
                desc.position.y = ground.y;
            }
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        viewportDragging_ = false;
    }
}
#else
void StageEditor::RenderHierarchy() { }
void StageEditor::RenderInspector() { }
void StageEditor::RenderFlagsPanel() { }
void StageEditor::DrawGizmos() { }
void StageEditor::UpdateFreeCamera(Input*, float) { }
void StageEditor::UpdateViewportInteraction() { }
void StageEditor::DrawHierarchyEntry(int, int) { }
bool StageEditor::MouseToGround(float, float, Vector3&) const { return false; }
#endif
