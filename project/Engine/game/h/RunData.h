#pragma once
#include <vector>

// ゲームの1ランを通じて保持されるデータ（シングルトン）
class RunData {
public:
    static RunData* GetInstance() {
        static RunData inst;
        return &inst;
    }

    enum class Skill {
        BlinkPlus,      // ブリンク距離 x1.5
        ComboExtend,    // コンボ最大数 +1
        FastFire,       // 連射速度 2倍
        AwakenBoost,    // 覚醒ゲージ蓄積 x1.5
        SpeedUp,        // 移動速度 x1.2
        HighJump,       // ジャンプ力 x1.25
        JuggleExtend,   // 乱舞スラッシュ +4
        StylePersist,   // スタイルメーター減少 x0.6
        kCount
    };
    static constexpr int kSkillCount = static_cast<int>(Skill::kCount);

    enum class NodeType { Combat, Elite, Shop, Rest, Boss };

    bool     isRunActive = false;
    int      hp          = 30;
    int      maxHp       = 30;
    int      gold        = 0;
    int      floor       = 0;   // 0=floor1, 1=floor2, 2=floor3, 3=boss
    NodeType currentNode = NodeType::Combat;

    std::vector<Skill> skills;

    bool HasSkill(Skill s) const {
        for (auto sk : skills) if (sk == s) return true;
        return false;
    }
    void AddSkill(Skill s) { skills.push_back(s); }

    void StartNewRun() {
        isRunActive = true;
        hp = maxHp = 30;
        gold = 0;
        floor = 0;
        skills.clear();
        currentNode = NodeType::Combat;
    }

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

    // スタイルランク・ゴールド計算
    static const char* CalcRank(float peak) {
        if (peak >= 0.95f) return "SSS";
        if (peak >= 0.90f) return "SS";
        if (peak >= 0.80f) return "S";
        if (peak >= 0.65f) return "A";
        if (peak >= 0.45f) return "B";
        if (peak >= 0.25f) return "C";
        return "D";
    }
    static int CalcGold(float peak) {
        if (peak >= 0.95f) return 50;
        if (peak >= 0.90f) return 35;
        if (peak >= 0.80f) return 25;
        if (peak >= 0.65f) return 18;
        if (peak >= 0.45f) return 12;
        if (peak >= 0.25f) return  8;
        return 5;
    }

private:
    RunData() = default;
};
