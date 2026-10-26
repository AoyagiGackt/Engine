/**
 * @file PauseMenu.cpp
 * @brief プレイ中のシーンで共通に使う一時停止画面（PauseMenu）の実装
 */
#include "PauseMenu.h"
#include "Audio.h"
#include "ComboTrial.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "GameSettings.h"
#include "Input.h"
#include "SceneShared.h"
#include "UILayout.h"
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kWhiteTexture = "Resources/white.png";
constexpr Vector4 kOverlayColor = { 0.0f, 0.0f, 0.0f, 0.75f };
constexpr Vector2 kMenuPosition = { 440.0f, 220.0f };
constexpr Vector2 kMenuItemSize = { 400.0f, 60.0f };
constexpr Vector2 kBgmVolumeTextPosition = { 900.0f, 291.0f };
constexpr Vector2 kSeVolumeTextPosition = { 900.0f, 351.0f };
constexpr Vector2 kAutoComboTextPosition = { 900.0f, 411.0f };
constexpr float kValueTextScale = 1.5f;
constexpr Vector4 kValueTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector2 kTitlePosition = { 560.0f, 130.0f };
constexpr float kTitleScale = 2.0f;
constexpr Vector4 kTitleColor = { 1.0f, 1.0f, 0.6f, 1.0f };

// コンボ練習の一覧と退出の確認（画面中央に見出しと選択肢）
constexpr Vector2 kSubMenuPosition = { 440.0f, 260.0f };
constexpr Vector2 kSubMenuItemSize = { 400.0f, 56.0f };
constexpr float kSubMenuLabelScale = 1.6f;
constexpr float kSubTitleY = 190.0f;
constexpr float kSubTitleScale = 2.0f;
constexpr const wchar_t* kComboTrialTitle = L"コンボ練習";
constexpr const wchar_t* kConfirmExitTitle = L"本当に戻りますか？";
constexpr const char* kStopTrialLabel = "練習をやめる";
constexpr const char* kBackLabel = "もどる";
constexpr const char* kConfirmNoLabel = "いいえ";
constexpr const char* kConfirmYesLabel = "はい";
constexpr int kConfirmYesRow = 1; // いいえを先に置き、うっかり決定しても戻らないようにする
}

// ══════════════════════════════════════════════════════
// ページ（メニュー / コマンド表）
// ══════════════════════════════════════════════════════

/** @brief 一時停止中の画面の1ページ */
class PauseMenu::IPage {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IPage() = default;
    /** @brief 入力を受け付ける（ページの切り替えもここで行う） */
    virtual void Update(PauseMenu& pause, Input* input) const = 0;
    /** @brief 文字とパネルを積む */
    virtual void Draw(PauseMenu& pause, const Input* input) const = 0;
    /** @brief 戻るボタンをこのページで使うか（使うなら一時停止の解除には回さない） */
    virtual bool HandlesBack() const = 0;
};

/** @brief ポーズメニューの入力と描画を処理するページ */
class PauseMenu::MenuPage : public PauseMenu::IPage {
public:
    /** @brief ポーズメニューの入力を処理する */
    void Update(PauseMenu& pause, Input* input) const override {
        pause.UpdateMenu(input);
    }
    /** @brief ポーズメニューを描画する */
    void Draw(PauseMenu& pause, const Input*) const override {
        pause.DrawMenu();
    }
    /** @brief 戻る入力をこのページで扱うかを返す */
    bool HandlesBack() const override {
        return false;
    }
};

/** @brief ポーズ中のコマンド表と戻る操作を処理するページ */
class PauseMenu::CommandListPage : public PauseMenu::IPage {
public:
    /** @brief コマンド表のページの入力を処理する（戻るでメニューへ戻る） */
    void Update(PauseMenu& pause, Input* input) const override {
        if (pause.commandList_.Update(input)) {
            pause.page_ = &Menu();
        }
    }
    /** @brief コマンド表のページを描画する */
    void Draw(PauseMenu& pause, const Input* input) const override {
        pause.commandList_.Draw(input);
    }
    // 戻るボタンはコマンド表を閉じるのに使う（一時停止は解除しない）
    bool HandlesBack() const override {
        return true;
    }
};

/** @brief コンボ練習の一覧のページ（戻るでメニューへ） */
class PauseMenu::ComboTrialPage : public PauseMenu::IPage {
public:
    /** @brief 一覧の入力を処理する */
    void Update(PauseMenu& pause, Input* input) const override {
        pause.UpdateComboTrialList(input);
    }
    /** @brief 見出しと一覧を描画する */
    void Draw(PauseMenu& pause, const Input*) const override {
        pause.DrawSubMenu(kComboTrialTitle, pause.trialMenu_);
    }
    // 戻るボタンは一覧を閉じるのに使う（一時停止は解除しない）
    bool HandlesBack() const override {
        return true;
    }
};

/** @brief 退出してよいかを聞くページ（戻るでメニューへ） */
class PauseMenu::ConfirmExitPage : public PauseMenu::IPage {
public:
    /** @brief 確認の入力を処理する */
    void Update(PauseMenu& pause, Input* input) const override {
        pause.UpdateConfirmExit(input);
    }
    /** @brief 見出しと いいえ/はい を描画する */
    void Draw(PauseMenu& pause, const Input*) const override {
        pause.DrawSubMenu(kConfirmExitTitle, pause.confirmMenu_);
    }
    // 戻るボタンは確認を閉じるのに使う（一時停止は解除しない）
    bool HandlesBack() const override {
        return true;
    }
};

/** @brief メニューのページの共有インスタンスを返す */
const PauseMenu::IPage& PauseMenu::Menu() {
    static const MenuPage page;
    return page;
}

/** @brief コマンド表のページの共有インスタンスを返す */
const PauseMenu::IPage& PauseMenu::CommandList() {
    static const CommandListPage page;
    return page;
}

/** @brief コンボ練習の一覧のページの共有インスタンスを返す */
const PauseMenu::IPage& PauseMenu::ComboTrialList() {
    static const ComboTrialPage page;
    return page;
}

/** @brief 退出の確認のページの共有インスタンスを返す */
const PauseMenu::IPage& PauseMenu::ConfirmExit() {
    static const ConfirmExitPage page;
    return page;
}

// ══════════════════════════════════════════════════════
// 初期化・更新・描画
// ══════════════════════════════════════════════════════

/** @brief 一時停止メニューの描画に使う部品と、退出項目の文言と動作を受け取って準備する */
void PauseMenu::Initialize(SpriteCommon* spriteCommon, FontRenderer* fontRenderer, Audio* audio,
    const std::string& layoutName, ExitItem exitItem) {
    fontRenderer_ = fontRenderer;
    audio_ = audio;
    layoutName_ = layoutName;
    exitItem_ = std::move(exitItem);

    overlay_ = std::make_unique<Sprite>();
    overlay_->Initialize(spriteCommon, kWhiteTexture);
    overlay_->SetPosition({ 0.0f, 0.0f });
    overlay_->SetSize({ gameConstants::kScreenWidth, gameConstants::kScreenHeight });

    menu_.Initialize(spriteCommon, fontRenderer, audio);
    menu_.BindLayout(layoutName_, "pause_menu", kMenuPosition, kMenuItemSize);
    RebuildItems();
    commandList_.Initialize(fontRenderer, audio);

    UIMenuStyle subStyle;
    subStyle.labelScale = kSubMenuLabelScale;
    subStyle.centerLabels = true;
    trialMenu_.Initialize(spriteCommon, fontRenderer, audio);
    trialMenu_.BindLayout(layoutName_, "pause_trial_menu", kSubMenuPosition, kSubMenuItemSize, subStyle);
    confirmMenu_.Initialize(spriteCommon, fontRenderer, audio);
    confirmMenu_.BindLayout(layoutName_, "pause_confirm_menu", kSubMenuPosition, kSubMenuItemSize, subStyle);
    confirmMenu_.SetItems({ { kConfirmNoLabel }, { kConfirmYesLabel } });
    page_ = &Menu();
}

/** @brief コンボ練習の項目を出す */
void PauseMenu::EnableComboTrial(ComboTrial* trial) {
    comboTrial_ = trial;
    RebuildItems();
}

/** @brief 今の設定に合わせてメニューの項目を並べ直す（音量の値は行1〜3の横に出すので、その並びは変えない） */
void PauseMenu::RebuildItems() {
    rows_ = { Row::kResume, Row::kBgmVolume, Row::kSeVolume, Row::kAutoCombo, Row::kCommandList };
    std::vector<UIButton> items = { { "RESUME" }, { "BGM VOLUME" }, { "SE VOLUME" }, { "AUTO COMBO" }, { "COMMAND LIST" } };
    if (comboTrial_ != nullptr && !comboTrial_->Trials().empty()) {
        rows_.push_back(Row::kComboTrial);
        items.push_back({ "COMBO PRACTICE" });
    }
    rows_.push_back(Row::kExit);
    items.push_back({ exitItem_.label });
    menu_.SetItems(items);
}

/** @brief 開閉と一時停止中の入力を処理する */
bool PauseMenu::Update(Input* input, bool canOpen) {
    // コマンド表を開いている間の戻るボタンはコマンド表を閉じるのに使い、一時停止の解除には回さない
    const bool toggle = input->TriggerAction(Input::Action::kPause)
        || (controller_.IsPaused() && !page_->HandlesBack() && input->TriggerMenuCancel());
    const bool wasPaused = controller_.IsPaused();
    const GamePauseController::UpdateMode mode = controller_.Advance(toggle && canOpen);
    if (mode == GamePauseController::UpdateMode::kMenu) {
        page_->Update(*this, input);
    }
    if (!controller_.IsPaused()) {
        page_ = &Menu(); // 次に一時停止した時はメニューから始める
    }
    // 閉じたフレームも止める（閉じるのに使った戻るボタンがシーン側の操作にまで届かないようにする）
    return wasPaused || mode != GamePauseController::UpdateMode::kPlay;
}

/** @brief メニューページの入力（音量・オートコンボ・決定） */
void PauseMenu::UpdateMenu(Input* input) {
    menu_.Update(input);

    const int index = menu_.GetSelectedIndex();
    const Row row = index >= 0 && index < static_cast<int>(rows_.size()) ? rows_[index] : Row::kResume;
    if (row == Row::kBgmVolume || row == Row::kSeVolume) {
        float delta = 0.0f;
        if (input->TriggerMenuLeft()) {
            delta = -sceneShared::kVolumeStep;
        } else if (input->TriggerMenuRight()) {
            delta = sceneShared::kVolumeStep;
        }
        if (delta != 0.0f) {
            sceneShared::AdjustVolume(audio_, row == Row::kBgmVolume, delta);
        }
    }

    const bool confirmed = menu_.ConsumeConfirm(input);
    if (row == Row::kAutoCombo && (confirmed || input->TriggerMenuLeft() || input->TriggerMenuRight())) {
        sceneShared::ToggleAutoCombo(audio_);
    }
    if (!confirmed) {
        return;
    }
    if (row == Row::kResume) {
        controller_.Resume();
    } else if (row == Row::kCommandList) {
        commandList_.Open();
        page_ = &CommandList();
    } else if (row == Row::kComboTrial) {
        OpenComboTrialList();
    } else if (row == Row::kExit) {
        // 戻ると今の進み具合が失われるので、一度確かめる（カーソルは いいえ から）
        confirmMenu_.SetItems({ { kConfirmNoLabel }, { kConfirmYesLabel } });
        page_ = &ConfirmExit();
    }
}

/** @brief コンボ練習の一覧を、今練習中かに合わせて並べ直して開く */
void PauseMenu::OpenComboTrialList() {
    std::vector<UIButton> items;
    for (const ComboTrial::Trial& trial : comboTrial_->Trials()) {
        items.push_back({ trial.name });
    }
    if (comboTrial_->IsActive()) {
        items.push_back({ kStopTrialLabel });
    }
    items.push_back({ kBackLabel });
    trialMenu_.SetItems(items);
    page_ = &ComboTrialList();
}

/** @brief コンボ練習の一覧の入力（コンボ名で練習を始めて再開、練習をやめる、もどる） */
void PauseMenu::UpdateComboTrialList(Input* input) {
    trialMenu_.Update(input);
    if (input->TriggerMenuCancel()) {
        page_ = &Menu();
        return;
    }
    if (!trialMenu_.ConsumeConfirm(input)) {
        return;
    }
    const size_t index = static_cast<size_t>(trialMenu_.GetSelectedIndex());
    const size_t trialCount = comboTrial_->Trials().size();
    if (index < trialCount) {
        comboTrial_->Start(index);
        controller_.Resume();
    } else if (index == trialCount && comboTrial_->IsActive()) {
        comboTrial_->Stop();
        controller_.Resume();
    } else {
        page_ = &Menu();
    }
}

/** @brief 退出の確認の入力（はいで退出、いいえ・戻るでメニューへ） */
void PauseMenu::UpdateConfirmExit(Input* input) {
    confirmMenu_.Update(input);
    if (input->TriggerMenuCancel()) {
        page_ = &Menu();
        return;
    }
    if (!confirmMenu_.ConsumeConfirm(input)) {
        return;
    }
    if (confirmMenu_.GetSelectedIndex() != kConfirmYesRow) {
        page_ = &Menu();
        return;
    }
    controller_.Resume();
    if (exitItem_.onSelect) {
        exitItem_.onSelect();
    }
}

/** @brief 見出しと選択肢を中央に積む */
void PauseMenu::DrawSubMenu(const wchar_t* title, UIMenu& menu) {
    UILayout& layout = UILayout::Get(layoutName_);
    menu.Draw();
    fontRenderer_->DrawStringCenteredW(title, layout.Float("pause.sub_title_y", kSubTitleY),
        layout.Float("pause.sub_title_scale", kSubTitleScale), layout.Color("pause.title_color", kTitleColor));
}

/** @brief 一時停止中なら暗幕と今のページを積む（一時停止中でなければ何もしない） */
void PauseMenu::Draw(const Input* input) {
    if (!controller_.IsPaused()) {
        return;
    }
    UILayout& layout = UILayout::Get(layoutName_);
    overlay_->SetColor(layout.Color("pause.background_color", kOverlayColor));
    overlay_->Update();
    overlay_->Draw();
    page_->Draw(*this, input);
}

/** @brief メニューページの描画（項目・現在値・見出し） */
void PauseMenu::DrawMenu() {
    UILayout& layout = UILayout::Get(layoutName_);
    menu_.Draw();

    const float valueScale = layout.Float("pause.volume_scale", kValueTextScale);
    const Vector4 valueColor = layout.Color("pause.volume_color", kValueTextColor);
    const GameSettings& settings = GameSettingsManager::GetInstance()->Get();
    auto drawVolume = [&](const char* key, const Vector2& defaultPosition, float volume) {
        const Vector2 position = layout.Pos(key, defaultPosition);
        fontRenderer_->DrawString(sceneShared::VolumeLabel(volume), position.x, position.y, valueScale, valueColor);
    };
    drawVolume("pause.bgm_volume_pos", kBgmVolumeTextPosition, settings.bgmVolume);
    drawVolume("pause.se_volume_pos", kSeVolumeTextPosition, settings.seVolume);
    const Vector2 autoComboPosition = layout.Pos("pause.auto_combo_pos", kAutoComboTextPosition);
    fontRenderer_->DrawString(sceneShared::AutoComboLabel(), autoComboPosition.x, autoComboPosition.y, valueScale, valueColor);

    const Vector2 titlePosition = layout.Pos("pause.title_pos", kTitlePosition);
    fontRenderer_->DrawString("PAUSED", titlePosition.x, titlePosition.y,
        layout.Float("pause.title_scale", kTitleScale), layout.Color("pause.title_color", kTitleColor));
}

} // namespace engine::game
