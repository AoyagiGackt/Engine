#include "Animation.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include "EngineAssert.h"
#include "JsonHelper.h"
#include <cmath>
using namespace engine;

namespace engine::game {

// =================================================================
// ファイル読み込み
// =================================================================

Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename, const std::string& animationName)
{
    Animation animation;

    Assimp::Importer importer;
    std::string filePath = directoryPath + "/" + filename;
    const aiScene* scene = importer.ReadFile(filePath.c_str(), 0);

    ENGINE_ASSERT(scene->mNumAnimations != 0); // アニメーションがなければ止める

    // 指定名（末尾一致）のアニメーションを探す見つからなければ先頭を採用
    aiAnimation* animationAssimp = scene->mAnimations[0];
    if (!animationName.empty()) {
        for (uint32_t i = 0; i < scene->mNumAnimations; ++i) {
            std::string name = scene->mAnimations[i]->mName.C_Str();
            auto barPos = name.find_last_of('|');
            if (barPos != std::string::npos) { name = name.substr(barPos + 1); }
            if (name == animationName) {
                animationAssimp = scene->mAnimations[i];
                break;
            }
        }
    }

    // 時間単位をtick→秒に変換した尺を記録
    animation.duration = float(animationAssimp->mDuration / animationAssimp->mTicksPerSecond);

    for (uint32_t channelIndex = 0; channelIndex < animationAssimp->mNumChannels; ++channelIndex) {

        aiNodeAnim* nodeAnimationAssimp = animationAssimp->mChannels[channelIndex];
        NodeAnimation& nodeAnimation = animation.nodeAnimations[nodeAnimationAssimp->mNodeName.C_Str()];

        // ---- Translate（位置） ----
        for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumPositionKeys; ++keyIndex) {
            aiVectorKey& keyAssimp = nodeAnimationAssimp->mPositionKeys[keyIndex];
            KeyframeVector3 keyframe;
            keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
            // assimpは右手系なのでX軸を反転して左手系に変換する
            keyframe.value = { -keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
            nodeAnimation.translate.keyframes.push_back(keyframe);
        }

        // ---- Rotate（回転） ----
        for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumRotationKeys; ++keyIndex) {
            aiQuatKey& keyAssimp = nodeAnimationAssimp->mRotationKeys[keyIndex];
            KeyframeQuaternion keyframe;
            keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
            // y, z を反転する
            keyframe.value = {
                keyAssimp.mValue.x,
                -keyAssimp.mValue.y,
                -keyAssimp.mValue.z,
                keyAssimp.mValue.w
            };
            nodeAnimation.rotate.keyframes.push_back(keyframe);
        }

        // ---- Scale（スケール） ----
        for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumScalingKeys; ++keyIndex) {
            aiVectorKey& keyAssimp = nodeAnimationAssimp->mScalingKeys[keyIndex];
            KeyframeVector3 keyframe;
            keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
            keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
            nodeAnimation.scale.keyframes.push_back(keyframe);
        }
    }

    return animation;
}

// =================================================================
// 独自JSON形式の保存/読込（AnimationEditorScene用）
// =================================================================

void SaveAnimationJson(const std::string& path, const Animation& anim)
{
    nlohmann::json j;
    j["duration"] = anim.duration;

    nlohmann::json nodes = nlohmann::json::object();
    for (const auto& [name, na] : anim.nodeAnimations) {
        nlohmann::json jn;

        nlohmann::json translateArr = nlohmann::json::array();
        for (const auto& k : na.translate.keyframes) {
            translateArr.push_back({ { "time", k.time }, { "value", { k.value.x, k.value.y, k.value.z } } });
        }
        jn["translate"] = translateArr;

        nlohmann::json rotateArr = nlohmann::json::array();
        for (const auto& k : na.rotate.keyframes) {
            rotateArr.push_back({ { "time", k.time }, { "value", { k.value.x, k.value.y, k.value.z, k.value.w } } });
        }
        jn["rotate"] = rotateArr;

        nlohmann::json scaleArr = nlohmann::json::array();
        for (const auto& k : na.scale.keyframes) {
            scaleArr.push_back({ { "time", k.time }, { "value", { k.value.x, k.value.y, k.value.z } } });
        }
        jn["scale"] = scaleArr;

        nodes[name] = jn;
    }
    j["nodeAnimations"] = nodes;

    JsonHelper::Save(path, j);
}

Animation LoadAnimationJson(const std::string& path)
{
    Animation animation;
    auto j = JsonHelper::Load(path);
    if (j.empty()) { return animation; }

    animation.duration = j.value("duration", 0.0f);

    if (j.contains("nodeAnimations")) {
        for (auto& [name, jn] : j["nodeAnimations"].items()) {
            NodeAnimation na;

            if (jn.contains("translate")) {
                for (auto& jk : jn["translate"]) {
                    KeyframeVector3 kf;
                    kf.time = jk.value("time", 0.0f);
                    const auto& v = jk["value"];
                    kf.value = { v[0], v[1], v[2] };
                    na.translate.keyframes.push_back(kf);
                }
            }
            if (jn.contains("rotate")) {
                for (auto& jk : jn["rotate"]) {
                    KeyframeQuaternion kf;
                    kf.time = jk.value("time", 0.0f);
                    const auto& v = jk["value"];
                    kf.value = { v[0], v[1], v[2], v[3] };
                    na.rotate.keyframes.push_back(kf);
                }
            }
            if (jn.contains("scale")) {
                for (auto& jk : jn["scale"]) {
                    KeyframeVector3 kf;
                    kf.time = jk.value("time", 0.0f);
                    const auto& v = jk["value"];
                    kf.value = { v[0], v[1], v[2] };
                    na.scale.keyframes.push_back(kf);
                }
            }

            animation.nodeAnimations[name] = std::move(na);
        }
    }
    return animation;
}

// =================================================================
// 線形補間
// =================================================================

Vector3 CalculateValue(const AnimationCurve<Vector3>& curve, float time)
{
    if (curve.keyframes.empty()) {
        return { 0.0f, 0.0f, 0.0f };
    }

    // キーが1つ、または時刻がアニメーション開始前なら先頭の値をそのまま返す
    if (curve.keyframes.size() == 1 || time <= curve.keyframes.front().time) {
        return curve.keyframes.front().value;
    }

    // この区間内かを調べて補間する
    for (size_t index = 0; index < curve.keyframes.size() - 1; ++index) {
        size_t nextIndex = index + 1;
        if (curve.keyframes[index].time <= time && time <= curve.keyframes[nextIndex].time) {
            // 区間内にある → 正規化した補間係数 t を求めて線形補間
            float t = (time - curve.keyframes[index].time)
                / (curve.keyframes[nextIndex].time - curve.keyframes[index].time);
            return Lerp(curve.keyframes[index].value, curve.keyframes[nextIndex].value, t);
        }
    }

    // ループで見つからなかったら最後の値を返す
    return curve.keyframes.back().value;
}

// =================================================================
// 球面線形補間
// =================================================================

Quaternion CalculateValue(const AnimationCurve<Quaternion>& curve, float time)
{
    if (curve.keyframes.empty()) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }

    if (curve.keyframes.size() == 1 || time <= curve.keyframes.front().time) {
        return curve.keyframes.front().value;
    }

    for (size_t index = 0; index < curve.keyframes.size() - 1; ++index) {
        size_t nextIndex = index + 1;
        if (curve.keyframes[index].time <= time && time <= curve.keyframes[nextIndex].time) {
            float t = (time - curve.keyframes[index].time)
                / (curve.keyframes[nextIndex].time - curve.keyframes[index].time);
            // 球面線形補間で滑らかに回転を補間する
            return Slerp(curve.keyframes[index].value, curve.keyframes[nextIndex].value, t);
        }
    }

    return curve.keyframes.back().value;
}

}