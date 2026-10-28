/**
 * @file ImGuiControlPanel.cpp
 * @brief 描画設定を調整する ImGui のパネル（ImGuiControlPanel）の実装
 */
#include "ImGuiControlPanel.h"
#include "BloomEffect.h"
#include "GlassShatterEffect.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiManager.h"
#include "ImageFilter.h"
#include "LightManager.h"
#include "LightingMode.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "TextureManager.h"
#include "VignetteEffect.h"
#include <algorithm>
#include <initializer_list>
#include <vector>


namespace engine::graphics {

// 静的メンバの実体

Object3dCommon* ImGuiControlPanel::sObj3dCommon = nullptr;
std::vector<EditorPointLight> ImGuiControlPanel::sEditorLights;
std::function<void()> ImGuiControlPanel::sGlassShatterTrigger;

// 公開 API

// ══════════════════════════════════════════════════════
// 登録とメインパネル
// ══════════════════════════════════════════════════════

/** @brief ライト設定の対象にする Object3dCommon を登録する（初期化時に一度だけ呼ぶ） */
void ImGuiControlPanel::RegisterObject3dCommon(Object3dCommon* common) {
    sObj3dCommon = common;
}

/** @brief ガラス割れエフェクトのテスト再生コールバックを登録する（シーン切替時は nullptr で解除する） */
void ImGuiControlPanel::RegisterGlassShatterTrigger(std::function<void()> trigger) {
    sGlassShatterTrigger = std::move(trigger);
}


/** @brief デバッグコントロールウィンドウを描画する（ImGuiManager::Begin/Endの間に呼ぶ） */
void ImGuiControlPanel::ShowControls() {
#ifdef USE_IMGUI
    ImGui::Begin("コントロール");
    ShowMeshSettings();
    ShowMaterialSettings();
    ShowLightingSettings();
    ShowPostProcessSettings();
    ImGui::End();

    ShowEffectAndLightSettings();
#endif
}

#ifdef USE_IMGUI

namespace {
constexpr float kEffectWindowWidth = 360.0f;
constexpr float kNoiseScaleDragSpeed = 0.05f;
constexpr float kNoiseScaleMin = 0.1f;
constexpr float kNoiseScaleMax = 50.0f;
constexpr float kNoiseSeedDragSpeed = 0.01f;
constexpr float kNoiseSeedRange = 100.0f;
constexpr float kRadialCenterDefault = 0.5f; // 画面の中心（UV）
constexpr float kLightDirectionDragSpeed = 0.01f;
constexpr int kMaxKernelRadius = 8; // ぼかしカーネルの半径の上限（シェーダー側の配列長）
constexpr float kGaussianRadiusPerSigma = 3.0f; // ガウスぼかしはシグマの3倍まで見る
constexpr float kHsvNeutral = 1.0f; // 彩度・明度の無補正の値

/** @brief float を取得→スライダー→設定する1行 */
template <class Target>
struct FloatSlider {
    const char* label;
    float (*get)(Target*);
    void (*set)(Target*, float);
    float min;
    float max;
    const char* format = "%.3f";
    const char* help = nullptr; ///< 横に (?) を出して説明するなら文
};

/** @brief int を取得→スライダー→設定する1行 */
template <class Target>
struct IntSlider {
    const char* label;
    int (*get)(Target*);
    void (*set)(Target*, int);
    int min;
    int max;
};

/** @brief 直前の項目の横に (?) を出し、乗せたら説明を出す */
void HelpMarker(const char* help) {
    if (help == nullptr) {
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", help);
    }
}

/** @brief 表の各行の float スライダーを描き、変更を対象へ反映する */
template <class Target, size_t N>
void DrawSliders(Target* target, const FloatSlider<Target> (&rows)[N]) {
    for (const FloatSlider<Target>& row : rows) {
        float value = row.get(target);
        if (ImGui::SliderFloat(row.label, &value, row.min, row.max, row.format)) {
            row.set(target, value);
        }
        HelpMarker(row.help);
    }
}

/** @brief 表の各行の int スライダーを描き、変更を対象へ反映する */
template <class Target, size_t N>
void DrawSliders(Target* target, const IntSlider<Target> (&rows)[N]) {
    for (const IntSlider<Target>& row : rows) {
        int value = row.get(target);
        if (ImGui::SliderInt(row.label, &value, row.min, row.max)) {
            row.set(target, value);
        }
    }
}

/** @brief 有効/無効のチェックボックスを描き、有効なら true を返す */
template <class Target>
bool DrawToggle(Target* target, const char* label) {
    bool enabled = target->IsEnabled();
    if (ImGui::Checkbox(label, &enabled)) {
        target->SetEnabled(enabled);
    }
    return enabled;
}

/**
 * @brief イメージフィルターの特定モード用の折りたたみセクションを始める
 * @return 開いていて、かつフィルターがそのモードの時だけtrue（違えば案内文を出す）
 */
bool BeginFilterSection(ImageFilter* filter, const char* header, const char* hint,
    const std::initializer_list<ImageFilter::Mode>& modes) {
    if (!ImGui::CollapsingHeader(header)) {
        return false;
    }
    const bool modeMatches = std::find(modes.begin(), modes.end(), filter->GetMode()) != modes.end();
    if (!filter->IsEnabled() || !modeMatches) {
        ImGui::TextDisabled("%s", hint);
        return false;
    }
    return true;
}

/** @brief ボックスぼかしの半径のスライダーを描く */
void DrawBoxKernel(ImageFilter* filter) {
    int radius = filter->GetRadius();
    if (ImGui::SliderInt("半径", &radius, 0, kMaxKernelRadius)) {
        filter->SetRadius(radius);
    }
    ImGui::TextDisabled("タップ数: %d x %d", 2 * radius + 1, 2 * radius + 1);
}

/** @brief ガウスぼかしの設定のスライダーを描く */
void DrawGaussianKernel(ImageFilter* filter) {
    static constexpr FloatSlider<ImageFilter> kSigma[] = {
        {
            "シグマ", [](ImageFilter* f) {
                return f->GetSigma();
            }, [](ImageFilter* f, float v) {
                f->SetSigma(v);
            }, 0.5f, 8.0f, "%.2f"
        },
    };
    DrawSliders(filter, kSigma);
    const int radius = (std::min)(static_cast<int>(filter->GetSigma() * kGaussianRadiusPerSigma), kMaxKernelRadius);
    ImGui::TextDisabled("半径: %d, タップ数: %d x %d", radius, 2 * radius + 1, 2 * radius + 1);
}

/** @brief ぼかし系モードごとのカーネル設定（このモード以外ではカーネル設定を出さない） */
struct KernelControl {
    ImageFilter::Mode mode;
    void (*draw)(ImageFilter*);
};
constexpr KernelControl kKernelControls[] = {
    { ImageFilter::Mode::kBox, DrawBoxKernel },
    { ImageFilter::Mode::kGaussian, DrawGaussianKernel },
};

// ImageFilter::Mode の並び順の表示名
constexpr const char* kFilterModeNames[] = { "Box", "Linear (Gaussian)", "Prewitt エッジ", "深度アウトライン", "ラジアルブラー", "ディゾルブ", "GPU ノイズ" };
} // namespace

// ══════════════════════════════════════════════════════
// 描画設定
// ══════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════
// ポストエフェクト設定
// ══════════════════════════════════════════════════════

/** @brief ポストプロセス（グレースケール等）の調整スライダーを表示する */
void ImGuiControlPanel::ShowPostProcessSettings() {
    if (!ImGui::CollapsingHeader("ポストプロセス", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    // グレースケール
    static constexpr FloatSlider<GrayscaleEffect> kGrayscale[] = {
        {
            "適用量##gs", [](GrayscaleEffect* e) {
                return e->GetAmount();
            }, [](GrayscaleEffect* e, float v) {
                e->SetAmount(v);
            }, 0.0f, 1.0f
        },
    };
    auto* grayscale = GrayscaleEffect::GetInstance();
    if (DrawToggle(grayscale, "グレースケール")) {
        DrawSliders(grayscale, kGrayscale);
    }
    ImGui::Separator();

    // Bloom
    static constexpr FloatSlider<BloomEffect> kBloom[] = {
        { "しきい値##bloom", [](BloomEffect* e) {
            return e->GetThreshold();
        }, [](BloomEffect* e, float v) {
            e->SetThreshold(v);
        }, 0.0f, 1.0f, "%.2f",
            "この輝度以上のピクセルが光って見えます。\n低いほど広い範囲が光る。" },
        {
            "強度##bloom", [](BloomEffect* e) {
                return e->GetIntensity();
            }, [](BloomEffect* e, float v) {
                e->SetIntensity(v);
            }, 0.0f, 4.0f, "%.2f"
        },
    };
    auto* bloom = BloomEffect::GetInstance();
    if (DrawToggle(bloom, "Bloom（発光）")) {
        DrawSliders(bloom, kBloom);
    }
    ImGui::Separator();

    // イメージフィルター（ぼかし系はモードごとのカーネル設定を表から引く）
    auto* imgFilter = ImageFilter::GetInstance();
    if (DrawToggle(imgFilter, "イメージフィルター")) {
        int currentMode = static_cast<int>(imgFilter->GetMode());
        if (ImGui::Combo("フィルターモード", &currentMode, kFilterModeNames, IM_ARRAYSIZE(kFilterModeNames))) {
            imgFilter->SetMode(static_cast<ImageFilter::Mode>(currentMode));
        }
        for (const KernelControl& control : kKernelControls) {
            if (control.mode == imgFilter->GetMode()) {
                control.draw(imgFilter);
            }
        }
    }
    ImGui::Separator();

    // ビネット
    static constexpr FloatSlider<VignetteEffect> kVignette[] = {
        {
            "強度##vig", [](VignetteEffect* e) {
                return e->GetIntensity();
            }, [](VignetteEffect* e, float v) {
                e->SetIntensity(v);
            }, 0.0f, 2.0f
        },
        {
            "半径##vig", [](VignetteEffect* e) {
                return e->GetRadius();
            }, [](VignetteEffect* e, float v) {
                e->SetRadius(v);
            }, 0.0f, 1.0f
        },
        {
            "ソフトネス##vig", [](VignetteEffect* e) {
                return e->GetSoftness();
            }, [](VignetteEffect* e, float v) {
                e->SetSoftness(v);
            }, 0.0f, 1.0f
        },
    };
    auto* vignette = VignetteEffect::GetInstance();
    if (DrawToggle(vignette, "ビネット")) {
        DrawSliders(vignette, kVignette);
    }
    ImGui::Separator();

    // HSV フィルター
    static constexpr FloatSlider<HsvFilter> kHue[] = {
        {
            "色相シフト", [](HsvFilter* e) {
                return e->GetHueShift();
            }, [](HsvFilter* e, float v) {
                e->SetHueShift(v);
            }, -180.0f, 180.0f, "%.1f°"
        },
    };
    static constexpr FloatSlider<HsvFilter> kSaturationValue[] = {
        {
            "彩度", [](HsvFilter* e) {
                return e->GetSaturation();
            }, [](HsvFilter* e, float v) {
                e->SetSaturation(v);
            }, 0.0f, 2.0f, "%.2f"
        },
        {
            "明度", [](HsvFilter* e) {
                return e->GetValue();
            }, [](HsvFilter* e, float v) {
                e->SetValue(v);
            }, 0.0f, 2.0f, "%.2f"
        },
    };
    auto* hsv = HsvFilter::GetInstance();
    if (DrawToggle(hsv, "HSV フィルター")) {
        DrawSliders(hsv, kHue);
        if (ImGui::Button("色相リセット")) {
            hsv->SetHueShift(0.0f);
        }
        DrawSliders(hsv, kSaturationValue);
        if (ImGui::Button("全パラメーターリセット")) {
            hsv->SetHueShift(0.0f);
            hsv->SetSaturation(kHsvNeutral);
            hsv->SetValue(kHsvNeutral);
        }
        ImGui::TextDisabled("※ イメージフィルター・グレースケールと同時使用不可");
    }
}
// ══════════════════════════════════════════════════════
// エフェクト詳細設定
// ══════════════════════════════════════════════════════
void ImGuiControlPanel::ShowEffectAndLightSettings() {
    // アウトライン / ラジアルブラー / ディゾルブ / GPUノイズ / ガラス割れ / ライト
    // を1ウィンドウにまとめ、セクションごとに折りたたみヘッダーで表示する
    auto* imgFilter = ImageFilter::GetInstance();
    ImGui::SetNextWindowSize(ImVec2(kEffectWindowWidth, 0), ImGuiCond_Once);
    ImGui::Begin("エフェクト・ライト設定");
    ShowOutlineSection(imgFilter);
    ShowRadialBlurSection(imgFilter);
    ShowDissolveSection(imgFilter);
    ShowNoiseSection(imgFilter);
    ShowGlassShatterSection();
    ShowLightSection();
    ImGui::End();
}
/** @brief 輪郭抽出の設定欄を出す */
void ImGuiControlPanel::ShowOutlineSection(ImageFilter* imgFilter) {
    if (!BeginFilterSection(imgFilter, "アウトライン", "イメージフィルターで\n「Prewitt エッジ」か\n「深度アウトライン」を選択してください",
            { ImageFilter::Mode::kPrewittEdge, ImageFilter::Mode::kDepthOutline })) {
        return;
    }
    const bool depth = imgFilter->GetMode() == ImageFilter::Mode::kDepthOutline;
    ImGui::Text("モード: %s", depth ? "深度アウトライン" : "Prewitt エッジ（輝度ベース）");
    ImGui::Separator();
    static constexpr FloatSlider<ImageFilter> kOutline[] = {
        { "検出閾値", [](ImageFilter* f) {
            return f->GetOutlineThreshold();
        }, [](ImageFilter* f, float v) {
            f->SetOutlineThreshold(v);
        }, 0.0f, 0.5f, "%.3f",
            "エッジとみなす勾配の最小値。\n小さいほど細かいエッジも検出される。" },
        {
            "エッジ強度", [](ImageFilter* f) {
                return f->GetOutlineStrength();
            }, [](ImageFilter* f, float v) {
                f->SetOutlineStrength(v);
            }, 0.1f, 30.0f, "%.1f"
        },
    };
    DrawSliders(imgFilter, kOutline);
    float color[4];
    imgFilter->GetOutlineColor(color);
    if (ImGui::ColorEdit4("アウトライン色", color)) {
        imgFilter->SetOutlineColor(color[0], color[1], color[2], color[3]);
    }
    if (depth) {
        ImGui::Separator();
        ImGui::TextDisabled("--- 深度アウトライン専用 ---");
        static constexpr FloatSlider<ImageFilter> kDepth[] = {
            { "深度スケール", [](ImageFilter* f) {
                return f->GetDepthScale();
            }, [](ImageFilter* f, float v) {
                f->SetDepthScale(v);
            }, 1.0f, 500.0f, "%.1f",
                "深度差を増幅する倍率。" },
        };
        DrawSliders(imgFilter, kDepth);
    }
}
/** @brief 放射ブラーの設定欄を出す */
void ImGuiControlPanel::ShowRadialBlurSection(ImageFilter* imgFilter) {
    if (!BeginFilterSection(imgFilter, "ラジアルブラー", "イメージフィルターで\n「ラジアルブラー」を選択してください",
            { ImageFilter::Mode::kRadialBlur })) {
        return;
    }
    ImGui::TextDisabled("中心から放射状にブラーをかけます");
    ImGui::Separator();
    static constexpr FloatSlider<ImageFilter> kCenter[] = {
        {
            "中心 X", [](ImageFilter* f) {
                return f->GetRadialCenterX();
            }, [](ImageFilter* f, float v) {
                f->SetRadialCenter(v, f->GetRadialCenterY());
            }, 0.0f, 1.0f, "%.2f"
        },
        {
            "中心 Y", [](ImageFilter* f) {
                return f->GetRadialCenterY();
            }, [](ImageFilter* f, float v) {
                f->SetRadialCenter(f->GetRadialCenterX(), v);
            }, 0.0f, 1.0f, "%.2f"
        },
    };
    DrawSliders(imgFilter, kCenter);
    if (ImGui::Button("中心にリセット")) {
        imgFilter->SetRadialCenter(kRadialCenterDefault, kRadialCenterDefault);
    }
    ImGui::Separator();
    static constexpr FloatSlider<ImageFilter> kStrength[] = {
        {
            "強度", [](ImageFilter* f) {
                return f->GetRadialStrength();
            }, [](ImageFilter* f, float v) {
                f->SetRadialStrength(v);
            }, 0.0f, 1.0f, "%.3f"
        },
    };
    static constexpr IntSlider<ImageFilter> kSamples[] = {
        {
            "サンプル数", [](ImageFilter* f) {
                return f->GetRadialSampleCount();
            }, [](ImageFilter* f, int v) {
                f->SetRadialSampleCount(v);
            }, 2, 32
        },
    };
    DrawSliders(imgFilter, kStrength);
    DrawSliders(imgFilter, kSamples);
}
/** @brief ディゾルブの設定欄を出す */
void ImGuiControlPanel::ShowDissolveSection(ImageFilter* imgFilter) {
    if (!BeginFilterSection(imgFilter, "ディゾルブ", "イメージフィルターで\n「ディゾルブ」を選択してください",
            { ImageFilter::Mode::kDissolve })) {
        return;
    }
    ImGui::TextDisabled("ノイズマスクで画面を溶かします");
    ImGui::Separator();
    const char* maskItems[] = { "noise0.png", "noise1.png" };
    int maskIdx = imgFilter->GetDissolveMaskIndex();
    if (ImGui::Combo("マスク", &maskIdx, maskItems, IM_ARRAYSIZE(maskItems))) {
        imgFilter->SetDissolveMaskIndex(maskIdx);
    }
    ImGui::Separator();
    static constexpr FloatSlider<ImageFilter> kDissolve[] = {
        {
            "進行度", [](ImageFilter* f) {
                return f->GetDissolveThreshold();
            }, [](ImageFilter* f, float v) {
                f->SetDissolveThreshold(v);
            }, 0.0f, 1.0f, "%.3f"
        },
        {
            "エッジ幅", [](ImageFilter* f) {
                return f->GetDissolveEdgeWidth();
            }, [](ImageFilter* f, float v) {
                f->SetDissolveEdgeWidth(v);
            }, 0.0f, 0.3f, "%.3f"
        },
    };
    DrawSliders(imgFilter, kDissolve);
    float edgeCol[4];
    imgFilter->GetDissolveEdgeColor(edgeCol);
    if (ImGui::ColorEdit4("エッジ色", edgeCol)) {
        imgFilter->SetDissolveEdgeColor(edgeCol[0], edgeCol[1], edgeCol[2], edgeCol[3]);
    }
}
/** @brief ノイズの設定欄を出す */
void ImGuiControlPanel::ShowNoiseSection(ImageFilter* imgFilter) {
    if (!BeginFilterSection(imgFilter, "GPU ノイズ", "イメージフィルターで\n「GPU ノイズ」を選択してください",
            { ImageFilter::Mode::kNoiseGen })) {
        return;
    }
    bool anim = imgFilter->GetNoiseAnimate();
    if (ImGui::Checkbox("自動更新（毎フレーム seed を変化）", &anim)) {
        imgFilter->SetNoiseAnimate(anim);
    }
    if (anim) {
        static constexpr FloatSlider<ImageFilter> kSpeed[] = {
            {
                "速度", [](ImageFilter* f) {
                    return f->GetNoiseSpeed();
                }, [](ImageFilter* f, float v) {
                    f->SetNoiseSpeed(v);
                }, 0.0005f, 0.05f, "%.4f"
            },
        };
        DrawSliders(imgFilter, kSpeed);
    }
    if (ImGui::Button("リセット")) {
        imgFilter->ResetNoiseTime();
    }
    ImGui::Separator();
    static constexpr FloatSlider<ImageFilter> kOpacity[] = {
        {
            "不透明度", [](ImageFilter* f) {
                return f->GetNoiseOpacity();
            }, [](ImageFilter* f, float v) {
                f->SetNoiseOpacity(v);
            }, 0.0f, 1.0f, "%.2f"
        },
    };
    DrawSliders(imgFilter, kOpacity);
    ImGui::Separator();
    float sx = imgFilter->GetNoiseScaleX();
    float sy = imgFilter->GetNoiseScaleY();
    if (ImGui::DragFloat("スケール X", &sx, kNoiseScaleDragSpeed, kNoiseScaleMin, kNoiseScaleMax, "%.2f")) {
        imgFilter->SetNoiseScale(sx, sy);
    }
    if (ImGui::DragFloat("スケール Y", &sy, kNoiseScaleDragSpeed, kNoiseScaleMin, kNoiseScaleMax, "%.2f")) {
        imgFilter->SetNoiseScale(sx, sy);
    }
    if (ImGui::Button("均一にする")) {
        imgFilter->SetNoiseScale(sx, sx);
    }
    ImGui::Separator();
    float seed = imgFilter->GetNoiseSeed();
    if (ImGui::DragFloat("シード（手動）", &seed, kNoiseSeedDragSpeed, -kNoiseSeedRange, kNoiseSeedRange, "%.3f")) {
        imgFilter->SetNoiseSeed(seed);
    }
    ImGui::Separator();
    static constexpr IntSlider<ImageFilter> kOctaves[] = {
        {
            "オクターブ数", [](ImageFilter* f) {
                return f->GetNoiseOctaves();
            }, [](ImageFilter* f, int v) {
                f->SetNoiseOctaves(v);
            }, 1, 8
        },
    };
    static constexpr FloatSlider<ImageFilter> kFractal[] = {
        {
            "パーシステンス", [](ImageFilter* f) {
                return f->GetNoisePersistence();
            }, [](ImageFilter* f, float v) {
                f->SetNoisePersistence(v);
            }, 0.1f, 1.0f, "%.2f"
        },
        {
            "ラクナリティ", [](ImageFilter* f) {
                return f->GetNoiseLacunarity();
            }, [](ImageFilter* f, float v) {
                f->SetNoiseLacunarity(v);
            }, 1.0f, 4.0f, "%.2f"
        },
    };
    DrawSliders(imgFilter, kOctaves);
    DrawSliders(imgFilter, kFractal);
    ImGui::Separator();
    const char* colorItems[] = { "グレースケール", "カラー" };
    int colorMode = imgFilter->GetNoiseColorMode();
    if (ImGui::Combo("カラーモード", &colorMode, colorItems, IM_ARRAYSIZE(colorItems))) {
        imgFilter->SetNoiseColorMode(colorMode);
    }
}
// ══════════════════════════════════════════════════════
// ライト設定
// ══════════════════════════════════════════════════════
/** @brief 平行光源の設定欄を出す */
void ImGuiControlPanel::ShowDirectionalLightSection() {
    // 平行光源（DirectionalLight）
    if (!ImGui::TreeNodeEx("平行光源", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    bool manualOverride = sObj3dCommon->GetManualLightOverride();
    if (ImGui::Checkbox("手動オーバーライド（時刻自動更新を停止）", &manualOverride)) {
        sObj3dCommon->SetManualLightOverride(manualOverride);
    }
    HelpMarker("チェックすると UpdateLight() がスキップされ\n以下のスライダーで固定設定できます。");
    if (manualOverride) {
        // 方向
        Vector3 dir = sObj3dCommon->GetLightDirectionRaw();
        if (ImGui::DragFloat3("方向 (XYZ)", &dir.x, kLightDirectionDragSpeed, -1.0f, 1.0f, "%.3f")) {
            sObj3dCommon->SetLightDirection(dir);
        }
        HelpMarker("ライトが向かう方向ベクトル（負の値が多いほど上から光が当たる）");
        // 色
        Vector4 col = sObj3dCommon->GetLightColor();
        if (ImGui::ColorEdit3("ライト色", &col.x)) {
            sObj3dCommon->SetLightColor(col);
        }
        static constexpr FloatSlider<Object3dCommon> kLightIntensity[] = {
            {
                "強度##dlight", [](Object3dCommon* c) {
                    return c->GetLightIntensity();
                }, [](Object3dCommon* c, float v) {
                    c->SetLightIntensity(v);
                }, 0.0f, 3.0f, "%.2f"
            },
        };
        DrawSliders(sObj3dCommon, kLightIntensity);
        ImGui::Separator();
        // アンビエント
        Vector3 ambCol = sObj3dCommon->GetAmbientColor();
        if (ImGui::ColorEdit3("アンビエント色", &ambCol.x)) {
            sObj3dCommon->SetAmbientColor(ambCol);
        }
        static constexpr FloatSlider<Object3dCommon> kAmbient[] = {
            {
                "アンビエント強度", [](Object3dCommon* c) {
                    return c->GetAmbientIntensity();
                }, [](Object3dCommon* c, float v) {
                    c->SetAmbientIntensity(v);
                }, 0.0f, 1.0f, "%.2f"
            },
        };
        DrawSliders(sObj3dCommon, kAmbient);
    } else {
        ImGui::TextDisabled("手動オーバーライドが OFF のため\n時刻ベースで自動更新されています。");
    }
    ImGui::TreePop();
}
#endif
} // namespace engine::graphics
