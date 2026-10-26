/** @file ImGuiControlSceneSettings.cpp
 * @brief メッシュ・マテリアル・ライト・シーン演出の設定欄
 */
#include "ImGuiControl.h"
#include "ImGuiManager.h"
#include "LightManager.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include <cstdio>

namespace engine::graphics {
#ifdef USE_IMGUI
namespace {
constexpr size_t kPointLightLabelCapacity = 64;
constexpr ImVec4 kWarningTextColor = { 1.0f, 0.5f, 0.0f, 1.0f };
constexpr float kTransformDragSpeed = 0.01f;
constexpr float kPointLightDragSpeed = 0.1f;
}

/** @brief メッシュの設定欄を出す */
void ImGuiControlPanel::ShowMeshSettings() {
    // メッシュ設定
    if (ImGui::CollapsingHeader("メッシュ設定", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* meshItems[] = { "球体", "立方体", "平面" };
        int currentMesh = static_cast<int>(MeshManager::GetInstance()->GetCurrentMeshType());

        if (ImGui::Combo("メッシュ種類", &currentMesh, meshItems, IM_ARRAYSIZE(meshItems))) {
            MeshManager::GetInstance()->SetCurrentMeshType(static_cast<MeshType>(currentMesh));
        }

        const char* meshNames[] = { "Sphere", "Cube", "Plane" };
        for (int i = 0; i < static_cast<int>(MeshType::kCount); ++i) {
            ImGui::PushID(i);
            if (ImGui::TreeNode(meshNames[i])) {
                const MeshType type = static_cast<MeshType>(i);
                Transform transform = MeshManager::GetInstance()->GetMeshTransform(type);
                bool changed = ImGui::DragFloat3("スケール", &transform.scale.x, kTransformDragSpeed);
                changed |= ImGui::DragFloat3("回転", &transform.rotate.x, kTransformDragSpeed);
                changed |= ImGui::DragFloat3("移動", &transform.translate.x, kTransformDragSpeed);
                if (changed) {
                    MeshManager::GetInstance()->SetMeshTransform(type, transform);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
}

/** @brief マテリアルの設定欄を出す */
void ImGuiControlPanel::ShowMaterialSettings() {
    // マテリアル設定
    if (ImGui::CollapsingHeader("マテリアル設定", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* matItems[] = { "赤", "緑", "青", "白" };
        int currentMat = (int)MaterialManager::GetInstance()->GetCurrentMaterialIndex();
        if (ImGui::Combo("マテリアルカラー", &currentMat, matItems, IM_ARRAYSIZE(matItems))) {
            MaterialManager::GetInstance()->SetCurrentMaterialIndex(currentMat);
        }
    }
}

/** @brief ライティングの設定欄を出す */
void ImGuiControlPanel::ShowLightingSettings() {
    // ライティングモード
    if (ImGui::CollapsingHeader("ライティング設定", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* lightItems[] = { "なし", "Lambert", "Half Lambert", "Lambert+Phong", "HalfLambert+Phong" };
        int currentMode = LightManager::GetInstance()->GetLightingMode();
        if (ImGui::Combo("ライティングモード", &currentMode, lightItems, IM_ARRAYSIZE(lightItems))) {
            LightManager::GetInstance()->SetLightingMode(currentMode);
        }
    }
}

/** @brief ガラス割れの設定欄を出す */
void ImGuiControlPanel::ShowGlassShatterSection() {
    // ガラス割れエフェクト
    if (ImGui::CollapsingHeader("ガラス割れエフェクト")) {
        if (sGlassShatterTrigger) {
            if (ImGui::Button("テスト再生")) {
                sGlassShatterTrigger();
            }
            ImGui::TextDisabled("※ 演出中は再度押しても無視されます");
        } else {
            ImGui::TextWrapped("このシーンではガラス割れエフェクトの\n"
                               "テスト再生は登録されていません。");
        }
    }
}

/** @brief ライトの設定欄を出す */
void ImGuiControlPanel::ShowLightSection() {
    // ライト（平行光源 + ポイントライト）
    if (ImGui::CollapsingHeader("ライト", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!sObj3dCommon) {
            ImGui::TextColored(kWarningTextColor,
                "RegisterObject3dCommon() が未呼出しです。\n"
                "初期化時に RegisterObject3dCommon(obj3dCommon) を呼んでください。");
        } else {
            ShowDirectionalLightSection();
            ImGui::Separator();
            ShowPointLightSection();
        }
    }
}

/** @brief 点光源の設定欄を出す */
void ImGuiControlPanel::ShowPointLightSection() {
    // ポイントライト
    if (!ImGui::TreeNodeEx("ポイントライト（デバッグ）", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const int maxLights = static_cast<int>(Object3dCommon::kMaxPointLights);
    ImGui::TextDisabled("最大 %d 個。毎フレーム VisitEditorPointLights() をシーンで適用してください。", maxLights);
    ImGui::Separator();

    // ライト追加ボタン（初期位置は EditorPointLight の既定値）
    if (static_cast<int>(sEditorLights.size()) < maxLights) {
        if (ImGui::Button("+ ライトを追加")) {
            sEditorLights.push_back(EditorPointLight {});
        }
    } else {
        ImGui::TextDisabled("（上限 %d 個に達しています）", maxLights);
    }

    // 各ライトの設定
    /** @brief 点光源の調整できる項目と、その範囲 */
    struct LightSlider {
        const char* label;
        float EditorPointLight::* field;
        float min;
        float max;
        const char* format;
    };
    static constexpr LightSlider kLightSliders[] = {
        { "半径", &EditorPointLight::radius, 0.1f, 50.0f, "%.1f" },
        { "強度", &EditorPointLight::intensity, 0.0f, 10.0f, "%.2f" },
    };
    for (int i = 0; i < static_cast<int>(sEditorLights.size());) {
        auto& pl = sEditorLights[i];
        ImGui::PushID(i);

        char label[kPointLightLabelCapacity];
        snprintf(label, sizeof(label), "ポイントライト %d", i);
        bool open = ImGui::CollapsingHeader(label);

        // ヘッダー行に有効/削除ボタン
        ImGui::SameLine();
        ImGui::Checkbox("##enabled", &pl.enabled);
        ImGui::SameLine();
        bool removed = ImGui::SmallButton("削除");

        if (open) {
            ImGui::DragFloat3("位置", &pl.position.x, kPointLightDragSpeed);
            ImGui::ColorEdit3("色", &pl.color.x);
            for (const LightSlider& slider : kLightSliders) {
                ImGui::SliderFloat(slider.label, &(pl.*slider.field), slider.min, slider.max, slider.format);
            }
        }

        ImGui::PopID();

        if (removed) {
            sEditorLights.erase(sEditorLights.begin() + i);
        } else {
            ++i;
        }
    }
    ImGui::TreePop();
}

#endif
} // namespace engine::graphics
