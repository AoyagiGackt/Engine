/**
 * @file LevelGraphRunner.h
 * @brief レベルJSONに紐付いたノードグラフ（常駐グラフ＋フラグ起動グラフ）を実行する
 * @note StageEditorがレベルを開いた時にStart()し、毎フレームUpdate()する。
 * ステージのトリガーや条件がGameFlagsを立てると、その立ち上がりで対応するグラフが起動する
 */
#pragma once
#include "GraphRuntime.h"
#include "GraphTypes.h"
#include "LevelLoader.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace engine::game {

/**
 * @brief レベル1つぶんのグラフ実行を束ねる
 * @details 常駐グラフはレベル読込直後から1回だけ走る。フラグ起動グラフはフラグがfalse→trueになるたびに
 * 新しいインスタンスとして起動し、Haltしたら破棄する（同じグラフの多重起動も許容する）
 */
class LevelGraphRunner {
public:
    /**
     * @brief レベルデータのグラフ設定を読み込んで実行を開始する
     * @param data 常駐グラフパスとフラグ起動グラフ一覧を持つレベルデータ
     */
    void Start(const LevelData& data);

    /** @brief 全グラフを停止して破棄する */
    void Stop();

    /**
     * @brief 毎フレーム呼ぶ。常駐グラフを進め、フラグの立ち上がりで起動グラフを開始する
     * @param dt 経過秒数
     */
    void Update(float dt);

    /** @brief 常駐グラフを含めて何かが実行中か */
    bool IsAnyRunning() const;

    /** @brief 実行中のグラフ本数（エディタの状態表示用） */
    int GetRunningCount() const;

    /** @brief 常駐グラフのパス（未設定なら空） */
    const std::string& GetMainGraphPath() const { return mainPath_; }

private:
    /** @brief 実行中のグラフ1本ぶん（定義と実行状態を一緒に所有する） */
    struct RunningGraph {
        std::string path;
        std::unique_ptr<GraphDesc> desc;
        std::unique_ptr<GraphRuntime> runtime;
    };

    /** @brief パスのグラフを読み込んで起動する。読み込み失敗や開始ノード未設定なら何もしない */
    void Launch(const std::string& path);

    std::string mainPath_;
    std::vector<FlagGraphBinding> bindings_;
    std::map<std::string, bool> lastFlagState_; // 立ち上がり検出用の前フレームのフラグ値
    std::vector<RunningGraph> running_;
};

} // namespace engine::game
