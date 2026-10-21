/**
 * @file GameFlags.h
 * @brief ノードグラフとステージ（トリガー）の両方から読み書きする、名前付きbool変数のグローバルストア
 * @note GraphRuntimeの変数はグラフインスタンスごとにローカルだが、
 * こちらはゲーム全体で1つを共有する（ドアの開閉、フラグ回収済みなどステージの状態を表す）
 */
#pragma once
#include <map>
#include <string>
namespace engine::game {

/**
 * @brief 名前付きbool変数（フラグ）をゲーム全体で共有するグローバルストア
 * @details ドアの開閉やアイテム回収済みなど、ステージをまたいで保持したい状態を
 * 文字列キーで登録・参照する。ノードグラフのSetFlag/GetFlagノードとステージトリガーの両方から使われる
 */
class GameFlags {
public:
    /**
     * @brief 唯一のGameFlagsインスタンスを取得する（未生成なら生成する）
     * @return GameFlagsのインスタンス
     */
    static GameFlags* GetInstance();

    /** @brief フラグを設定する（無ければ新規作成） */
    void SetFlag(const std::string& name, bool value);

    /** @brief フラグの値を取得する未設定なら false を返す */
    bool GetFlag(const std::string& name) const;

    /** @brief フラグが一度でも設定されたことがあるか */
    bool HasFlag(const std::string& name) const;

    /** @brief 全フラグを消去する（ステージ再読み込み時などに使う想定） */
    void Clear();

    /** @brief 各フラグを名前順に読み取り専用の処理へ渡す。処理中は登録・消去しない */
    template<class Visitor>
    void VisitFlags(const Visitor& visitor) const
    {
        for (const auto& [name, value] : flags_)
        {
            visitor(name, value);
        }
    }

private:
    /** @brief 空のフラグストアを生成する */
    GameFlags() = default;
    GameFlags(const GameFlags&) = delete;
    GameFlags& operator=(const GameFlags&) = delete;

    std::map<std::string, bool> flags_;
};

} // namespace engine::game
