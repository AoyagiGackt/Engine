/**
 * @file GameSounds.cpp
 * @brief 場面ごとの効果音（sounds.json）の読み込みと再生（gameSounds）の実装
 */
#include "GameSounds.h"
#include "AudioBridge.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "Player.h"
#include "TimeManager.h"
#include <unordered_map>
#include <iterator>
#include <string>

namespace engine::game::gameSounds {
namespace {
constexpr const char* kConfigPath = "Resources/Config/sounds.json";
constexpr float kDefaultVolume = 1.0f;

/** @brief 場面1つに割り当てた音 */
struct CueSound {
    std::string path; ///< 鳴らす音のパス（空なら鳴らさない）
    float volume = kDefaultVolume; ///< 音量（0〜1）
};

/** @brief SoundCue の並び順に、sounds.json での項目名 */
constexpr const char* kCueKeys[] = {
    "dodge",
    "swing",
    "shot",
    "finisherBeat",
    "finisherRelease",
    "glassShatter",
};
static_assert(std::size(kCueKeys) == static_cast<size_t>(SoundCue::kCount), "SoundCue と項目名の数をそろえる");

using CueTable = std::unordered_map<std::string, CueSound>;

/** @brief sounds.json の項目をすべて名前で引ける形に読む（path を持つ項目だけ） */
CueTable LoadCues() {
    CueTable cues;
    const nlohmann::json root = JsonHelper::Load(kConfigPath);
    if (!root.is_object()) {
        Logger::LogWarning(std::string("GameSounds: ") + kConfigPath + " が読めません。効果音を鳴らしません");
        return cues;
    }
    for (const auto& [key, entry] : root.items()) {
        if (!entry.is_object()) {
            continue;
        }
        cues[key] = { entry.value("path", std::string()), entry.value("volume", kDefaultVolume) };
    }
    return cues;
}

/** @brief 読み込み済みの割り当て（初回と保存時に読む） */
CueTable& Cues() {
    static CueTable sCues = LoadCues();
    return sCues;
}

/** @brief 名前の音のパスが書いてあれば鳴らす（鳴らしたら true） */
bool PlayKey(const std::string& key) {
    const auto it = Cues().find(key);
    if (it == Cues().end() || it->second.path.empty()) {
        return false;
    }
    AudioBridge::GetInstance()->PlaySE(it->second.path, it->second.volume);
    return true;
}
} // namespace

/** @brief 効果音の割り当てを読み直す */
void Reload() {
    Cues() = LoadCues();
}

/** @brief 場面に割り当てた効果音を鳴らす（パスが空なら何もしない） */
void Play(SoundCue cue) {
    const size_t index = static_cast<size_t>(cue);
    if (index < std::size(kCueKeys)) {
        PlayKey(kCueKeys[index]);
    }
}

/** @brief sounds.json の 名前_style の音を鳴らす（無いか空なら 名前 の音） */
void PlayStyled(const std::string& name, const std::string& style) {
    if (!style.empty() && PlayKey(name + "_" + style)) {
        return;
    }
    PlayKey(name);
}

/** @brief プレイヤーのこのフレームの行動（回避・攻撃の振り・射撃）に合わせて鳴らす */
void PlayPlayerActions(const Player& player) {
    // 止まっている間は行動の印が前のフレームのまま残っているので鳴らさない
    if (TimeManager::GetInstance()->IsHitStopped()) {
        return;
    }
    if (player.JustDodged()) {
        Play(SoundCue::kDodge);
    }
    if (player.JustStartedSwing()) {
        Play(SoundCue::kSwing);
    }
    if (player.JustFired()) {
        Play(SoundCue::kShot);
    }
}

} // namespace engine::game::gameSounds
