/** @file SceneEditorPersistence.cpp
 * @brief カメラ設定とUIレイアウトの保存と復元
 */
#include "SceneEditor.h"
#include "JsonHelper.h"
#include <utility>
using namespace engine::graphics;
namespace engine::game {
/** @brief カメラの目標位置・角度・スムージングフレーム数をJSONへ書き出す */
void SceneEditor::SaveCameraParams(const EditContext& ctx) {
    const Vector3& pos = *ctx.cameraTargetPos;
    const Vector3& rot = *ctx.cameraTargetRot;
    nlohmann::json j;
    j["camera_pos"] = { pos.x, pos.y, pos.z };
    j["camera_rot"] = { rot.x, rot.y, rot.z };
    j["camera_smooth_frames"] = *ctx.cameraSmoothFrames;
    JsonHelper::Save("Resources/editor_camera.json", j);
}

// カメラの位置・角度・スムージングフレーム数を JSON ファイルから読み込む
void SceneEditor::LoadCameraParams(const EditContext& ctx) {
    auto j = JsonHelper::Load("Resources/editor_camera.json");
    if (j.empty()) {
        return;
    }

    Vector3& pos = *ctx.cameraTargetPos;
    Vector3& rot = *ctx.cameraTargetRot;

    if (j.contains("camera_pos") && j["camera_pos"].is_array()) {
        pos = { j["camera_pos"][0], j["camera_pos"][1], j["camera_pos"][2] };
    }
    if (j.contains("camera_rot") && j["camera_rot"].is_array()) {
        rot = { j["camera_rot"][0], j["camera_rot"][1], j["camera_rot"][2] };
    }
    *ctx.cameraSmoothFrames = j.value("camera_smooth_frames", *ctx.cameraSmoothFrames);

    ctx.camera->SetTranslate(pos);
    ctx.camera->SetRotate(rot);
    ctx.cameraPosHistory->clear();
    ctx.cameraRotHistory->clear();
}

// UI スプライトのレイアウト（位置・サイズ・色・テクスチャパスなど）を JSON に書き出す
void SceneEditor::SaveUILayout() {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : uiElements_) {
        Sprite* sp = e.sprite.get();
        Vector2 pos = sp->GetPosition();
        Vector2 sz = sp->GetSize();
        Vector4 col = sp->GetColor();
        float rot = sp->GetRotation();
        arr.push_back({ { "name", e.name },
            { "tex", e.texPath },
            { "pos", { pos.x, pos.y } },
            { "size", { sz.x, sz.y } },
            { "rot", rot },
            { "color", { col.x, col.y, col.z, col.w } } });
    }
    JsonHelper::Save("Resources/Config/editor_ui.json", arr);
}

// 起動時に1度だけ呼ばれ、カメラパラメータと UI レイアウトを一括で読み込む
void SceneEditor::LoadAll(const EditContext& ctx) {
    LoadCameraParams(ctx);
    LoadUILayout(ctx);
}

// UI スプライトのレイアウトを JSON ファイルから復元する
void SceneEditor::LoadUILayout(const EditContext& ctx) {
    auto j = JsonHelper::Load("Resources/Config/editor_ui.json");
    if (!j.is_array()) {
        return;
    }

    uiElements_.clear();
    ChangeState(Selection::kNone);

    for (const auto& item : j) {
        UIEntry entry;
        entry.name = item.value("name", std::string("UI Element"));
        entry.texPath = item.value("tex", std::string(""));

        entry.sprite = std::make_unique<Sprite>();
        entry.sprite->Initialize(ctx.spriteCommon, entry.texPath);

        if (item.contains("pos") && item["pos"].is_array()) {
            entry.sprite->SetPosition({ item["pos"][0], item["pos"][1] });
        }
        if (item.contains("size") && item["size"].is_array()) {
            entry.sprite->SetSize({ item["size"][0], item["size"][1] });
        }
        entry.sprite->SetRotation(item.value("rot", 0.0f));
        if (item.contains("color") && item["color"].is_array()) {
            const auto& c = item["color"];
            entry.sprite->SetColor({ c[0], c[1], c[2], c[3] });
        }

        uiElements_.push_back(std::move(entry));
    }
}

}
