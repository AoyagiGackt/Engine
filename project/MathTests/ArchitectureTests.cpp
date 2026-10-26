/** @file ArchitectureTests.cpp
 * @brief シーン制御・HUD所有権・編集履歴・イベント接続とJSON上書きを確かめるテスト
 */
#include "../Engine/game/h/Scene/SceneTransitionFlow.h"
#include "../Engine/game/h/Scene/GamePauseController.h"
#include "../Engine/game/h/UI/HudLayer.h"
#include "../Engine/game/h/Level/EditorHistory.h"
#include "../Engine/game/h/Level/StageEditorEventConnection.h"
#include "../Engine/game/h/Level/StageEditorObjectPresets.h"
#include "JsonHelper.h"
#include "TestReport.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace engine::game;
namespace engine::tests {
namespace {
constexpr int kNotificationOffset = 10;
constexpr int kWidgetIds[] = { 1, 2 };

/** @brief 配置プリセットの部分座標、見た目、名前の保持と不正入力時のロールバックを検証する */
void CheckObjectPresets(testReport::TestResults& results) {
    constexpr Vector3 kViewCenter = { 11.0f, 22.0f, 33.0f };
    constexpr Vector3 kPresetScale = { 1.5f, 2.5f, 3.5f };
    constexpr float kFixedY = -0.6f;
    constexpr float kFixedZ = 6.0f;
    const nlohmann::json document = {
        { "sample", {
            { "model", "preset.obj" }, { "texture", "preset.png" },
            { "scale", { kPresetScale.x, kPresetScale.y, kPresetScale.z } },
            { "position", { { "y", kFixedY }, { "z", kFixedZ } } },
            { "lighting", false }, { "solid", true }, { "meshCollider", true },
        } },
    };
    ObjectDesc desc;
    desc.name = "object_1";
    desc.kind = "terrain";
    desc.activationFlag = "event_active";
    desc.position = kViewCenter;
    results.Check(stageEditorObjectPresets::TryApply(document, "sample", desc), "正常な外部プリセットを適用できる");
    results.Check(desc.model == "preset.obj" && desc.texture == "preset.png"
        && !desc.lighting && desc.solid && desc.meshCollider, "モデル・材質と衝突設定を反映する");
    results.Check(desc.scale.x == kPresetScale.x && desc.scale.y == kPresetScale.y && desc.scale.z == kPresetScale.z,
        "軸ごとの拡大率を反映する");
    results.Check(desc.position.x == kViewCenter.x && desc.position.y == kFixedY && desc.position.z == kFixedZ,
        "指定座標だけ固定し、未指定のXはビュー中心を保つ");
    results.Check(desc.name == "object_1" && desc.kind == "terrain" && desc.activationFlag == "event_active",
        "配置の名前・種類・接続フラグはプリセットで変更しない");
    results.Check(!stageEditorObjectPresets::TryApply(document, "missing", desc), "存在しないプリセットでは配置しない");

    const std::vector<nlohmann::json> invalidOverrides = {
        { { "scale", { 1.0f, 1.0f } } },
        { { "scale", { 1.0f, 0.0f, 1.0f } } },
        { { "scale", { 1.0f, "invalid", 1.0f } } },
        { { "scale", { 1.0f, std::numeric_limits<double>::infinity(), 1.0f } } },
        { { "position", { { "y", "invalid" } } } },
        { { "lighting", "invalid" } },
        { { "meshCollider", "invalid" } },
        { { "model", false } },
    };
    for (const auto& invalidOverride : invalidOverrides) {
        auto invalid = document;
        invalid["sample"]["model"] = "changed.obj";
        invalid["sample"].update(invalidOverride);
        results.Check(!stageEditorObjectPresets::TryApply(invalid, "sample", desc)
            && desc.model == "preset.obj" && desc.position.x == kViewCenter.x
            && desc.position.y == kFixedY && desc.position.z == kFixedZ,
            "不正な値が含まれるプリセットでは既存データを部分更新しない");
    }
}

/** @brief 設定の記載順、未知IDの扱い、同じIDを共有する複数対象への上書きを検証する */
void CheckNamedJsonOverrides(testReport::TestResults& results) {
    constexpr int kFirstDamage = 2;
    constexpr int kLastDamage = 5;
    constexpr int kOtherDamage = 9;
    const nlohmann::json root = {
        { "attacks", nlohmann::json::array({
            { { "id", "shared" }, { "damage", kFirstDamage } },
            { { "id", "unknown" }, { "damage", kOtherDamage } },
            { { "id", "shared" }, { "damage", kLastDamage } },
        }) },
    };
    int first = 0;
    int second = 0;
    int other = kOtherDamage;
    const std::unordered_map<std::string, std::vector<int*>> targets = {
        { "shared", { &first, &second } }, { "other", { &other } },
    };
    std::vector<int> applied;
    std::vector<std::string> unknown;
    JsonHelper::VisitNamedOverrides(root, "attacks", targets,
        [&](const std::vector<int*>& matches, const nlohmann::json& record) {
            const int damage = record.at("damage").get<int>();
            for (int* target : matches) {
                *target = damage;
            }
            applied.push_back(damage);
        },
        [&](const std::string& id) { unknown.push_back(id); });
    results.Check(first == kLastDamage && second == kLastDamage, "共有IDの全対象に最後の設定が反映される");
    results.Check(other == kOtherDamage, "JSONに記載されていない対象の値は保つ");
    results.Check(applied == std::vector<int> { kFirstDamage, kLastDamage }, "重複IDもJSONの記載順で適用する");
    results.Check(unknown == std::vector<std::string> { "unknown" }, "未知IDだけを通知して適用は続行する");

    int single = 0;
    const std::unordered_map<std::string, int*> singleTarget = { { "shared", &single } };
    JsonHelper::VisitNamedOverrides(root, "attacks", singleTarget,
        [](int* target, const nlohmann::json& record) { *target = record.at("damage").get<int>(); },
        [](const std::string&) {});
    results.Check(single == kLastDamage, "射撃コンボと同じ単一対象の対応表でも上書きできる");

    int callbacks = 0;
    JsonHelper::VisitNamedOverrides(root, "missing section", targets,
        [&](const auto&, const nlohmann::json&) { ++callbacks; },
        [&](const std::string&) { ++callbacks; });
    results.Check(callbacks == 0, "設定節がなければ値の適用も未知IDの通知もしない");
}

/** @brief イベント接続の入力を公開参照経由で書き換えず、無効な指定を安全な状態へ正規化する */
void CheckEventConnectionInputs(testReport::TestResults& results) {
    StageEditorEventConnection connection;
    constexpr int kSelectedIndex = 3;
    constexpr int kInvalidIndex = -2;
    connection.SetSourceIndex(kSelectedIndex);
    connection.SetTargetIndex(kSelectedIndex);
    results.Check(connection.SourceIndex() == kSelectedIndex && connection.TargetIndex() == kSelectedIndex,
        "接続元と接続先の選択をメソッドから設定できる");
    connection.SetSourceIndex(kInvalidIndex);
    connection.SetTargetIndex(kInvalidIndex);
    results.Check(connection.SourceIndex() == -1 && connection.TargetIndex() == -1,
        "負の接続番号は未選択へ正規化する");
    constexpr float kDelaySeconds = 1.25f;
    connection.SetDelaySeconds(kDelaySeconds);
    results.Check(connection.DelaySeconds() == kDelaySeconds, "有限の待機時間を保持する");
    connection.SetDelaySeconds(-kDelaySeconds);
    results.Check(connection.DelaySeconds() == 0.0f, "負の待機時間は即時実行へ戻す");
    connection.SetDelaySeconds(std::numeric_limits<float>::quiet_NaN());
    results.Check(connection.DelaySeconds() == 0.0f, "NaNの待機時間は即時実行へ戻す");
    connection.SetDelaySeconds(std::numeric_limits<float>::infinity());
    results.Check(connection.DelaySeconds() == 0.0f, "無限の待機時間は即時実行へ戻す");
    connection.SetAction(StageEditorEventConnection::Action::kDeactivate);
    results.Check(connection.GetAction() == StageEditorEventConnection::Action::kDeactivate,
        "対象を無効化する動作を列挙型で指定できる");
}

/** @brief Undo/Redoの対称性、履歴上限、編集分岐、連続編集とmove専用状態の復元を検証する */
void CheckEditorHistory(testReport::TestResults& results) {
    constexpr std::size_t kCapacity = 2;
    EditorHistory<std::string> history(kCapacity);
    results.Check(!history.Undo("current") && !history.Redo("current"), "空の履歴ではUndo/Redoしても復元しない");
    history.Record("first");
    history.Record("second");
    history.Record("third");
    results.Check(history.Undo("current") == "third", "Undoは最後に記録した状態を取り出す");
    results.Check(history.Undo("third") == "second", "Undoは一つ前の状態へ順に戻る");
    results.Check(!history.CanUndo(), "上限を超えて古い状態は保持しない");
    results.Check(history.Redo("second") == "third", "RedoはUndoで戻る前の状態を復元する");
    results.Check(history.Redo("third") == "current" && !history.CanRedo(), "Redoは元の現在状態まで進める");
    history.Undo("current");
    history.Record("branch");
    results.Check(!history.CanRedo(), "Undo後の新しい編集でRedo分岐を破棄する");

    history.Clear();
    history.Begin("before drag");
    history.Begin("during drag");
    history.MarkChanged();
    history.Commit();
    results.Check(!history.IsCapturing() && history.Undo("after drag") == "before drag", "連続編集は最初の状態まで一度で戻る");
    history.Clear();
    history.Begin("unchanged");
    history.Commit();
    results.Check(!history.CanUndo() && !history.IsCapturing(), "変更のない連続操作は履歴に加えない");
    results.Check(!history.CanRedo(), "ClearはRedoも破棄する");

    EditorHistory<std::string> disabled(0);
    disabled.Record("ignored");
    results.Check(!disabled.CanUndo(), "履歴上限0では状態を保持しない");

    constexpr int kBeforeValue = 7;
    constexpr int kAfterValue = 9;
    EditorHistory<std::unique_ptr<int>> moveOnly(kCapacity);
    moveOnly.Record(std::make_unique<int>(kBeforeValue));
    auto restored = moveOnly.Undo(std::make_unique<int>(kAfterValue));
    results.Check(restored && *restored && **restored == kBeforeValue, "コピーできない状態もUndoで所有権を復元する");
    auto reapplied = moveOnly.Redo(restored ? std::move(*restored) : std::unique_ptr<int> {});
    results.Check(reapplied && *reapplied && **reapplied == kAfterValue, "コピーできない状態もRedoで所有権を復元する");
}
/** @brief HUDの描画と通知と破棄の呼び出し順を記録するテスト用ウィジェット */
class RecordingWidget final : public IHudWidget {
public:
    /** @brief 番号と、呼ばれた順を書く一覧を受け取る */
    RecordingWidget(int id, std::vector<int>& calls) : id_(id), calls_(calls) {}
    /** @brief 破棄されたことを負の番号で記録する */
    ~RecordingWidget() override {
        calls_.push_back(-id_);
    }
    /** @brief 描画資源を作らずに初期化を受ける */
    void Initialize(const HudServices&) override {}
    /** @brief 呼ばれた順を書き換えずに更新を受ける */
    void Update(const HudFrame&) override {}
    /** @brief 文字は出さない */
    void QueueText(FontRenderer&) const override {}
    /** @brief 描画された順を記録する */
    void Draw() override {
        calls_.push_back(id_);
    }
    /** @brief 知らせを描画とは別に記録する */
    void Notify(HudEvent) override {
        calls_.push_back(id_ + kNotificationOffset);
    }
private:
    int id_;
    std::vector<int>& calls_;
};
}

/** @brief シーン遷移とポーズ状態およびHUDの所有権と呼び出し順を検証する */
int RunArchitectureTests() {
    testReport::TestResults results;
    CheckObjectPresets(results);
    CheckNamedJsonOverrides(results);
    CheckEditorHistory(results);
    CheckEventConnectionInputs(results);
    using Action = SceneTransitionFlow::Action;
    SceneTransitionFlow transition;
    results.Check(!transition.IsChanging() && !transition.IsLoading(), "起動直後はシーン切り替え中でもロード画面中でもない");
    results.Check(transition.Update(true, true) == Action::kNone, "何も始めていなければシーンは切り替わらない");
    transition.Begin();
    results.Check(transition.IsChanging() && !transition.IsLoading(), "切り替えはまずフェードアウトから始まる（ロード画面はまだ）");
    results.Check(transition.Update(false, true) == Action::kNone, "フェードアウトが終わる前はシーンを切り替えない");
    transition.Begin();
    results.Check(transition.Update(true, false) == Action::kShowLoading, "切り替え中にもう一度頼んでもフェードは最初からやり直さない");
    results.Check(transition.IsLoading(), "フェードアウトの後にロード画面になる");
    results.Check(transition.Update(true, false) == Action::kNone, "ロード画面を1回描くまではシーンを切り替えない");
    results.Check(transition.Update(true, true) == Action::kCommitSwitch, "ロード画面を描いた後ならシーンを切り替える");
    transition.FinishSwitch();
    results.Check(!transition.IsChanging() && !transition.IsLoading(), "新しいシーンはフェードイン中から動き出す");
    results.Check(transition.Update(false, true) == Action::kNone, "フェードイン中にシーンをもう一度切り替えない");
    transition.Begin();
    results.Check(transition.IsChanging(), "フェードイン中でも次の切り替えを始められる");
    transition.Reset();
    results.Check(!transition.IsChanging() && transition.Update(true, true) == Action::kNone, "Resetで切り替えが取り消される");
    using Mode = GamePauseController::UpdateMode;
    GamePauseController pause;
    results.Check(pause.Advance(false) == Mode::kPlay && !pause.IsPaused(), "最初はポーズしていないプレイ中の状態");
    results.Check(pause.Advance(true) == Mode::kSkipFrame && pause.IsPaused(), "ポーズを開いたフレームは、開くのに使った入力でメニューを操作しない");
    results.Check(pause.Advance(false) == Mode::kMenu, "ポーズ中の入力はメニューに渡る");
    results.Check(pause.Advance(true) == Mode::kPlay && !pause.IsPaused(), "もう一度押すとプレイに戻る");
    pause.Advance(true);
    pause.Resume();
    results.Check(pause.Advance(false) == Mode::kPlay, "Resume()でプレイに戻る");
    std::vector<int> calls;
    std::vector<int> expectedCalls;
    for (int id : kWidgetIds) {
        expectedCalls.push_back(id);
    }
    for (int id : kWidgetIds) {
        expectedCalls.push_back(id + kNotificationOffset);
    }
    {
        HudLayer hud;
        // 期待する順番は、ウィジェットを登録したのと同じ表から作る
        for (int id : kWidgetIds) {
            hud.Add(std::make_unique<RecordingWidget>(id, calls));
        }
        hud.Draw();
        hud.Notify(HudEvent::kWeaponAcquired);
        results.Check(calls == expectedCalls, "HUDは描画とイベントを登録した順に各部品へ渡す");
    }
    results.Check(calls.size() == expectedCalls.size() + std::size(kWidgetIds)
        && std::all_of(std::begin(kWidgetIds), std::end(kWidgetIds), [&](int id) {
            return std::count(calls.begin(), calls.end(), -id) == 1;
        }), "HUDは持っている部品を全部破棄する");
    results.PrintSummary("Architecture (transitions, pause, HUD, history, connections, JSON and presets)");
    return results.FailureCount();
}

} // namespace engine::tests
