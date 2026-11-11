/**
 * @file CommandListView.cpp
 * @brief CommandListViewのJSON読み込み、武器と技の選択、一覧と説明欄の描画を実装するファイル
 */
#include "CommandListView.h"
#include "Audio.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "MeleeCombo.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Player.h"
#include "PostEffectRenderTarget.h"
#include "ShadowManager.h"
#include "SkinnedObject3d.h"
#include "SrvManager.h"
#include "StringUtility.h"
#include "UILayout.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>

namespace engine::game {
using engine::graphics::Camera;
using engine::graphics::Object3d;
using engine::graphics::SkinnedObject3d;

namespace {

constexpr const char* kDataPath = "Resources/Config/command_list.json";
constexpr const char* kLayoutName = "command_list";

// 画面の配置（UILayout の command_list で F2 から動かせる）
constexpr Vector2 kTitlePos = { 60.0f, 36.0f };
constexpr float kTitleScale = 2.0f;
constexpr Vector2 kTabsPos = { 60.0f, 96.0f };
constexpr float kTabScale = 1.4f;
constexpr float kTabGap = 28.0f; // 武器名どうしの間隔
constexpr float kTabPadding = 8.0f; // 選択中の武器名の下敷きの余白
constexpr Vector2 kTaglinePos = { 60.0f, 144.0f };
constexpr float kTaglineScale = 1.2f;
constexpr Vector2 kListPos = { 60.0f, 184.0f };
constexpr float kListWidth = 800.0f;
constexpr float kRowHeight = 42.0f;
constexpr int kVisibleRows = 9;
constexpr float kRowScale = 1.4f;
constexpr float kRowTextInset = 16.0f; // 行の左右の余白
constexpr float kNameColumn = 150.0f; // 技名を書き始める位置（行の左端から）
constexpr float kRowTextOffsetY = 10.0f; // 行の上端から文字までの余白
constexpr Vector2 kDetailPos = { 60.0f, 576.0f };
constexpr float kDetailWidth = 1160.0f;
constexpr float kDetailHeight = 92.0f;
constexpr Vector2 kPreviewPos = { 876.0f, 184.0f }; // 技の見本の欄（一覧の右）
constexpr Vector2 kPreviewSize = { 344.0f, 378.0f };
constexpr Vector2 kPreviewLabelOffset = { 12.0f, 10.0f }; // 欄の左上から見出しまで
constexpr float kPreviewLabelScale = 1.1f;
constexpr Vector2 kPreviewEmptyOffset = { 24.0f, 170.0f }; // 見本の無い技の案内の位置
constexpr float kPreviewEmptyScale = 1.2f;
constexpr float kDetailScale = 1.3f;
constexpr float kDetailPadding = 14.0f;
constexpr Vector2 kHelpPos = { 60.0f, 686.0f };
constexpr float kHelpScale = 1.0f;

constexpr Vector4 kTitleColor = { 1.0f, 0.78f, 0.15f, 1.0f };
constexpr Vector4 kTabColor = { 0.6f, 0.62f, 0.7f, 1.0f };
constexpr Vector4 kTabSelectedColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kTabSelectedPanel = { 0.85f, 0.25f, 0.2f, 0.9f };
constexpr Vector4 kTaglineColor = { 0.85f, 0.85f, 0.9f, 1.0f };
constexpr Vector4 kListPanel = { 0.08f, 0.08f, 0.12f, 0.92f };
constexpr Vector4 kRowStripe = { 0.12f, 0.12f, 0.17f, 0.92f }; // 1行おきの縞（行を目で追いやすくする）
constexpr Vector4 kRowSelectedPanel = { 0.85f, 0.25f, 0.2f, 0.85f };
constexpr Vector4 kCategoryColor = { 1.0f, 0.78f, 0.15f, 1.0f };
constexpr Vector4 kNameColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kInputColor = { 0.55f, 0.85f, 1.0f, 1.0f };
constexpr Vector4 kDetailPanel = { 0.05f, 0.05f, 0.08f, 0.95f };
constexpr Vector4 kDetailColor = { 0.95f, 0.92f, 0.85f, 1.0f };
constexpr Vector4 kHelpColor = { 0.7f, 0.7f, 0.7f, 1.0f };
constexpr Vector4 kLockedColor = { 0.45f, 0.45f, 0.5f, 1.0f };
constexpr Vector4 kPreviewPanel = { 0.10f, 0.11f, 0.16f, 0.95f };
constexpr Vector4 kPreviewLabelColor = { 0.7f, 0.72f, 0.8f, 1.0f };

const std::wstring kLockedName = L"？？？";
const std::wstring kLockedTagline = L"まだ手に入れていない武器";
const std::wstring kPreviewLabel = L"見本";
const std::wstring kPreviewEmptyText = L"この技の見本はありません";

// 技の見本の再生（preview に書く skill は、その武器の固有技の連撃をまるごと再生する）
constexpr const char* kSkillPreviewId = "skill";
constexpr float kPreviewLoopPause = 0.5f; // 最後の段を出し切ってから最初に戻るまで止めておく秒数

constexpr float kDepthClearValue = 1.0f;

// 見本で技を受ける的（練習場のダミーと同じスライム）
constexpr const char* kTargetModelPath = "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Slime.obj";
constexpr const char* kTargetTexturePath = "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SlimePalette.png";
constexpr float kTargetModelScale = 0.55f;
constexpr float kTargetFootOffsetY = -0.65f; // モデルは足元が原点なので、的の中心から下げて置く
constexpr float kTargetYawOffset = -gameConstants::kHalfPi; // スライムのモデルの正面を右向きに合わせる補正
constexpr float kTargetFlashSeconds = 0.12f;
constexpr Vector4 kTargetFlashColor = { 1.6f, 1.6f, 1.6f, 1.0f };
constexpr Vector4 kTargetNormalColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr float kHalf = 0.5f;

// 見本の見せ方の既定値（command_list.json の preview_camera / preview_target に項目が無い時に使う）
constexpr float kDefaultCameraDistance = 9.0f;
constexpr float kDefaultCameraHeight = 0.5f;
constexpr float kDefaultCameraPitch = 0.06f;
constexpr float kDefaultCameraFovY = 0.45f;
constexpr float kDefaultTargetDistance = 1.4f;
constexpr float kDefaultTargetMinGap = 0.7f;
constexpr float kDefaultTargetMaxDistance = 3.0f;
constexpr float kDefaultTargetGravity = 0.012f; // 練習場のダミーと同じ重さ
constexpr float kDefaultTargetDragX = 0.84f;
constexpr float kDefaultTargetDragY = 0.88f;
constexpr float kDefaultAirHeight = 1.2f;

/**
 * @brief その武器の近接の段をIDで探す（地上・空中・打ち上げ。見つからなければnullptr）
 * @param outAir 見つかった段が空中コンボの段なら true
 */
const MeleeAttackDef* FindMeleeStep(WeaponType type, const std::string& id, bool& outAir) {
    const MeleeComboSet& set = GetMeleeComboSet(type);
    for (const ComboArray<MeleeAttackDef>* table : { &set.ground, &set.air }) {
        for (int i = 0; i < table->count; ++i) {
            if (id == (*table)[i].id) {
                outAir = (table == &set.air);
                return &(*table)[i];
            }
        }
    }
    outAir = false;
    if (set.launcher != nullptr && id == set.launcher->id) {
        return set.launcher;
    }
    return nullptr;
}

/** @brief 見本を描く時の深度と、画面全体にあたるビューポート */
struct PreviewTarget {
    D3D12_CPU_DESCRIPTOR_HANDLE dsv; ///< 描画先と組になる深度
    D3D12_VIEWPORT screen; ///< UI の座標 (0,0)〜(画面幅,画面高さ) にあたる範囲
};

/** @brief 描画先がバックバッファかどうかで、深度とビューポートを選ぶ（SetupSceneRenderTarget と同じ選び方） */
PreviewTarget SelectPreviewTarget(engine::DirectXCommon* dxCommon, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (rtv.ptr == dxCommon->GetCurrentBackBufferHandle().ptr) {
        return { dxCommon->GetBackBufferDsvHandle(), dxCommon->GetCenteredClientViewport() };
    }
    const D3D12_VIEWPORT full = { 0.0f, 0.0f, static_cast<float>(engine::WinApp::kClientWidth),
        static_cast<float>(engine::WinApp::kClientHeight), 0.0f, 1.0f };
    return { dxCommon->GetDsvHandle(), full };
}

/** @brief JSON の文字列項目を表示用の文字列で返す（無ければ空） */
std::wstring ReadText(const nlohmann::json& j, const char* key) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) {
        return { };
    }
    return StringUtility::ConvertString(j[key].get<std::string>());
}

/** @brief JSON配列からコマンド表の技の一覧を読む */
std::vector<CommandMove> ReadMoves(const nlohmann::json& list) {
    std::vector<CommandMove> moves;
    if (!list.is_array()) {
        return moves;
    }
    for (const nlohmann::json& m : list) {
        CommandMove move { ReadText(m, "category"), ReadText(m, "name"), ReadText(m, "input"), ReadText(m, "detail"), { } };
        if (m.is_object() && m.contains("preview") && m["preview"].is_array()) {
            for (const nlohmann::json& id : m["preview"]) {
                if (id.is_string()) {
                    move.preview.push_back(id.get<std::string>());
                }
            }
        }
        if (!move.name.empty()) {
            moves.push_back(std::move(move));
        }
    }
    return moves;
}

} // namespace

/** @brief 見本のカメラを用意する */
CommandListView::CommandListView() : previewCamera_(std::make_unique<Camera>()) {
}

/** @brief 見本のカメラを片付ける（Camera の定義が見えるここで破棄する） */
CommandListView::~CommandListView() = default;

/** @brief 文字の描画先と効果音の再生先を受け取り、JSON を読み込む */
void CommandListView::Initialize(FontRenderer* fontRenderer, Audio* audio) {
    fontRenderer_ = fontRenderer;
    audio_ = audio;
    tuning_ = { kDefaultCameraDistance, kDefaultCameraHeight, kDefaultCameraPitch, kDefaultCameraFovY, kDefaultTargetDistance,
        kDefaultTargetMinGap, kDefaultTargetMaxDistance, kDefaultTargetGravity, kDefaultTargetDragX, kDefaultTargetDragY, kDefaultAirHeight };

    sheets_.clear();
    commonMoves_.clear();
    const nlohmann::json j = JsonHelper::Load(kDataPath);
    if (j.is_object() && j.contains("weapons") && j["weapons"].is_object()) {
        const nlohmann::json& weapons = j["weapons"];
        for (auto it = weapons.begin(); it != weapons.end(); ++it) {
            CommandSheet sheet;
            sheet.tagline = ReadText(it.value(), "tagline");
            sheet.moves = ReadMoves(it.value().is_object() && it.value().contains("moves") ? it.value()["moves"] : nlohmann::json { });
            sheets_[ParseWeaponTypeName(it.key())] = std::move(sheet);
        }
    }
    if (j.is_object() && j.contains("common")) {
        commonMoves_ = ReadMoves(j["common"]);
    }
    if (j.is_object() && j.contains("preview_camera") && j["preview_camera"].is_object()) {
        const nlohmann::json& camera = j["preview_camera"];
        tuning_.cameraDistance = camera.value("distance", tuning_.cameraDistance);
        tuning_.cameraHeight = camera.value("height", tuning_.cameraHeight);
        tuning_.cameraPitch = camera.value("pitch", tuning_.cameraPitch);
        tuning_.cameraFovY = camera.value("fov_y", tuning_.cameraFovY);
    }
    if (j.is_object() && j.contains("preview_target") && j["preview_target"].is_object()) {
        const nlohmann::json& target = j["preview_target"];
        tuning_.targetDistance = target.value("distance", tuning_.targetDistance);
        tuning_.targetMinGap = target.value("min_gap", tuning_.targetMinGap);
        tuning_.targetMaxDistance = target.value("max_distance", tuning_.targetMaxDistance);
        tuning_.targetGravity = target.value("gravity", tuning_.targetGravity);
        tuning_.targetDragX = target.value("drag_x", tuning_.targetDragX);
        tuning_.targetDragY = target.value("drag_y", tuning_.targetDragY);
        tuning_.airHeight = target.value("air_height", tuning_.airHeight);
    }
}

/** @brief 見本で技を受ける的のモデルを読み込む（シーンの初期化で1回） */
void CommandListView::InitializePreview(engine::graphics::ModelCommon* modelCommon) {
    if (modelCommon == nullptr || previewTarget_ != nullptr) {
        return;
    }
    previewTarget_ = std::make_unique<Object3d>();
    previewTarget_->Initialize(modelCommon);
    previewTarget_->SetModel(engine::graphics::ModelManager::GetInstance()->GetOrLoad(modelCommon, kTargetModelPath, kTargetTexturePath));
    previewTarget_->SetEnableLighting(false);
    previewTarget_->SetScale({ kTargetModelScale, kTargetModelScale, kTargetModelScale });
}

/** @brief 的とプレイヤーの位置を最初に戻す（最初の段からやり直す時） */
void CommandListView::ResetPreviewScene() {
    previewStep_ = 0;
    previewTime_ = 0.0f;
    previewHitStop_ = 0.0f;
    previewLunge_ = 0.0f;
    previewHitDone_ = false;
    targetOffset_ = { tuning_.targetDistance, 0.0f, 0.0f };
    targetVelocity_ = { };
    targetFlash_ = 0.0f;
}

/** @brief 開いた時に呼ぶ（今装備している武器のページから始める） */
void CommandListView::Open() {
    weaponIndex_ = WeaponManager::GetInstance()->GetIndex();
    RebuildRows();
}

/** @brief 閉じた時に呼ぶ（見本の姿勢をやめて元に戻す） */
void CommandListView::Close() {
    previewSteps_.clear();
    if (previewPosed_ && previewActor_ != nullptr) {
        previewActor_->EndMeleePreview();
    }
    previewPosed_ = false;
}

/** @brief 選んでいる技の見本で再生する段を選び直し、最初から再生する */
void CommandListView::RebuildPreview() {
    previewSteps_.clear();
    previewAirSteps_.clear();
    ResetPreviewScene();
    const auto& list = WeaponManager::GetInstance()->GetList();
    const bool weaponValid = weaponIndex_ >= 0 && weaponIndex_ < static_cast<int>(list.size());
    if (!weaponValid || cursor_ < 0 || cursor_ >= static_cast<int>(rows_.size())) {
        return;
    }
    const WeaponType type = list[weaponIndex_].type;
    for (const std::string& id : rows_[cursor_]->preview) {
        if (id == kSkillPreviewId) {
            const ComboArray<MeleeAttackDef>& sequence = GetSkillSequence(type);
            for (int i = 0; i < sequence.count; ++i) {
                previewSteps_.push_back(&sequence[i]);
                previewAirSteps_.push_back(false);
            }
            continue;
        }
        bool air = false;
        if (const MeleeAttackDef* step = FindMeleeStep(type, id, air)) {
            previewSteps_.push_back(step);
            previewAirSteps_.push_back(air);
        }
    }
}

/** @brief 見本を1フレーム進め、見本の姿勢をプレイヤーに取らせる */
void CommandListView::AdvancePreview() {
    if (previewActor_ == nullptr) {
        return;
    }
    const auto& list = WeaponManager::GetInstance()->GetList();
    const bool weaponValid = weaponIndex_ >= 0 && weaponIndex_ < static_cast<int>(list.size());
    if (previewSteps_.empty() || !weaponValid) {
        // 見本の無い技を選んでいる間は、見本の姿勢をやめておく
        if (previewPosed_) {
            previewActor_->EndMeleePreview();
            previewPosed_ = false;
        }
        return;
    }

    AdvancePreviewClock();
    const MeleeAttackDef& step = *previewSteps_[previewStep_];
    const WeaponData& weapon = list[weaponIndex_];
    UpdatePreviewTarget(step, weapon.knockbackMult);

    // 当たる時刻までに踏み込みの距離だけ前へ出す（空中の段は持ち上げ、踏み込まない）
    const bool air = previewAirSteps_[previewStep_];
    const float lungeProgress = step.hitTime > 0.0f ? (std::min)(previewTime_ / step.hitTime, 1.0f) : 1.0f;
    const float lunge = previewLunge_ + (air ? 0.0f : step.lungeDist * lungeProgress);
    const float dirX = previewActor_->GetLastDirX() >= 0.0f ? 1.0f : -1.0f;
    previewActor_->SetPreviewOffset({ lunge * dirX, air ? tuning_.airHeight : 0.0f, 0.0f });

    const float progress = step.duration > 0.0f ? (std::min)(previewTime_ / step.duration, 1.0f) : 1.0f;
    previewActor_->PreviewMeleePose(&step, progress, weapon.type);
    previewPosed_ = true;
}

/** @brief 見本の段の時間を進める（ヒットストップ中は止め、段を出し切ったら次の段へ、最後まで出したら最初から） */
void CommandListView::AdvancePreviewClock() {
    if (previewHitStop_ > 0.0f) {
        previewHitStop_ -= gameConstants::kFrameDeltaTime;
        return;
    }
    previewTime_ += gameConstants::kFrameDeltaTime;
    const MeleeAttackDef& step = *previewSteps_[previewStep_];
    const bool lastStep = previewStep_ + 1 >= static_cast<int>(previewSteps_.size());
    if (!lastStep && previewTime_ >= step.duration) {
        previewTime_ -= step.duration;
        previewLunge_ += previewAirSteps_[previewStep_] ? 0.0f : step.lungeDist;
        ++previewStep_;
        previewHitDone_ = false;
    } else if (lastStep && previewTime_ >= step.duration + kPreviewLoopPause) {
        ResetPreviewScene();
    }
}

/** @brief 段の当たる時刻に的を吹き飛ばし、的の動きを1フレーム進める */
void CommandListView::UpdatePreviewTarget(const MeleeAttackDef& step, float knockbackMult) {
    targetFlash_ = (std::max)(targetFlash_ - gameConstants::kFrameDeltaTime, 0.0f);
    if (!previewHitDone_ && previewTime_ >= step.hitTime) {
        // 練習場のダミーと同じく、段の吹き飛ばしに武器の倍率を掛けて的へ与え、段のヒットストップのぶんだけ止める
        previewHitDone_ = true;
        targetVelocity_.x += step.knockX * knockbackMult;
        targetVelocity_.y += step.knockY * knockbackMult;
        targetFlash_ = kTargetFlashSeconds;
        previewHitStop_ = static_cast<float>(step.hitStop) * gameConstants::kFrameDeltaTime;
    }
    if (previewHitStop_ > 0.0f) {
        return;
    }

    // 的の高さはプレイヤーの足場（空中の段では持ち上げた高さ）を床にして落とす
    const float floorY = previewAirSteps_[previewStep_] ? tuning_.airHeight : 0.0f;
    targetVelocity_.y -= tuning_.targetGravity;
    targetOffset_ = targetOffset_ + targetVelocity_;
    if (targetOffset_.y <= floorY) {
        targetOffset_.y = floorY;
        targetVelocity_.y = (std::max)(targetVelocity_.y, 0.0f);
    }
    targetVelocity_.x *= tuning_.targetDragX;
    targetVelocity_.y *= tuning_.targetDragY;

    // 踏み込んだプレイヤーを追い越させず、欄の外へも出さない
    const float playerX = previewLunge_ + (previewAirSteps_[previewStep_] ? 0.0f : step.lungeDist);
    const float nearest = playerX + tuning_.targetMinGap;
    const float farthest = (std::max)(previewLunge_ + tuning_.targetMaxDistance, nearest);
    targetOffset_.x = std::clamp(targetOffset_.x, nearest, farthest);
}

/** @brief 今のページに並べる技（武器の技＋共通の操作）を作り直す */
void CommandListView::RebuildRows() {
    rows_.clear();
    previewSteps_.clear();
    cursor_ = 0;
    scroll_ = 0;
    const auto& list = WeaponManager::GetInstance()->GetList();
    if (weaponIndex_ < 0 || weaponIndex_ >= static_cast<int>(list.size())) {
        return;
    }
    // 持っていない武器は技を伏せ、共通の操作だけを見せる
    if (WeaponManager::GetInstance()->IsUnlocked(weaponIndex_)) {
        const auto sheet = sheets_.find(list[weaponIndex_].type);
        if (sheet != sheets_.end()) {
            for (const CommandMove& move : sheet->second.moves) {
                rows_.push_back(&move);
            }
        }
    }
    for (const CommandMove& move : commonMoves_) {
        rows_.push_back(&move);
    }
    RebuildPreview();
}

/** @brief 指定方向へ、持っている武器の中で次のページへ移る（持っていない武器も伏せて並べる） */
void CommandListView::MoveWeapon(int step) {
    const int count = static_cast<int>(WeaponManager::GetInstance()->GetList().size());
    if (count <= 0) {
        return;
    }
    weaponIndex_ = (weaponIndex_ + step + count) % count;
    RebuildRows();
    if (audio_ != nullptr) {
        audio_->PlayMenuSelect();
    }
}

/** @brief 武器の切り替え・技の選択を受け付ける */
bool CommandListView::Update(Input* input) {
    if (input->TriggerMenuCancel()) {
        if (audio_ != nullptr) {
            audio_->PlayMenuSelect();
        }
        Close();
        return true;
    }
    if (input->TriggerMenuLeft()) {
        MoveWeapon(-1);
    }
    if (input->TriggerMenuRight()) {
        MoveWeapon(1);
    }
    const int rowCount = static_cast<int>(rows_.size());
    if (rowCount > 0) {
        const int previousCursor = cursor_;
        if (input->TriggerMenuUp()) {
            cursor_ = (cursor_ - 1 + rowCount) % rowCount;
        }
        if (input->TriggerMenuDown()) {
            cursor_ = (cursor_ + 1) % rowCount;
        }
        // 選んでいる行が一覧の外へ出たら、見える位置までずらす
        scroll_ = std::clamp(scroll_, (std::max)(cursor_ - kVisibleRows + 1, 0), cursor_);
        if (cursor_ != previousCursor) {
            RebuildPreview();
        }
    }
    AdvancePreview();
    return false;
}

/** @brief 説明文を指定幅で折り返す */
std::wstring CommandListView::WrapText(const std::wstring& text, float maxWidth, float scale) {
    std::wstring result;
    std::wstring line;
    for (wchar_t ch : text) {
        const std::wstring candidate = line + ch;
        if (!line.empty() && FontRenderer::MeasureStringW(candidate, scale).x > maxWidth) {
            result += line + L'\n';
            line = std::wstring(1, ch);
        } else {
            line = candidate;
        }
    }
    return result + line;
}

/** @brief 文字とパネルを積む（毎フレーム、FontRenderer::Reset() の後に呼ぶ） */
void CommandListView::Draw(const Input* input) {
    if (fontRenderer_ == nullptr) {
        return;
    }
    FontRenderer& font = *fontRenderer_;
    UILayout& layout = UILayout::Get(kLayoutName);
    auto expand = [input](const std::wstring& text) {
        return input != nullptr ? input->ExpandPrompts(text) : text;
    };

    // ── 見出し ──
    const Vector2 titlePos = layout.Pos("title.pos", kTitlePos);
    font.DrawStringW(L"コマンド表", titlePos.x, titlePos.y, layout.Float("title.scale", kTitleScale), layout.Color("title.color", kTitleColor));

    // ── 武器の切り替え（持っていない武器は伏せる） ──
    const auto& list = WeaponManager::GetInstance()->GetList();
    const Vector2 tabsPos = layout.Pos("tabs.pos", kTabsPos);
    const float tabScale = layout.Float("tabs.scale", kTabScale);
    float tabX = tabsPos.x;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const bool unlocked = WeaponManager::GetInstance()->IsUnlocked(i);
        const std::wstring name = unlocked ? list[i].styleNameJp : kLockedName;
        const Vector2 size = FontRenderer::MeasureStringW(name, tabScale);
        if (i == weaponIndex_) {
            font.DrawPanel(tabX - kTabPadding, tabsPos.y - kTabPadding, size.x + kTabPadding * 2.0f, size.y + kTabPadding * 2.0f,
                layout.Color("tabs.selected_panel", kTabSelectedPanel));
        }
        const Vector4 color = (i == weaponIndex_) ? kTabSelectedColor : (unlocked ? kTabColor : kLockedColor);
        font.DrawStringW(name, tabX, tabsPos.y, tabScale, color);
        tabX += size.x + kTabGap;
    }

    // ── 武器の一言説明 ──
    const bool weaponValid = weaponIndex_ >= 0 && weaponIndex_ < static_cast<int>(list.size());
    std::wstring tagline;
    if (weaponValid && WeaponManager::GetInstance()->IsUnlocked(weaponIndex_)) {
        const auto sheet = sheets_.find(list[weaponIndex_].type);
        tagline = (sheet != sheets_.end()) ? sheet->second.tagline : std::wstring { };
    } else {
        tagline = kLockedTagline;
    }
    const Vector2 taglinePos = layout.Pos("tagline.pos", kTaglinePos);
    font.DrawStringW(tagline, taglinePos.x, taglinePos.y, layout.Float("tagline.scale", kTaglineScale), layout.Color("tagline.color", kTaglineColor));

    // ── 技の一覧 ──
    const Vector2 listPos = layout.Pos("list.pos", kListPos);
    // 見本を出さない画面では、右の見本の欄のぶんまで一覧を広げる
    const float listWidth = (previewActor_ != nullptr) ? layout.Float("list.width", kListWidth) : layout.Float("detail.width", kDetailWidth);
    const float rowHeight = layout.Float("list.row_height", kRowHeight);
    const float rowScale = layout.Float("list.scale", kRowScale);
    font.DrawPanel(listPos.x, listPos.y, listWidth, rowHeight * kVisibleRows, layout.Color("list.panel", kListPanel));
    const int lastRow = (std::min)(scroll_ + kVisibleRows, static_cast<int>(rows_.size()));
    for (int i = scroll_; i < lastRow; ++i) {
        const CommandMove& move = *rows_[i];
        const float rowY = listPos.y + rowHeight * static_cast<float>(i - scroll_);
        if (i == cursor_) {
            font.DrawPanel(listPos.x, rowY, listWidth, rowHeight, layout.Color("list.selected_panel", kRowSelectedPanel));
        } else if ((i % 2) == 1) {
            font.DrawPanel(listPos.x, rowY, listWidth, rowHeight, kRowStripe);
        }
        const float textY = rowY + kRowTextOffsetY;
        font.DrawStringW(L"[" + move.category + L"]", listPos.x + kRowTextInset, textY, rowScale, kCategoryColor);
        font.DrawStringW(move.name, listPos.x + kNameColumn, textY, rowScale, kNameColor);
        // 入力は右端にそろえる（{Attack} などは最後に使った機器のボタン名になる）
        const std::wstring inputText = expand(move.input);
        const float inputWidth = FontRenderer::MeasureStringW(inputText, rowScale).x;
        font.DrawStringW(inputText, listPos.x + listWidth - kRowTextInset - inputWidth, textY, rowScale, kInputColor);
    }

    // ── 選んでいる技の説明 ──
    const Vector2 detailPos = layout.Pos("detail.pos", kDetailPos);
    const float detailScale = layout.Float("detail.scale", kDetailScale);
    const float detailWidth = layout.Float("detail.width", kDetailWidth);
    font.DrawPanel(detailPos.x, detailPos.y, detailWidth, kDetailHeight, layout.Color("detail.panel", kDetailPanel));
    if (cursor_ >= 0 && cursor_ < static_cast<int>(rows_.size())) {
        const std::wstring detail = WrapText(expand(rows_[cursor_]->detail), detailWidth - kDetailPadding * 2.0f, detailScale);
        font.DrawStringW(detail, detailPos.x + kDetailPadding, detailPos.y + kDetailPadding, detailScale, kDetailColor);
    }

    // ── 技の見本の欄（プレイヤーの3D描画は DrawPreview() がこの上へ重ねる） ──
    if (previewActor_ != nullptr) {
        previewPos_ = layout.Pos("preview.pos", kPreviewPos);
        previewSize_ = layout.Vec2("preview.size", kPreviewSize);
        font.DrawPanel(previewPos_.x, previewPos_.y, previewSize_.x, previewSize_.y, layout.Color("preview.panel", kPreviewPanel));
        font.DrawStringW(kPreviewLabel, previewPos_.x + kPreviewLabelOffset.x, previewPos_.y + kPreviewLabelOffset.y,
            kPreviewLabelScale, kPreviewLabelColor);
        if (previewSteps_.empty()) {
            font.DrawStringW(kPreviewEmptyText, previewPos_.x + kPreviewEmptyOffset.x, previewPos_.y + kPreviewEmptyOffset.y,
                kPreviewEmptyScale, kPreviewLabelColor);
        }
    }

    // ── 操作の案内 ──
    const Vector2 helpPos = layout.Pos("help.pos", kHelpPos);
    // 戻るは Backspace（パッドは B）。ESC はアプリの終了に使われているので案内しない
    const bool gamepad = (input != nullptr) && input->IsUsingGamepad();
    const std::wstring backLabel = gamepad ? L"B" : L"Backspace";
    font.DrawStringW(L"←→: 武器を切り替え    ↑↓: 技を選ぶ    " + backLabel + L": 戻る", helpPos.x, helpPos.y,
        layout.Float("help.scale", kHelpScale), kHelpColor);
}

/** @brief 見本の欄にプレイヤーを3Dで描く（FontRenderer::Draw() の後に呼ぶ。見本が無ければ何もしない） */
void CommandListView::DrawPreview(const CommandPreviewRenderer& renderer) {
    const bool ready = renderer.dxCommon != nullptr && renderer.modelCommon != nullptr && renderer.objectCommon != nullptr
        && renderer.shadowManager != nullptr && renderer.srvManager != nullptr;
    if (!previewPosed_ || previewActor_ == nullptr || !ready || previewSize_.x <= 0.0f || previewSize_.y <= 0.0f) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = renderer.dxCommon->GetCommandList();

    // 文字と同じ描画先のうち、見本の欄だけを描画範囲にし、その範囲の深度だけを消す（欄のパネルの上に重なる）
    const PreviewTarget target = SelectPreviewTarget(renderer.dxCommon, renderer.rtv);
    const float scale = target.screen.Width / gameConstants::kScreenWidth;
    const D3D12_VIEWPORT viewport = { target.screen.TopLeftX + previewPos_.x * scale, target.screen.TopLeftY + previewPos_.y * scale,
        previewSize_.x * scale, previewSize_.y * scale, 0.0f, 1.0f };
    const D3D12_RECT scissor = { static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
        static_cast<LONG>(viewport.TopLeftX + viewport.Width), static_cast<LONG>(viewport.TopLeftY + viewport.Height) };
    commandList->OMSetRenderTargets(1, &renderer.rtv, FALSE, &target.dsv);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->ClearDepthStencilView(target.dsv, D3D12_CLEAR_FLAG_DEPTH, kDepthClearValue, 0, 1, &scissor);

    // プレイヤーと的の真ん中を真横から写すカメラ（プレイヤーは左右を向くので、手前から見ると技の動きが横に見える）
    const Vector3 origin = previewActor_->GetPosition();
    const float dirX = previewActor_->GetLastDirX() >= 0.0f ? 1.0f : -1.0f;
    const Vector3 targetPos = { origin.x + targetOffset_.x * dirX, origin.y + targetOffset_.y, origin.z };
    const float centerX = origin.x + (previewLunge_ + targetOffset_.x) * kHalf * dirX;
    previewCamera_->SetFovY(tuning_.cameraFovY);
    previewCamera_->SetAspectRatio(previewSize_.x / previewSize_.y);
    previewCamera_->SetRotate({ tuning_.cameraPitch, 0.0f, 0.0f });
    previewCamera_->SetTranslate({ centerX, origin.y + tuning_.cameraHeight, origin.z - tuning_.cameraDistance });

    // 共通カメラを一時的に見本のカメラへ差し替えて行列を作り直し、描いたら元のカメラで作り直す
    Camera* const sceneObjectCamera = Object3d::GetCommonCamera();
    Camera* const sceneSkinnedCamera = SkinnedObject3d::GetCommonCamera();
    Object3d::SetCommonCamera(previewCamera_.get());
    SkinnedObject3d::SetCommonCamera(previewCamera_.get());
    previewActor_->RefreshVisualTransforms();

    renderer.modelCommon->CommonDrawSettings();
    renderer.objectCommon->SetDefaultLight(commandList);
    renderer.shadowManager->SetShadowMap(commandList, renderer.srvManager);
    previewActor_->DrawBodyAndWeapons();
    if (previewTarget_ != nullptr) {
        // 的はいつもプレイヤーの方を向き、当たった直後だけ光る
        const float facingYaw = (dirX > 0.0f) ? -gameConstants::kHalfPi : gameConstants::kHalfPi;
        previewTarget_->SetPosition({ targetPos.x, targetPos.y + kTargetFootOffsetY, targetPos.z });
        previewTarget_->SetRotation({ 0.0f, facingYaw + kTargetYawOffset, 0.0f });
        previewTarget_->SetColor(targetFlash_ > 0.0f ? kTargetFlashColor : kTargetNormalColor);
        previewTarget_->Update();
        previewTarget_->Draw();
    }

    Object3d::SetCommonCamera(sceneObjectCamera);
    SkinnedObject3d::SetCommonCamera(sceneSkinnedCamera);
    previewActor_->RefreshVisualTransforms();

    // 描画先は渡された描画先の全画面へ戻す
    engine::graphics::SetupSceneRenderTarget(renderer.dxCommon, renderer.rtv);
}

} // namespace engine::game
