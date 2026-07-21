/**
 * @file EditorHistory.h
 * @brief エディタのUndoとRedoに使用するスナップショット履歴を管理するファイル
 */
#pragma once
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace engine::game {

/**
 * @brief 任意のスナップショット型に対応する編集履歴クラス
 * @tparam Snapshot 復元に必要な編集状態を保持する型
 */
template <class Snapshot>
class EditorHistory {
public:
    /** @brief 最大履歴数を指定して初期化する */
    explicit EditorHistory(std::size_t maxHistory = 50)
        : maxHistory_(maxHistory)
    {
    }

    /** @brief Undo、Redo、編集中の一時状態をすべて破棄する */
    void Clear()
    {
        undo_.clear();
        redo_.clear();
        pending_.reset();
        pendingChanged_ = false;
    }

    /** @brief 単発操作の変更前状態をUndo履歴へ追加する */
    void Record(Snapshot snapshot)
    {
        PushBounded(undo_, std::move(snapshot));
        redo_.clear();
    }

    /** @brief ドラッグなど連続操作の変更前状態を一度だけ保持する */
    void Begin(Snapshot snapshot)
    {
        if (pending_) {
            return;
        }
        pending_ = std::move(snapshot);
        pendingChanged_ = false;
    }

    /** @brief 開始した連続操作で値が変更されたことを記録する */
    void MarkChanged()
    {
        if (pending_) {
            pendingChanged_ = true;
        }
    }

    /** @brief 連続操作を一回分のUndo履歴として確定する */
    void Commit()
    {
        if (pending_ && pendingChanged_) {
            Record(std::move(*pending_));
        }
        pending_.reset();
        pendingChanged_ = false;
    }

    /** @brief Undoできる履歴があるか返す */
    bool CanUndo() const { return !undo_.empty(); }

    /** @brief Redoできる履歴があるか返す */
    bool CanRedo() const { return !redo_.empty(); }

    /** @brief 連続操作の変更前状態を保持中か返す */
    bool IsCapturing() const { return pending_.has_value(); }

    /** @brief 現在状態をRedoへ積み、直前の状態を返す */
    std::optional<Snapshot> Undo(Snapshot current)
    {
        if (undo_.empty()) {
            return std::nullopt;
        }
        PushBounded(redo_, std::move(current));
        Snapshot result = std::move(undo_.back());
        undo_.pop_back();
        return result;
    }

    /** @brief 現在状態をUndoへ積み、取り消した状態を返す */
    std::optional<Snapshot> Redo(Snapshot current)
    {
        if (redo_.empty()) {
            return std::nullopt;
        }
        PushBounded(undo_, std::move(current));
        Snapshot result = std::move(redo_.back());
        redo_.pop_back();
        return result;
    }

private:
    /** @brief 最大履歴数を超えないよう古い状態を捨てて追加する */
    void PushBounded(std::vector<Snapshot>& destination, Snapshot snapshot)
    {
        destination.push_back(std::move(snapshot));
        if (destination.size() > maxHistory_) {
            destination.erase(destination.begin());
        }
    }

    std::size_t maxHistory_ = 50;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    std::optional<Snapshot> pending_;
    bool pendingChanged_ = false;
};

} // namespace engine::game
