/**
 * @file NodeRegistry.cpp
 * @brief NodeRegistryのイベントグラフのデータ、編集、実行に関する具体的な処理を実装するファイル
 */
#include "NodeRegistry.h"
#include "AudioBridge.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "EventBus.h"
#include "GameFlags.h"
#include "GraphRuntime.h"
#include "Logger.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "RunData.h"
#include "ScreenFlash.h"
#include "TimeManager.h"
#include <random>
#include <utility>
using namespace engine::game;
using namespace engine;

namespace {

// パラメータ解決はGraphRuntime::ResolveParamに委譲する（データ配線があれば供給元ノードの出力を優先）

NodeResult ExecSetVariable(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string name = AsString(rt.ResolveParam(node, "name", std::string { }));
    if (name.empty()) {
        Logger::LogError("[Graph] SetVariable node '" + node.id + "' is missing 'name' param");
    } else {
        GraphValue value = rt.ResolveParam(node, "value", GraphValue { 0.0f });
        rt.SetVariable(name, value);
        rt.SetNodeOutput(node.id, std::move(value)); // 設定した値をそのまま出力ピンにも流す
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecIf(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string varName = AsString(rt.ResolveParam(node, "var", std::string { }));
    CompareOp op = ParseCompareOp(AsString(rt.ResolveParam(node, "op", std::string("=="))));
    GraphValue rhs = rt.ResolveParam(node, "value", GraphValue { 0.0f });

    const GraphValue* lhs = rt.GetVariable(varName);
    bool result = lhs ? Compare(op, *lhs, rhs) : false;
    if (!lhs) {
        Logger::LogError("[Graph] If node '" + node.id + "' references undefined variable '" + varName + "'");
    }

    outNextId = result ? node.nextTrue : node.nextFalse;
    return NodeResult::Continue;
}

NodeResult ExecWait(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    float seconds = AsFloat(rt.ResolveParam(node, "seconds", GraphValue { 0.0f }));
    rt.BeginWait(seconds);
    outNextId = node.next; // 待機完了後にRunUntilSuspendOrHalt()側がこれをresume先として使う
    return NodeResult::Suspend;
}

NodeResult ExecEmitEvent(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string eventName = AsString(rt.ResolveParam(node, "event", std::string { }));
    if (eventName.empty()) {
        Logger::LogError("[Graph] EmitEvent node '" + node.id + "' is missing 'event' param");
    } else {
        EventBus::GetInstance()->Emit(eventName);
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

// ステージエディタで置いたトリガー等が立てたフラグを、グラフ側のロジックで参照できるようにする
// GameFlags本体はグラフインスタンス間・ステージ間で共有のグローバルストア（GraphRuntimeのローカル変数とは別物）
NodeResult ExecSetFlag(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string flag = AsString(rt.ResolveParam(node, "flag", std::string { }));
    if (flag.empty()) {
        Logger::LogError("[Graph] SetFlag node '" + node.id + "' is missing 'flag' param");
    } else {
        bool value = AsBool(rt.ResolveParam(node, "value", GraphValue { false }));
        GameFlags::GetInstance()->SetFlag(flag, value);
        rt.SetNodeOutput(node.id, GraphValue { value });
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

// フラグの値をグラフのローカル変数へコピーする（既存のIfノードで分岐に使うため）
// あわせて出力データピンにも流すので、他ノードのboolパラメータへ直接配線もできる
NodeResult ExecGetFlag(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
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
    return NodeResult::Continue;
}

// 別ファイルのグラフを呼び出して再利用する（カプセル化）子グラフがHaltするまで親は待機する
NodeResult ExecSubgraph(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string path = AsString(rt.ResolveParam(node, "path", std::string { }));
    outNextId = node.next;
    if (path.empty() || !rt.BeginSubgraph(path)) {
        Logger::LogError("[Graph] Subgraph node '" + node.id + "' failed to start: " + path);
        return NodeResult::Continue;
    }
    return NodeResult::Suspend;
}

// a op b を計算して出力データピンに流す（op  "+" "-" "*" "/"）
NodeResult ExecMath(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    float a = AsFloat(rt.ResolveParam(node, "a", GraphValue { 0.0f }));
    float b = AsFloat(rt.ResolveParam(node, "b", GraphValue { 0.0f }));
    MathOp op = ParseMathOp(AsString(rt.ResolveParam(node, "op", std::string("+"))));
    rt.SetNodeOutput(node.id, GraphValue { ApplyMath(op, a, b) });
    outNextId = node.next;
    return NodeResult::Continue;
}

// Ifと違い変数名ではなく2つのデータピン（リテラルまたは配線）を直接比較してbool出力する
// 結果はSetVariable経由でIfへ渡すか、他ノードのbool入力へそのまま配線して使う
NodeResult ExecCompare(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    GraphValue a = rt.ResolveParam(node, "a", GraphValue { 0.0f });
    GraphValue b = rt.ResolveParam(node, "b", GraphValue { 0.0f });
    CompareOp op = ParseCompareOp(AsString(rt.ResolveParam(node, "op", std::string("=="))));
    rt.SetNodeOutput(node.id, GraphValue { Compare(op, a, b) });
    outNextId = node.next;
    return NodeResult::Continue;
}

// [min, max] のFloat乱数を出力データピンに流す
NodeResult ExecRandom(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    float mn = AsFloat(rt.ResolveParam(node, "min", GraphValue { 0.0f }));
    float mx = AsFloat(rt.ResolveParam(node, "max", GraphValue { 1.0f }));
    if (mn > mx) {
        std::swap(mn, mx);
    }
    static std::mt19937 rng { std::random_device { }() };
    std::uniform_real_distribution<float> dist(mn, mx);
    rt.SetNodeOutput(node.id, GraphValue { dist(rng) });
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecDamagePlayer(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    int amount = static_cast<int>(AsFloat(rt.ResolveParam(node, "amount", GraphValue { 1.0f })));
    RunData::GetInstance()->TakeDamage(amount);
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecHealPlayer(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    int amount = static_cast<int>(AsFloat(rt.ResolveParam(node, "amount", GraphValue { 1.0f })));
    RunData::GetInstance()->Heal(amount);
    outNextId = node.next;
    return NodeResult::Continue;
}

// targetはEnemyRegistryに登録されたid（Scene側がEnemyEntity::SetId+Register済みであること）
NodeResult ExecDamageEnemy(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string target = AsString(rt.ResolveParam(node, "target", std::string { }));
    int amount = static_cast<int>(AsFloat(rt.ResolveParam(node, "amount", GraphValue { 1.0f })));
    EnemyEntity* enemy = EnemyRegistry::GetInstance()->Find(target);
    if (!enemy) {
        Logger::LogError("[Graph] DamageEnemy node '" + node.id + "' unknown target '" + target + "'");
    } else {
        enemy->TakeDamage(amount);
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecHealEnemy(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string target = AsString(rt.ResolveParam(node, "target", std::string { }));
    int amount = static_cast<int>(AsFloat(rt.ResolveParam(node, "amount", GraphValue { 1.0f })));
    EnemyEntity* enemy = EnemyRegistry::GetInstance()->Find(target);
    if (!enemy) {
        Logger::LogError("[Graph] HealEnemy node '" + node.id + "' unknown target '" + target + "'");
    } else {
        enemy->Heal(amount);
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

// pathはWAV/MP3ファイルパス初回のみ読み込み、以降はAudioBridge内のキャッシュから再生する
NodeResult ExecPlaySE(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string path = AsString(rt.ResolveParam(node, "path", std::string { }));
    float volume = AsFloat(rt.ResolveParam(node, "volume", GraphValue { 1.0f }));
    if (path.empty()) {
        Logger::LogError("[Graph] PlaySE node '" + node.id + "' is missing 'path' param");
    } else {
        AudioBridge::GetInstance()->PlaySE(path, volume);
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecPlayBGM(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string path = AsString(rt.ResolveParam(node, "path", std::string { }));
    bool loop = AsBool(rt.ResolveParam(node, "loop", GraphValue { true }));
    if (path.empty()) {
        Logger::LogError("[Graph] PlayBGM node '" + node.id + "' is missing 'path' param");
    } else {
        AudioBridge::GetInstance()->PlayBGM(path, loop);
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecStopBGM(GraphRuntime&, const GraphNode& node, std::string& outNextId)
{
    AudioBridge::GetInstance()->StopBGM();
    outNextId = node.next;
    return NodeResult::Continue;
}

// 画面全体を指定色でフラッシュさせる（durationかけて元に戻る）
NodeResult ExecScreenFlash(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    float r = AsFloat(rt.ResolveParam(node, "r", GraphValue { 1.0f }));
    float g = AsFloat(rt.ResolveParam(node, "g", GraphValue { 1.0f }));
    float b = AsFloat(rt.ResolveParam(node, "b", GraphValue { 1.0f }));
    float a = AsFloat(rt.ResolveParam(node, "a", GraphValue { 0.5f }));
    float duration = AsFloat(rt.ResolveParam(node, "duration", GraphValue { 0.15f }));
    ScreenFlash::GetInstance()->Request({ r, g, b, a }, duration);
    outNextId = node.next;
    return NodeResult::Continue;
}

// framesフレームぶんゲーム内時間を止める演出用（Wait秒数指定とは別物、TimeManagerのヒットストップ機構を使う）
NodeResult ExecHitStop(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    int frames = static_cast<int>(AsFloat(rt.ResolveParam(node, "frames", GraphValue { 3.0f })));
    TimeManager::GetInstance()->RequestHitStop(frames);
    outNextId = node.next;
    return NodeResult::Continue;
}

// targetはEnemyRegistryに登録されたid（EnemyEntity限定KnightEnemyには可視切り替えAPIが無い）
NodeResult ExecSetEnemyVisible(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string target = AsString(rt.ResolveParam(node, "target", std::string { }));
    bool visible = AsBool(rt.ResolveParam(node, "visible", GraphValue { true }));
    EnemyEntity* enemy = EnemyRegistry::GetInstance()->Find(target);
    if (!enemy) {
        Logger::LogError("[Graph] SetEnemyVisible node '" + node.id + "' unknown target '" + target + "'");
    } else {
        enemy->SetVisible(visible);
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

// GraphValueにVector3型が無いため、座標はx/y/zの3つのFloatパラメータに分けて受け取る
NodeResult ExecTeleportEnemy(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    std::string target = AsString(rt.ResolveParam(node, "target", std::string { }));
    float x = AsFloat(rt.ResolveParam(node, "x", GraphValue { 0.0f }));
    float y = AsFloat(rt.ResolveParam(node, "y", GraphValue { 0.0f }));
    float z = AsFloat(rt.ResolveParam(node, "z", GraphValue { 0.0f }));
    EnemyEntity* enemy = EnemyRegistry::GetInstance()->Find(target);
    if (!enemy) {
        Logger::LogError("[Graph] TeleportEnemy node '" + node.id + "' unknown target '" + target + "'");
    } else {
        enemy->GetPositionRef() = { x, y, z };
        enemy->RefreshVisualTransforms();
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

// PlayerBridgeに登録されたシーンのプレイヤーを瞬間移動させる（登録が無ければ何もしない）
NodeResult ExecTeleportPlayer(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    float x = AsFloat(rt.ResolveParam(node, "x", GraphValue { 0.0f }));
    float y = AsFloat(rt.ResolveParam(node, "y", GraphValue { 0.0f }));
    float z = AsFloat(rt.ResolveParam(node, "z", GraphValue { 0.0f }));
    Player* player = PlayerBridge::GetInstance()->Get();
    if (!player) {
        Logger::LogError("[Graph] TeleportPlayer node '" + node.id + "' no Player registered (PlayerBridge)");
    } else {
        player->SetPosition({ x, y, z });
    }
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecAnd(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    bool a = AsBool(rt.ResolveParam(node, "a", GraphValue { false }));
    bool b = AsBool(rt.ResolveParam(node, "b", GraphValue { false }));
    rt.SetNodeOutput(node.id, GraphValue { a && b });
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecOr(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    bool a = AsBool(rt.ResolveParam(node, "a", GraphValue { false }));
    bool b = AsBool(rt.ResolveParam(node, "b", GraphValue { false }));
    rt.SetNodeOutput(node.id, GraphValue { a || b });
    outNextId = node.next;
    return NodeResult::Continue;
}

NodeResult ExecNot(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)
{
    bool a = AsBool(rt.ResolveParam(node, "a", GraphValue { false }));
    rt.SetNodeOutput(node.id, GraphValue { !a });
    outNextId = node.next;
    return NodeResult::Continue;
}

} // namespace

NodeRegistry* NodeRegistry::GetInstance()
{
    static NodeRegistry instance;
    return &instance;
}

NodeRegistry::NodeRegistry()
{
    RegisterBuiltins();
}

void NodeRegistry::RegisterBuiltins()
{
    using VT = GraphValueType;

    // ── よく使う（変数の読み書き・分岐・待機・イベント発火など基本のフロー制御）──
    Register("SetVariable", ExecSetVariable);
    RegisterSpec("SetVariable", { { { "name", VT::String }, { "value", VT::Any } }, true, VT::Any, "変数nameにvalueを代入する（無ければ新規作成）", "よく使う" });

    Register("If", ExecIf);
    RegisterSpec("If", { { { "var", VT::String }, { "op", VT::String }, { "value", VT::Any } }, false, VT::Any, "変数varの値をopでvalueと比較し、真ならnextTrue／偽ならnextFalseへ分岐する", "よく使う" });

    Register("Wait", ExecWait);
    RegisterSpec("Wait", { { { "seconds", VT::Float } }, false, VT::Any, "seconds秒待ってから次へ進む（フレームをまたいで待機する）", "よく使う" });

    Register("EmitEvent", ExecEmitEvent);
    RegisterSpec("EmitEvent", { { { "event", VT::String } }, false, VT::Any, "名前付きイベントを発火する（EventBus経由、コード側でSubscribe済みの処理が呼ばれる）", "よく使う" });

    Register("Compare", ExecCompare);
    RegisterSpec("Compare", { { { "a", VT::Any }, { "b", VT::Any }, { "op", VT::String } }, true, VT::Bool, "2つの値をop（== != < <= > >=）で比較しboolを出力する（Ifと違い変数名でなくデータピンを直接比較できる）", "よく使う" });

    // ── 変数・フラグ（GameFlagsによるステージ間共有フラグ、サブグラフ呼び出し）──
    Register("SetFlag", ExecSetFlag);
    RegisterSpec("SetFlag", { { { "flag", VT::String }, { "value", VT::Bool } }, true, VT::Bool, "グローバルなbool変数(GameFlags)を書き換えるステージのトリガーやIf/GetFlagから参照できる", "変数・フラグ" });

    Register("GetFlag", ExecGetFlag);
    RegisterSpec("GetFlag", { { { "flag", VT::String }, { "into", VT::String } }, true, VT::Bool, "GameFlagsの値を読む（intoを指定するとグラフのローカル変数にもコピーされる）", "変数・フラグ" });

    Register("Subgraph", ExecSubgraph);
    RegisterSpec("Subgraph", { { { "path", VT::String } }, false, VT::Any, "別のグラフJSONを呼び出す（子がHaltするまで待機、ロジックの再利用・整理に使う）", "変数・フラグ" });

    // ── 数値・論理（四則演算・比較・乱数・ブール演算）──
    Register("Math", ExecMath);
    RegisterSpec("Math", { { { "a", VT::Float }, { "b", VT::Float }, { "op", VT::String } }, true, VT::Float, "a op b を計算する（op: + - * /）", "数値・論理" });

    Register("Random", ExecRandom);
    RegisterSpec("Random", { { { "min", VT::Float }, { "max", VT::Float } }, true, VT::Float, "min〜maxの範囲でFloat乱数を出力する", "数値・論理" });

    Register("And", ExecAnd);
    RegisterSpec("And", { { { "a", VT::Bool }, { "b", VT::Bool } }, true, VT::Bool, "aとbが両方trueならtrueを出力する", "数値・論理" });

    Register("Or", ExecOr);
    RegisterSpec("Or", { { { "a", VT::Bool }, { "b", VT::Bool } }, true, VT::Bool, "aとbのどちらかがtrueならtrueを出力する", "数値・論理" });

    Register("Not", ExecNot);
    RegisterSpec("Not", { { { "a", VT::Bool } }, true, VT::Bool, "aの真偽を反転して出力する", "数値・論理" });

    // ── プレイヤー（PlayerBridge/RunData経由でプレイヤーの状態を操作）──
    Register("DamagePlayer", ExecDamagePlayer);
    RegisterSpec("DamagePlayer", { { { "amount", VT::Float } }, false, VT::Any, "プレイヤーのHPをamount減らす", "プレイヤー" });

    Register("HealPlayer", ExecHealPlayer);
    RegisterSpec("HealPlayer", { { { "amount", VT::Float } }, false, VT::Any, "プレイヤーのHPをamount回復する（最大値は超えない）", "プレイヤー" });

    Register("TeleportPlayer", ExecTeleportPlayer);
    RegisterSpec("TeleportPlayer", { { { "x", VT::Float }, { "y", VT::Float }, { "z", VT::Float } }, false, VT::Any, "プレイヤーを座標(x,y,z)へ瞬間移動させる", "プレイヤー" });

    // ── 敵（EnemyRegistryに登録済みのidで対象を引いて操作）──
    Register("DamageEnemy", ExecDamageEnemy);
    RegisterSpec("DamageEnemy", { { { "target", VT::String }, { "amount", VT::Float } }, false, VT::Any, "targetという名前で登録された敵（EnemyRegistry）にamountダメージを与える", "敵" });

    Register("HealEnemy", ExecHealEnemy);
    RegisterSpec("HealEnemy", { { { "target", VT::String }, { "amount", VT::Float } }, false, VT::Any, "targetという名前で登録された敵をamount回復する", "敵" });

    Register("SetEnemyVisible", ExecSetEnemyVisible);
    RegisterSpec("SetEnemyVisible", { { { "target", VT::String }, { "visible", VT::Bool } }, false, VT::Any, "targetという名前で登録された敵の表示/非表示を切り替える（EnemyEntity限定）", "敵" });

    Register("TeleportEnemy", ExecTeleportEnemy);
    RegisterSpec("TeleportEnemy", { { { "target", VT::String }, { "x", VT::Float }, { "y", VT::Float }, { "z", VT::Float } }, false, VT::Any, "targetという名前で登録された敵を座標(x,y,z)へ瞬間移動させる（EnemyEntity限定）", "敵" });

    // ── 演出・音声（SE/BGM、画面フラッシュ、ヒットストップ）──
    Register("PlaySE", ExecPlaySE);
    RegisterSpec("PlaySE", { { { "path", VT::String }, { "volume", VT::Float } }, false, VT::Any, "効果音を再生する（複数同時再生可、pathは音声ファイルのパス）", "演出・音声" });

    Register("PlayBGM", ExecPlayBGM);
    RegisterSpec("PlayBGM", { { { "path", VT::String }, { "loop", VT::Bool } }, false, VT::Any, "BGMを再生する（前のBGMは自動停止、loopでループ有無を指定）", "演出・音声" });

    Register("StopBGM", ExecStopBGM);
    RegisterSpec("StopBGM", { { }, false, VT::Any, "再生中のBGMを止める", "演出・音声" });

    Register("ScreenFlash", ExecScreenFlash);
    RegisterSpec("ScreenFlash", { { { "r", VT::Float }, { "g", VT::Float }, { "b", VT::Float }, { "a", VT::Float }, { "duration", VT::Float } }, false, VT::Any, "画面全体を指定色(r,g,b,a)でフラッシュさせ、duration秒かけて元に戻す", "演出・音声" });

    Register("HitStop", ExecHitStop);
    RegisterSpec("HitStop", { { { "frames", VT::Float } }, false, VT::Any, "framesフレームぶんゲームを一瞬止める演出用のヒットストップ", "演出・音声" });
}

void NodeRegistry::Register(const std::string& type, NodeExecuteFn fn)
{
    executors_[type] = std::move(fn);
}

void NodeRegistry::RegisterSpec(const std::string& type, NodeTypeSpec spec)
{
    specs_[type] = std::move(spec);
}

const NodeExecuteFn* NodeRegistry::Find(const std::string& type) const
{
    auto it = executors_.find(type);
    return (it != executors_.end()) ? &it->second : nullptr;
}

const NodeTypeSpec* NodeRegistry::FindSpec(const std::string& type) const
{
    auto it = specs_.find(type);
    return (it != specs_.end()) ? &it->second : nullptr;
}

std::vector<std::string> NodeRegistry::GetRegisteredTypes() const
{
    std::vector<std::string> types;
    types.reserve(executors_.size());
    for (const auto& [type, fn] : executors_) {
        types.push_back(type);
    }
    return types;
}
