/**
 * @file EnemyTypeEditor.cpp
 * @brief 「敵を作る」画面の一覧・設定欄・新しい見た目の作成・素材の自動検出を実装するファイル
 */
#include "EnemyTypeEditor.h"
#ifdef USE_IMGUI
#include "EditorUI.h"
#include "EnemyTuning.h"
#include "GameConstants.h"
#include "JsonHelper.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <filesystem>
#include <imgui.h>

namespace engine::game {
namespace {
namespace editorUI = engine::graphics::editorUI;
constexpr const char* kEnemyParamsPath = "Resources/Config/enemy_params.json";
constexpr const char* kAssetRoot = "Resources";
constexpr const char* kKnightLook = "騎士";
constexpr float kTypeListWidth = 200.0f;
constexpr float kThumbnailSize = 96.0f;
constexpr float kMinStatScale = 0.1f;
constexpr float kMaxStatScale = 10.0f;
constexpr float kStatDragStep = 0.01f;
constexpr float kLookScaleDragStep = 0.005f;
constexpr float kMinLookScale = 0.01f;
constexpr float kMaxLookScale = 10.0f;
constexpr float kYawDragStep = 1.0f;
constexpr float kMaxYawDegrees = 180.0f;
constexpr size_t kTextCapacity = 128;
constexpr ImVec4 kDirtyColor = { 1.0f, 0.75f, 0.2f, 1.0f };

// 動き方（EnemyEntity の性格名）と表示名
constexpr std::array<const char*, 3> kBehaviorValues = { "basic", "flying", "healer" };
constexpr std::array<const char*, 3> kBehaviorLabels = { "歩く", "飛ぶ", "回復役" };
// 奪わせる武器（ObjectKindMarkers の「奪取可能な武器」と同じ並び。先頭は持たない）
constexpr std::array<const char*, 9> kWeaponValues = { "", "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe" };
constexpr std::array<const char*, 9> kWeaponLabels = { "持たない", "剣", "槍", "ハンマー", "短剣", "ボール", "大剣", "鎌", "斧" };

/** @brief パスをUTF-8の文字列にする（日本語のフォルダ名でも化けないように） */
std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

/** @brief 拡張子を小文字で返す */
std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = ToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension;
}

/** @brief 固定の選択肢から1つ選ぶ（values と labels は同じ並び） */
template <size_t N>
bool ComboFromArray(const char* label, std::string& value, const std::array<const char*, N>& values, const std::array<const char*, N>& labels) {
    size_t index = 0;
    for (size_t i = 0; i < N; ++i) {
        if (value == values[i]) {
            index = i;
            break;
        }
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, labels[index])) {
        for (size_t i = 0; i < N; ++i) {
            if (ImGui::Selectable(labels[i], i == index)) {
                value = values[i];
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/** @brief ファイルの候補から1つ選ぶ（検索欄付き） */
bool ComboFromFiles(const char* label, std::string& value, const std::vector<std::string>& files, char* filter, size_t filterSize) {
    bool changed = false;
    if (ImGui::BeginCombo(label, value.empty() ? "（未選択）" : value.c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "絞り込み", filter, filterSize);
        for (const std::string& file : files) {
            if (filter[0] != '\0' && file.find(filter) == std::string::npos) {
                continue;
            }
            if (ImGui::Selectable(file.c_str(), file == value)) {
                value = file;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/** @brief std::string を入力欄で編集する */
bool InputText(const char* label, std::string& value) {
    std::array<char, kTextCapacity> buffer {};
    value.copy(buffer.data(), buffer.size() - 1);
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
        return false;
    }
    value = buffer.data();
    return true;
}
} // namespace

/** @brief 開いている間だけ画面を描く */
void EnemyTypeEditor::Draw(bool* open, const PlaceCallback& place) {
    if (open == nullptr || !*open) {
        return;
    }
    thumbnails_.Update();
    // Resources を全部たどるのは重いので、最初の1回と更新ボタンを押した時だけにする
    if (!assetsScanned_) {
        ScanAssets();
        assetsScanned_ = true;
    }

    // ステージエディタの補助パネル領域のタブとして出す（選ばれていない間は中身を作らない）
    if (!ImGui::BeginTabItem("敵を作る", open)) {
        return;
    }
    auto& types = EnemyTypeLibrary::GetInstance()->MutableTypes();
    // 保存・元に戻すと状態の表示
    ImGui::BeginDisabled(!dirty_);
    if (ImGui::Button("保存")) {
        EnemyTypeLibrary::GetInstance()->Save();
        dirty_ = false;
        status_ = "保存しました（Resources/Config/enemy_types.json）";
    }
    ImGui::SameLine();
    if (ImGui::Button("元に戻す")) {
        EnemyTypeLibrary::GetInstance()->Reload();
        dirty_ = false;
        status_.clear();
    }
    ImGui::EndDisabled();
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(kDirtyColor, "未保存");
    }
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status_.c_str());
    }
    ImGui::Separator();

    // 元に戻すで数が減っても範囲外を指さないよう、一覧を描く前に選択を収める
    selected_ = std::clamp(selected_, 0, (std::max)(static_cast<int>(types.size()) - 1, 0));
    DrawTypeList();
    ImGui::SameLine();
    ImGui::BeginChild("##typeFields", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    // 一覧で削除した直後も範囲内にする
    selected_ = std::clamp(selected_, 0, (std::max)(static_cast<int>(types.size()) - 1, 0));
    if (types.empty()) {
        ImGui::TextWrapped("まだ敵の種類がありません。左の「新しく作る」から作ってください。");
    } else {
        DrawTypeFields(types[selected_], place);
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
}

/** @brief 左側の種類の一覧と、新規・複製・削除のボタンを描く */
void EnemyTypeEditor::DrawTypeList() {
    auto& types = EnemyTypeLibrary::GetInstance()->MutableTypes();
    ImGui::BeginChild("##typeList", ImVec2(kTypeListWidth, 0.0f), ImGuiChildFlags_Borders);
    if (ImGui::Button("新しく作る")) {
        EnemyTypeDef type;
        type.id = MakeUniqueId();
        type.label = "新しい敵";
        types.push_back(std::move(type));
        selected_ = static_cast<int>(types.size()) - 1;
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(types.empty());
    if (ImGui::Button("複製")) {
        EnemyTypeDef copy = types[selected_];
        copy.id = MakeUniqueId();
        copy.label += " のコピー";
        types.push_back(std::move(copy));
        selected_ = static_cast<int>(types.size()) - 1;
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("削除")) {
        types.erase(types.begin() + selected_);
        selected_ = (std::max)(selected_ - 1, 0);
        dirty_ = true;
    }
    ImGui::EndDisabled();
    editorUI::HelpMarker("消しても、もうステージに置いた敵はそのまま残ります（置いた時に値を写しているため）");
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(types.size()); ++i) {
        const std::string caption = (types[i].label.empty() ? types[i].id : types[i].label) + "##" + types[i].id;
        if (ImGui::Selectable(caption.c_str(), i == selected_)) {
            selected_ = i;
        }
    }
    ImGui::EndChild();
}

/** @brief 選んだ種類の設定欄を描く */
void EnemyTypeEditor::DrawTypeFields(EnemyTypeDef& type, const PlaceCallback& place) {
    ImGui::PushID(type.id.c_str());
    dirty_ |= InputText("名前", type.label);
    dirty_ |= InputText("保存用の名前", type.id);
    editorUI::HelpMarker("ステージのデータに残る名前です（英数字にしてください）");
    dirty_ |= ComboFromArray("動き方", type.behavior, kBehaviorValues, kBehaviorLabels);
    ImGui::Separator();
    DrawLookFields(type);
    ImGui::Separator();
    dirty_ |= ComboFromArray("奪える武器", type.weapon, kWeaponValues, kWeaponLabels);
    editorUI::HelpMarker("武器を持たせると、倒した後にその武器を奪えます。飛ぶ敵には持たせられません");
    if (type.behavior == "flying" && !type.weapon.empty()) {
        ImGui::TextColored(kDirtyColor, "飛ぶ敵は武器を持てません（ステージの検査で警告が出ます）");
    }
    dirty_ |= ImGui::DragFloat("HPの倍率", &type.hpScale, kStatDragStep, kMinStatScale, kMaxStatScale);
    dirty_ |= ImGui::DragFloat("攻撃力の倍率", &type.damageScale, kStatDragStep, kMinStatScale, kMaxStatScale);
    dirty_ |= ImGui::DragFloat("速さの倍率", &type.speedScale, kStatDragStep, kMinStatScale, kMaxStatScale);
    ImGui::Separator();
    if (ImGui::Button("ステージに置く") && place) {
        place(type);
    }
    editorUI::HelpMarker("画面の真ん中にこの敵を置きます。置いた後もステージエディタの設定欄で個別に直せます");
    ImGui::PopID();
}

/** @brief 見た目の選択欄（サムネイル付き）と、新しい見た目を作る欄を描く */
void EnemyTypeEditor::DrawLookFields(EnemyTypeDef& type) {
    // 見た目は enemy_params.json の visuals から選ぶ（空は騎士）
    const std::vector<std::string> looks = EnemyTuning::GetInstance()->VisualNames();
    if (ImGui::BeginCombo("見た目", type.look.empty() ? kKnightLook : type.look.c_str())) {
        if (ImGui::Selectable(kKnightLook, type.look.empty())) {
            type.look.clear();
            dirty_ = true;
        }
        for (const std::string& look : looks) {
            if (ImGui::Selectable(look.c_str(), look == type.look)) {
                type.look = look;
                dirty_ = true;
            }
        }
        ImGui::EndCombo();
    }
    if (const EnemyVisualTuning* visual = EnemyTuning::GetInstance()->FindVisual(type.look)) {
        DrawThumbnail(visual->model, visual->texture);
    } else {
        ImGui::TextDisabled("騎士の見た目（武器を持てる、通常の敵）");
    }
    DrawNewLookFields(type);
}

/** @brief 新しい見た目を作る欄を描く（モデルとテクスチャは Resources から自動で集めた候補から選ぶ） */
void EnemyTypeEditor::DrawNewLookFields(EnemyTypeDef& type) {
    if (!ImGui::CollapsingHeader("新しい見た目を作る")) {
        return;
    }
    static char sModelFilter[kTextCapacity] = {};
    static char sTextureFilter[kTextCapacity] = {};
    ImGui::InputText("見た目の名前", newLook_.name, sizeof(newLook_.name));
    ComboFromFiles("モデル", newLook_.model, models_, sModelFilter, sizeof(sModelFilter));
    ComboFromFiles("テクスチャ", newLook_.texture, textures_, sTextureFilter, sizeof(sTextureFilter));
    ImGui::DragFloat("大きさ", &newLook_.scale, kLookScaleDragStep, kMinLookScale, kMaxLookScale);
    ImGui::DragFloat("向きの補正（度）", &newLook_.yawDegrees, kYawDragStep, -kMaxYawDegrees, kMaxYawDegrees);
    if (!newLook_.model.empty()) {
        DrawThumbnail(newLook_.model, newLook_.texture);
    }
    if (ImGui::Button("一覧を更新")) {
        ScanAssets();
    }
    editorUI::HelpMarker("Resources フォルダにモデルや画像を置くと、数秒で候補に出ます");
    ImGui::SameLine();
    const bool ready = newLook_.name[0] != '\0' && !newLook_.model.empty() && !newLook_.texture.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("この見た目を追加して使う") && SaveNewLook(newLook_)) {
        type.look = newLook_.name;
        dirty_ = true;
        status_ = std::string("見た目「") + newLook_.name + "」を追加しました（enemy_params.json）";
        newLook_ = NewLook {};
    }
    ImGui::EndDisabled();
    if (!ready) {
        ImGui::TextDisabled("名前・モデル・テクスチャを全部選ぶと追加できます");
    }
}

/** @brief 見た目のサムネイルを出す（まだ作っていなければ予約して何も出さない） */
void EnemyTypeEditor::DrawThumbnail(const std::string& model, const std::string& texture) {
    const uint64_t thumbnail = thumbnails_.RequestThumbnail(model, texture);
    if (thumbnail != 0) {
        ImGui::Image((ImTextureID)thumbnail, ImVec2(kThumbnailSize, kThumbnailSize));
    } else {
        ImGui::TextDisabled("（サムネイルを作っています）");
    }
}

/** @brief Resources からモデルとテクスチャの候補を集め直す */
void EnemyTypeEditor::ScanAssets() {
    models_.clear();
    textures_.clear();
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(kAssetRoot, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && it != end) {
        if (it->is_regular_file(error)) {
            const std::string extension = LowerExtension(it->path());
            if (extension == ".obj" || extension == ".gltf" || extension == ".glb") {
                models_.push_back(ToUtf8(it->path()));
            } else if (extension == ".png" || extension == ".jpg") {
                textures_.push_back(ToUtf8(it->path()));
            }
        }
        it.increment(error);
    }
    std::sort(models_.begin(), models_.end());
    std::sort(textures_.begin(), textures_.end());
}

/** @brief 見た目を enemy_params.json に書き足し、敵の調整値を読み直す */
bool EnemyTypeEditor::SaveNewLook(const NewLook& look) {
    nlohmann::json root = JsonHelper::Load(kEnemyParamsPath);
    if (!root.is_object()) {
        status_ = std::string(kEnemyParamsPath) + " を読めませんでした";
        return false;
    }
    if (!root.contains("visuals") || !root["visuals"].is_object()) {
        root["visuals"] = nlohmann::json::object();
    }
    root["visuals"][look.name] = {
        { "model", look.model },
        { "texture", look.texture },
        { "scale", look.scale },
        { "yawOffset", look.yawDegrees * gameConstants::kDegToRad },
    };
    JsonHelper::Save(kEnemyParamsPath, root);
    EnemyTuning::GetInstance()->Reload();
    return true;
}

/** @brief 一覧の中で使われていない id を作る */
std::string EnemyTypeEditor::MakeUniqueId() const {
    const auto& types = EnemyTypeLibrary::GetInstance()->Types();
    for (int serial = static_cast<int>(types.size()) + 1;; ++serial) {
        const std::string id = "enemy_type_" + std::to_string(serial);
        if (EnemyTypeLibrary::GetInstance()->Find(id) == nullptr) {
            return id;
        }
    }
}

} // namespace engine::game
#else
namespace engine::game {
/** @brief ImGui を使わない構成では何もしない */
void EnemyTypeEditor::Draw(bool* open, const PlaceCallback& place) {
    (void)open;
    (void)place;
}
} // namespace engine::game
#endif
