#pragma once
#include "MakeAffine.h"
namespace engine::graphics {
// GPU マテリアル定数バッファレイアウト（Object3dPS.hlsl の Material 構造体と一致させること）
// Object3d と InstancedObject3d で共用する
struct ObjectMaterialLayout {
    Vector4 color = { 1, 1, 1, 1 };
    int enableLighting = 1;
    int shadingType = 1;
    int useCubemap = 0;
    int useTexture = 1;
    Matrix4x4 uvTransform = { };
    Vector3 specularColor = { 1, 1, 1 };
    float shininess = 32.0f;
    Vector3 cameraWorldPos = { };
    float envMapIntensity = 0.0f;
    Vector3 rimColor = { };
    float rimPower = 3.0f;
    float rimIntensity = 0.0f;
    int enableRim = 0;
    int useNormalMap = 0;
    float metallic = 0.0f;
    float roughness = 0.5f;
    float _pbr_pad[3] = { };
};

} // namespace engine::graphics
