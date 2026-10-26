/**
 * @file SoundAssignEditor.h
 * @brief 効果音・BGM・演出の音を割り当てるエディター
 */
#pragma once
#include "json.hpp"
#include <string>
#include <vector>

namespace engine::game {
/** @brief JSON の音声割り当てを編集する */
class SoundAssignEditor {
public:
    /** @brief 開いている間だけ音の割り当て画面を描く */
    void Draw(bool* open);

private:
    bool initialized_ = false;
    std::vector<nlohmann::json> original_;
    std::vector<nlohmann::json> edited_;
    std::vector<std::vector<std::string>> effectPaths_;
    std::vector<std::string> seFiles_;
    std::vector<std::string> bgmFiles_;
    std::vector<std::string> styles_;
    std::vector<char> styleInput_;
    std::string status_;
};
} // namespace engine::game
