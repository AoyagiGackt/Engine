/**
 * @file CommandListView.h
 * @brief 持っている武器ごとの技と入力を一覧で見せるコマンド表の画面部品を定義するファイル
 */
#pragma once
#include <map>
#include <string>
#include <vector>

namespace engine {
class Input;
class Audio;
} // namespace engine

namespace engine::game {

class FontRenderer;
enum class WeaponType;

/** @brief コマンド表の1行（技1つぶん） */
struct CommandMove {
    std::wstring category; ///< 区分（地上・空中・打ち上げ・固有技など）
    std::wstring name; ///< 技名
    std::wstring input; ///< 入力（{Attack} などの操作名は表示の時にボタン名へ置き換える）
    std::wstring detail; ///< 下の説明欄に出す詳しい説明
};

/** @brief 1武器ぶんのコマンド表 */
struct CommandSheet {
    std::wstring tagline; ///< 武器の一言説明
    std::vector<CommandMove> moves; ///< その武器だけの技
};

/**
 * @brief コマンド表（Resources/Config/command_list.json）
 * @note 武器ごとの技に、全武器共通の操作（銃・回避・覚醒など）を後ろへ足して並べる。
 *       上で武器を切り替え、真ん中の一覧で技を選ぶと、下の欄にその技の説明が出る。
 *       まだ手に入れていない武器は名前も技も伏せる。
 */
class CommandListView {
public:
    /**
     * @brief 文字の描画先と効果音の再生先を受け取り、JSON を読み込む
     * @param fontRenderer 文字とパネルを積む先（描画の実行は呼び出し側の Draw() で行う）
     * @param audio        カーソル移動の効果音（nullptrなら鳴らさない）
     */
    void Initialize(FontRenderer* fontRenderer, engine::Audio* audio);

    /** @brief 開いた時に呼ぶ（今装備している武器のページから始める） */
    void Open();

    /**
     * @brief 武器の切り替え・技の選択を受け付ける
     * @return 戻る操作がされたらtrue
     */
    bool Update(engine::Input* input);

    /** @brief 文字とパネルを積む（毎フレーム、FontRenderer::Reset() の後に呼ぶ） */
    void Draw(const engine::Input* input);

private:
    /** @brief 今のページに並べる技（武器の技＋共通の操作）を作り直す */
    void RebuildRows();
    /** @brief 指定方向へ、持っている武器の中で次のページへ移る（持っていない武器も伏せて並べる） */
    void MoveWeapon(int step);
    /** @brief 説明文を指定幅で折り返す */
    static std::wstring WrapText(const std::wstring& text, float maxWidth, float scale);

    FontRenderer* fontRenderer_ = nullptr;
    engine::Audio* audio_ = nullptr;

    std::map<WeaponType, CommandSheet> sheets_; ///< 武器ごとの技
    std::vector<CommandMove> commonMoves_; ///< 全武器共通の操作
    std::vector<const CommandMove*> rows_; ///< 今のページに並んでいる技
    int weaponIndex_ = 0; ///< WeaponManager::GetList() の添字
    int cursor_ = 0; ///< 選んでいる行
    int scroll_ = 0; ///< 一覧の先頭に出している行
};

} // namespace engine::game
