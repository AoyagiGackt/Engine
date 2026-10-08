/**
 * @file Animation.cpp
 * @brief glTF/FBXアニメーションの読み込みとキーフレーム補間（Animation）の実装
 */
#include "Animation.h"
#include "AssetPack.h"
#include "EngineAssert.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cmath>
#include <map>
using namespace engine;

namespace engine::game {

namespace {
/** @brief pakへ入れるため、アニメーション1本をノード名とキーフレーム配列の並びに変換する */
std::vector<uint8_t> SerializeAnimation(const Animation& animation)
{
    BinaryWriter writer;
    writer.Write(animation.duration);
    writer.Write(static_cast<uint64_t>(animation.nodeAnimations.size()));
    for (const auto& [nodeName, nodeAnimation] : animation.nodeAnimations) {
        writer.WriteString(nodeName);
        writer.WriteVector(nodeAnimation.translate.keyframes);
        writer.WriteVector(nodeAnimation.rotate.keyframes);
        writer.WriteVector(nodeAnimation.scale.keyframes);
    }
    return std::move(writer.Bytes());
}

/** @brief SerializeAnimationの逆。壊れていればfalse */
bool DeserializeAnimation(const std::vector<uint8_t>& bytes, Animation& animation)
{
    BinaryReader reader(bytes);
    animation.duration = reader.Read<float>();
    const uint64_t nodeCount = reader.Read<uint64_t>();
    for (uint64_t i = 0; i < nodeCount && reader.IsValid(); ++i) {
        NodeAnimation& nodeAnimation = animation.nodeAnimations[reader.ReadString()];
        nodeAnimation.translate.keyframes = reader.ReadVector<KeyframeVector3>();
        nodeAnimation.rotate.keyframes = reader.ReadVector<KeyframeQuaternion>();
        nodeAnimation.scale.keyframes = reader.ReadVector<KeyframeVector3>();
    }
    return reader.IsValid();
}
} // namespace

// ファイル読み込み

Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename, const std::string& animationName)
{
    // 同じ敵種別を複数体・複数レベルで生成する際にassimpの再解析を避けるため、結果を使い回す
    static std::map<std::string, Animation> cache;
    const std::string cacheKey = directoryPath + "/" + filename + "|" + animationName;
    auto cacheIt = cache.find(cacheKey);
    if (cacheIt != cache.end()) {
        return cacheIt->second;
    }

    Animation animation;

    // pakに変換済みのキーフレームがあれば、数MBあるglbファイル全体のassimp解析を省く
    // （1つのファイルからアニメを何本も読むため、元ファイルからだと本数ぶん解析し直していた）
    const std::string packKey = "anim:" + cacheKey;
    std::vector<uint8_t> packed;
    if (AssetPack::GetInstance()->Read(packKey, packed) && DeserializeAnimation(packed, animation)) {
        cache[cacheKey] = animation;
        return animation;
    }
    animation = Animation {};

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
            if (barPos != std::string::npos) {
                name = name.substr(barPos + 1);
            }
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

        // Translate（位置）
        for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumPositionKeys; ++keyIndex) {
            aiVectorKey& keyAssimp = nodeAnimationAssimp->mPositionKeys[keyIndex];
            KeyframeVector3 keyframe;
            keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
            // assimpは右手系なのでX軸を反転して左手系に変換する
            keyframe.value = { -keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
            nodeAnimation.translate.keyframes.push_back(keyframe);
        }

        // Rotate（回転）
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

        // Scale（スケール）
        for (uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumScalingKeys; ++keyIndex) {
            aiVectorKey& keyAssimp = nodeAnimationAssimp->mScalingKeys[keyIndex];
            KeyframeVector3 keyframe;
            keyframe.time = float(keyAssimp.mTime / animationAssimp->mTicksPerSecond);
            keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
            nodeAnimation.scale.keyframes.push_back(keyframe);
        }
    }

    if (AssetPack::GetInstance()->IsCooking()) {
        AssetPack::GetInstance()->Record(packKey, SerializeAnimation(animation));
    }
    cache[cacheKey] = animation;
    return animation;
}

// 線形補間

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

// 球面線形補間

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
