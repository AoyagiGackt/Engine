#include "ImguiControl.h"
#include "ImGuiManager.h"
#include "LightingMode.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "LightManager.h"
#include "GrayscaleEffect.h"
#include "ImageFilter.h"
#include "VignetteEffect.h"
#include "HsvFilter.h"
#include "BloomEffect.h"
#include "GlassShatterEffect.h"
#include "TextureManager.h"
#include <vector>

// =====================================================
// モジュール内スタティック
// =====================================================

static Object3dCommon*           s_obj3dCommon = nullptr;
static std::vector<DebugPointLight> s_debugLights;

// =====================================================
// 公開 API
// =====================================================

void RegisterObject3dCommon(Object3dCommon* common)
{
    s_obj3dCommon = common;
}

const std::vector<DebugPointLight>& GetDebugPointLights()
{
    return s_debugLights;
}

// =====================================================
// ShowControls
// =====================================================

void ShowControls()
{
#ifdef USE_IMGUI

    // -------------------------------------------------------
    // メインコントロールウィンドウ
    // -------------------------------------------------------
    ImGui::Begin("コントロール");

    // ---- メッシュ設定 ----
    if (ImGui::CollapsingHeader("メッシュ設定", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* meshItems[] = { "球体", "立方体", "平面" };
        int currentMesh = (int)MeshManager::GetInstance()->GetCurrentMeshType();

        if (ImGui::Combo("メッシュ種類", &currentMesh, meshItems, IM_ARRAYSIZE(meshItems))) {
            MeshManager::GetInstance()->SetCurrentMeshType((MeshType)currentMesh);
        }

        const char* meshNames[] = { "Sphere", "Cube", "Plane" };
        for (int i = 0; i < MeshType_Count; ++i) {
            ImGui::PushID(i);
            if (ImGui::TreeNode(meshNames[i])) {
                ImGui::DragFloat3("スケール", &MeshManager::GetInstance()->meshes[i].transform.scale.x,     0.01f);
                ImGui::DragFloat3("回転",     &MeshManager::GetInstance()->meshes[i].transform.rotate.x,    0.01f);
                ImGui::DragFloat3("移動",     &MeshManager::GetInstance()->meshes[i].transform.translate.x, 0.01f);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    // ---- マテリアル設定 ----
    if (ImGui::CollapsingHeader("マテリアル設定", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* matItems[] = { "赤", "緑", "青", "白" };
        int currentMat = (int)MaterialManager::GetInstance()->GetCurrentMaterialIndex();
        if (ImGui::Combo("マテリアルカラー", &currentMat, matItems, IM_ARRAYSIZE(matItems))) {
            MaterialManager::GetInstance()->SetCurrentMaterialIndex(currentMat);
        }
    }

    // ---- ライティングモード ----
    if (ImGui::CollapsingHeader("ライティング設定", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* lightItems[] = { "なし", "Lambert", "Half Lambert", "Lambert+Phong", "HalfLambert+Phong" };
        int currentMode = LightManager::GetInstance()->GetLightingMode();
        if (ImGui::Combo("ライティングモード", &currentMode, lightItems, IM_ARRAYSIZE(lightItems))) {
            LightManager::GetInstance()->SetLightingMode(currentMode);
        }
    }

    // ---- ポストプロセス ----
    if (ImGui::CollapsingHeader("ポストプロセス", ImGuiTreeNodeFlags_DefaultOpen)) {

        // グレースケール
        auto* gs = GrayscaleEffect::GetInstance();
        bool gsEnabled = gs->IsEnabled();
        if (ImGui::Checkbox("グレースケール", &gsEnabled)) { gs->SetEnabled(gsEnabled); }
        if (gsEnabled) {
            float amount = gs->GetAmount();
            if (ImGui::SliderFloat("適用量##gs", &amount, 0.0f, 1.0f)) { gs->SetAmount(amount); }
        }

        ImGui::Separator();

        // Bloom
        auto* bloom = BloomEffect::GetInstance();
        bool bloomEnabled = bloom->IsEnabled();
        if (ImGui::Checkbox("Bloom（発光）", &bloomEnabled)) { bloom->SetEnabled(bloomEnabled); }
        if (bloomEnabled) {
            float threshold = bloom->GetThreshold();
            if (ImGui::SliderFloat("しきい値##bloom", &threshold, 0.0f, 1.0f, "%.2f")) {
                bloom->SetThreshold(threshold);
            }
            ImGui::SameLine(); ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("この輝度以上のピクセルが光って見えます。\n低いほど広い範囲が光る。");
            }
            float intensity = bloom->GetIntensity();
            if (ImGui::SliderFloat("強度##bloom", &intensity, 0.0f, 4.0f, "%.2f")) {
                bloom->SetIntensity(intensity);
            }
        }

        ImGui::Separator();

        // イメージフィルター
        auto* imgFilter = ImageFilter::GetInstance();
        bool filterEnabled = imgFilter->IsEnabled();
        if (ImGui::Checkbox("イメージフィルター", &filterEnabled)) { imgFilter->SetEnabled(filterEnabled); }

        if (filterEnabled) {
            const char* modeItems[] = { "Box", "Linear (Gaussian)", "Prewitt エッジ", "深度アウトライン", "ラジアルブラー", "ディゾルブ", "GPU ノイズ" };
            int currentMode = (int)imgFilter->GetMode();
            if (ImGui::Combo("フィルターモード", &currentMode, modeItems, IM_ARRAYSIZE(modeItems))) {
                imgFilter->SetMode((ImageFilter::Mode)currentMode);
            }

            auto mode = imgFilter->GetMode();
            if (mode == ImageFilter::Mode::Box) {
                int r = imgFilter->GetRadius();
                if (ImGui::SliderInt("半径", &r, 0, 8)) { imgFilter->SetRadius(r); }
                ImGui::TextDisabled("タップ数: %d x %d", 2*r+1, 2*r+1);
            } else if (mode == ImageFilter::Mode::Gaussian) {
                float sigma = imgFilter->GetSigma();
                if (ImGui::SliderFloat("シグマ", &sigma, 0.5f, 8.0f, "%.2f")) { imgFilter->SetSigma(sigma); }
                int r = (int)(sigma * 3.0f); if (r > 8) r = 8;
                ImGui::TextDisabled("半径: %d, タップ数: %d x %d", r, 2*r+1, 2*r+1);
            }
        }

        ImGui::Separator();

        // ビネット
        auto* vg = VignetteEffect::GetInstance();
        bool vigEnabled = vg->IsEnabled();
        if (ImGui::Checkbox("ビネット", &vigEnabled)) { vg->SetEnabled(vigEnabled); }
        if (vigEnabled) {
            float intensity = vg->GetIntensity();
            if (ImGui::SliderFloat("強度##vig",     &intensity, 0.0f, 2.0f)) { vg->SetIntensity(intensity); }
            float radius = vg->GetRadius();
            if (ImGui::SliderFloat("半径##vig",     &radius,    0.0f, 1.0f)) { vg->SetRadius(radius); }
            float softness = vg->GetSoftness();
            if (ImGui::SliderFloat("ソフトネス##vig", &softness, 0.0f, 1.0f)) { vg->SetSoftness(softness); }
        }

        ImGui::Separator();

        // HSV フィルター
        auto* hsv = HsvFilter::GetInstance();
        bool hsvEnabled = hsv->IsEnabled();
        if (ImGui::Checkbox("HSV フィルター", &hsvEnabled)) { hsv->SetEnabled(hsvEnabled); }
        if (hsvEnabled) {
            float hueShift = hsv->GetHueShift();
            if (ImGui::SliderFloat("色相シフト", &hueShift, -180.0f, 180.0f, "%.1f°")) { hsv->SetHueShift(hueShift); }
            if (ImGui::Button("色相リセット")) { hsv->SetHueShift(0.0f); }
            float sat = hsv->GetSaturation();
            if (ImGui::SliderFloat("彩度",       &sat,      0.0f, 2.0f, "%.2f")) { hsv->SetSaturation(sat); }
            float val = hsv->GetValue();
            if (ImGui::SliderFloat("明度",       &val,      0.0f, 2.0f, "%.2f")) { hsv->SetValue(val); }
            if (ImGui::Button("全パラメーターリセット")) {
                hsv->SetHueShift(0.0f); hsv->SetSaturation(1.0f); hsv->SetValue(1.0f);
            }
            ImGui::TextDisabled("※ イメージフィルター・グレースケールと同時使用不可");
        }
    }

    ImGui::End();

    // -------------------------------------------------------
    // アウトライン設定ウィンドウ
    // -------------------------------------------------------
    {
        auto* imgFilter = ImageFilter::GetInstance();
        auto  mode      = imgFilter->GetMode();
        bool  isOutline = (mode == ImageFilter::Mode::PrewittEdge ||
                           mode == ImageFilter::Mode::DepthOutline);

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Once);
        ImGui::Begin("アウトライン設定");

        if (!imgFilter->IsEnabled() || !isOutline) {
            ImGui::TextDisabled("イメージフィルターで\n「Prewitt エッジ」か\n「深度アウトライン」を選択してください");
        } else {
            ImGui::Text("モード: %s",
                mode == ImageFilter::Mode::PrewittEdge ? "Prewitt エッジ（輝度ベース）" : "深度アウトライン");
            ImGui::Separator();

            float threshold = imgFilter->GetOutlineThreshold();
            if (ImGui::SliderFloat("検出閾値", &threshold, 0.0f, 0.5f, "%.3f")) { imgFilter->SetOutlineThreshold(threshold); }
            ImGui::SameLine(); ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("エッジとみなす勾配の最小値。\n小さいほど細かいエッジも検出される。"); }

            float strength = imgFilter->GetOutlineStrength();
            if (ImGui::SliderFloat("エッジ強度", &strength, 0.1f, 30.0f, "%.1f")) { imgFilter->SetOutlineStrength(strength); }

            float color[4]; imgFilter->GetOutlineColor(color);
            if (ImGui::ColorEdit4("アウトライン色", color)) { imgFilter->SetOutlineColor(color[0], color[1], color[2], color[3]); }

            if (mode == ImageFilter::Mode::DepthOutline) {
                ImGui::Separator();
                ImGui::TextDisabled("--- 深度アウトライン専用 ---");
                float ds = imgFilter->GetDepthScale();
                if (ImGui::SliderFloat("深度スケール", &ds, 1.0f, 500.0f, "%.1f")) { imgFilter->SetDepthScale(ds); }
                ImGui::SameLine(); ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) { ImGui::SetTooltip("深度差を増幅する倍率。"); }
            }
        }
        ImGui::End();
    }

    // -------------------------------------------------------
    // ラジアルブラー設定ウィンドウ
    // -------------------------------------------------------
    {
        auto* imgFilter = ImageFilter::GetInstance();
        bool  isRadial  = (imgFilter->GetMode() == ImageFilter::Mode::RadialBlur);

        ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Once);
        ImGui::Begin("ラジアルブラー設定");

        if (!imgFilter->IsEnabled() || !isRadial) {
            ImGui::TextDisabled("イメージフィルターで\n「ラジアルブラー」を選択してください");
        } else {
            ImGui::TextDisabled("中心から放射状にブラーをかけます");
            ImGui::Separator();

            float cx = imgFilter->GetRadialCenterX();
            float cy = imgFilter->GetRadialCenterY();
            if (ImGui::SliderFloat("中心 X", &cx, 0.0f, 1.0f, "%.2f")) { imgFilter->SetRadialCenter(cx, cy); }
            if (ImGui::SliderFloat("中心 Y", &cy, 0.0f, 1.0f, "%.2f")) { imgFilter->SetRadialCenter(cx, cy); }
            if (ImGui::Button("中心にリセット")) { imgFilter->SetRadialCenter(0.5f, 0.5f); }
            ImGui::Separator();

            float strength = imgFilter->GetRadialStrength();
            if (ImGui::SliderFloat("強度", &strength, 0.0f, 1.0f, "%.3f")) { imgFilter->SetRadialStrength(strength); }

            int sampleCount = imgFilter->GetRadialSampleCount();
            if (ImGui::SliderInt("サンプル数", &sampleCount, 2, 32)) { imgFilter->SetRadialSampleCount(sampleCount); }
        }
        ImGui::End();
    }

    // -------------------------------------------------------
    // ディゾルブ設定ウィンドウ
    // -------------------------------------------------------
    {
        auto* imgFilter  = ImageFilter::GetInstance();
        bool  isDissolve = (imgFilter->GetMode() == ImageFilter::Mode::Dissolve);

        ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Once);
        ImGui::Begin("ディゾルブ設定");

        if (!imgFilter->IsEnabled() || !isDissolve) {
            ImGui::TextDisabled("イメージフィルターで\n「ディゾルブ」を選択してください");
        } else {
            ImGui::TextDisabled("ノイズマスクで画面を溶かします");
            ImGui::Separator();

            const char* maskItems[] = { "noise0.png", "noise1.png" };
            int maskIdx = imgFilter->GetDissolveMaskIndex();
            if (ImGui::Combo("マスク", &maskIdx, maskItems, IM_ARRAYSIZE(maskItems))) { imgFilter->SetDissolveMaskIndex(maskIdx); }
            ImGui::Separator();

            float threshold = imgFilter->GetDissolveThreshold();
            if (ImGui::SliderFloat("進行度", &threshold, 0.0f, 1.0f, "%.3f")) { imgFilter->SetDissolveThreshold(threshold); }

            float edgeWidth = imgFilter->GetDissolveEdgeWidth();
            if (ImGui::SliderFloat("エッジ幅", &edgeWidth, 0.0f, 0.3f, "%.3f")) { imgFilter->SetDissolveEdgeWidth(edgeWidth); }

            float edgeCol[4]; imgFilter->GetDissolveEdgeColor(edgeCol);
            if (ImGui::ColorEdit4("エッジ色", edgeCol)) { imgFilter->SetDissolveEdgeColor(edgeCol[0], edgeCol[1], edgeCol[2], edgeCol[3]); }
        }
        ImGui::End();
    }

    // -------------------------------------------------------
    // GPU ノイズ設定ウィンドウ
    // -------------------------------------------------------
    {
        auto* imgFilter = ImageFilter::GetInstance();
        bool  isNoise   = (imgFilter->GetMode() == ImageFilter::Mode::NoiseGen);

        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_Once);
        ImGui::Begin("GPU ノイズ設定");

        if (!imgFilter->IsEnabled() || !isNoise) {
            ImGui::TextDisabled("イメージフィルターで\n「GPU ノイズ」を選択してください");
        } else {
            bool anim = imgFilter->GetNoiseAnimate();
            if (ImGui::Checkbox("自動更新（毎フレーム seed を変化）", &anim)) { imgFilter->SetNoiseAnimate(anim); }
            if (anim) {
                float speed = imgFilter->GetNoiseSpeed();
                if (ImGui::SliderFloat("速度", &speed, 0.0005f, 0.05f, "%.4f")) { imgFilter->SetNoiseSpeed(speed); }
            }
            if (ImGui::Button("リセット")) { imgFilter->ResetNoiseTime(); }
            ImGui::Separator();

            float opacity = imgFilter->GetNoiseOpacity();
            if (ImGui::SliderFloat("不透明度", &opacity, 0.0f, 1.0f, "%.2f")) { imgFilter->SetNoiseOpacity(opacity); }
            ImGui::Separator();

            float sx = imgFilter->GetNoiseScaleX(), sy = imgFilter->GetNoiseScaleY();
            if (ImGui::DragFloat("スケール X", &sx, 0.05f, 0.1f, 50.0f, "%.2f")) { imgFilter->SetNoiseScale(sx, sy); }
            if (ImGui::DragFloat("スケール Y", &sy, 0.05f, 0.1f, 50.0f, "%.2f")) { imgFilter->SetNoiseScale(sx, sy); }
            if (ImGui::Button("均一にする")) { imgFilter->SetNoiseScale(sx, sx); }
            ImGui::Separator();

            float seed = imgFilter->GetNoiseSeed();
            if (ImGui::DragFloat("シード（手動）", &seed, 0.01f, -100.0f, 100.0f, "%.3f")) { imgFilter->SetNoiseSeed(seed); }
            ImGui::Separator();

            int octaves = imgFilter->GetNoiseOctaves();
            if (ImGui::SliderInt("オクターブ数",    &octaves, 1, 8)) { imgFilter->SetNoiseOctaves(octaves); }
            float persistence = imgFilter->GetNoisePersistence();
            if (ImGui::SliderFloat("パーシステンス", &persistence, 0.1f, 1.0f, "%.2f")) { imgFilter->SetNoisePersistence(persistence); }
            float lacunarity = imgFilter->GetNoiseLacunarity();
            if (ImGui::SliderFloat("ラクナリティ",   &lacunarity,  1.0f, 4.0f, "%.2f")) { imgFilter->SetNoiseLacunarity(lacunarity); }
            ImGui::Separator();

            const char* colorItems[] = { "グレースケール", "カラー" };
            int colorMode = imgFilter->GetNoiseColorMode();
            if (ImGui::Combo("カラーモード", &colorMode, colorItems, IM_ARRAYSIZE(colorItems))) { imgFilter->SetNoiseColorMode(colorMode); }
        }
        ImGui::End();
    }

    // -------------------------------------------------------
    // ガラス割れエフェクト設定ウィンドウ
    // -------------------------------------------------------
    {
        ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Once);
        ImGui::Begin("ガラス割れエフェクト");
        ImGui::TextDisabled("GlassShatterEffect");
        ImGui::Separator();

        // NOTE: GlassShatterEffect はシングルトンではないためポインタ取得ができない。
        //       シーン側から GetInstance 相当が提供されていない場合は
        //       「シーン側で外部から Start() などを呼ぶ」設計になっている。
        //       ここでは操作ガイドのみ表示する。
        ImGui::TextWrapped("GlassShatterEffect はシーン側から\n"
                           "Start() / Reset() を呼んでください。\n\n"
                           "パラメータは Initialize() 後に\n"
                           "SetImpactUV / SetCrackWidth /\n"
                           "SetShardSpeed / SetDuration で設定できます。");

        ImGui::End();
    }

    // -------------------------------------------------------
    // ライト設定ウィンドウ（平行光源 + ポイントライト）
    // -------------------------------------------------------
    {
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Once);
        ImGui::Begin("ライト設定");

        if (!s_obj3dCommon) {
            ImGui::TextColored(ImVec4(1,0.5f,0,1),
                "RegisterObject3dCommon() が未呼出しです。\n"
                "初期化時に RegisterObject3dCommon(obj3dCommon) を呼んでください。");
        } else {

            // ---- 平行光源（DirectionalLight） ----
            if (ImGui::CollapsingHeader("平行光源", ImGuiTreeNodeFlags_DefaultOpen)) {

                bool manualOverride = s_obj3dCommon->GetManualLightOverride();
                if (ImGui::Checkbox("手動オーバーライド（時刻自動更新を停止）", &manualOverride)) {
                    s_obj3dCommon->SetManualLightOverride(manualOverride);
                }
                ImGui::SameLine(); ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("チェックすると UpdateLight() がスキップされ\n"
                                      "以下のスライダーで固定設定できます。");
                }

                if (manualOverride) {
                    // 方向
                    Vector3 dir = s_obj3dCommon->GetLightDirectionRaw();
                    if (ImGui::DragFloat3("方向 (XYZ)", &dir.x, 0.01f, -1.0f, 1.0f, "%.3f")) {
                        s_obj3dCommon->SetLightDirection(dir);
                    }
                    ImGui::SameLine(); ImGui::TextDisabled("(?)");
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("ライトが向かう方向ベクトル（負の値が多いほど上から光が当たる）"); }

                    // 色
                    Vector4 col = s_obj3dCommon->GetLightColor();
                    if (ImGui::ColorEdit3("ライト色", &col.x)) { s_obj3dCommon->SetLightColor(col); }

                    // 強度
                    float intensity = s_obj3dCommon->GetLightIntensity();
                    if (ImGui::SliderFloat("強度##dlight", &intensity, 0.0f, 3.0f, "%.2f")) { s_obj3dCommon->SetLightIntensity(intensity); }

                    ImGui::Separator();

                    // アンビエント
                    Vector3 ambCol = s_obj3dCommon->GetAmbientColor();
                    if (ImGui::ColorEdit3("アンビエント色", &ambCol.x)) { s_obj3dCommon->SetAmbientColor(ambCol); }

                    float ambIntensity = s_obj3dCommon->GetAmbientIntensity();
                    if (ImGui::SliderFloat("アンビエント強度", &ambIntensity, 0.0f, 1.0f, "%.2f")) { s_obj3dCommon->SetAmbientIntensity(ambIntensity); }
                } else {
                    ImGui::TextDisabled("手動オーバーライドが OFF のため\n時刻ベースで自動更新されています。");
                }
            }

            ImGui::Separator();

            // ---- ポイントライト ----
            if (ImGui::CollapsingHeader("ポイントライト（デバッグ）", ImGuiTreeNodeFlags_DefaultOpen)) {

                ImGui::TextDisabled("最大 %d 個。毎フレーム GetDebugPointLights() をシーンで適用してください。",
                                    (int)Object3dCommon::kMaxPointLights);
                ImGui::Separator();

                // ライト追加ボタン
                if ((int)s_debugLights.size() < (int)Object3dCommon::kMaxPointLights) {
                    if (ImGui::Button("+ ライトを追加")) {
                        DebugPointLight newLight;
                        newLight.position = { 0.f, 2.f, 0.f };
                        s_debugLights.push_back(newLight);
                    }
                } else {
                    ImGui::TextDisabled("（上限 %d 個に達しています）", (int)Object3dCommon::kMaxPointLights);
                }

                // 各ライトの設定
                for (int i = 0; i < (int)s_debugLights.size(); ) {
                    auto& pl = s_debugLights[i];
                    ImGui::PushID(i);

                    char label[64];
                    snprintf(label, sizeof(label), "ポイントライト %d", i);
                    bool open = ImGui::CollapsingHeader(label);

                    // ヘッダー行に有効/削除ボタン
                    ImGui::SameLine();
                    ImGui::Checkbox("##enabled", &pl.enabled);
                    ImGui::SameLine();
                    bool removed = ImGui::SmallButton("削除");

                    if (open) {
                        ImGui::DragFloat3("位置",  &pl.position.x, 0.1f);
                        ImGui::SliderFloat("半径",   &pl.radius,    0.1f, 50.f,  "%.1f");
                        ImGui::ColorEdit3("色",     &pl.color.x);
                        ImGui::SliderFloat("強度",   &pl.intensity, 0.0f, 10.f,  "%.2f");
                    }

                    ImGui::PopID();

                    if (removed) {
                        s_debugLights.erase(s_debugLights.begin() + i);
                    } else {
                        ++i;
                    }
                }
            }
        }

        ImGui::End();
    }

#endif
}
