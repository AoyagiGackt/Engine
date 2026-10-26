/**
 * @file NodeRegistry.cpp
 * @brief 組み込みノードの定義と実行関数の登録（NodeRegistry）の実装
 */
#include "NodeRegistry.h"
#include "AudioBridge.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "EventBus.h"
#include "GameFlags.h"
#include "GameRules.h"
#include "GraphRuntime.h"
#include "Logger.h"
#include "ParticleManager.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "RunData.h"
#include "SceneEffectBridge.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include <random>
#include <utility>
using namespace engine::game;
using namespace engine;
namespace engine::game {
namespace {
// GraphValueTypeの並び順（Float/Bool/String/Any）に対応する、初期値を省略した時の既定値
const GraphValue kTypeDefaults[] = { 0.0f, false, std::string { }, 0.0f };
// GraphValueはboolへも変換できるため、文字列の初期値は明示的にstd::stringへ包む
GraphValue Text(const char* text) {
    return std::string(text);
}
// ノード追加時の初期値で共通して使う値
constexpr float kOne = 1.0f;
const GraphValue kDefaultVariable = Text("var");
const GraphValue kDefaultEnemyTarget = Text("enemy");
const GraphValue kDefaultObjectTarget = Text("obj_0");
const GraphValue kDefaultSoundPath = Text("Resources/sound/se.wav");
// パラメータ解決はGraphRuntime::ResolveParamに委譲する（データ配線があれば供給元ノードの出力を優先）

/** @brief ノードの target パラメータ（操作する敵や配置物の名前）を読む */
std::string ResolveTarget(GraphRuntime& rt, const GraphNode& node) {
    return AsString(rt.ResolveParam(node, "target", std::string { }));
}

/** @brief ノードの x/y/z パラメータを座標として読む（GraphValue に Vector3 型が無いため3つに分けて受け取る） */
Vector3 ResolvePosition(GraphRuntime& rt, const GraphNode& node) {
    return {
        AsFloat(rt.ResolveParam(node, "x", GraphValue { 0.0f })),
        AsFloat(rt.ResolveParam(node, "y", GraphValue { 0.0f })),
        AsFloat(rt.ResolveParam(node, "z", GraphValue { 0.0f })),
    };
}

/** @brief target の名前の敵や配置物が見つからなかったことをエラーログに出す */
void LogUnknownTarget(const GraphNode& node, const std::string& target) {
    Logger::LogError("[Graph] " + node.type + " node '" + node.id + "' unknown target '" + target + "'");
}

/** @brief target の敵を探す（登録されていなければエラーログを出して nullptr） */
EnemyEntity* FindEnemyOrLog(const GraphNode& node, const std::string& target) {
    EnemyEntity* enemy = EnemyRegistry::GetInstance()->Find(target);
    if (enemy == nullptr) {
        LogUnknownTarget(node, target);
    }
    return enemy;
}

/** @brief SetVariable ノード: name の変数に value を代入して次のノードへ進む */
NodeResult ExecSetVariable(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    std::string name = AsString(rt.ResolveParam(node, "name", std::string { }));
    if (name.empty()) {
        Logger::LogError("[Graph] SetVariable node '" + node.id + "' is missing 'name' param");
    } else {
        GraphValue value = rt.ResolveParam(node, "value", GraphValue { 0.0f });
        rt.SetVariable(name, value);
        rt.SetNodeOutput(node.id, std::move(value)); // 設定した値をそのまま出力ピンにも流す
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief If ノード: 条件に応じて次に進むノードを選ぶ */
NodeResult ExecIf(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    std::string varName = AsString(rt.ResolveParam(node, "var", std::string { }));
    CompareOp op = ParseCompareOp(AsString(rt.ResolveParam(node, "op", std::string("=="))));
    GraphValue rhs = rt.ResolveParam(node, "value", GraphValue { 0.0f });

    const GraphValue* lhs = rt.GetVariable(varName);
    bool result = lhs ? Compare(op, *lhs, rhs) : false;
    if (!lhs) {
        Logger::LogError("[Graph] If node '" + node.id + "' references undefined variable '" + varName + "'");
    }

    outNextId = result ? node.nextTrue : node.nextFalse;
    return NodeResult::kContinue;
}

/** @brief Wait ノード: 指定秒数たつまで待つ */
NodeResult ExecWait(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    float seconds = AsFloat(rt.ResolveParam(node, "seconds", GraphValue { 0.0f }));
    rt.BeginWait(seconds);
    outNextId = node.next; // 待機完了後にRunUntilSuspendOrHalt()側がこれをresume先として使う
    return NodeResult::kSuspend;
}

/** @brief EmitEvent ノード: イベントを発行する */
NodeResult ExecEmitEvent(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    std::string eventName = AsString(rt.ResolveParam(node, "event", std::string { }));
    if (eventName.empty()) {
        Logger::LogError("[Graph] EmitEvent node '" + node.id + "' is missing 'event' param");
    } else {
        EventBus::GetInstance()->Emit(eventName);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

// ステージエディタで置いたトリガー等が立てたフラグを、グラフ側のロジックで参照できるようにする
// GameFlags本体はグラフインスタンス間・ステージ間で共有のグローバルストア（GraphRuntimeのローカル変数とは別物）
NodeResult ExecSetFlag(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    std::string flag = AsString(rt.ResolveParam(node, "flag", std::string { }));
    if (flag.empty()) {
        Logger::LogError("[Graph] SetFlag node '" + node.id + "' is missing 'flag' param");
    } else {
        bool value = AsBool(rt.ResolveParam(node, "value", GraphValue { false }));
        GameFlags::GetInstance()->SetFlag(flag, value);
        rt.SetNodeOutput(node.id, GraphValue { value });
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

// フラグの値をグラフのローカル変数へコピーする（既存のIfノードで分岐に使うため）
// あわせて出力データピンにも流すので、他ノードのboolパラメータへ直接配線もできる
NodeResult ExecGetFlag(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    std::string flag = AsString(rt.ResolveParam(node, "flag", std::string { }));
    std::string into = AsString(rt.ResolveParam(node, "into", std::string { }));
    if (flag.empty()) {
        Logger::LogError("[Graph] GetFlag node '" + node.id + "' is missing 'flag' param");
    } else {
        bool value = GameFlags::GetInstance()->GetFlag(flag);
        if (!into.empty()) {
            rt.SetVariable(into, GraphValue { value });
        }
        rt.SetNodeOutput(node.id, GraphValue { value });
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

// 別ファイルのグラフを呼び出して再利用する（カプセル化）子グラフがHaltするまで親は待機する
NodeResult ExecSubgraph(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    std::string path = AsString(rt.ResolveParam(node, "path", std::string { }));
    outNextId = node.next;
    if (path.empty() || !rt.BeginSubgraph(path)) {
        Logger::LogError("[Graph] Subgraph node '" + node.id + "' failed to start: " + path);
        return NodeResult::kContinue;
    }
    return NodeResult::kSuspend;
}

// a op b を計算して出力データピンに流す（op  "+" "-" "*" "/"）
NodeResult ExecMath(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    float a = AsFloat(rt.ResolveParam(node, "a", GraphValue { 0.0f }));
    float b = AsFloat(rt.ResolveParam(node, "b", GraphValue { 0.0f }));
    MathOp op = ParseMathOp(AsString(rt.ResolveParam(node, "op", std::string("+"))));
    rt.SetNodeOutput(node.id, GraphValue { ApplyMath(op, a, b) });
    outNextId = node.next;
    return NodeResult::kContinue;
}

// Ifと違い変数名ではなく2つのデータピン（リテラルまたは配線）を直接比較してbool出力する
// 結果はSetVariable経由でIfへ渡すか、他ノードのbool入力へそのまま配線して使う
NodeResult ExecCompare(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    GraphValue a = rt.ResolveParam(node, "a", GraphValue { 0.0f });
    GraphValue b = rt.ResolveParam(node, "b", GraphValue { 0.0f });
    CompareOp op = ParseCompareOp(AsString(rt.ResolveParam(node, "op", std::string("=="))));
    rt.SetNodeOutput(node.id, GraphValue { Compare(op, a, b) });
    outNextId = node.next;
    return NodeResult::kContinue;
}

// [min, max] のFloat乱数を出力データピンに流す
NodeResult ExecRandom(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    float mn = AsFloat(rt.ResolveParam(node, "min", GraphValue { 0.0f }));
    float mx = AsFloat(rt.ResolveParam(node, "max", GraphValue { 1.0f }));
    if (mn > mx) {
        std::swap(mn, mx);
    }
    static std::mt19937 sRng { std::random_device { }() };
    std::uniform_real_distribution<float> dist(mn, mx);
    rt.SetNodeOutput(node.id, GraphValue { dist(sRng) });
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief DamagePlayer ノード: プレイヤーにダメージを与える */
NodeResult ExecDamagePlayer(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    int amount = static_cast<int>(AsFloat(rt.ResolveParam(node, "amount", GraphValue { 1.0f })));
    RunData::GetInstance()->TakeDamage(amount);
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief HealPlayer ノード: プレイヤーを回復する */
NodeResult ExecHealPlayer(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    int amount = static_cast<int>(AsFloat(rt.ResolveParam(node, "amount", GraphValue { 1.0f })));
    RunData::GetInstance()->Heal(amount);
    outNextId = node.next;
    return NodeResult::kContinue;
}

/**
 * @brief ノードの target（EnemyRegistry に登録された id）の敵へ、amount を使った操作を行う
 * @param nodeName エラー表示に使うノードの種類名
 * @param apply 敵と量を受け取って敵の状態を変える処理
 */
template <class Apply>
NodeResult ApplyToTargetEnemy(GraphRuntime& rt, const GraphNode& node, const char* nodeName, std::string& outNextId, const Apply& apply) {
    const std::string target = ResolveTarget(rt, node);
    int amount = static_cast<int>(AsFloat(rt.ResolveParam(node, "amount", GraphValue { 1.0f })));
    EnemyEntity* enemy = EnemyRegistry::GetInstance()->Find(target);
    // 登録されていない id ならエラーを出して何もしない（Scene 側で Register 済みであること）
    if (!enemy) {
        Logger::LogError(std::string("[Graph] ") + nodeName + " node '" + node.id + "' unknown target '" + target + "'");
    } else {
        apply(*enemy, amount);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief DamageEnemy ノード: 指定した敵にダメージを与える */
NodeResult ExecDamageEnemy(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    return ApplyToTargetEnemy(rt, node, "DamageEnemy", outNextId, [](EnemyEntity& enemy, int amount) {
        enemy.TakeDamage(amount);
    });
}

/** @brief HealEnemy ノード: 指定した敵を回復する */
NodeResult ExecHealEnemy(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    return ApplyToTargetEnemy(rt, node, "HealEnemy", outNextId, [](EnemyEntity& enemy, int amount) {
        enemy.Heal(amount);
    });
}

// pathはWAV/MP3ファイルパス初回のみ読み込み、以降はAudioBridge内のキャッシュから再生する
NodeResult ExecPlaySE(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    std::string path = AsString(rt.ResolveParam(node, "path", std::string { }));
    float volume = AsFloat(rt.ResolveParam(node, "volume", GraphValue { 1.0f }));
    if (path.empty()) {
        Logger::LogError("[Graph] PlaySE node '" + node.id + "' is missing 'path' param");
    } else {
        AudioBridge::GetInstance()->PlaySE(path, volume);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief PlayBGM ノード: BGMを再生する */
NodeResult ExecPlayBGM(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    std::string path = AsString(rt.ResolveParam(node, "path", std::string { }));
    bool loop = AsBool(rt.ResolveParam(node, "loop", GraphValue { true }));
    if (path.empty()) {
        Logger::LogError("[Graph] PlayBGM node '" + node.id + "' is missing 'path' param");
    } else {
        AudioBridge::GetInstance()->PlayBGM(path, loop);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief StopBGM ノード: BGMを止める */
NodeResult ExecStopBGM(GraphRuntime&, const GraphNode& node, std::string& outNextId) {
    AudioBridge::GetInstance()->StopBGM();
    outNextId = node.next;
    return NodeResult::kContinue;
}

// 画面全体を指定色でフラッシュさせる（durationかけて元に戻る）
NodeResult ExecScreenFlash(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    float r = AsFloat(rt.ResolveParam(node, "r", GraphValue { 1.0f }));
    float g = AsFloat(rt.ResolveParam(node, "g", GraphValue { 1.0f }));
    float b = AsFloat(rt.ResolveParam(node, "b", GraphValue { 1.0f }));
    float a = AsFloat(rt.ResolveParam(node, "a", GraphValue { 0.5f }));
    float duration = AsFloat(rt.ResolveParam(node, "duration", GraphValue { graphNodeDefaults::kFlashDuration }));
    /** @brief シングルトンインスタンスを返す（初回呼び出しで生成される） */
    ScreenFlash::GetInstance()->Request({ r, g, b, a }, duration);
    outNextId = node.next;
    return NodeResult::kContinue;
}

// framesフレームぶんゲーム内時間を止める演出用（Wait秒数指定とは別物、TimeManagerのヒットストップ機構を使う）
NodeResult ExecHitStop(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    int frames = static_cast<int>(AsFloat(rt.ResolveParam(node, "frames", GraphValue { graphNodeDefaults::kHitStopFrames })));
    TimeManager::GetInstance()->RequestHitStop(frames);
    outNextId = node.next;
    return NodeResult::kContinue;
}

// targetはEnemyRegistryに登録されたid（EnemyEntity 限定。KnightEnemy には可視切り替えのAPIが無い）
NodeResult ExecSetEnemyVisible(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string target = ResolveTarget(rt, node);
    bool visible = AsBool(rt.ResolveParam(node, "visible", GraphValue { true }));
    if (EnemyEntity* enemy = FindEnemyOrLog(node, target)) {
        enemy->SetVisible(visible);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

// GraphValueにVector3型が無いため、座標はx/y/zの3つのFloatパラメータに分けて受け取る
NodeResult ExecTeleportEnemy(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string target = ResolveTarget(rt, node);
    const Vector3 position = ResolvePosition(rt, node);
    if (EnemyEntity* enemy = FindEnemyOrLog(node, target)) {
        enemy->SetPosition(position);
        enemy->RefreshVisualTransforms();
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

// PlayerBridgeに登録されたシーンのプレイヤーを瞬間移動させる（登録が無ければ何もしない）
NodeResult ExecTeleportPlayer(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const Vector3 position = ResolvePosition(rt, node);
    Player* player = PlayerBridge::GetInstance()->Get();
    if (!player) {
        Logger::LogError("[Graph] TeleportPlayer node '" + node.id + "' no Player registered (PlayerBridge)");
    } else {
        player->SetPosition(position);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief And ノード: 2つの入力がどちらも真かを出力する */
NodeResult ExecAnd(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    bool a = AsBool(rt.ResolveParam(node, "a", GraphValue { false }));
    bool b = AsBool(rt.ResolveParam(node, "b", GraphValue { false }));
    rt.SetNodeOutput(node.id, GraphValue { a && b });
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief Or ノード: 2つの入力のどちらかが真かを出力する */
NodeResult ExecOr(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    bool a = AsBool(rt.ResolveParam(node, "a", GraphValue { false }));
    bool b = AsBool(rt.ResolveParam(node, "b", GraphValue { false }));
    rt.SetNodeOutput(node.id, GraphValue { a || b });
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief Not ノード: 入力の真偽を反転して出力する */
NodeResult ExecNot(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    bool a = AsBool(rt.ResolveParam(node, "a", GraphValue { false }));
    rt.SetNodeOutput(node.id, GraphValue { !a });
    outNextId = node.next;
    return NodeResult::kContinue;
}

// ── 配置物（StageEditorが開いているレベルの配置物を名前で操作する）──
// 対象はレベルJSONのname。StageEditor::GetActive()が無い（レベル未読込）場合はログを出して何もしない

/** @brief 開いているレベルの StageEditor を返す（無ければエラーログを出して nullptr） */
StageEditor* ActiveStageEditorOrLog(const GraphNode& node) {
    StageEditor* editor = StageEditor::GetActive();
    if (!editor) {
        Logger::LogError("[Graph] " + node.type + " node '" + node.id + "' has no level open (StageEditor)");
    }
    return editor;
}

/**
 * @brief 開いているステージの配置物 target に apply を行う（ステージが無い時と、名前が見つからない時はエラーログ）
 * @param apply StageEditor を受け取り、対象が見つかれば true を返す処理
 */
template <class Apply>
void ApplyToTargetObject(const GraphNode& node, const std::string& target, const Apply& apply) {
    if (StageEditor* editor = ActiveStageEditorOrLog(node)) {
        if (!apply(*editor)) {
            LogUnknownTarget(node, target);
        }
    }
}

/** @brief SetObjectVisible ノード: 配置物の表示・非表示を切り替える */
NodeResult ExecSetObjectVisible(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string target = ResolveTarget(rt, node);
    bool visible = AsBool(rt.ResolveParam(node, "visible", GraphValue { true }));
    ApplyToTargetObject(node, target, [&](StageEditor& editor) {
        return editor.SetObjectVisibleByName(target, visible);
    });
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief SetObjectEnabled ノード: 配置物の有効・無効を切り替える */
NodeResult ExecSetObjectEnabled(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string target = ResolveTarget(rt, node);
    bool enabled = AsBool(rt.ResolveParam(node, "enabled", GraphValue { true }));
    ApplyToTargetObject(node, target, [&](StageEditor& editor) {
        return editor.SetObjectEnabledByName(target, enabled);
    });
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief TeleportObject ノード: 配置物を指定の位置へ移す */
NodeResult ExecTeleportObject(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string target = ResolveTarget(rt, node);
    const Vector3 position = ResolvePosition(rt, node);
    ApplyToTargetObject(node, target, [&](StageEditor& editor) {
        return editor.TeleportObjectByName(target, position);
    });
    outNextId = node.next;
    return NodeResult::kContinue;
}

// seconds秒かけて目標位置へ補間移動させる。移動完了を待たずに次のノードへ進む（待ちたい場合はWaitを続ける）
NodeResult ExecMoveObject(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string target = ResolveTarget(rt, node);
    const Vector3 position = ResolvePosition(rt, node);
    float seconds = AsFloat(rt.ResolveParam(node, "seconds", GraphValue { 1.0f }));
    ApplyToTargetObject(node, target, [&](StageEditor& editor) {
        return editor.MoveObjectByName(target, position, seconds);
    });
    outNextId = node.next;
    return NodeResult::kContinue;
}

// ── 進行（シーン遷移とステージクリア）──

// sceneはSceneFactoryの登録名（"TITLE" "MAP" "GAMEPLAY" 等）。フェード秒数は省略可
NodeResult ExecChangeScene(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    constexpr float kDefaultFadeSeconds = graphNodeDefaults::kSceneFadeSeconds;
    std::string scene = AsString(rt.ResolveParam(node, "scene", std::string { }));
    float fadeOut = AsFloat(rt.ResolveParam(node, "fadeOut", GraphValue { kDefaultFadeSeconds }));
    float fadeIn = AsFloat(rt.ResolveParam(node, "fadeIn", GraphValue { kDefaultFadeSeconds }));
    if (scene.empty()) {
        Logger::LogError("[Graph] ChangeScene node '" + node.id + "' is missing 'scene' param");
    } else {
        SceneManager::GetInstance()->ChangeScene(scene, fadeOut, fadeIn);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

// game_rules.jsonのclearFlagを立てる。本編シーンはこのフラグを毎フレーム見てクリア演出へ進む
NodeResult ExecRequestStageClear(GraphRuntime&, const GraphNode& node, std::string& outNextId) {
    GameFlags::GetInstance()->SetFlag(GameRules::GetInstance()->Get().clearFlag, true);
    outNextId = node.next;
    return NodeResult::kContinue;
}

// spawn_pointを名前で起動する（有効化フラグを待たずに敵を出す。SetObjectEnabled(true)の敵出現向けの別名）
NodeResult ExecSpawnEnemy(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string target = ResolveTarget(rt, node);
    ApplyToTargetObject(node, target, [&](StageEditor& editor) {
        return editor.SetObjectEnabledByName(target, true);
    });
    outNextId = node.next;
    return NodeResult::kContinue;
}

// ── 演出（シーンのカメラシェイク、パーティクル）──

/** @brief ShakeCamera ノード: 指定の強さと秒数でカメラシェイクを要求して次のノードへ進む */
NodeResult ExecShakeCamera(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    constexpr float kDefaultShakeAmount = 0.25f;
    constexpr float kDefaultShakeSeconds = 0.2f;
    float amount = AsFloat(rt.ResolveParam(node, "amount", GraphValue { kDefaultShakeAmount }));
    float seconds = AsFloat(rt.ResolveParam(node, "seconds", GraphValue { kDefaultShakeSeconds }));
    SceneEffectBridge::GetInstance()->RequestCameraShake(amount, seconds);
    outNextId = node.next;
    return NodeResult::kContinue;
}

// groupはparticles/*.jsonで定義したパーティクルグループ名（"hit_ring" "awaken_aura" 等）
NodeResult ExecEmitRing(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    constexpr float kDefaultRadius = 3.0f;
    constexpr int kRingParticleCount = 16;
    constexpr float kRingLifetime = 0.4f;
    constexpr float kRingSize = 0.25f;
    std::string group = AsString(rt.ResolveParam(node, "group", std::string("hit_ring")));
    const Vector3 position = ResolvePosition(rt, node);
    float radius = AsFloat(rt.ResolveParam(node, "radius", GraphValue { kDefaultRadius }));
    float r = AsFloat(rt.ResolveParam(node, "r", GraphValue { 1.0f }));
    float g = AsFloat(rt.ResolveParam(node, "g", GraphValue { 1.0f }));
    float b = AsFloat(rt.ResolveParam(node, "b", GraphValue { 1.0f }));
    /** @brief 共有インスタンスを取得する */
    engine::graphics::ParticleManager::GetInstance()->EmitRing(group, position, radius,
        { r, g, b, 1.0f }, kRingParticleCount, kRingLifetime, kRingSize);
    outNextId = node.next;
    return NodeResult::kContinue;
}
} // namespace
/** @brief 唯一のNodeRegistryインスタンスを取得する（未生成なら生成し、組み込みノードを登録する） */
NodeRegistry* NodeRegistry::GetInstance() {
    static NodeRegistry sInstance;
    return &sInstance;
}
/** @brief 組み込みノードを登録して初期化する */
NodeRegistry::NodeRegistry() {
    RegisterBuiltins();
}
/** @brief 組み込みノードの定義と実行関数を登録する */
void NodeRegistry::RegisterBuiltins() {
    using VT = GraphValueType;
    // ── よく使う（変数の読み書き・分岐・待機・イベント発火など基本のフロー制御）──
    Register("SetVariable", ExecSetVariable);
    RegisterSpec("SetVariable", { { { "name", VT::kString, kDefaultVariable }, { "value", VT::kAny } }, true, VT::kAny, "変数nameにvalueを代入する（無ければ新規作成）", "よく使う" });
    Register("If", ExecIf);
    RegisterSpec("If", { { { "var", VT::kString, kDefaultVariable }, { "op", VT::kString, Text("==") }, { "value", VT::kAny } }, false, VT::kAny, "変数varの値をopでvalueと比較し、真ならnextTrue／偽ならnextFalseへ分岐する", "よく使う", true });
    Register("Wait", ExecWait);
    RegisterSpec("Wait", { { { "seconds", VT::kFloat, kOne } }, false, VT::kAny, "seconds秒待ってから次へ進む（フレームをまたいで待機する）", "よく使う" });
    Register("EmitEvent", ExecEmitEvent);
    RegisterSpec("EmitEvent", { { { "event", VT::kString, Text("event_name") } }, false, VT::kAny, "名前付きイベントを発火する（EventBus経由、コード側でSubscribe済みの処理が呼ばれる）", "よく使う" });
    Register("Compare", ExecCompare);
    RegisterSpec("Compare", { { { "a", VT::kAny }, { "b", VT::kAny }, { "op", VT::kString, Text("==") } }, true, VT::kBool, "2つの値をop（== != < <= > >=）で比較しboolを出力する（Ifと違い変数名でなくデータピンを直接比較できる）", "よく使う" });
    // ── 変数・フラグ（GameFlagsによるステージ間共有フラグ、サブグラフ呼び出し）──
    Register("SetFlag", ExecSetFlag);
    RegisterSpec("SetFlag", { { { "flag", VT::kString, Text("flag_name") }, { "value", VT::kBool } }, true, VT::kBool, "グローバルなbool変数(GameFlags)を書き換えるステージのトリガーやIf/GetFlagから参照できる", "変数・フラグ" });
    Register("GetFlag", ExecGetFlag);
    RegisterSpec("GetFlag", { { { "flag", VT::kString, Text("flag_name") }, { "into", VT::kString, kDefaultVariable } }, true, VT::kBool, "GameFlagsの値を読む（intoを指定するとグラフのローカル変数にもコピーされる）", "変数・フラグ" });
    Register("Subgraph", ExecSubgraph);
    RegisterSpec("Subgraph", { { { "path", VT::kString, Text("Resources/Graphs/sub_graph.json") } }, false, VT::kAny, "別のグラフJSONを呼び出す（子がHaltするまで待機、ロジックの再利用・整理に使う）", "変数・フラグ", false, "path" });
    // ── 数値・論理（四則演算・比較・乱数・ブール演算）──
    Register("Math", ExecMath);
    RegisterSpec("Math", { { { "a", VT::kFloat }, { "b", VT::kFloat }, { "op", VT::kString, Text("+") } }, true, VT::kFloat, "a op b を計算する（op: + - * /）", "数値・論理" });
    Register("Random", ExecRandom);
    RegisterSpec("Random", { { { "min", VT::kFloat }, { "max", VT::kFloat, kOne } }, true, VT::kFloat, "min〜maxの範囲でFloat乱数を出力する", "数値・論理" });
    Register("And", ExecAnd);
    RegisterSpec("And", { { { "a", VT::kBool }, { "b", VT::kBool } }, true, VT::kBool, "aとbが両方trueならtrueを出力する", "数値・論理" });
    Register("Or", ExecOr);
    RegisterSpec("Or", { { { "a", VT::kBool }, { "b", VT::kBool } }, true, VT::kBool, "aとbのどちらかがtrueならtrueを出力する", "数値・論理" });
    Register("Not", ExecNot);
    RegisterSpec("Not", { { { "a", VT::kBool } }, true, VT::kBool, "aの真偽を反転して出力する", "数値・論理" });
    // ── プレイヤー（PlayerBridge/RunData経由でプレイヤーの状態を操作）──
    Register("DamagePlayer", ExecDamagePlayer);
    RegisterSpec("DamagePlayer", { { { "amount", VT::kFloat, kOne } }, false, VT::kAny, "プレイヤーのHPをamount減らす", "プレイヤー" });
    Register("HealPlayer", ExecHealPlayer);
    RegisterSpec("HealPlayer", { { { "amount", VT::kFloat, kOne } }, false, VT::kAny, "プレイヤーのHPをamount回復する（最大値は超えない）", "プレイヤー" });
    Register("TeleportPlayer", ExecTeleportPlayer);
    RegisterSpec("TeleportPlayer", { { { "x", VT::kFloat }, { "y", VT::kFloat }, { "z", VT::kFloat } }, false, VT::kAny, "プレイヤーを座標(x,y,z)へ瞬間移動させる", "プレイヤー" });
    // ── 敵（EnemyRegistryに登録済みのidで対象を引いて操作）──
    Register("DamageEnemy", ExecDamageEnemy);
    RegisterSpec("DamageEnemy", { { { "target", VT::kString, kDefaultEnemyTarget }, { "amount", VT::kFloat, kOne } }, false, VT::kAny, "targetという名前で登録された敵（EnemyRegistry）にamountダメージを与える", "敵" });
    Register("HealEnemy", ExecHealEnemy);
    RegisterSpec("HealEnemy", { { { "target", VT::kString, kDefaultEnemyTarget }, { "amount", VT::kFloat, kOne } }, false, VT::kAny, "targetという名前で登録された敵をamount回復する", "敵" });
    Register("SetEnemyVisible", ExecSetEnemyVisible);
    RegisterSpec("SetEnemyVisible", { { { "target", VT::kString, kDefaultEnemyTarget }, { "visible", VT::kBool, true } }, false, VT::kAny, "targetという名前で登録された敵の表示/非表示を切り替える（EnemyEntity限定）", "敵" });
    Register("TeleportEnemy", ExecTeleportEnemy);
    RegisterSpec("TeleportEnemy", { { { "target", VT::kString, kDefaultEnemyTarget }, { "x", VT::kFloat }, { "y", VT::kFloat }, { "z", VT::kFloat } }, false, VT::kAny, "targetという名前で登録された敵を座標(x,y,z)へ瞬間移動させる（EnemyEntity限定）", "敵" });
    // ── 演出・音声（SE/BGM、画面フラッシュ、ヒットストップ）──
    Register("PlaySE", ExecPlaySE);
    RegisterSpec("PlaySE", { { { "path", VT::kString, kDefaultSoundPath }, { "volume", VT::kFloat, kOne } }, false, VT::kAny, "効果音を再生する（複数同時再生可、pathは音声ファイルのパス）", "演出・音声" });
    Register("PlayBGM", ExecPlayBGM);
    RegisterSpec("PlayBGM", { { { "path", VT::kString, kDefaultSoundPath }, { "loop", VT::kBool, true } }, false, VT::kAny, "BGMを再生する（前のBGMは自動停止、loopでループ有無を指定）", "演出・音声" });
    Register("StopBGM", ExecStopBGM);
    RegisterSpec("StopBGM", { { }, false, VT::kAny, "再生中のBGMを止める", "演出・音声" });
    Register("ScreenFlash", ExecScreenFlash);
    RegisterSpec("ScreenFlash", { { { "r", VT::kFloat, kOne }, { "g", VT::kFloat, kOne }, { "b", VT::kFloat, kOne }, { "a", VT::kFloat, graphNodeDefaults::kFlashAlpha }, { "duration", VT::kFloat, graphNodeDefaults::kFlashDuration } }, false, VT::kAny, "画面全体を指定色(r,g,b,a)でフラッシュさせ、duration秒かけて元に戻す", "演出・音声" });
    Register("HitStop", ExecHitStop);
    RegisterSpec("HitStop", { { { "frames", VT::kFloat, graphNodeDefaults::kHitStopFrames } }, false, VT::kAny, "framesフレームぶんゲームを一瞬止める演出用のヒットストップ", "演出・音声" });
    // ── 配置物（ステージエディタで置いた配置物をnameで操作。敵・ギミック・ブロック等すべて対象）──
    Register("SetObjectVisible", ExecSetObjectVisible);
    RegisterSpec("SetObjectVisible", { { { "target", VT::kString, kDefaultObjectTarget }, { "visible", VT::kBool, true } }, false, VT::kAny, "レベルの配置物targetの表示/非表示を切り替える（当たり判定も一緒に消える）", "配置物" });
    Register("SetObjectEnabled", ExecSetObjectEnabled);
    RegisterSpec("SetObjectEnabled", { { { "target", VT::kString, kDefaultObjectTarget }, { "enabled", VT::kBool, true } }, false, VT::kAny, "レベルの配置物targetを有効/無効にする（有効化フラグより優先。敵なら生成/消滅、ギミックなら動作停止）", "配置物" });
    Register("TeleportObject", ExecTeleportObject);
    RegisterSpec("TeleportObject", { { { "target", VT::kString, kDefaultObjectTarget }, { "x", VT::kFloat }, { "y", VT::kFloat }, { "z", VT::kFloat } }, false, VT::kAny, "レベルの配置物targetを座標(x,y,z)へ瞬間移動させる（親がいる場合は親からの相対位置）", "配置物" });
    Register("MoveObject", ExecMoveObject);
    RegisterSpec("MoveObject", { { { "target", VT::kString, kDefaultObjectTarget }, { "x", VT::kFloat }, { "y", VT::kFloat }, { "z", VT::kFloat }, { "seconds", VT::kFloat, kOne } }, false, VT::kAny, "レベルの配置物targetをseconds秒かけて座標(x,y,z)へ滑らかに移動させる（扉の開閉や足場の移動に）", "配置物" });
    // ── 進行（シーン遷移・ステージクリア）──
    Register("ChangeScene", ExecChangeScene);
    RegisterSpec("ChangeScene", { { { "scene", VT::kString, Text("MAP") }, { "fadeOut", VT::kFloat, graphNodeDefaults::kSceneFadeSeconds }, { "fadeIn", VT::kFloat, graphNodeDefaults::kSceneFadeSeconds } }, false, VT::kAny, "指定シーン（TITLE / MAP / GAMEPLAY / CLEAR / GAMEOVER 等）へ切り替える", "進行" });
    Register("RequestStageClear", ExecRequestStageClear);
    RegisterSpec("RequestStageClear", { { }, false, VT::kAny, "本編ステージのクリア条件を成立させる（game_rules.jsonのclearFlagを立てる）", "進行" });
    Register("SpawnEnemy", ExecSpawnEnemy);
    RegisterSpec("SpawnEnemy", { { { "target", VT::kString, Text("spawn_0") } }, false, VT::kAny, "名前で指定したspawn_pointを起動して敵を出す（有効化フラグを待たない）", "配置物" });
    // ── 演出（カメラシェイク・パーティクル）──
    Register("ShakeCamera", ExecShakeCamera);
    RegisterSpec("ShakeCamera", { { { "amount", VT::kFloat, graphNodeDefaults::kShakeAmount }, { "seconds", VT::kFloat, graphNodeDefaults::kShakeSeconds } }, false, VT::kAny, "カメラをamountの強さでseconds秒揺らす", "演出・音声" });
    Register("EmitRing", ExecEmitRing);
    RegisterSpec("EmitRing", { { { "group", VT::kString, Text("hit_ring") }, { "x", VT::kFloat }, { "y", VT::kFloat }, { "z", VT::kFloat }, { "radius", VT::kFloat, graphNodeDefaults::kRingRadius }, { "r", VT::kFloat, kOne }, { "g", VT::kFloat, kOne }, { "b", VT::kFloat, kOne } }, false, VT::kAny, "座標(x,y,z)に色(r,g,b)のリング状パーティクルを出す（groupはparticles JSONのグループ名）", "演出・音声" });
}
/** @brief エディタでノードを追加した時に入れる初期値を返す */
GraphValue NodeParamSpec::InitialValue() const {
    return defaultValue ? *defaultValue : kTypeDefaults[static_cast<size_t>(type)];
}
/** @brief 新しいノード種別を登録する（同名で上書きも可） */
void NodeRegistry::Register(const std::string& type, NodeExecuteFn fn) {
    executors_[type] = std::move(fn);
}
/** @brief ノード種別のピン情報を登録する（データ配線を使わないノードは省略可） */
void NodeRegistry::RegisterSpec(const std::string& type, NodeTypeSpec spec) {
    specs_[type] = std::move(spec);
}
/** @brief 型名から実行ロジックを引く登録が無ければnullptr */
const NodeExecuteFn* NodeRegistry::Find(const std::string& type) const {
    auto it = executors_.find(type);
    return (it != executors_.end()) ? &it->second : nullptr;
}
/** @brief 型名からピン情報を引く登録が無ければnullptr（エディタはAny扱いにする） */
const NodeTypeSpec* NodeRegistry::FindSpec(const std::string& type) const {
    auto it = specs_.find(type);
    return (it != specs_.end()) ? &it->second : nullptr;
}
/** @brief 登録済みの型名一覧を返す（GraphEditorのノード追加メニュー用） */
std::vector<std::string> NodeRegistry::GetRegisteredTypes() const {
    std::vector<std::string> types;
    types.reserve(executors_.size());
    for (const auto& [type, fn] : executors_) {
        types.push_back(type);
    }
    return types;
}
} // namespace engine::game
