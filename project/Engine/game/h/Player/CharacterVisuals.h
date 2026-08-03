/**
 * @file CharacterVisuals.h
 * @brief Player.cpp が使うリグ・手持ち武器の見た目定義
 * @note データそのものはここが唯一の定義元。個別シーン側でテーブルを複製しない。
 */
#pragma once
#include "Object3d.h"
#include "Skeleton.h"
#include "Vector3.h"
#include "Weapon.h"
#include <MakeAffine.h>

namespace engine::game {
using engine::graphics::Object3d;
using engine::graphics::Skeleton;

// 見た目リグ（通常/覚醒フォーム）のアセット定義
// モデルごとにアニメーション名・手のボーン名が異なるためリグ単位でまとめて持つ
/** @brief 1フォーム（通常/覚醒）ぶんのモデル・スケール・アタッチボーン・アニメーション名の静的定義 */
struct RigVisualDef {
    const char* dir; ///< glTF のディレクトリ
    const char* file; ///< glTF のファイル名
    const char* texture;
    const char* staticModelPath; ///< 残像・分身演出用のボーンなし OBJ
    float scale;
    float offsetY; ///< モデル原点（足元）を AABB 中心の pos_ に合わせる下げ幅
    const char* meleeBone; ///< 近接武器のアタッチ先ボーン
    const char* gunBone; ///< 銃のアタッチ先ボーン
    // アニメーション名（素材に無いバリエーションは近いモーションで代用する）
    const char* idle;
    const char* run;
    const char* jump;
    const char* runningJump;
    const char* swim;
    const char* idleHold;
    const char* runHold;
    const char* slash;
    const char* punch;
};

// 通常フォーム: マテリアル単色を焼き込んだパレットテクスチャ（UVは変換時にブロック中心へ書き換え済み）
// モデル身長は約2.93。スケールは当たり判定(1x1x1 AABB)の高さに合わせる
constexpr RigVisualDef kNormalRigVisual = {
    "Resources/AlienAnimated/glTF",
    "Alien.gltf",
    "Resources/AlienAnimated/glTF/AlienPalette.png",
    "Resources/AlienAnimated/OBJ/Alien.obj",
    0.34f,
    -0.5f,
    "Palm.R",
    "Palm.L",
    "Alien_Idle",
    "Alien_Run",
    "Alien_Jump",
    "Alien_RunningJump",
    "Alien_Swimming",
    "Alien_IdleHold",
    "Alien_RunHold",
    "Alien_SwordSlash",
    "Alien_Punch",
};

// 覚醒フォーム: メカ（実テクスチャ付き素材）。モデル身長は約5.35で、覚醒の迫力を出すため
// 通常フォームよりひと回り大きく見せる（当たり判定は変えない）
// Swim/IdleHold/RunningJump 相当のモーションが無いため Walk/Idle/Jump で代用。
// 手のボーンは指ごとに分かれているため、人差し指の付け根（PalmI.*）に武器を握らせる
constexpr RigVisualDef kAwakenedRigVisual = {
    "Resources/AnimatedMechPack/Textured/glTF",
    "Mike.gltf",
    "Resources/AnimatedMechPack/Textured/Textures/Mike_Texture.png",
    "Resources/AnimatedMechPack/Textured/OBJ/Mike.obj",
    0.22f,
    -0.5f,
    "PalmI.R",
    "PalmI.L",
    "Idle",
    "Run",
    "Jump",
    "Jump",
    "Walk",
    "Idle",
    "Run_Holding",
    "SwordSlash",
    "Punch",
};

// 全武器共通の拡大率（画面上で小さく見えづらかったため、各武器の基準スケールに一律で掛けている。
// 見た目の大きさを変えたい時はここだけ調整すればよい）
constexpr float kWeaponScaleBoost = 2.0f;

constexpr float kMeleeYaw180 = 3.14159265f;

/** @brief 近接武器1種ぶんのモデルパスと、手ボーンへ握らせる際のローカルスケール/回転/位置 */
struct HeldWeaponVisual {
    WeaponType type;
    const char* modelPath;
    const char* texturePath;
    Vector3 gripScale;
    Vector3 gripRotate; // ラジアン
    Vector3 gripTranslate;
};
constexpr HeldWeaponVisual kHeldWeaponVisuals[] = {
    // カタナ: 原点は柄と鍔の境目、刃が +Y（実寸高さ約4.35）
    { WeaponType::Sword, "Resources/Knight/OBJ/Katana.obj", "Resources/Knight/OBJ/KatanaPalette.png", { 0.40f * kWeaponScaleBoost, 0.40f * kWeaponScaleBoost, 0.40f * kWeaponScaleBoost }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // ダガー: 柄が根元(-Y側)、刃が +Y（実寸高さ約2.6）
    { WeaponType::Dagger, "Resources/MedievalWeaponsPack/OBJ/Dagger.obj", "Resources/MedievalWeaponsPack/OBJ/DaggerPalette.png", { 0.35f * kWeaponScaleBoost, 0.35f * kWeaponScaleBoost, 0.35f * kWeaponScaleBoost }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // ハンマー: 柄が根元(-Y側)、柄自体は原点をまっすぐ通っている（実寸高さ約4.33）。
    // ただしヘッド(+Y側)は左右非対称な形状で、+X側だけに大きく張り出す(頂点実測でmaxAbsXが柄の
    // 直径の6倍以上)。回転無しだとその張り出しが自分側を向いてしまうため、Y軸180度で反転させる
    { WeaponType::Hammer, "Resources/MedievalWeaponsPack/OBJ/Hammer_Small.obj", "Resources/MedievalWeaponsPack/OBJ/Hammer_SmallPalette.png", { 0.35f * kWeaponScaleBoost, 0.35f * kWeaponScaleBoost, 0.35f * kWeaponScaleBoost }, { 0.0f, kMeleeYaw180, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // スピア: 実寸高さ約9.7と長いため小さめのスケール
    { WeaponType::Spear, "Resources/MedievalWeaponsPack/OBJ/Spear.obj", "Resources/MedievalWeaponsPack/OBJ/SpearPalette.png", { 0.18f * kWeaponScaleBoost, 0.18f * kWeaponScaleBoost, 0.18f * kWeaponScaleBoost }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // クレイモア: 柄が根元付近(Y=-0.32)、刃が +Y（実寸高さ約6.6）
    { WeaponType::Greatsword, "Resources/MedievalWeaponsPack/OBJ/Claymore.obj", "Resources/MedievalWeaponsPack/OBJ/ClaymorePalette.png", { 0.26f * kWeaponScaleBoost, 0.26f * kWeaponScaleBoost, 0.26f * kWeaponScaleBoost }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // 鎌: 柄の中程が原点、刃が +X 側へ張り出す（実寸高さ約5.6）。
    // 他の武器と違い刃が柄と同軸(+Y)ではなく横に張り出す形状のため、回転無しだと刃が自分側を向いてしまう。
    // Y軸180度で+X側の張り出しを反転させ、刃が外側（キャラの正面側）を向くようにする
    { WeaponType::Scythe, "Resources/MedievalWeaponsPack/OBJ/Scythe.obj", "Resources/MedievalWeaponsPack/OBJ/ScythePalette.png", { 0.22f * kWeaponScaleBoost, 0.22f * kWeaponScaleBoost, 0.22f * kWeaponScaleBoost }, { 0.0f, kMeleeYaw180, 0.0f }, { 0.0f, 0.05f, 0.0f } },
    // 両刃斧: 頂点座標を実測したところ、刃(左右対称の両刃ヘッド)は+Y端のみにあり(実寸高さ約6.35)、
    // 柄そのものが原点(X=0)ではなくX≈+0.605の位置を通っている(=原点は柄から外れた位置にある)。
    // 刃は左右対称なので回転は無意味(向きに関係無い)、回転はせずXだけ補正する。
    // ・Y: 柄の反対側の端(-Y、無地の石突き)を握り手に合わせて、刃を全部前方へ出す(+1.525相当)
    // ・X: 柄の実際の通り道(+0.605相当)を握り手(原点)に合わせるため-0.29ぶんずらす。
    //      これをしないと武器全体が横にずれ、キャラの向きによっては刃が体のほうへ回り込んで見えていた
    { WeaponType::Axe, "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj", "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png", { 0.24f * kWeaponScaleBoost, 0.24f * kWeaponScaleBoost, 0.24f * kWeaponScaleBoost }, { 0.0f, 0.0f, 0.0f }, { -0.29f, 1.58f, 0.0f } },
};
constexpr int kHeldWeaponVisualCount = static_cast<int>(sizeof(kHeldWeaponVisuals) / sizeof(kHeldWeaponVisuals[0]));

/** @brief 武器を構えた待機/走りモーション（IdleHold/RunHold）を使うタイプか */
constexpr bool UsesHoldPose(WeaponType type)
{
    return type == WeaponType::Sword || type == WeaponType::Greatsword
        || type == WeaponType::Scythe || type == WeaponType::Axe
        || type == WeaponType::Spear;
}

// 銃アセットの定義（左手ボーンにアタッチ、G キーで切り替えた1丁だけ表示。
// K キー射撃コンボと対応。近接の kHeldWeaponVisuals と同方式）
// Pistol は他4丁と違い銃口が逆向き（グリップ側から弾が出ているように見える）だったため
// rotate.y = 180°で反転させている。他4丁は Z 軸沿い（銃口 -Z）のため rotate.y = +90°で向きを揃える。
// translate はグリップ位置を手のひらへ寄せる調整
/** @brief 銃1種ぶんのモデルパスと、左手ボーンへ握らせる際のローカルスケール/回転/位置 */
struct GunVisual {
    GunType type;
    const char* modelPath;
    const char* texturePath;
    Vector3 gripScale;
    Vector3 gripRotate; // ラジアン
    Vector3 gripTranslate;
};
constexpr float kGunYaw90 = 3.14159265f / 2.0f;
constexpr float kGunYaw180 = kGunYaw90 * 2.0f;
// gripScaleの基準値は剣等と同じ最終見た目基準に合わせてあり、kWeaponScaleBoostも共通で適用される
constexpr GunVisual kGunVisuals[] = {
    // ハンドガン: 原点はグリップ中心（実寸長さ約9.7）
    { GunType::Pistol, "Resources/AnimatedFPSGuns/OBJ/Pistol.obj", "Resources/AnimatedFPSGuns/OBJ/PistolPalette.png", { 0.09f * kWeaponScaleBoost, 0.09f * kWeaponScaleBoost, 0.09f * kWeaponScaleBoost }, { 0.0f, -kGunYaw90, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    // マグナム: グリップ(Wood)が +Z 側（実寸長さ約9.6）
    { GunType::Magnum, "Resources/AnimatedFPSGuns/OBJ/Revolver.obj", "Resources/AnimatedFPSGuns/OBJ/RevolverPalette.png", { 0.09f * kWeaponScaleBoost, 0.09f * kWeaponScaleBoost, 0.09f * kWeaponScaleBoost }, { 0.0f, kGunYaw90, 0.0f }, { -0.17f, 0.065f, 0.0f } },
    // マシンピストル: ブルパップ型、グリップは中央やや前（実寸長さ約11.1）
    { GunType::SMG, "Resources/AnimatedFPSGuns/OBJ/P90.obj", "Resources/AnimatedFPSGuns/OBJ/P90Palette.png", { 0.09f * kWeaponScaleBoost, 0.09f * kWeaponScaleBoost, 0.09f * kWeaponScaleBoost }, { 0.0f, kGunYaw90, 0.0f }, { 0.08f, 0.04f, 0.0f } },
    // ショットガン: トリガーが中央やや前（実寸長さ約8.5）
    { GunType::Shotgun, "Resources/AnimatedFPSGuns/OBJ/Shotgun.obj", "Resources/AnimatedFPSGuns/OBJ/ShotgunPalette.png", { 0.13f * kWeaponScaleBoost, 0.13f * kWeaponScaleBoost, 0.13f * kWeaponScaleBoost }, { 0.0f, kGunYaw90, 0.0f }, { 0.08f, 0.013f, 0.0f } },
    // レールガン: トリガーが +Z 側（実寸長さ約9.8、スナイパーライフルを流用）
    { GunType::Railgun, "Resources/AnimatedFPSGuns/OBJ/SniperRifle.obj", "Resources/AnimatedFPSGuns/OBJ/SniperRiflePalette.png", { 0.13f * kWeaponScaleBoost, 0.13f * kWeaponScaleBoost, 0.13f * kWeaponScaleBoost }, { 0.0f, kGunYaw90, 0.0f }, { -0.16f, 0.026f, 0.0f } },
};
constexpr int kGunVisualCount = static_cast<int>(sizeof(kGunVisuals) / sizeof(kGunVisuals[0]));

/**
 * @brief スケルトンの指定ボーンへオブジェクトを追従させる
 * @note 握りローカル → ジョイントのスケルトン空間 → ルートワールド の順で合成する。
 *       ボーンが見つからない（リグにその部位が無い）場合は何もしない。
 */
inline void AttachToBone(Object3d* obj, const Skeleton& skel, const Matrix4x4& rootWorld,
    const char* boneName, const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate)
{
    auto jt = skel.GetJointMap().find(boneName);
    if (jt == skel.GetJointMap().end()) {
        return;
    }

    Matrix4x4 grip = MakeAffineMatrix(gripScale, gripRotate, gripTranslate);
    Matrix4x4 world = Multiply(grip, Multiply(skel.GetJoints()[jt->second].skeletonSpaceMatrix, rootWorld));
    obj->SetLocalMatrix(world);
    obj->Update();
}

} // namespace engine::game
