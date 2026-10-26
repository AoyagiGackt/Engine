/**
 * @file PauseMenu.h
 * @brief プレイ中のシーンで共通に使う一時停止画面（再開・音量・オートコンボ・コマンド表・コンボ練習・退出の確認）を定義するファイル
 */
#pragma once
#include "CommandListView.h"
#include "GamePauseController.h"
#include "UIMenu.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine {
class Audio;
class Input;
} // namespace engine

namespace engine::game {

class ComboTrial;
class FontRenderer;

/**
 * @brief Pauseキーで開閉する一時停止画面
 * @note シーンは毎フレーム Update() を呼び、true が返ったらゲームの更新を止める。
 *       描画は Draw() を FontRenderer::Draw() の前に呼ぶ（暗幕のスプライトと文字を積む）。
 *       退出項目の文言と遷移先はシーンごとに Initialize() で渡す。
 */
class PauseMenu {
public:
    /** @brief メニュー最下段の退出項目（タイトルへ戻る、トレーニングへ戻る など） */
    struct ExitItem {
        std::string label; ///< 項目の表示名
        std::function<void()> onSelect; ///< 選ばれた時の処理（シーン遷移など）
    };

    /**
     * @brief スプライトと項目を用意する
     * @param spriteCommon 暗幕とメニュー枠の描画に使う
     * @param fontRenderer 文字を積む先（Reset/Drawはシーン側が行う）
     * @param audio        効果音・音量変更の対象
     * @param layoutName   UILayout の名前（F2で動かした配置はそのシーンのレイアウトへ保存される）
     * @param exitItem     最下段の退出項目
     */
    void Initialize(SpriteCommon* spriteCommon, FontRenderer* fontRenderer, engine::Audio* audio,
        const std::string& layoutName, ExitItem exitItem);

    /**
     * @brief 開閉と一時停止中の入力を処理する
     * @param input   入力
     * @param canOpen 今開閉してよいか（武器交換中などは false）
     * @return このフレームのゲーム更新を止めるなら true（開いた・閉じたフレームも含む）
     */
    bool Update(engine::Input* input, bool canOpen = true);

    /**
     * @brief コンボ練習の項目を出す（Initialize の後に呼ぶ。呼ばなければ項目は出ない）
     * @param trial 練習の本体（非所有。シーンが持つ）
     */
    void EnableComboTrial(ComboTrial* trial);

    /** @brief ポーズ中かを返す */
    bool IsPaused() const {
        return controller_.IsPaused();
    }

    /** @brief 一時停止中なら暗幕と今のページを積む（一時停止中でなければ何もしない） */
    void Draw(const engine::Input* input);

private:
    class IPage;
    class MenuPage;
    class CommandListPage;
    class ComboTrialPage;
    class ConfirmExitPage;
    static const IPage& Menu();
    static const IPage& CommandList();
    static const IPage& ComboTrialList();
    static const IPage& ConfirmExit();

    /** @brief メニューの行の種類（並び順は RebuildItems が決める） */
    enum class Row {
        kResume,
        kBgmVolume,
        kSeVolume,
        kAutoCombo,
        kCommandList,
        kComboTrial,
        kExit,
    };
    /** @brief 今の設定に合わせてメニューの項目を並べ直す */
    void RebuildItems();
    /** @brief コンボ練習の一覧を、今練習中かに合わせて並べ直して開く */
    void OpenComboTrialList();
    /** @brief コンボ練習の一覧の入力（決定で練習を始めて再開、戻るでメニューへ） */
    void UpdateComboTrialList(engine::Input* input);
    /** @brief 退出の確認の入力（はいで退出、いいえ・戻るでメニューへ） */
    void UpdateConfirmExit(engine::Input* input);
    /** @brief 見出しと選択肢を中央に積む（コンボ練習の一覧と退出の確認で共通） */
    void DrawSubMenu(const wchar_t* title, UIMenu& menu);

    /** @brief メニューページの入力（音量・オートコンボ・決定） */
    void UpdateMenu(engine::Input* input);
    /** @brief メニューページの描画（項目・現在値・見出し） */
    void DrawMenu();

    GamePauseController controller_;
    const IPage* page_ = nullptr;
    UIMenu menu_;
    std::vector<Row> rows_; ///< メニューの行番号ごとの種類
    UIMenu trialMenu_; ///< コンボ練習の一覧（練習するコンボ名・練習をやめる・もどる）
    UIMenu confirmMenu_; ///< 退出の確認（いいえ・はい）
    ComboTrial* comboTrial_ = nullptr;
    CommandListView commandList_;
    std::unique_ptr<engine::graphics::Sprite> overlay_;
    FontRenderer* fontRenderer_ = nullptr;
    engine::Audio* audio_ = nullptr;
    std::string layoutName_;
    ExitItem exitItem_;
};

} // namespace engine::game
