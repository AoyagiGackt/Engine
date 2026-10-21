/**
 * @file TitleScene.cpp
 * @brief タイトル画面のメニュー表示と入力待ち・シーン遷移（TitleScene）の実装
 */
#include "TitleScene.h"
#include "GameConstants.h"
#include "GameSettings.h"
#include "HitEffectLibrary.h"
#include "ModelManager.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "TitleDemoTuning.h"
#include "UILayout.h"
#include "SkinnedObject3d.h"
#include "SrvManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <numbers>
#include <random>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine::graphics;

namespace engine::game {
namespace {
// デモの調整値は Resources/Config/title_demo.json（TitleDemoTuning）、命中演出の形は hit_effects.json の title_ レシピ

// メニューの既定の配置（F2のエディタで動かした値は Resources/Config/UI/title.json に保存される）
constexpr const char* kLayoutName = "title";
constexpr Vector2 kMenuPosition = { 440.0f, 470.0f };
constexpr Vector2 kMenuItemSize = { 400.0f, 48.0f };

// 設定メニュー（タイトル上でメニューの中身だけ入れ替えて表示する）
constexpr int kOptionsRowBgm = 0;
constexpr int kOptionsRowSe = 1;
constexpr int kOptionsRowCommandList = 2;
constexpr int kOptionsRowBack = 3;
constexpr float kVolumeTextOffsetX = 260.0f; // メニュー左端から音量表示までの距離
constexpr float kVolumeTextTopRatio = 0.3f; // UIMenuのラベルと同じ高さに揃える
constexpr float kVolumeTextScale = 1.5f;
constexpr Vector4 kVolumeTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };

/** @brief 設定メニューの行を表示順に作る */
std::vector<UIButton> OptionsMenuItems()
{
    return { { "BGM VOLUME" }, { "SE VOLUME" }, { "COMMAND LIST" }, { "BACK" } };
}

} // namespace

/** @brief シーンの初期化 */
void TitleScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    fontRenderer_.Initialize(spriteCommon_.get());

    menu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    optionsMenu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    menu_.BindLayout(kLayoutName, "menu", kMenuPosition, kMenuItemSize);
    optionsMenu_.BindLayout(kLayoutName, "menu", kMenuPosition, kMenuItemSize);
    menu_.SetItems({
        { "NEW GAME" },
        { "CONTINUE", SaveDataManager::GetInstance()->HasContinue() },
        { "TRAINING" },
        { "OPTIONS" },
    });
    optionsMenu_.SetItems(OptionsMenuItems());
    commandList_.Initialize(&fontRenderer_, audio_);
    menuPage_ = &MainMenu();

    InitializeDemo();
    floatingTitle_.Initialize(dxCommon_, input_, modelCommon_.get(), shadowManager_.get());
    audio_->PlaySceneBGM(BgmKey::kTitle);
}






// ══════════════════════════════════════════════════════
// メニューのページ（メイン / 設定 / コマンド表）
// ══════════════════════════════════════════════════════

/** @brief メインメニューの入力と描画をまとめるページ */
class TitleScene::MainMenuPage : public TitleScene::IMenuPage {
public:
    /** @brief メインメニューの入力を処理する */
    void Update(TitleScene& scene) const override {
        scene.UpdateMainMenu();
    }
    /** @brief メインメニューを描画する */
    void Draw(TitleScene& scene) const override {
        scene.menu_.Draw();
    }
};

/** @brief 音量設定とコマンド表への操作を受け付けるページ */
class TitleScene::OptionsMenuPage : public TitleScene::IMenuPage {
public:
    /** @brief 設定メニューの入力を処理する */
    void Update(TitleScene& scene) const override {
        scene.UpdateOptionsMenu();
    }
    /** @brief 設定メニューを描画する */
    void Draw(TitleScene& scene) const override
    {
        scene.optionsMenu_.Draw();
        const GameSettings& settings = GameSettingsManager::GetInstance()->Get();
        scene.DrawVolumeValue(kOptionsRowBgm, settings.bgmVolume);
        scene.DrawVolumeValue(kOptionsRowSe, settings.seVolume);
    }
};

/** @brief コマンド表を表示し設定ページへ戻る操作を処理するページ */
class TitleScene::CommandListPage : public TitleScene::IMenuPage {
public:
    /** @brief コマンド表のページの入力を処理する（戻るで設定へ戻る） */
    void Update(TitleScene& scene) const override
    {
        if (scene.commandList_.Update(scene.input_))
        {
            scene.menuPage_ = &OptionsMenu();
        }
    }
    /** @brief コマンド表のページを描画する */
    void Draw(TitleScene& scene) const override {
        scene.commandList_.Draw(scene.input_);
    }
};

/** @brief メインメニューの共有インスタンスを返す */
const TitleScene::IMenuPage& TitleScene::MainMenu()
{
    static const MainMenuPage page;
    return page;
}

/** @brief 設定メニューの共有インスタンスを返す */
const TitleScene::IMenuPage& TitleScene::OptionsMenu()
{
    static const OptionsMenuPage page;
    return page;
}

/** @brief コマンド表のページの共有インスタンスを返す */
const TitleScene::IMenuPage& TitleScene::CommandListMenu()
{
    static const CommandListPage page;
    return page;
}

/** @brief シーンの更新処理 */
void TitleScene::Update()
{
    menuPage_->Update(*this);

    UpdateDemo();

    floatingTitle_.Update(!GetStageEditor().IsVisible());
}

/** @brief NEW GAME等の選択と決定（OPTIONSでは画面を切り替えず設定メニューを開く） */
void TitleScene::UpdateMainMenu()
{
    // メニューの行（Initialize の SetItems と同じ並び）を選んだ時の処理
    using RowAction = void (*)(TitleScene&);
    static constexpr RowAction kMainMenuActions[] = {
        [](TitleScene& scene) { // NEW GAME
            RunData::GetInstance()->StartNewRun();
            WeaponManager::GetInstance()->ResetForNewRun();
            // タイトルを抜ける時はこの退避先へ戻すので、デモ前の状態ではなく初期装備を持ったランの開始状態にしておく
            scene.weaponManagerSnapshot_ = WeaponManager::GetInstance()->SaveSnapshot();
            SaveDataManager::GetInstance()->ClearContinue();
            SceneFlow::GetInstance()->Transition("TITLE", "new_game", "MAP");
        },
        [](TitleScene& scene) { // CONTINUE
            WeaponManager::GetInstance()->RestoreSnapshot(scene.weaponManagerSnapshot_);
            SaveDataManager::GetInstance()->LoadContinue(*RunData::GetInstance());
            SceneFlow::GetInstance()->Transition("TITLE", "continue", "MAP");
        },
        [](TitleScene& scene) { // TRAINING
            WeaponManager::GetInstance()->RestoreSnapshot(scene.weaponManagerSnapshot_);
            SceneFlow::GetInstance()->Transition("TITLE", "training", "TRAINING");
        },
        [](TitleScene& scene) { // OPTIONS
            // 画面は切り替えず、メニューの中身だけ設定項目に入れ替える
            scene.menuPage_ = &OptionsMenu();
            scene.optionsMenu_.SetItems(OptionsMenuItems()); // 開くたびにカーソルを先頭へ戻す
        },
    };

    menu_.Update(input_);
    if (menu_.ConsumeConfirm(input_))
    {
        const int row = menu_.GetSelectedIndex();
        if (row >= 0 && row < static_cast<int>(std::size(kMainMenuActions)))
        {
            kMainMenuActions[row](*this);
        }
    }
}

/** @brief 設定メニューの音量調整と、BACK/Backspaceでメインメニューへ戻る処理 */
void TitleScene::UpdateOptionsMenu()
{
    optionsMenu_.Update(input_);

    const int row = optionsMenu_.GetSelectedIndex();
    if (row == kOptionsRowBgm || row == kOptionsRowSe)
    {
        if (input_->TriggerMenuLeft())
        {
            SceneShared::AdjustVolume(audio_, row == kOptionsRowBgm, -SceneShared::kVolumeStep);
        }
        if (input_->TriggerMenuRight())
        {
            SceneShared::AdjustVolume(audio_, row == kOptionsRowBgm, SceneShared::kVolumeStep);
        }
    }

    const bool confirmed = optionsMenu_.ConsumeConfirm(input_);
    if (confirmed && row == kOptionsRowCommandList)
    {
        commandList_.Open();
        menuPage_ = &CommandListMenu();
        return;
    }
    const bool backSelected = confirmed && row == kOptionsRowBack;
    const bool cancelPressed = input_->TriggerMenuCancel();
    if (cancelPressed)
    {
        audio_->PlayMenuSelect();
    }
    if (backSelected || cancelPressed)
    {
        menuPage_ = &MainMenu();
    }
}

/** @brief エディタ表示中（デモ停止中）もカメラ移動に合わせてデモの見た目の行列だけ更新する */
void TitleScene::RefreshVisualTransformsForEditor()
{
    // エディタの自由カメラで視点を動かしても、止まっているデモの見た目が画面に張り付かないようにする
    skydome_->Update(camera_.get());
    player_->RefreshVisualTransforms();
    for (auto& dummy : demoDummies_)
    {
        dummy.object->Update();
    }
}

/** @brief シーンの描画処理 */
void TitleScene::Draw()
{
    // エディタ表示中はUpdate()が止まるため、文字コマンドの破棄は毎フレーム必ず通るここで行う
    fontRenderer_.Reset();
    DrawDemoShadowPass();
    DrawDemoWorld();
    GetStageEditor().DrawObjects();
    floatingTitle_.Draw();

    spriteCommon_->CommonDrawSettings();

    menuPage_->Draw(*this);
    GetStageEditor().DrawUIText(fontRenderer_);
    fontRenderer_.Draw();
}

/** @brief 設定メニューの音量行の右側に現在の音量を百分率で描く */
void TitleScene::DrawVolumeValue(int row, float volume)
{
    UILayout& layout = UILayout::Get(kLayoutName);
    const float offsetX = layout.Float("options.volume_offset_x", kVolumeTextOffsetX);
    const float topRatio = layout.Float("options.volume_top_ratio", kVolumeTextTopRatio);
    const float scale = layout.Float("options.volume_scale", kVolumeTextScale);
    const Vector4 color = layout.Color("options.volume_color", kVolumeTextColor);

    const Vector2 menuPosition = optionsMenu_.GetPosition();
    const float itemHeight = optionsMenu_.GetItemSize().y;
    const float y = menuPosition.y + itemHeight * (static_cast<float>(row) + topRatio);
    fontRenderer_.DrawString(SceneShared::VolumeLabel(volume), menuPosition.x + offsetX, y, scale, color);
}

/** @brief 背景デモのシャドウパス描画 */
void TitleScene::DrawDemoShadowPass()
{
    // TrainingScene::Draw()と同じ最小構成（シャドウマップの状態遷移だけ行い、投影物は描かない）
    shadowManager_->RunShadowPass(dxCommon_->GetCommandList(), modelCommon_.get());
}

/** @brief 背景デモの本描画（3Dワールド） */
void TitleScene::DrawDemoWorld()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    // 影のパスの描画先から通常画面へ戻す
    dxCommon_->SetBackBufferRenderTarget();

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(commandList);
    shadowManager_->SetShadowMap(commandList, SrvManager::GetInstance());
    skydome_->Draw();
    for (auto& dummy : demoDummies_)
    {
        if (!dummy.defeated)
        {
            dummy.object->Draw();
        }
    }
    player_->Draw();
    if (particleManager_)
    {
        particleManager_->Update(camera_.get());
        particleManager_->Draw(camera_.get());
    }
}

/** @brief シーンの終了処理 */
void TitleScene::Finalize()
{
    // デモ用のグループを残すと、本編が同名のグループを生成する際に停止する。
    // シーン切り替え元でGPUの完了を待ってから呼ばれるので、安全に解放できる。
    if (particleManager_)
    {
        particleManager_->ClearAllGroups();
        particleManager_ = nullptr;
    }
    // デモ用に全解放していた武器を元に戻し、自動操作用のオーバーライドも解除する
    // （NEW GAME では退避先をランの開始状態に差し替え済み。CONTINUE/TRAINING は既に戻し済みで、二重に戻しても無害）
    WeaponManager::GetInstance()->RestoreSnapshot(weaponManagerSnapshot_);
    input_->ClearActionOverrides();
}

} // namespace engine::game
