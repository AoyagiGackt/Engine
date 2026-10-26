/**
 * @file JsonConfigEditor.h
 * @brief 設定フォルダのJSONを一覧し、スライダーや色の選択で直して保存できる汎用の調整値エディタ
 * @note ゲームに依存しないエンジン層の部品。ゲーム側は「保存したら何を読み直すか」と日本語の名前の表を渡すだけでよい
 */
#pragma once
#ifdef USE_IMGUI
#include "json.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::graphics {

/**
 * @brief 設定JSONをそのまま木として編集するエディタ
 * @note 値の型から編集部品を選ぶ（数値=ドラッグ、真偽=チェック、色=カラーピッカー、ファイルの場所=プルダウン）。
 *       ファイルの場所のプルダウンは素材フォルダを定期的に調べ直すので、ファイルを置くだけで候補に出る。
 *       名前の表（labels）が無くても動き、その時はJSONのキーをそのまま表示する
 */
class JsonConfigEditor {
public:
    static JsonConfigEditor* GetInstance();

    /**
     * @brief 対象フォルダと名前の表を設定する
     * @param configDirectory 一覧に出すJSONのフォルダ（直下の *.json だけを対象にする）
     * @param labelsPath      日本語の名前の表（無ければキーをそのまま表示）
     * @param assetDirectory  ファイルの場所のプルダウンで候補を探すフォルダ（サブフォルダも含める）
     */
    void Initialize(const std::string& configDirectory, const std::string& labelsPath, const std::string& assetDirectory);

    /** @brief 指定ファイルを保存した直後に呼ぶ処理を登録する（登録が無いファイルは次回起動時の反映になる） */
    void SetReloadCallback(const std::string& fileName, std::function<void()> onSaved);

    /** @brief 表示を切り替える */
    void Toggle() {
        open_ = !open_;
    }
    /** @brief 表示中かを返す */
    bool IsOpen() const {
        return open_;
    }

    /** @brief 表示中ならウィンドウを描く（ImGuiのフレーム内で毎フレーム呼ぶ） */
    void Draw();

private:
    /** @brief 一覧に出す1ファイルぶんの状態 */
    struct FileEntry {
        std::string fileName; ///< 例: player_params.json
        std::string path; ///< 読み書きするパス
        std::string title; ///< 一覧に出す名前（名前の表に無ければファイル名）
        nlohmann::json data; ///< 編集中の内容
        nlohmann::json saved; ///< 最後に読み込んだ・保存した内容（元に戻す用）
        bool loaded = false;
        bool dirty = false;
    };

    /** @brief フォルダを調べてファイルの一覧を作り直す（開いている内容は残す） */
    void ScanConfigFiles();
    /** @brief 素材フォルダを調べてファイルの場所の候補を作り直す */
    void ScanAssets();
    void LoadEntry(FileEntry& entry);
    void SaveEntry(FileEntry& entry);

    void DrawFileList();
    void DrawFileContents(FileEntry& entry);
    /**
     * @brief JSONの1つの値を編集部品として描く
     * @param path   名前の表を引くための "グループ.キー"（配列の番号は含めない）
     * @param key    この値のキー（配列の要素なら空）
     * @param depth  入れ子の深さ（0がファイルの直下）
     * @return 値を変えたら true
     */
    bool DrawValue(nlohmann::json& value, const std::string& path, const std::string& key, const std::string& label, int depth);
    bool DrawNumber(nlohmann::json& value, const std::string& path, const std::string& key);
    bool DrawString(nlohmann::json& value);
    bool DrawArray(nlohmann::json& value, const std::string& path, const std::string& key, const std::string& label, int depth);
    /** @brief 項目名を左側に出し、続く編集部品を右側に置く準備をする */
    void BeginRow(const std::string& label, const std::string& path);

    /** @brief 検索語に当てはまる項目を含むか（入れ子の中も調べる） */
    bool MatchesFilter(const nlohmann::json& value, const std::string& path, const std::string& key) const;
    /** @brief 名前の表から表示名を引く（無ければキー） */
    std::string LabelFor(const std::string& path, const std::string& key) const;
    /** @brief 名前の表から説明を引く */
    std::string HelpFor(const std::string& path) const;
    /** @brief 名前の表から型（"int" / "float"）を引く */
    std::string TypeFor(const std::string& path, const std::string& key) const;

    std::string configDirectory_;
    std::string labelsPath_;
    std::string assetDirectory_;
    nlohmann::json labels_;
    std::vector<FileEntry> files_;
    std::unordered_map<std::string, std::function<void()>> reloadCallbacks_;
    std::unordered_map<std::string, std::vector<std::string>> assetsByCategory_; ///< "image" / "model" / "sound" ごとのファイルの場所
    double lastAssetScanTime_ = -1.0;
    std::string currentFile_; ///< 編集中のファイル名（名前の表を引くのに使う）
    int selected_ = 0;
    char filter_[128] = {};
    char assetFilter_[128] = {};
    bool open_ = false;
};

} // namespace engine::graphics

#endif // USE_IMGUI
