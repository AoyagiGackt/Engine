/**
 * @file RunData.h
 * @brief ローグライトの1ランを通じて保持するゲームデータを管理するファイル
 */
#pragma once
#include <algorithm>
#include <vector>
namespace engine::game {
/**
 * @brief ローグライトの1ランを通じて保持するデータを管理するシングルトンクラス
 * @note シーン遷移をまたいで HP・ゴールド・スキル・フロア番号を共有する
 * 新しいランを開始する際は StartNewRun() で全データをリセットすること
 */
class RunData {
public:
    /** @brief RunData の唯一のインスタンスを取得する */
    static RunData* GetInstance() {
        static RunData inst;
        return &inst;
    }

    /** @brief プレイヤーが習得可能なスキルの種類 */
    enum class Skill {
        BlinkPlus,      ///< ブリンク距離 x1.5
        ComboExtend,    ///< コンボ最大数 +1
        FastFire,       ///< 連射速度 2倍
        AwakenBoost,    ///< 覚醒ゲージ蓄積 x1.5
        SpeedUp,        ///< 移動速度 x1.2
        HighJump,       ///< ジャンプ力 x1.25
        JuggleExtend,   ///< 乱舞スラッシュ +4
        StylePersist,   ///< スタイルメーター減少 x0.6
        kCount
    };
    static constexpr int kSkillCount = static_cast<int>(Skill::kCount);

    /** @brief マップ上のノードの種類 */
    enum class NodeType { Combat, Elite, Shop, Rest, Boss };

    /** @brief ラン進行中かどうか */
    bool IsRunActive() const { return isRunActive_; }

    /** @brief 現在 HP */
    int GetHp() const { return hp_; }

    /** @brief 最大 HP */
    int GetMaxHp() const { return maxHp_; }

    /** @brief 所持ゴールド */
    int GetGold() const { return gold_; }

    /** @brief 現在フロア（0=floor1, 1=floor2, 2=floor3, 3=boss） */
    int GetFloor() const { return floor_; }

    /** @brief 現在選択されているノード種別 */
    NodeType GetCurrentNode() const { return currentNode_; }

    /** @brief 習得済みスキルのリスト */
    const std::vector<Skill>& GetSkills() const { return skills_; }

    /** @brief 選択中のノード種別を設定する */
    void SetCurrentNode(NodeType node) { currentNode_ = node; }

    /** @brief フロアを1つ進める */
    void AdvanceFloor() { ++floor_; }

    /** @brief ゴールドを加算する */
    void AddGold(int amount) { gold_ += amount; }

    /**
     * @brief HPを回復する（maxHpを超えない）
     * @param amount 回復量
     */
    void Heal(int amount) { hp_ = (std::min)(hp_ + amount, maxHp_); }

    /**
     * @brief 指定スキルを習得済みかどうかを返す
     * @param s チェックするスキル
     * @return 習得済みなら true
     */
    bool HasSkill(Skill s) const {
        for (auto sk : skills_) {
            if (sk == s) { return true; }
        }
        return false;
    }

    /**
     * @brief スキルを習得リストに追加する
     * @param s 追加するスキル
     */
    void AddSkill(Skill s) { skills_.push_back(s); }

    /**
     * @brief 新しいランを開始し、全データを初期状態にリセットする
     */
    void StartNewRun() {
        isRunActive_ = true;
        hp_ = maxHp_ = 30;
        gold_ = 0;
        floor_ = 0;
        skills_.clear();
        currentNode_ = NodeType::Combat;
    }

    /**
     * @brief セーブデータからラン状態を復元する
     * @note SaveDataManager::LoadContinue から呼び出される
     */
    void RestoreFromSave(int hp, int maxHp, int gold, int floor, NodeType node, const std::vector<Skill>& skills) {
        isRunActive_ = true;
        hp_          = hp;
        maxHp_       = maxHp;
        gold_        = gold;
        floor_       = floor;
        currentNode_ = node;
        skills_      = skills;
    }

    /**
     * @brief スキルの表示名を返す
     * @param s 名前を取得したいスキル
     * @return スキルの表示文字列
     */
    static const char* SkillName(Skill s) {
        switch (s) {
        case Skill::BlinkPlus:    return "BLINK+    dist x1.5";
        case Skill::ComboExtend:  return "COMBO+    max +1";
        case Skill::FastFire:     return "FASTFIRE  rate x2";
        case Skill::AwakenBoost:  return "AWAKEN+   gauge x1.5";
        case Skill::SpeedUp:      return "SPEED+    move x1.2";
        case Skill::HighJump:     return "HIGHJUMP  jump x1.25";
        case Skill::JuggleExtend: return "JUGGLE+   +4 slashes";
        case Skill::StylePersist: return "STYLEKEEP decay x0.6";
        default:                  return "???";
        }
    }

    /**
     * @brief スタイルゲージのピーク値からランク文字列を返す
     * @param peak スタイルゲージのピーク値（0.0〜1.0）
     * @return ランク文字列（"SSS" 〜 "D"）
     */
    static const char* CalcRank(float peak) {
        if (peak >= 0.95f) { return "SSS"; }
        if (peak >= 0.90f) { return "SS"; }
        if (peak >= 0.80f) { return "S"; }
        if (peak >= 0.65f) { return "A"; }
        if (peak >= 0.45f) { return "B"; }
        if (peak >= 0.25f) { return "C"; }
        return "D";
    }

    /**
     * @brief スタイルゲージのピーク値から獲得ゴールドを計算して返す
     * @param peak スタイルゲージのピーク値（0.0〜1.0）
     * @return 獲得ゴールド量
     */
    static int CalcGold(float peak) {
        if (peak >= 0.95f) { return 50; }
        if (peak >= 0.90f) { return 35; }
        if (peak >= 0.80f) { return 25; }
        if (peak >= 0.65f) { return 18; }
        if (peak >= 0.45f) { return 12; }
        if (peak >= 0.25f) { return  8; }
        return 5;
    }

private:
    RunData() = default;

    bool     isRunActive_ = false;            ///< ラン進行中かどうか
    int      hp_          = 30;               ///< 現在 HP
    int      maxHp_       = 30;               ///< 最大 HP
    int      gold_        = 0;                ///< 所持ゴールド
    int      floor_       = 0;                ///< 現在フロア（0=floor1, 1=floor2, 2=floor3, 3=boss）
    NodeType currentNode_ = NodeType::Combat;  ///< 現在選択されているノード種別

    /** @brief 習得済みスキルのリスト */
    std::vector<Skill> skills_;
};

} // namespace engine::game
