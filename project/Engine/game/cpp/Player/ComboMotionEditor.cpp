/**
 * @file ComboMotionEditor.cpp
 * @brief ComboMotionEditorの段の選択、時間軸、ボーン一覧、キー編集、保存操作のパネルを実装するファイル
 */
#include "ComboMotionEditor.h"
#include "ComboMotion.h"
#include "GameConstants.h"
#include "Input.h"
#include "MeleeCombo.h"
#include "Player.h"
#include "Skeleton.h"
#include "Weapon.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine;
using namespace engine::game;

namespace {

constexpr float kKeySnap = 0.01f; // この時刻差以内のキーは同じキーとして扱う
constexpr float kAngleDragSpeed = 0.5f; // 角度ドラッグの感度（度/ピクセル）
constexpr float kMaxAngle = 180.0f; // 角度入力の上限（度）
constexpr float kMinPlaySpeed = 0.05f;
constexpr float kMaxPlaySpeed = 2.0f;
constexpr float kPlaySpeedWidthRatio = 0.5f; // 再生の速さスライダーを行の何割の幅にするか
constexpr size_t kKeyLabelSize = 96; // キー一覧の1行の文字数上限
constexpr Vector4 kStatsOkColor = { 0.5f, 1.0f, 0.5f, 1.0f }; // 全トラックが掛かっている
constexpr Vector4 kStatsWarnColor = { 1.0f, 0.6f, 0.2f, 1.0f }; // 掛かっていないトラックがある

/** @brief 段の一覧の1行（表示名と段の定義） */
struct StepEntry {
    std::string label;
    const MeleeAttackDef* attack;
};

/** @brief 今の武器で編集できる段（地上・空中・打ち上げ・固有技の連撃）を並べる */
std::vector<StepEntry> CollectSteps(WeaponType weaponType)
{
    std::vector<StepEntry> steps;
    const MeleeComboSet& set = GetMeleeComboSet(weaponType);
    for (int i = 0; i < set.ground.count; ++i) {
        steps.push_back({ "地上 " + std::to_string(i + 1) + " : " + set.ground[i].id, &set.ground[i] });
    }
    for (int i = 0; i < set.air.count; ++i) {
        steps.push_back({ "空中 " + std::to_string(i + 1) + " : " + set.air[i].id, &set.air[i] });
    }
    if (set.launcher != nullptr) {
        steps.push_back({ std::string("打ち上げ : ") + set.launcher->id, set.launcher });
    }
    const ComboArray<MeleeAttackDef>& skill = GetSkillSequence(weaponType);
    for (int i = 0; i < skill.count; ++i) {
        steps.push_back({ "固有技 " + std::to_string(i + 1) + " : " + skill[i].id, &skill[i] });
    }
    return steps;
}

const MeleeAttackDef* FindAttack(WeaponType weaponType, const std::string& id)
{
    for (const StepEntry& step : CollectSteps(weaponType)) {
        if (id == step.attack->id) {
            return step.attack;
        }
    }
    return nullptr;
}

/** @brief 指の骨か（一覧が長くなりすぎるので既定では隠す） */
bool IsFingerBone(const std::string& name)
{
    return name.find("Finger") != std::string::npos || name.find("Thumb") != std::string::npos
        || name.find("finger") != std::string::npos || name.find("thumb") != std::string::npos;
}

} // namespace

bool ComboMotionEditor::Update(Input* input, Player& player, WeaponType weaponType)
{
#ifdef USE_IMGUI
    if (input != nullptr && input->TriggerKey(DIK_F7)) {
        open_ = !open_;
    }

    const MeleeAttackDef* attack = open_ ? FindAttack(weaponType, selectedAttackId_) : nullptr;
    // 武器を持ち替えて選んでいた段が無くなったら、その武器の最初の段を選び直す
    // （段が無いままだとプレビューが切れて、止めていたゲームが勝手に動き出すため）
    if (open_ && attack == nullptr) {
        const std::vector<StepEntry> steps = CollectSteps(weaponType);
        if (!steps.empty()) {
            attack = steps.front().attack;
            selectedAttackId_ = attack->id;
            time_ = 0.0f;
        }
    }
    ComboMotionLibrary* library = ComboMotionLibrary::GetInstance();

    if (open_) {
        if (ImGui::Begin("モーションエディタ (F7)", &open_)) {
            ImGui::Checkbox("プレビュー（ゲームを止める）", &previewing_);
            ImGui::SameLine();
            bool enabled = library->IsEnabled();
            if (ImGui::Checkbox("加算モーションを掛ける", &enabled)) {
                library->SetEnabled(enabled);
            }
            ImGui::TextDisabled("武器はQ/Eで切り替え。角度を動かすと今の時刻にキーが打たれます");

            // 掛かり具合の確認（データが読めていない・ボーン名が合わない時はここが0になる）
            int trackCount = 0;
            int appliedCount = 0;
            player.GetMotionLayerStats(trackCount, appliedCount);
            const MeleeAttackDef* playing = player.GetActiveMeleeAttack();
            const Vector4 statsColor = (trackCount > 0 && appliedCount == trackCount) ? kStatsOkColor : kStatsWarnColor;
            ImGui::TextColored({ statsColor.x, statsColor.y, statsColor.z, statsColor.w },
                "リグ %s / 段 %s / トラック %d / 掛かったボーン %d",
                player.GetMotionRigId(), playing != nullptr ? playing->id : "(攻撃していない)", trackCount, appliedCount);

            attack = DrawStepList(weaponType);
            if (attack != nullptr) {
                MotionClip& clip = library->GetOrCreate(player.GetMotionRigId(), attack->id);
                DrawTimeline(*attack, &clip);
                DrawBoneList(player, &clip);
                DrawKeyEditor(clip);
            }
            DrawFileControls();
        }
        ImGui::End();
    }

    // 自動再生は段の実際の長さに合わせて進める
    if (playing_ && attack != nullptr && attack->duration > 0.0f) {
        time_ += GameConstants::kFrameDeltaTime * playSpeed_ / attack->duration;
        if (time_ > 1.0f) {
            time_ = 0.0f;
        }
    }

    const bool previewNow = open_ && previewing_ && attack != nullptr;
    if (previewNow) {
        player.PreviewMeleePose(attack, time_);
        wasPreviewing_ = true;
        return true;
    }
    if (wasPreviewing_) {
        player.EndMeleePreview();
        wasPreviewing_ = false;
    }
    return false;
#else
    (void)input;
    (void)player;
    (void)weaponType;
    return false;
#endif
}

void ComboMotionEditor::Close(Player& player)
{
    open_ = false;
    playing_ = false;
    if (wasPreviewing_) {
        player.EndMeleePreview();
        wasPreviewing_ = false;
    }
}

const MeleeAttackDef* ComboMotionEditor::DrawStepList(WeaponType weaponType)
{
    const MeleeAttackDef* selected = nullptr;
#ifdef USE_IMGUI
    constexpr float kStepListHeight = 120.0f;
    ImGui::SeparatorText("段");
    if (ImGui::BeginListBox("##steps", { -FLT_MIN, kStepListHeight })) {
        for (const StepEntry& step : CollectSteps(weaponType)) {
            const bool isSelected = (selectedAttackId_ == step.attack->id);
            if (ImGui::Selectable(step.label.c_str(), isSelected)) {
                selectedAttackId_ = step.attack->id;
                time_ = 0.0f;
            }
            if (selectedAttackId_ == step.attack->id) {
                selected = step.attack;
            }
        }
        ImGui::EndListBox();
    }
#else
    (void)weaponType;
#endif
    return selected;
}

void ComboMotionEditor::DrawTimeline(const MeleeAttackDef& attack, const MotionClip* clip)
{
#ifdef USE_IMGUI
    constexpr float kTimelineHeight = 34.0f;
    constexpr float kKeyMarkHalfWidth = 3.0f;
    constexpr float kOtherKeyInset = 10.0f; // 選んでいないボーンのキーは短い印にする
    constexpr float kMarkerThickness = 2.0f;
    constexpr ImU32 kBackgroundColor = IM_COL32(30, 32, 40, 255);
    constexpr ImU32 kHitColor = IM_COL32(255, 90, 70, 255);
    constexpr ImU32 kCancelColor = IM_COL32(90, 200, 255, 255);
    constexpr ImU32 kSelectedKeyColor = IM_COL32(255, 220, 80, 255);
    constexpr ImU32 kOtherKeyColor = IM_COL32(150, 150, 150, 255);
    constexpr ImU32 kCursorColor = IM_COL32(255, 255, 255, 255);

    ImGui::SeparatorText("時間軸");
    ImGui::Checkbox("再生", &playing_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * kPlaySpeedWidthRatio);
    ImGui::SliderFloat("速さ", &playSpeed_, kMinPlaySpeed, kMaxPlaySpeed);
    ImGui::SliderFloat("時刻", &time_, 0.0f, 1.0f);

    const float duration = (attack.duration > 0.0f) ? attack.duration : 1.0f;
    const float hitRatio = std::clamp(attack.hitTime / duration, 0.0f, 1.0f);
    const float cancelRatio = std::clamp(attack.cancelTime / duration, 0.0f, 1.0f);

    // クリックとドラッグで時刻を動かせる帯
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = { ImGui::GetContentRegionAvail().x, kTimelineHeight };
    ImGui::InvisibleButton("##timeline", size);
    if (ImGui::IsItemActive() && size.x > 0.0f) {
        time_ = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
        playing_ = false;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end = { origin.x + size.x, origin.y + size.y };
    draw->AddRectFilled(origin, end, kBackgroundColor);
    auto xAt = [&](float ratio) { return origin.x + size.x * ratio; };
    draw->AddLine({ xAt(hitRatio), origin.y }, { xAt(hitRatio), end.y }, kHitColor, kMarkerThickness);
    draw->AddLine({ xAt(cancelRatio), origin.y }, { xAt(cancelRatio), end.y }, kCancelColor, kMarkerThickness);
    if (clip != nullptr) {
        for (const MotionTrack& track : clip->tracks) {
            const bool selectedTrack = (track.bone == selectedBone_);
            const float inset = selectedTrack ? 0.0f : kOtherKeyInset;
            for (const MotionKey& key : track.keys) {
                const float x = xAt(key.time);
                draw->AddRectFilled({ x - kKeyMarkHalfWidth, origin.y + inset }, { x + kKeyMarkHalfWidth, end.y - inset },
                    selectedTrack ? kSelectedKeyColor : kOtherKeyColor);
            }
        }
    }
    draw->AddLine({ xAt(time_), origin.y }, { xAt(time_), end.y }, kCursorColor, kMarkerThickness);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kHitColor), "ヒット %.2f", hitRatio);
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kCancelColor), "キャンセル %.2f", cancelRatio);
    ImGui::SameLine();
    ImGui::Text("段の長さ %.2f 秒", attack.duration);
#else
    (void)attack;
    (void)clip;
#endif
}

void ComboMotionEditor::DrawBoneList(const Player& player, const MotionClip* clip)
{
#ifdef USE_IMGUI
    constexpr float kBoneListHeight = 160.0f;
    ImGui::SeparatorText("ボーン");
    ImGui::Checkbox("指の骨も出す", &showFingers_);
    if (ImGui::BeginListBox("##bones", { -FLT_MIN, kBoneListHeight })) {
        for (const auto& joint : player.GetRigSkeleton().GetJoints()) {
            if (!showFingers_ && IsFingerBone(joint.name)) {
                continue;
            }
            const bool hasTrack = (clip != nullptr && clip->FindTrack(joint.name) != nullptr);
            const std::string label = (hasTrack ? "* " : "  ") + joint.name;
            if (ImGui::Selectable(label.c_str(), selectedBone_ == joint.name)) {
                selectedBone_ = joint.name;
            }
        }
        ImGui::EndListBox();
    }
#else
    (void)player;
    (void)clip;
#endif
}

void ComboMotionEditor::DrawKeyEditor(MotionClip& clip)
{
#ifdef USE_IMGUI
    ImGui::SeparatorText("キー");
    if (selectedBone_.empty()) {
        ImGui::TextDisabled("ボーンを選んでください");
        return;
    }
    MotionTrack* track = clip.FindTrack(selectedBone_);
    MotionKey* keyHere = (track != nullptr) ? track->FindKey(time_, kKeySnap) : nullptr;
    Vector3 angle = (keyHere != nullptr) ? keyHere->eulerDeg
        : (track != nullptr ? track->EvaluateEuler(time_) : Vector3 { });

    ImGui::Text("%s  /  時刻 %.2f", selectedBone_.c_str(), time_);
    if (ImGui::DragFloat3("角度 (度)", &angle.x, kAngleDragSpeed, -kMaxAngle, kMaxAngle)) {
        // 動かした時点でこの時刻にキーを打つ（トラックが無ければ始端・終端が無回転のトラックを作る）
        clip.GetOrCreateTrack(selectedBone_).SetKey(time_, angle, kKeySnap);
        playing_ = false;
    }
    ImGui::TextDisabled(keyHere != nullptr ? "この時刻のキーを編集中" : "動かすとこの時刻に新しいキーが打たれます");

    if (track == nullptr) {
        return;
    }
    if (keyHere != nullptr && ImGui::Button("この時刻のキーを消す")) {
        track->RemoveKey(time_, kKeySnap);
    }
    ImGui::SameLine();
    if (ImGui::Button("このボーンのキーを全部消す")) {
        clip.RemoveTrack(selectedBone_);
        return;
    }

    // キーの一覧（押すとその時刻へ移動する）
    for (size_t i = 0; i < track->keys.size(); ++i) {
        const MotionKey& key = track->keys[i];
        char label[kKeyLabelSize];
        snprintf(label, sizeof(label), "%.2f  (%.0f, %.0f, %.0f)##key%zu",
            key.time, key.eulerDeg.x, key.eulerDeg.y, key.eulerDeg.z, i);
        if (ImGui::Selectable(label, std::abs(key.time - time_) <= kKeySnap)) {
            time_ = key.time;
            playing_ = false;
        }
    }
#else
    (void)clip;
#endif
}

void ComboMotionEditor::DrawFileControls()
{
#ifdef USE_IMGUI
    ImGui::Separator();
    if (ImGui::Button("保存")) {
        status_ = ComboMotionLibrary::GetInstance()->Save()
            ? "Resources/Config/combo_motion.json に保存しました"
            : "保存に失敗しました（ファイルが書き込めるか確認してください）";
    }
    ImGui::SameLine();
    if (ImGui::Button("読み直し")) {
        status_ = ComboMotionLibrary::GetInstance()->Load()
            ? "読み直しました（保存していない変更は消えました）"
            : "読み直せませんでした（ファイルが無いか形が違うため、今のデータを残しています）";
    }
    if (!status_.empty()) {
        ImGui::TextDisabled("%s", status_.c_str());
    }
#endif
}
