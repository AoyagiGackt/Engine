/**
 * @file ComboMotion.cpp
 * @brief ComboMotionのキー補間、クリップの編集操作、JSONの読み書き、ポーズレイヤーへの受け渡しを実装するファイル
 */
#include "ComboMotion.h"
#include "GameConstants.h"
#include "JsonHelper.h"
#include "Skeleton.h"
#include <algorithm>
#include <cmath>
#include <exception>
using namespace engine;
using namespace engine::game;
using engine::graphics::PoseLayerRotation;
using engine::graphics::Skeleton;

namespace {

constexpr const char* kMotionPath = "Resources/Config/combo_motion.json";
constexpr int kSchemaVersion = 1;
constexpr size_t kVector3Size = 3;
constexpr float kMinKeySpan = 1.0e-4f; // 同じ時刻に重なったキーで0除算しないための下限

Quaternion EulerDegToQuaternion(const Vector3& eulerDeg)
{
    return MakeRotateXYZQuaternion({ eulerDeg.x * GameConstants::kDegToRad,
        eulerDeg.y * GameConstants::kDegToRad, eulerDeg.z * GameConstants::kDegToRad });
}

/** @brief 時刻を挟む2つのキーと、その間の割合を求める（範囲外は端のキー同士で割合0） */
void FindSegment(const std::vector<MotionKey>& keys, float time, const MotionKey*& outA, const MotionKey*& outB, float& outT)
{
    if (time <= keys.front().time) {
        outA = outB = &keys.front();
        outT = 0.0f;
        return;
    }
    if (time >= keys.back().time) {
        outA = outB = &keys.back();
        outT = 0.0f;
        return;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time <= keys[i + 1].time) {
            outA = &keys[i];
            outB = &keys[i + 1];
            const float span = (std::max)(keys[i + 1].time - keys[i].time, kMinKeySpan);
            outT = (time - keys[i].time) / span;
            return;
        }
    }
    outA = outB = &keys.back();
    outT = 0.0f;
}

nlohmann::json ToJson(const Vector3& v) { return nlohmann::json::array({ v.x, v.y, v.z }); }

/** @brief 有限の数値なら取り出す */
bool FiniteNumberFromJson(const nlohmann::json& j, float& out)
{
    if (!j.is_number()) {
        return false;
    }
    out = j.get<float>();
    return std::isfinite(out);
}

bool Vector3FromJson(const nlohmann::json& j, Vector3& out)
{
    if (!j.is_array() || j.size() != kVector3Size) {
        return false;
    }
    return FiniteNumberFromJson(j[0], out.x) && FiniteNumberFromJson(j[1], out.y) && FiniteNumberFromJson(j[2], out.z);
}

/** @brief キー1つを読む（型が違えばfalse） */
bool ReadKey(const nlohmann::json& keyJson, MotionKey& out)
{
    if (!keyJson.is_object() || !keyJson.contains("t") || !keyJson.contains("euler")) {
        return false;
    }
    float time = 0.0f;
    if (!FiniteNumberFromJson(keyJson["t"], time) || !Vector3FromJson(keyJson["euler"], out.eulerDeg)) {
        return false;
    }
    out.time = std::clamp(time, 0.0f, 1.0f);
    return true;
}

/** @brief トラック1本を読む（ボーン名が無ければfalse、読めないキーは飛ばす） */
bool ReadTrack(const nlohmann::json& trackJson, MotionTrack& out)
{
    if (!trackJson.is_object() || !trackJson.contains("bone") || !trackJson["bone"].is_string()) {
        return false;
    }
    out.bone = trackJson["bone"].get<std::string>();
    if (out.bone.empty()) {
        return false;
    }
    if (trackJson.contains("keys") && trackJson["keys"].is_array()) {
        for (const nlohmann::json& keyJson : trackJson["keys"]) {
            MotionKey key;
            if (ReadKey(keyJson, key)) {
                out.keys.push_back(key);
            }
        }
    }
    out.SortKeys();
    return true;
}

/** @brief クリップ1つを読む（オブジェクトでなければfalse、読めないトラックは飛ばす） */
bool ReadClip(const nlohmann::json& clipJson, MotionClip& out)
{
    if (!clipJson.is_object()) {
        return false;
    }
    if (clipJson.contains("tracks") && clipJson["tracks"].is_array()) {
        for (const nlohmann::json& trackJson : clipJson["tracks"]) {
            MotionTrack track;
            if (ReadTrack(trackJson, track)) {
                out.tracks.push_back(std::move(track));
            }
        }
    }
    return true;
}

} // namespace

// ══════════════════════════════════════════════════════
// トラック
// ══════════════════════════════════════════════════════

Quaternion MotionTrack::Evaluate(float time) const
{
    // 再生もエディタの表示値と同じ角度の補間にそろえる
    // （球面補間にすると、新しいキーの初期値と再生中の姿勢が食い違って、キーを打った瞬間に姿勢が跳ぶ）
    return Normalize(EulerDegToQuaternion(EvaluateEuler(time)));
}

Vector3 MotionTrack::EvaluateEuler(float time) const
{
    if (keys.empty()) {
        return { };
    }
    const MotionKey* a = nullptr;
    const MotionKey* b = nullptr;
    float t = 0.0f;
    FindSegment(keys, time, a, b, t);
    return Lerp(a->eulerDeg, b->eulerDeg, t);
}

MotionKey& MotionTrack::SetKey(float time, const Vector3& eulerDeg, float snap)
{
    if (MotionKey* existing = FindKey(time, snap)) {
        existing->eulerDeg = eulerDeg;
        return *existing;
    }
    const float clamped = std::clamp(time, 0.0f, 1.0f);
    keys.push_back({ clamped, eulerDeg });
    SortKeys();
    return *FindKey(clamped, snap);
}

MotionKey* MotionTrack::FindKey(float time, float snap)
{
    for (MotionKey& key : keys) {
        if (std::abs(key.time - time) <= snap) {
            return &key;
        }
    }
    return nullptr;
}

void MotionTrack::RemoveKey(float time, float snap)
{
    std::erase_if(keys, [time, snap](const MotionKey& key) { return std::abs(key.time - time) <= snap; });
}

void MotionTrack::SortKeys()
{
    std::sort(keys.begin(), keys.end(), [](const MotionKey& a, const MotionKey& b) { return a.time < b.time; });
}

// ══════════════════════════════════════════════════════
// クリップ
// ══════════════════════════════════════════════════════

MotionTrack* MotionClip::FindTrack(const std::string& bone)
{
    for (MotionTrack& track : tracks) {
        if (track.bone == bone) {
            return &track;
        }
    }
    return nullptr;
}

const MotionTrack* MotionClip::FindTrack(const std::string& bone) const
{
    for (const MotionTrack& track : tracks) {
        if (track.bone == bone) {
            return &track;
        }
    }
    return nullptr;
}

MotionTrack& MotionClip::GetOrCreateTrack(const std::string& bone)
{
    if (MotionTrack* existing = FindTrack(bone)) {
        return *existing;
    }
    // 始端と終端を無回転にしておき、段の切り替わりで姿勢が跳ばないようにする
    MotionTrack track;
    track.bone = bone;
    track.keys = { { 0.0f, { } }, { 1.0f, { } } };
    tracks.push_back(std::move(track));
    return tracks.back();
}

void MotionClip::RemoveTrack(const std::string& bone)
{
    std::erase_if(tracks, [&bone](const MotionTrack& track) { return track.bone == bone; });
}

// ══════════════════════════════════════════════════════
// 保管庫
// ══════════════════════════════════════════════════════

ComboMotionLibrary* ComboMotionLibrary::GetInstance()
{
    static ComboMotionLibrary instance;
    return &instance;
}

ComboMotionLibrary::ComboMotionLibrary()
{
    Load();
}

bool ComboMotionLibrary::Load()
{
    const nlohmann::json j = JsonHelper::Load(kMotionPath);
    if (!j.is_object() || !j.contains("rigs") || !j["rigs"].is_object()) {
        return false;
    }
    // 手で書き換えた JSON の型違いで落ちないよう、型を確かめながら読み、読めない要素は飛ばす
    // （それでも想定外の例外が出たら今のデータを残して失敗を返す）
    try {
        std::map<std::string, std::map<std::string, MotionClip>> loaded;
        const nlohmann::json& rigs = j["rigs"];
        for (auto rigIt = rigs.begin(); rigIt != rigs.end(); ++rigIt) {
            const nlohmann::json& attacks = rigIt.value();
            if (!attacks.is_object()) {
                continue;
            }
            for (auto attackIt = attacks.begin(); attackIt != attacks.end(); ++attackIt) {
                MotionClip clip;
                if (!ReadClip(attackIt.value(), clip)) {
                    continue;
                }
                loaded[rigIt.key()][attackIt.key()] = std::move(clip);
            }
        }
        clips_ = std::move(loaded);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool ComboMotionLibrary::Save() const
{
    nlohmann::json rigs = nlohmann::json::object();
    for (const auto& [rigId, attacks] : clips_) {
        nlohmann::json attacksJson = nlohmann::json::object();
        for (const auto& [attackId, clip] : attacks) {
            // トラックの無いクリップは保存しない（エディタで選んだだけの段でファイルを汚さない）
            nlohmann::json tracks = nlohmann::json::array();
            for (const MotionTrack& track : clip.tracks) {
                nlohmann::json keys = nlohmann::json::array();
                for (const MotionKey& key : track.keys) {
                    keys.push_back({ { "t", key.time }, { "euler", ToJson(key.eulerDeg) } });
                }
                tracks.push_back({ { "bone", track.bone }, { "keys", keys } });
            }
            if (!tracks.empty()) {
                attacksJson[attackId] = { { "tracks", tracks } };
            }
        }
        if (!attacksJson.empty()) {
            rigs[rigId] = attacksJson;
        }
    }
    const nlohmann::json root = { { "schemaVersion", kSchemaVersion }, { "rigs", rigs } };
    // JsonHelper::Save()は書き込みに失敗しても何も返さないため、読み戻して同じ内容になったかで成否を決める
    try {
        JsonHelper::Save(kMotionPath, root);
    } catch (const std::exception&) {
        return false;
    }
    return JsonHelper::Load(kMotionPath) == root;
}

const MotionClip* ComboMotionLibrary::Find(const std::string& rigId, const std::string& attackId) const
{
    const auto rig = clips_.find(rigId);
    if (rig == clips_.end()) {
        return nullptr;
    }
    const auto clip = rig->second.find(attackId);
    return (clip != rig->second.end()) ? &clip->second : nullptr;
}

MotionClip& ComboMotionLibrary::GetOrCreate(const std::string& rigId, const std::string& attackId)
{
    return clips_[rigId][attackId];
}

// ══════════════════════════════════════════════════════
// ポーズレイヤー
// ══════════════════════════════════════════════════════

void ComboMotionLayer::Collect(const Skeleton& skeleton, std::vector<PoseLayerRotation>& out) const
{
    lastTrackCount_ = 0;
    lastAppliedCount_ = 0;
    if (clip_ == nullptr) {
        return;
    }
    const auto& jointMap = skeleton.GetJointMap();
    lastTrackCount_ = static_cast<int>(clip_->tracks.size());
    for (const MotionTrack& track : clip_->tracks) {
        const auto it = jointMap.find(track.bone);
        if (it == jointMap.end()) {
            continue; // このリグに無いボーンは無視する（別リグ向けのデータを誤って読んでも崩れない）
        }
        out.push_back({ it->second, track.Evaluate(time_) });
        ++lastAppliedCount_;
    }
}
