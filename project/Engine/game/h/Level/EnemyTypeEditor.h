/**
 * @file EnemyTypeEditor.h
 * @brief ステージエディタの「敵を作る」画面（動き方・見た目・武器・強さを選んで敵の種類を作り、ステージに置く）
 */
#pragma once
#include "EnemyTypeLibrary.h"
#include "ModelThumbnailBaker.h"
#include <functional>
#include <string>
#include <vector>

namespace engine::game {

/**
 * @brief 敵の種類を作って直す画面
 * @note 種類は Resources/Config/enemy_types.json、新しく作った見た目は enemy_params.json の visuals に保存する。
 *       モデルとテクスチャの候補は Resources フォルダから自動で集めるので、ファイルを置くだけで選べる
 */
class EnemyTypeEditor {
public:
    /** @brief 「ステージに置く」を押した時に呼ぶ処理（今編集中の種類を渡す） */
    using PlaceCallback = std::function<void(const EnemyTypeDef&)>;

    /** @brief 開いている間だけ画面を描く（× で閉じたら *open を false にする） */
    void Draw(bool* open, const PlaceCallback& place);

private:
    /** @brief 新しく作る見た目の入力中の値 */
    struct NewLook {
        char name[64] = {};
        std::string model;
        std::string texture;
        float scale = 0.3f;
        float yawDegrees = 0.0f;
    };

    void DrawTypeList();
    void DrawTypeFields(EnemyTypeDef& type, const PlaceCallback& place);
    void DrawLookFields(EnemyTypeDef& type);
    void DrawNewLookFields(EnemyTypeDef& type);
    /** @brief 見た目のサムネイルを出す（まだ作っていなければ予約して何も出さない） */
    void DrawThumbnail(const std::string& model, const std::string& texture);
    /** @brief Resources からモデルとテクスチャの候補を集め直す */
    void ScanAssets();
    /** @brief 見た目を enemy_params.json に書き足し、敵の調整値を読み直す */
    bool SaveNewLook(const NewLook& look);
    /** @brief 一覧の中で使われていない id を作る */
    std::string MakeUniqueId() const;

    engine::graphics::ModelThumbnailBaker thumbnails_;
    std::vector<std::string> models_; ///< Resources にあるモデル（.obj / .gltf / .glb）
    std::vector<std::string> textures_; ///< Resources にある画像（.png / .jpg）
    bool assetsScanned_ = false; ///< 素材一覧を作ったか（以後は一覧を更新ボタンでだけ作り直す）
    NewLook newLook_;
    int selected_ = 0;
    bool dirty_ = false;
    std::string status_;
};

} // namespace engine::game
