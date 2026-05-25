#pragma once
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "Camera.h"
#include "Cylinder.h"
#include "Ring.h"
#include "Skydome.h"
#include "SkinnedObject3d.h"
#include "Sprite.h"
#include "SpriteCommon.h"

// ゲームプレイシーンの ImGui デバッグエディタ。
// State パターンにより選択オブジェクトごとの Inspector 描画を切り替える。
class SceneEditor {
public:
    // ---- 選択状態 ----
    enum class Selection {
        None, Camera, UIElement, Ring, Cylinder, Skydome, Human, WhiteParticles
    };

    // ---- エディタが編集できるシーンデータへの参照 ----
    struct EditContext {
        Camera*          camera          = nullptr;
        Ring*            ring            = nullptr;
        Cylinder*        cylinder        = nullptr;
        Skydome*         skydome         = nullptr;
        SkinnedObject3d* human           = nullptr;
        SpriteCommon*    spriteCommon    = nullptr;

        Vector3* cameraTargetPos    = nullptr;
        Vector3* cameraTargetRot    = nullptr;
        int*     cameraSmoothFrames = nullptr;
        std::deque<Vector3>* cameraPosHistory = nullptr;
        std::deque<Vector3>* cameraRotHistory = nullptr;

        Vector3* ringPosition     = nullptr;
        Vector3* ringRotation     = nullptr;
        Vector4* ringColor        = nullptr;
        float*   ringScale        = nullptr;
        float*   ringInnerRadius  = nullptr;
        float*   ringOuterRadius  = nullptr;

        Vector3* cylinderPosition     = nullptr;
        Vector3* cylinderRotation     = nullptr;
        Vector4* cylinderColor        = nullptr;
        float*   cylinderScale        = nullptr;
        float*   cylinderTopRadius    = nullptr;
        float*   cylinderBottomRadius = nullptr;
        float*   cylinderHeight       = nullptr;
        float*   cylinderAlphaRef     = nullptr;

        Vector4* skyColor       = nullptr;
        float*   skyRotOffsetY  = nullptr;

        Vector3* humanPosition    = nullptr;
        Vector3* humanRotation    = nullptr;
        Vector3* humanScale       = nullptr;
        float*   humanAnimSpeed   = nullptr;
        bool*    showSkeleton     = nullptr;

        Vector3* whiteParticlePos   = nullptr;
        Vector4* whiteParticleColor = nullptr;
        float*   whiteParticleScale = nullptr;
        int*     whiteParticleCount = nullptr;

        int gameHour   = 0;
        int gameMinute = 0;
    };

    // ---- UI 要素 ----
    struct UIEntry {
        std::string name;
        std::unique_ptr<Sprite> sprite;
        std::string texPath;
    };

    // ---- ImGui パネルレイアウト定数 ----
    static constexpr float kHierarchyX = 0.0f,    kHierarchyY = 0.0f;
    static constexpr float kHierarchyW = 220.0f,  kHierarchyH = 400.0f;
    static constexpr float kInspectorX = 1060.0f, kInspectorY = 0.0f;
    static constexpr float kInspectorW = 220.0f,  kInspectorH = 500.0f;
    static constexpr float kSceneCtrlX = 0.0f,    kSceneCtrlY = 400.0f;
    static constexpr float kSceneCtrlW = 220.0f,  kSceneCtrlH = 320.0f;
    static constexpr float kCamCtrlX   = 400.0f,  kCamCtrlY   = 0.0f;
    static constexpr float kCamCtrlW   = 300.0f,  kCamCtrlH   = 155.0f;

    // ---- インターフェイス ----
    void Update(const EditContext& ctx);
    void LoadAll(const EditContext& ctx);

    std::vector<UIEntry>&       GetUIElements()       { return uiElements_; }
    const std::vector<UIEntry>& GetUIElements() const { return uiElements_; }
    Selection                   GetSelection()  const { return selection_; }

private:
    // ---- State パターン ---- //
    // Inspector ペインの描画をオブジェクト種別ごとに委譲する。
    class IEditorState {
    public:
        virtual ~IEditorState() = default;
        virtual void RenderInspector(const EditContext& ctx, SceneEditor& editor) = 0;
    };

    class NoneState       : public IEditorState { public: void RenderInspector(const EditContext&, SceneEditor&) override; };
    class CameraState     : public IEditorState { public: void RenderInspector(const EditContext& ctx, SceneEditor& editor) override; };
    class RingState       : public IEditorState { public: void RenderInspector(const EditContext& ctx, SceneEditor& editor) override; };
    class CylinderState   : public IEditorState { public: void RenderInspector(const EditContext& ctx, SceneEditor& editor) override; };
    class SkydomeState    : public IEditorState { public: void RenderInspector(const EditContext& ctx, SceneEditor& editor) override; };
    class HumanState      : public IEditorState { public: void RenderInspector(const EditContext& ctx, SceneEditor& editor) override; };
    class ParticlesState  : public IEditorState { public: void RenderInspector(const EditContext& ctx, SceneEditor& editor) override; };
    class UIElementState  : public IEditorState { public: void RenderInspector(const EditContext& ctx, SceneEditor& editor) override; };

    void ChangeState(Selection sel);

    // ---- サブペイン描画 ----
    void RenderHierarchy(const EditContext& ctx);
    void RenderInspector(const EditContext& ctx);
    void RenderSceneControls(const EditContext& ctx);
    void RenderCameraControl(const EditContext& ctx);

    // ---- JSON 永続化 ----
    void SaveCameraParams(const EditContext& ctx);
    void LoadCameraParams(const EditContext& ctx);
    void SaveUILayout();
    void LoadUILayout(const EditContext& ctx);

    // ---- 状態 ----
    Selection                    selection_      = Selection::None;
    int                          selectionIndex_ = -1;
    std::unique_ptr<IEditorState> currentState_;

    std::vector<UIEntry> uiElements_;
    float savedTimer_ = 0.0f;
};
