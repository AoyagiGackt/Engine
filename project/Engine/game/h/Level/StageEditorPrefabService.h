/**
 * @file StageEditorPrefabService.h
 * @brief ステージエディタのプレハブ保存と読み込みを提供するファイル
 */
#pragma once
#include "LevelLoader.h"
#include <string>
#include <vector>

namespace engine::game {

/** @brief 配置物データを再利用可能なプレハブとして永続化するクラス */
class StageEditorPrefabService {
public:
    /** @brief 配置物を原点基準のプレハブとして保存し、保存先を返す */
    static std::string Save(const std::string& name, const ObjectDesc& object);

    /** @brief 名前に対応するプレハブ内の配置物を読み込む */
    static std::vector<ObjectDesc> Load(const std::string& name);

    /** @brief ファイル名に利用できる安全なプレハブ名へ変換する */
    static std::string SanitizeName(const std::string& name);

    /** @brief プレハブ名から保存先の相対パスを構築する */
    static std::string MakePath(const std::string& name);
};

} // namespace engine::game
