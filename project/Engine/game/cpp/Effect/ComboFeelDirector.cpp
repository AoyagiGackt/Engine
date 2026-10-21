/**
 * @file ComboFeelDirector.cpp
 * @brief ComboFeelDirectorの刃の軌跡サンプリング、締め演出の時間管理、斬撃線の向きの決定、調整パネルを実装するファイル
 */
#include "ComboFeelDirector.h"
#include "Camera.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include "MeleeCombo.h"
#include "Player.h"
#include "SceneShared.h"
#include "TimeManager.h"
#include "Weapon.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using engine::graphics::Camera;
using engine::graphics::SrvManager;
using engine::graphics::WeaponTrailStyle;

namespace engine::game {
namespace {

constexpr const char* kSettingsPath = "Resources/Config/combo_feel.json";

// combo_feel.json の1つの値で済む項目（刃の範囲は武器種ごとの表なので個別に読み書きする）
constexpr JsonField<ComboFeelSettings> kSettingsFields[] = {
    { nullptr, "trailEnabled", &ComboFeelSettings::trailEnabled },
    { nullptr, "trailSubdivisions", &ComboFeelSettings::trailSubdivisions },
    { nullptr, "trailLifetime", &ComboFeelSettings::trailLifetime },
    { nullptr, "trailTipAlpha", &ComboFeelSettings::trailTipAlpha },
    { nullptr, "trailBaseAlpha", &ComboFeelSettings::trailBaseAlpha },
    { nullptr, "trailTint", &ComboFeelSettings::trailTint },
    { nullptr, "impactEnabled", &ComboFeelSettings::impactEnabled },
    { nullptr, "radialBlurEnabled", &ComboFeelSettings::radialBlurEnabled },
    { nullptr, "blurPeak", &ComboFeelSettings::blurPeak },
    { nullptr, "blurSeconds", &ComboFeelSettings::blurSeconds },
    { nullptr, "blurSamples", &ComboFeelSettings::blurSamples },
    { nullptr, "zoomEnabled", &ComboFeelSettings::zoomEnabled },
    { nullptr, "zoomDistance", &ComboFeelSettings::zoomDistance },
    { nullptr, "zoomSeconds", &ComboFeelSettings::zoomSeconds },
    { nullptr, "slowEnabled", &ComboFeelSettings::slowEnabled },
    { nullptr, "slowScale", &ComboFeelSettings::slowScale },
    { nullptr, "slowSeconds", &ComboFeelSettings::slowSeconds },
    { nullptr, "slashEnabled", &ComboFeelSettings::slashEnabled },
    { nullptr, "slashLength", &ComboFeelSettings::slashLength },
    { nullptr, "slashThickness", &ComboFeelSettings::slashThickness },
    { nullptr, "finisherSlashThickness", &ComboFeelSettings::finisherSlashThickness },
    { nullptr, "finisherSlashScale", &ComboFeelSettings::finisherSlashScale },
    { nullptr, "slashSeconds", &ComboFeelSettings::slashSeconds },
    { nullptr, "slashDirectionWindow", &ComboFeelSettings::slashDirectionWindow },
    { nullptr, "awakenEnabled", &ComboFeelSettings::awakenEnabled },
    { nullptr, "awakenColor", &ComboFeelSettings::awakenColor },
    { nullptr, "awakenTint", &ComboFeelSettings::awakenTint },
    { nullptr, "awakenTrailLifetimeScale", &ComboFeelSettings::awakenTrailLifetimeScale },
    { nullptr, "awakenGlowExtend", &ComboFeelSettings::awakenGlowExtend },
    { nullptr, "awakenGlowAlpha", &ComboFeelSettings::awakenGlowAlpha },
    { nullptr, "awakenSlashCount", &ComboFeelSettings::awakenSlashCount },
    { nullptr, "awakenSlashSpreadDeg", &ComboFeelSettings::awakenSlashSpreadDeg },
    { nullptr, "awakenImpactScale", &ComboFeelSettings::awakenImpactScale },
};

// WeaponType の並び順と一致させる（パネルとJSONのキーに使う）
constexpr std::array<const char*, ComboFeelSettings::kWeaponTypeCount> kWeaponTypeNames = {
    "Sword", "Spear", "Hammer", "Dagger", "Ball", "Greatsword", "Scythe", "Axe"
};

constexpr float kMinClipW = 0.0001f; // 投影したw成分がこれ以下ならカメラの後ろとみなす
constexpr float kScreenCenterUV = 0.5f; // 画面中央のUV
constexpr float kMinSlashMotion = 0.05f; // 刃先の移動がこれ未満なら向きの推定に使わない
constexpr float kFallbackDiagonalRise = 0.55f; // 横振りの推定向きの傾き（袈裟斬りらしく斜めにする）
constexpr float kFallbackVerticalLean = 0.25f; // 縦振りの推定向きの前傾き
constexpr float kSlashWhiteMix = 0.55f; // 斬撃線の色を白へ寄せる割合
constexpr float kSecondSlashOffset = 0.12f; // 2本目の細い斬撃線をずらす距離（ワールド単位）
constexpr float kSecondSlashThicknessRatio = 0.35f; // 2本目の太さ（1本目に対する比）
constexpr float kSecondSlashLengthRatio = 0.8f; // 2本目の長さ（1本目に対する比）
constexpr float kZoomAttackRatio = 0.18f; // ズームの所要時間のうち寄り切るまでの割合（残りで戻る）
constexpr float kMicrosPerSecond = 1000000.0f;

/** @brief 2色を割合 t で混ぜた色を返す */
Vector4 MixColor(const Vector4& a, const Vector4& b, float t)
{
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
}

/** @brief Vector3 を3要素のJSON配列へ変換する */
nlohmann::json ToJson(const Vector3& v) {
    return nlohmann::json::array({ v.x, v.y, v.z });
}

/** @brief 数値の配列なら要素数ぶん読み出す（形が違えばfalse） */
bool FloatsFromJson(const nlohmann::json& j, float* out, size_t count)
{
    if (!j.is_array() || j.size() != count)
    {
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!j[i].is_number())
        {
            return false;
        }
        out[i] = j[i].get<float>();
    }
    return true;
}

/** @brief 3要素のJSON配列から Vector3 を読む（形が違えば既定値を返す） */
Vector3 Vector3FromJson(const nlohmann::json& j, const Vector3& fallback)
{
    constexpr size_t kVector3Size = 3;
    Vector3 v = fallback;
    return FloatsFromJson(j, &v.x, kVector3Size) ? v : fallback;
}


} // namespace

/** @brief 演出設定を既定値で初期化する */
ComboFeelSettings::ComboFeelSettings()
{
    // WeaponTypeの並びに合わせたモデル空間の根元と先端。
    static constexpr std::array<BladeRange, kWeaponTypeCount> kDefaultBlades = {{
        { { 0.0f, 0.3f, 0.0f }, { 0.0f, 4.35f, 0.0f } },
        { { 0.0f, 7.6f, 0.0f }, { 0.0f, 9.7f, 0.0f } },
        { { 0.0f, 3.3f, 0.0f }, { 0.0f, 4.33f, 0.0f } },
        { { 0.0f, 1.0f, 0.0f }, { 0.0f, 2.6f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 1.2f, 0.0f }, { 0.0f, 6.6f, 0.0f } },
        { { 0.3f, 2.2f, 0.0f }, { 2.2f, 2.0f, 0.0f } },
        { { 0.6f, 4.4f, 0.0f }, { 0.6f, 6.35f, 0.0f } },
    }};
    blades = kDefaultBlades;
}

// ══════════════════════════════════════════════════════
// 初期化・終了
// ══════════════════════════════════════════════════════

/** @brief 刃の軌跡・光る軌跡・ブラーを初期化し、演出設定を JSON から読み込む */
void ComboFeelDirector::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    trail_.Initialize(dxCommon);
    glowTrail_.Initialize(dxCommon);
    blur_.Initialize(dxCommon, srvManager);
    LoadSettings();
    lastAttack_ = nullptr;
    impactTimer_ = -1.0f;
}

/** @brief 演出の状態を消す（シーン終了時に呼ぶ） */
void ComboFeelDirector::Finalize()
{
    impactTimer_ = -1.0f;
    blur_.SetParams(kScreenCenterUV, kScreenCenterUV, 0.0f, settings_.blurSamples);
    trail_.Clear();
    glowTrail_.Clear();
}

// ══════════════════════════════════════════════════════
// 毎フレームの更新
// ══════════════════════════════════════════════════════

/** @brief 刃の位置を軌跡に積み、締め演出の時間を進める（毎フレーム呼ぶ） */
void ComboFeelDirector::Update(const Player& player, const Vector4& weaponColor, Camera* camera, bool gameAdvanced)
{
    lastPlayerPos_ = player.GetPosition();

    const auto begin = std::chrono::high_resolution_clock::now();
    ApplyTrailStyle(weaponColor);
    // ヒットストップやスローで止まったフレームは刃も止まっているので、積まずに帯ごと止める
    if (gameAdvanced)
    {
        SampleBlade(player);
    }
    const float trailDt = gameAdvanced ? GameConstants::kFrameDeltaTime : 0.0f;
    trail_.Update(trailDt, camera);
    glowTrail_.Update(trailDt, camera);
    const auto end = std::chrono::high_resolution_clock::now();
    lastTrailMicros_ = std::chrono::duration<float>(end - begin).count() * kMicrosPerSecond;

    UpdateImpact(camera);
}

/** @brief 刃の範囲を武器行列でワールドへ移して軌跡に積む */
void ComboFeelDirector::SampleBlade(const Player& player)
{
    // 近接の段が出ていない・武器を持っていない間は帯を途切れさせて何も積まない
    const MeleeAttackDef* attack = player.GetActiveMeleeAttack();
    WeaponType type { };
    Matrix4x4 weaponMatrix { };
    if (attack == nullptr || !player.GetHeldWeaponTransform(type, weaponMatrix))
    {
        if (lastAttack_ != nullptr)
        {
            trail_.BreakStroke();
            glowTrail_.BreakStroke();
        }
        lastAttack_ = nullptr;
        return;
    }
    // 段が切り替わったら前の振りと帯を繋がない（振り抜きと次の振りかぶりの間に帯が渡らないように）
    if (attack != lastAttack_)
    {
        trail_.BreakStroke();
        glowTrail_.BreakStroke();
    }
    lastAttack_ = attack;

    // 武器ごとの刃の根元と先端を、今の武器の行列でワールド座標へ移して帯に積む
    const size_t index = static_cast<size_t>(type);
    if (index >= settings_.blades.size())
    {
        return;
    }
    lastWeaponIndex_ = static_cast<int>(index);
    const BladeRange& blade = settings_.blades[index];
    const Vector3 base = TransformPoint(blade.base, weaponMatrix);
    const Vector3 tip = TransformPoint(blade.tip, weaponMatrix);
    trail_.AddSample(base, tip);

    // 外側の光の帯は刃の中ほどから刃先の先まで（覚醒していない間も積んでおき、覚醒した瞬間から途切れず出す）
    constexpr float kGlowStartRatio = 0.5f; // 光の帯を刃のどこから始めるか（根元0〜刃先1）
    const Vector3 blade3 = { tip.x - base.x, tip.y - base.y, tip.z - base.z };
    const Vector3 glowBase = base + blade3 * kGlowStartRatio;
    const Vector3 glowTip = tip + blade3 * settings_.awakenGlowExtend;
    glowTrail_.AddSample(glowBase, glowTip);
}

/** @brief 軌跡の色を設定値と武器の色から作る */
void ComboFeelDirector::ApplyTrailStyle(const Vector4& weaponColor)
{
    constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
    const bool boosted = IsAwakenBoosted();
    const float lifetime = settings_.trailLifetime * (boosted ? settings_.awakenTrailLifetimeScale : 1.0f);
    // 覚醒中は根元の色も覚醒色へ寄せ、全体を一色の光の刃に見せる
    const Vector4 bodyColor = boosted ? MixColor(weaponColor, settings_.awakenColor, settings_.awakenTint) : weaponColor;

    WeaponTrailStyle style;
    style.lifetime = lifetime;
    style.subdivisions = settings_.trailSubdivisions;
    // 刃先は白に近い芯、根元は武器の色で透けていく
    style.tipColor = MixColor(kWhite, bodyColor, settings_.trailTint);
    style.tipColor.w = settings_.trailTipAlpha;
    style.baseColor = { bodyColor.x, bodyColor.y, bodyColor.z, settings_.trailBaseAlpha };
    trail_.SetStyle(style);

    // 外側の光の帯は覚醒色で、刃先側だけ光って内側へ透ける
    WeaponTrailStyle glow;
    glow.lifetime = lifetime;
    glow.subdivisions = settings_.trailSubdivisions;
    glow.tipColor = { settings_.awakenColor.x, settings_.awakenColor.y, settings_.awakenColor.z, settings_.awakenGlowAlpha };
    glow.baseColor = { settings_.awakenColor.x, settings_.awakenColor.y, settings_.awakenColor.z, 0.0f };
    glowTrail_.SetStyle(glow);
}

/** @brief 刃の軌跡を描画する（3D描画パス内、パーティクルの後に呼ぶ） */
void ComboFeelDirector::DrawTrail()
{
    if (!settings_.trailEnabled)
    {
        return;
    }
    // 光の帯を先に描き、その上に芯の帯を重ねる
    if (IsAwakenBoosted())
    {
        glowTrail_.Draw();
    }
    trail_.Draw();
}

// ══════════════════════════════════════════════════════
// 締めの重さ（ブラー・ズーム・スロー）
// ══════════════════════════════════════════════════════

/** @brief 指定位置を中心に締め演出（ブラー・ズーム・スロー）を発動する */
void ComboFeelDirector::TriggerFinisherImpact(const Vector3& worldPos)
{
    if (!settings_.impactEnabled)
    {
        return;
    }
    impactTimer_ = 0.0f;
    impactPos_ = worldPos;
    // 覚醒中の締めはブラー・ズーム・スローをまとめて強める（スローは長くするだけで、遅さは変えない）
    impactScale_ = IsAwakenBoosted() ? settings_.awakenImpactScale : 1.0f;
    if (settings_.slowEnabled)
    {
        TimeManager::GetInstance()->RequestSlowMotion(settings_.slowSeconds * impactScale_, settings_.slowScale);
    }
}

/** @brief 締め演出の経過時間を進め、ブラーの強さと中心を更新する */
void ComboFeelDirector::UpdateImpact(const Camera* camera)
{
    float strength = 0.0f;
    if (impactTimer_ >= 0.0f)
    {
        // ヒットストップ中こそ見せたい演出なので、ゲームの停止とは無関係に実時間で進める
        impactTimer_ += GameConstants::kFrameDeltaTime;

        const float t = (settings_.blurSeconds > 0.0f) ? impactTimer_ / settings_.blurSeconds : 1.0f;
        if (settings_.impactEnabled && settings_.radialBlurEnabled && t < 1.0f)
        {
            const float remain = 1.0f - t;
            strength = settings_.blurPeak * impactScale_ * remain * remain;
        }
        // 当たった位置を画面UVへ投影して放射の中心にする（カメラが寄っても中心がずれない）
        if (camera != nullptr)
        {
            const Matrix4x4 vp = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
            const Vector3& p = impactPos_;
            const float cx = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
            const float cy = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
            const float cw = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
            if (cw > kMinClipW)
            {
                blurCenterU_ = cx / cw * 0.5f + 0.5f;
                blurCenterV_ = 0.5f - cy / cw * 0.5f;
            }
        }
        if (impactTimer_ >= (std::max)(settings_.blurSeconds, settings_.zoomSeconds))
        {
            impactTimer_ = -1.0f;
        }
    }
    blur_.SetParams(blurCenterU_, blurCenterV_, strength, settings_.blurSamples);
}

/** @brief 締めのラジアルブラーを画面へ掛ける（3D描画の後・UIの前、バックバッファ直描き時だけ呼ぶ） */
void ComboFeelDirector::ApplyScreenEffects()
{
    blur_.CaptureAndApply();
}

/** @brief 締めのズームをカメラ位置へ加える（シーンがカメラ位置を決め終えた後に呼ぶ） */
void ComboFeelDirector::ApplyCameraZoom(Camera* camera) const
{
    if (camera == nullptr || impactTimer_ < 0.0f || !settings_.impactEnabled || !settings_.zoomEnabled
        || settings_.zoomSeconds <= 0.0f)
    {
        return;
    }
    const float t = impactTimer_ / settings_.zoomSeconds;
    if (t >= 1.0f)
    {
        return;
    }
    // 一瞬で寄り、余韻を残しながらゆっくり戻す
    float amount = 0.0f;
    if (t < kZoomAttackRatio)
    {
        amount = t / kZoomAttackRatio;
    }
    else
    {
        const float back = 1.0f - (t - kZoomAttackRatio) / (1.0f - kZoomAttackRatio);
        amount = back * back;
    }
    // カメラは -Z 側から +Z を向いているので、Zを増やすと被写体へ寄る
    const Vector3& cam = camera->GetTranslate();
    camera->SetTranslate({ cam.x, cam.y, cam.z + settings_.zoomDistance * impactScale_ * amount });
}


// ══════════════════════════════════════════════════════
// 振りの向きに沿った斬撃線
// ══════════════════════════════════════════════════════

/** @brief 近接の段が当たった時に、締めなら締め演出を始め、振りの向きの斬撃線を出す */
void ComboFeelDirector::OnMeleeHit(const MeleeAttackDef* attack, bool finisher, const Vector3& hitPos,
    const Vector4& weaponColor, float facingX, const Camera* camera)
{
    // 締めの段なら、斬撃線より先にブラー・ズーム・スローの締め演出を始める
    if (finisher)
    {
        TriggerFinisherImpact(hitPos);
    }
    if (!settings_.slashEnabled || camera == nullptr)
    {
        return;
    }

    // 振りの向きに沿って、命中点を中心に武器色を白へ寄せた斬撃線を引く
    const Vector2 dir = DecideSlashDirection(attack, facingX);
    const float halfLen = settings_.slashLength * (finisher ? settings_.finisherSlashScale : 1.0f);
    const float thickness = finisher ? settings_.finisherSlashThickness : settings_.slashThickness;
    constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
    const Vector4 color = MixColor(weaponColor, kWhite, kSlashWhiteMix);
    const Vector3& cam = camera->GetTranslate();

    SceneShared::SpawnSlashMarkWorld(
        { hitPos.x - dir.x * halfLen, hitPos.y - dir.y * halfLen },
        { hitPos.x + dir.x * halfLen, hitPos.y + dir.y * halfLen },
        cam.x, cam.y, color, thickness, settings_.slashSeconds);

    // 少しずらした細い線を重ね、一本の線ではなく刃が通った筋に見せる
    const Vector2 normal = { -dir.y * kSecondSlashOffset, dir.x * kSecondSlashOffset };
    const float secondHalfLen = halfLen * kSecondSlashLengthRatio;
    SceneShared::SpawnSlashMarkWorld(
        { hitPos.x + normal.x - dir.x * secondHalfLen, hitPos.y + normal.y - dir.y * secondHalfLen },
        { hitPos.x + normal.x + dir.x * secondHalfLen, hitPos.y + normal.y + dir.y * secondHalfLen },
        cam.x, cam.y, kWhite, thickness * kSecondSlashThicknessRatio, settings_.slashSeconds);

    // 覚醒中は覚醒色の斬撃線を扇状に重ね、1振りで何本も斬ったように見せる
    if (!IsAwakenBoosted())
    {
        return;
    }
    const int extraLines = (std::max)(settings_.awakenSlashCount - 1, 0);
    const float baseAngle = std::atan2(dir.y, dir.x);
    for (int i = 0; i < extraLines; ++i)
    {
        // 元の線を挟んで左右交互に、外側ほど大きく開く（+1段, -1段, +2段, ...）
        const int stepIndex = i / 2 + 1;
        const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        const float angle = baseAngle + sign * static_cast<float>(stepIndex) * settings_.awakenSlashSpreadDeg * GameConstants::kDegToRad;
        const Vector2 fan = { std::cos(angle), std::sin(angle) };
        SceneShared::SpawnSlashMarkWorld(
            { hitPos.x - fan.x * halfLen, hitPos.y - fan.y * halfLen },
            { hitPos.x + fan.x * halfLen, hitPos.y + fan.y * halfLen },
            cam.x, cam.y, settings_.awakenColor, thickness * kSecondSlashThicknessRatio, settings_.slashSeconds);
    }
}

/** @brief 刃先の動きから斬撃線の向きを決める（動きが取れなければ段の振り方から推定する） */
Vector2 ComboFeelDirector::DecideSlashDirection(const MeleeAttackDef* attack, float facingX) const
{
    const float facing = (facingX >= 0.0f) ? 1.0f : -1.0f;

    // 実際に刃先が動いた向き（画面は XY 平面なので Z は捨てる）
    Vector3 motion { };
    if (trail_.GetRecentTipMotion(settings_.slashDirectionWindow, motion))
    {
        const float len = std::sqrt(motion.x * motion.x + motion.y * motion.y);
        if (len >= kMinSlashMotion)
        {
            return { motion.x / len, motion.y / len };
        }
    }

    // 刃先の記録が無い時は段の定義から推定する（Z軸の振り=画面内の弧、X軸の振り=縦の振り下ろし/突き上げ）
    Vector2 dir = { facing, 0.0f };
    if (attack != nullptr)
    {
        const float swingZ = attack->swingTo.z - attack->swingFrom.z;
        const float swingX = attack->swingTo.x - attack->swingFrom.x;
        if (std::abs(swingZ) >= std::abs(swingX))
        {
            dir = { facing, (swingZ > 0.0f ? kFallbackDiagonalRise : -kFallbackDiagonalRise) };
        }
        else
        {
            dir = { facing * kFallbackVerticalLean, (swingX > 0.0f ? -1.0f : 1.0f) };
        }
    }
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    return { dir.x / len, dir.y / len };
}

// ══════════════════════════════════════════════════════
// 設定の保存・読み込み
// ══════════════════════════════════════════════════════

/** @brief combo_feel.json を読み込み、全項目を読めた場合だけ設定へ反映する */
void ComboFeelDirector::LoadSettings()
{
    const nlohmann::json j = JsonHelper::Load(kSettingsPath);
    if (!j.is_object())
    {
        return;
    }
    // 手で書き換えた JSON の型違いで落ちないよう、作業用の写しに読み、全部読めた時だけ反映する
    ComboFeelSettings s = settings_;
    try
    {
        ReadJsonFields(j, kSettingsFields, s);
        if (j.contains("blades") && j["blades"].is_object())
        {
            const nlohmann::json& blades = j["blades"];
            for (size_t i = 0; i < kWeaponTypeNames.size(); ++i)
            {
                if (!blades.contains(kWeaponTypeNames[i]))
                {
                    continue;
                }
                const nlohmann::json& b = blades[kWeaponTypeNames[i]];
                s.blades[i].base = Vector3FromJson(b.value("base", nlohmann::json { }), s.blades[i].base);
                s.blades[i].tip = Vector3FromJson(b.value("tip", nlohmann::json { }), s.blades[i].tip);
            }
        }
    }
    catch (const nlohmann::json::exception&)
    {
        return;
    }
    settings_ = s;
}

/** @brief 編集した演出設定をJSONファイルへ保存する */
void ComboFeelDirector::SaveSettings() const
{
    const ComboFeelSettings& s = settings_;
    nlohmann::json blades = nlohmann::json::object();
    for (size_t i = 0; i < kWeaponTypeNames.size(); ++i)
    {
        blades[kWeaponTypeNames[i]] = { { "base", ToJson(s.blades[i].base) }, { "tip", ToJson(s.blades[i].tip) } };
    }
    nlohmann::json j = nlohmann::json::object();
    WriteJsonFields(j, kSettingsFields, s);
    j["blades"] = blades;
    JsonHelper::Save(kSettingsPath, j);
}

// ══════════════════════════════════════════════════════
// デバッグパネル（F6）
// ══════════════════════════════════════════════════════

/** @brief F6 でデバッグパネルを開閉し、開いていれば描画する（Update フェーズから呼ぶ） */
void ComboFeelDirector::UpdateDebugPanel(Input* input)
{
#ifdef USE_IMGUI
    // F6 でパネルの表示を切り替える
    if (input != nullptr && input->TriggerKey(DIK_F6))
    {
        showPanel_ = !showPanel_;
    }
    if (!showPanel_)
    {
        return;
    }

    // 各スライダーのドラッグ幅と上下限
    constexpr float kTimeDragStep = 0.005f;
    constexpr float kFineDragStep = 0.02f;
    constexpr float kMaxBlurStrength = 0.6f;
    constexpr float kDistanceDragStep = 0.05f;
    constexpr float kMinSlowScale = 0.05f;
    constexpr float kMinVisibleLifetime = 0.02f;
    constexpr float kMaxZoomDistance = 8.0f;
    constexpr float kMaxSlashHalfLength = 6.0f;
    constexpr float kThicknessDragStep = 0.1f;
    constexpr float kMaxSlashThickness = 40.0f;
    constexpr float kMaxTrailScale = 4.0f;
    constexpr float kMaxDirectionWindow = 0.3f;
    constexpr float kGlowDragStep = 0.01f;
    constexpr float kMaxGlowExtend = 1.5f;
    constexpr float kMaxSlashSpreadDegrees = 60.0f;
    constexpr float kMaxAwakenImpactScale = 3.0f;
    constexpr float kDragSpeed = kGlowDragStep;
    constexpr int kMaxSubdivisions = 12;
    constexpr int kMinBlurSamples = 4;
    constexpr int kMaxBlurSamples = 32;
    ComboFeelSettings& s = settings_;

    if (ImGui::Begin("Combo Feel (F6)", &showPanel_))
    {
        // 発表・比較用に全演出をまとめて切り替える
        if (ImGui::Button("全部オン"))
        {
            s.trailEnabled = s.impactEnabled = s.radialBlurEnabled = s.zoomEnabled = s.slowEnabled = s.slashEnabled = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("全部オフ"))
        {
            s.trailEnabled = s.impactEnabled = s.slashEnabled = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("締め演出を試す"))
        {
            TriggerFinisherImpact(lastPlayerPos_);
        }

        // 刃の帯の見た目と、武器ごとの刃の範囲
        if (ImGui::CollapsingHeader("1. 刃の軌跡", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("軌跡を描く", &s.trailEnabled);
            ImGui::SliderInt("補間分割数 (1=補間なし)", &s.trailSubdivisions, 1, kMaxSubdivisions);
            ImGui::DragFloat("寿命(秒)", &s.trailLifetime, kTimeDragStep, kMinVisibleLifetime, 1.0f);
            ImGui::SliderFloat("刃先の不透明度", &s.trailTipAlpha, 0.0f, 1.0f);
            ImGui::SliderFloat("根元の不透明度", &s.trailBaseAlpha, 0.0f, 1.0f);
            ImGui::SliderFloat("属性色の混ぜ具合", &s.trailTint, 0.0f, 1.0f);
            ImGui::Text("サンプル %d / 頂点 %u / 更新 %.1f us",
                trail_.GetSampleCount(), trail_.GetVertexCount(), lastTrailMicros_);

            ImGui::SeparatorText("刃の範囲（武器モデル空間）");
            ImGui::Combo("武器", &lastWeaponIndex_, kWeaponTypeNames.data(), static_cast<int>(kWeaponTypeNames.size()));
            BladeRange& blade = s.blades[static_cast<size_t>(std::clamp(lastWeaponIndex_, 0, ComboFeelSettings::kWeaponTypeCount - 1))];
            ImGui::DragFloat3("根元", &blade.base.x, kDragSpeed);
            ImGui::DragFloat3("先端", &blade.tip.x, kDragSpeed);
        }

        // 締めの段で出すブラー・ズーム・スロー
        if (ImGui::CollapsingHeader("2. 締めの重さ", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("締め演出を使う", &s.impactEnabled);
            ImGui::Checkbox("ラジアルブラー", &s.radialBlurEnabled);
            ImGui::SliderFloat("ブラー強度", &s.blurPeak, 0.0f, kMaxBlurStrength);
            ImGui::DragFloat("ブラー時間(秒)", &s.blurSeconds, kTimeDragStep, 0.0f, 1.0f);
            ImGui::SliderInt("ブラーサンプル数", &s.blurSamples, kMinBlurSamples, kMaxBlurSamples);
            ImGui::Checkbox("ズームパンチ", &s.zoomEnabled);
            ImGui::DragFloat("寄せる距離", &s.zoomDistance, kDistanceDragStep, 0.0f, kMaxZoomDistance);
            ImGui::DragFloat("ズーム時間(秒)", &s.zoomSeconds, kTimeDragStep, 0.0f, 1.0f);
            ImGui::Checkbox("ヒットストップ明けのスロー", &s.slowEnabled);
            ImGui::SliderFloat("スロー倍率", &s.slowScale, kMinSlowScale, 1.0f);
            ImGui::DragFloat("スロー時間(秒)", &s.slowSeconds, kTimeDragStep, 0.0f, 1.0f);
        }

        // 命中点に出す斬撃線の大きさと表示時間
        if (ImGui::CollapsingHeader("3. 振りの向きの斬撃線", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("斬撃線を出す", &s.slashEnabled);
            ImGui::DragFloat("長さ(半分)", &s.slashLength, kFineDragStep, 0.0f, kMaxSlashHalfLength);
            ImGui::DragFloat("太さ", &s.slashThickness, kThicknessDragStep, 0.0f, kMaxSlashThickness);
            ImGui::DragFloat("締めの太さ", &s.finisherSlashThickness, kThicknessDragStep, 0.0f, kMaxSlashThickness);
            ImGui::DragFloat("締めの長さ倍率", &s.finisherSlashScale, kFineDragStep, 0.5f, kMaxTrailScale);
            ImGui::DragFloat("表示時間(秒)", &s.slashSeconds, kTimeDragStep, kMinVisibleLifetime, 1.0f);
            ImGui::DragFloat("向きを測る時間(秒)", &s.slashDirectionWindow, kTimeDragStep, 0.0f, kMaxDirectionWindow);
        }

        // 覚醒中だけ強める色・帯・斬撃線の本数
        if (ImGui::CollapsingHeader("4. 覚醒", ImGuiTreeNodeFlags_DefaultOpen))
        {
            constexpr int kMaxAwakenSlashCount = 7;
            ImGui::Checkbox("覚醒中に派手にする", &s.awakenEnabled);
            ImGui::Checkbox("覚醒していなくても見る（確認用）", &previewAwaken_);
            ImGui::ColorEdit4("覚醒色", &s.awakenColor.x);
            ImGui::SliderFloat("軌跡を覚醒色へ寄せる", &s.awakenTint, 0.0f, 1.0f);
            ImGui::DragFloat("軌跡の寿命の倍率", &s.awakenTrailLifetimeScale, kFineDragStep, 1.0f, kMaxTrailScale);
            ImGui::DragFloat("光の帯の伸び", &s.awakenGlowExtend, kGlowDragStep, 0.0f, kMaxGlowExtend);
            ImGui::SliderFloat("光の帯の不透明度", &s.awakenGlowAlpha, 0.0f, 1.0f);
            ImGui::SliderInt("斬撃線の本数", &s.awakenSlashCount, 1, kMaxAwakenSlashCount);
            ImGui::DragFloat("斬撃線の開き(度)", &s.awakenSlashSpreadDeg, 0.5f, 0.0f, kMaxSlashSpreadDegrees);
            ImGui::DragFloat("締め演出の倍率", &s.awakenImpactScale, kFineDragStep, 1.0f, kMaxAwakenImpactScale);
        }

        // 調整した値を combo_feel.json へ保存・読み直しする
        ImGui::Separator();
        if (ImGui::Button("保存"))
        {
            SaveSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("読み直し"))
        {
            LoadSettings();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kSettingsPath);
    }
    ImGui::End();
#else
    (void)input;
#endif
}

} // namespace engine::game
