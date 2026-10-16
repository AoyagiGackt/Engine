/**
 * @file TitleScene.cpp
 * @brief タイトル画面のメニュー表示と入力待ち・シーン遷移（TitleScene）の実装
 */
#include "TitleScene.h"
#include "GameConstants.h"
#include "GameSettings.h"
#include "HitEffectLibrary.h"
#include "ModelManager.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "TitleDemoTuning.h"
#include "UILayout.h"
#include "SkinnedObject3d.h"
#include "SrvManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <numbers>
#include <random>
#ifdef USE_IMGUI
#include <imgui.h>
#endif
using namespace engine;
using namespace engine::graphics;
using namespace engine::game;

namespace {
// デモの調整値は Resources/Config/title_demo.json（TitleDemoTuning）、命中演出の形は hit_effects.json の title_ レシピ

// 最初に並べる的（BattleTestSceneのDummyと同じスライム）。1体ずつ近づいて倒しながら進む
struct InitialDummy {
    float x;
    bool airborne;
};
constexpr InitialDummy kInitialDummies[] = {
    { 14.0f, false },
    { 18.0f, false },
    { 26.0f, false },
    { 30.0f, false },
};
constexpr Vector4 kDummyNormalColor = { 1.0f, 1.0f, 1.0f, 1.0f };

// メニューの既定の配置（F2のエディタで動かした値は Resources/Config/UI/title.json に保存される）
constexpr const char* kLayoutName = "title";
constexpr Vector2 kMenuPosition = { 440.0f, 470.0f };
constexpr Vector2 kMenuItemSize = { 400.0f, 48.0f };

// 設定メニュー（タイトル上でメニューの中身だけ入れ替えて表示する）
constexpr int kOptionsRowBgm = 0;
constexpr int kOptionsRowSe = 1;
constexpr int kOptionsRowCommandList = 2;
constexpr int kOptionsRowBack = 3;
constexpr float kVolumeTextOffsetX = 260.0f; // メニュー左端から音量表示までの距離
constexpr float kVolumeTextTopRatio = 0.3f; // UIMenuのラベルと同じ高さに揃える
constexpr float kVolumeTextScale = 1.5f;
constexpr Vector4 kVolumeTextColor = { 1.0f, 1.0f, 1.0f, 1.0f };

std::vector<UIButton> OptionsMenuItems()
{
    return { { "BGM VOLUME" }, { "SE VOLUME" }, { "COMMAND LIST" }, { "BACK" } };
}

// 武器ごとのデモのコンボ段数
constexpr int kSwordComboLength = 4;
constexpr int kSpearComboLength = 3;
constexpr int kDaggerComboLength = 5;
constexpr int kHammerComboLength = 2;
constexpr int kDefaultComboLength = 3;

/** @brief デモの命中演出に渡す位置と色 */
struct DemoHitContext {
    const HitEffectPlayer& fx;
    Vector3 weaponPos; ///< 武器の位置（演出用Z適用済み）
    Vector3 targetPos; ///< 的の位置（演出用Z適用済み）
    float facing; ///< 的のある向き（右=+1, 左=-1）
    Vector4 color; ///< 武器の演出色
    bool skillPulse; ///< 武器固有技として演出するか
};

/** @brief タイトルデモでの武器ごとの振る舞い（コンボ段数・固有技の出し所・演出） */
class DemoWeaponStyle {
public:
    virtual ~DemoWeaponStyle() = default;
    /** @brief デモで振るコンボ段数 */
    virtual int ComboLength() const { return kDefaultComboLength; }
    /** @brief この段を武器固有技として出すか */
    virtual bool IsSkillPulse(int, int) const { return false; }
    /** @brief 命中時に撒く軌跡のレシピ名（hit_effects.json の trails） */
    virtual const char* HitTrail() const { return "title_hit"; }
    /** @brief 常時出す軌跡のレシピ名（hit_effects.json の trails） */
    virtual const char* IdleTrail() const { return "title_idle"; }
    /** @brief 武器ごとの追加命中演出 */
    virtual void EmitHit(const DemoHitContext&) const { }

    /** @brief 武器種に対応する振る舞いを取得する */
    static const DemoWeaponStyle& For(WeaponType type);
};

/** @brief 剣: 素早い交差斬りと、幅の広い締め攻撃 */
class SwordDemoStyle final : public DemoWeaponStyle {
public:
    int ComboLength() const override { return kSwordComboLength; }
    bool IsSkillPulse(int pulseIndex, int attackCount) const override { return pulseIndex == attackCount - 1; }
    void EmitHit(const DemoHitContext& ctx) const override
    {
        ctx.fx.Slash(ctx.skillPulse ? "title_sword_skill" : "title_sword", ctx.weaponPos, ctx.facing, { .color = ctx.color });
        ctx.fx.Slash(ctx.skillPulse ? "title_sword_cross_skill" : "title_sword_cross", ctx.targetPos, ctx.facing, { .color = ctx.color });
    }
};

/** @brief 槍: 細く長い突きの軌跡と、命中点に集中する衝撃リング */
class SpearDemoStyle final : public DemoWeaponStyle {
public:
    int ComboLength() const override { return kSpearComboLength; }
    const char* IdleTrail() const override { return "title_idle_spear"; }
    void EmitHit(const DemoHitContext& ctx) const override
    {
        const Vector3 midpoint = { (ctx.weaponPos.x + ctx.targetPos.x) * 0.5f, ctx.weaponPos.y, ctx.weaponPos.z };
        ctx.fx.Trail("title_spear_thrust", midpoint, ctx.color);
        ctx.fx.Slash("title_spear_thrust", ctx.targetPos, ctx.facing, { .color = ctx.color });
        ctx.fx.Ring("title_spear", ctx.targetPos, { .color = ctx.color });
    }
};

/** @brief 短剣: 小さな斬線を重ねて攻撃の速さを見せる */
class DaggerDemoStyle final : public DemoWeaponStyle {
public:
    int ComboLength() const override { return kDaggerComboLength; }
    bool IsSkillPulse(int pulseIndex, int) const override { return pulseIndex == 0; }
    const char* IdleTrail() const override { return "title_idle_dagger"; }
    void EmitHit(const DemoHitContext& ctx) const override
    {
        const TitleDemoTuning& tune = TitleDemoTune();
        const SlashDef& cut = HitEffectLibrary::GetInstance()->Slash("title_dagger_cut");
        const float baseAngle = ctx.facing > 0.0f ? cut.angleRight : cut.angleLeft;
        // 斬線を中央の1本を挟んで斜めにずらして重ねる
        const float center = static_cast<float>(tune.daggerCutCount - 1) * 0.5f;
        for (int i = 0; i < tune.daggerCutCount; ++i) {
            const float offset = (static_cast<float>(i) - center) * tune.daggerCutSpacing;
            ctx.fx.Slash("title_dagger_cut", { ctx.targetPos.x + offset, ctx.targetPos.y + offset, ctx.targetPos.z }, ctx.facing,
                { .angle = baseAngle + offset, .color = ctx.color });
        }
    }
};

/** @brief 槌: 地面への重い衝撃を強調する */
class HammerDemoStyle final : public DemoWeaponStyle {
public:
    int ComboLength() const override { return kHammerComboLength; }
    bool IsSkillPulse(int pulseIndex, int attackCount) const override { return pulseIndex == attackCount - 1; }
    const char* HitTrail() const override { return "title_hit_hammer"; }
    const char* IdleTrail() const override { return "title_idle_hammer"; }
    void EmitHit(const DemoHitContext& ctx) const override
    {
        const Vector3 ground = { ctx.targetPos.x, TitleDemoTune().groundY, ctx.targetPos.z };
        ctx.fx.Ring(ctx.skillPulse ? "title_hammer_skill" : "title_hammer", ground, { .color = ctx.color });
        ctx.fx.Star("title_hammer", ground, { .color = ctx.color });
    }
};

const DemoWeaponStyle& DemoWeaponStyle::For(WeaponType type)
{
    static const SwordDemoStyle sword {};
    static const SpearDemoStyle spear {};
    static const DaggerDemoStyle dagger {};
    static const HammerDemoStyle hammer {};
    static const DemoWeaponStyle def {};
    // WeaponType の並び順。専用の見せ方が無い武器は既定の見せ方
    static const std::array<const DemoWeaponStyle*, kWeaponTypeCount> kStyles = {
        &sword, // Sword
        &spear, // Spear
        &hammer, // Hammer
        &dagger, // Dagger
        &def, // Ball
        &def, // Greatsword
        &def, // Scythe
        &def, // Axe
    };
    const size_t index = static_cast<size_t>(type);
    return index < kStyles.size() ? *kStyles[index] : def;
}
} // namespace

void TitleScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio)
{
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);

    fontRenderer_.Initialize(spriteCommon_.get());

    menu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    optionsMenu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    menu_.BindLayout(kLayoutName, "menu", kMenuPosition, kMenuItemSize);
    optionsMenu_.BindLayout(kLayoutName, "menu", kMenuPosition, kMenuItemSize);
    menu_.SetItems({
        { "NEW GAME" },
        { "CONTINUE", SaveDataManager::GetInstance()->HasContinue() },
        { "TRAINING" },
        { "OPTIONS" },
    });
    optionsMenu_.SetItems(OptionsMenuItems());
    commandList_.Initialize(&fontRenderer_, audio_);
    menuPage_ = &MainMenu();

    InitializeDemo();
    floatingTitle_.Initialize(dxCommon_, input_, modelCommon_.get(), shadowManager_.get());
    audio_->PlayTitleBGM();
}






void TitleScene::InitializeDemo()
{
    modelCommon_ = std::make_unique<ModelCommon>();
    modelCommon_->Initialize(dxCommon_);
    objectCommon_ = std::make_unique<Object3dCommon>();
    objectCommon_->Initialize(dxCommon_);

    shadowManager_ = std::make_unique<ShadowManager>();
    shadowManager_->Initialize(dxCommon_, SrvManager::GetInstance());
    Object3d::SetCommonObjectCommon(objectCommon_.get());
    Object3d::SetCommonShadowManager(shadowManager_.get());
    SkinnedObject3d::SetCommonObjectCommon(objectCommon_.get());
    SkinnedObject3d::SetCommonShadowManager(shadowManager_.get());

    const TitleDemoTuning& tune = TitleDemoTune();
    camera_ = std::make_unique<Camera>();
    demoCameraX_ = tune.cameraX;
    camera_->SetTranslate({ demoCameraX_, tune.cameraY, GameConstants::kCameraDistanceZ });
    Object3d::SetCommonCamera(camera_.get());

    player_ = std::make_unique<Player>();
    player_->Initialize(modelCommon_.get());
    player_->SetHorizontalBounds(tune.arenaMinX, tune.arenaMaxX);
    player_->SetPosition({ tune.startX, tune.groundY, 0.0f });

    particleManager_ = ParticleManager::GetInstance();
    SceneShared::CreateParticleGroupsFromJson(particleManager_, "Resources/particles/gameplay.json");

    modelSkydome_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/SkyDome/SkyDome.obj",
        "Resources/SkyDome/skySphere.png");
    skydome_ = std::make_unique<Skydome>();
    skydome_->Initialize(modelCommon_.get(), modelSkydome_);

    // 床と背景のビルはレベルJSON（Resources/Levels/title.json）に置き、F2のステージエディタで編集する

    dummyModel_ = ModelManager::GetInstance()->GetOrLoad(modelCommon_.get(),
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/Slime.obj",
        "Resources/AnimatedMonsterPackby@Quaternius/OBJ/SlimePalette.png");
    demoDummies_.clear();
    int dummyIndex = 0;
    for (const InitialDummy& initial : kInitialDummies) {
        const float order = static_cast<float>(dummyIndex);
        DemoDummy dummy;
        dummy.x = initial.x;
        dummy.airborne = initial.airborne;
        dummy.baseY = tune.groundY + (dummy.airborne ? tune.dummyAirborneHeight : tune.dummyFootOffsetY);
        dummy.currentY = dummy.baseY;
        dummy.bobPhase = order * tune.dummyBobPhaseStep;
        dummy.bobSpeed = tune.dummyBobSpeedBase + order * tune.dummyBobSpeedStep;
        dummy.moveSpeed = tune.dummyMoveSpeedBase + order * tune.dummyMoveSpeedStep;
        dummy.attackCooldown = tune.dummyAttackCooldownBase + order * tune.dummyAttackCooldownStep;
        dummy.object = std::make_unique<Object3d>();
        dummy.object->Initialize(modelCommon_.get());
        dummy.object->SetModel(dummyModel_);
        dummy.object->SetEnableLighting(false);
        dummy.object->SetScale({ tune.dummyScale, tune.dummyScale, tune.dummyScale });
        // このモデルは正面軸が画面奥向きなので、カメラ側を向くよう180度反転する。
        dummy.object->SetRotation({ 0.0f, std::numbers::pi_v<float>, 0.0f });
        dummy.object->SetPosition({ dummy.x, dummy.currentY, 0.0f });
        dummy.object->Update();
        demoDummies_.push_back(std::move(dummy));
        ++dummyIndex;
    }
    demoTargetIndex_ = 0;
    demoAttackTimer_ = 0.0f;
    demoTravelTimer_ = 0.0f;
    demoMoveTrickUsed_ = false;
    demoBackstepTimer_ = 0.0f;

    // デモでは全武器を披露するため、実際の解放状況は退避しておいて終了時に必ず戻す
    // （Reset()せずUnlockAll()すると装備選択には影響しない＝CONTINUE直前でも安全）
    weaponManagerSnapshot_ = WeaponManager::GetInstance()->SaveSnapshot();
    WeaponManager::GetInstance()->UnlockAll();
    demoWeaponIndex_ = 0;
    if (!WeaponManager::GetInstance()->GetList().empty()) {
        WeaponManager::GetInstance()->EquipForDemo(WeaponManager::GetInstance()->GetList()[0].type);
    }
}

// ══════════════════════════════════════════════════════
// メニューのページ（メイン / 設定 / コマンド表）
// ══════════════════════════════════════════════════════

class TitleScene::MainMenuPage : public TitleScene::IMenuPage {
public:
    void Update(TitleScene& scene) const override { scene.UpdateMainMenu(); }
    void Draw(TitleScene& scene) const override { scene.menu_.Draw(); }
};

class TitleScene::OptionsMenuPage : public TitleScene::IMenuPage {
public:
    void Update(TitleScene& scene) const override { scene.UpdateOptionsMenu(); }
    void Draw(TitleScene& scene) const override
    {
        scene.optionsMenu_.Draw();
        const GameSettings& settings = GameSettingsManager::GetInstance()->Get();
        scene.DrawVolumeValue(kOptionsRowBgm, settings.bgmVolume);
        scene.DrawVolumeValue(kOptionsRowSe, settings.seVolume);
    }
};

class TitleScene::CommandListPage : public TitleScene::IMenuPage {
public:
    void Update(TitleScene& scene) const override
    {
        if (scene.commandList_.Update(scene.input_)) {
            scene.menuPage_ = &OptionsMenu();
        }
    }
    void Draw(TitleScene& scene) const override { scene.commandList_.Draw(scene.input_); }
};

const TitleScene::IMenuPage& TitleScene::MainMenu()
{
    static const MainMenuPage page;
    return page;
}

const TitleScene::IMenuPage& TitleScene::OptionsMenu()
{
    static const OptionsMenuPage page;
    return page;
}

const TitleScene::IMenuPage& TitleScene::CommandListMenu()
{
    static const CommandListPage page;
    return page;
}

void TitleScene::Update()
{
    menuPage_->Update(*this);

    UpdateDemo();

    floatingTitle_.Update(!GetStageEditor().IsVisible());
}

void TitleScene::UpdateMainMenu()
{
    // メニューの行（Initialize の SetItems と同じ並び）を選んだ時の処理
    using RowAction = void (*)(TitleScene&);
    static constexpr RowAction kMainMenuActions[] = {
        [](TitleScene& scene) { // NEW GAME
            RunData::GetInstance()->StartNewRun();
            WeaponManager::GetInstance()->ResetForNewRun();
            // タイトルを抜ける時はこの退避先へ戻すので、デモ前の状態ではなく初期装備を持ったランの開始状態にしておく
            scene.weaponManagerSnapshot_ = WeaponManager::GetInstance()->SaveSnapshot();
            SaveDataManager::GetInstance()->ClearContinue();
            SceneFlow::GetInstance()->Transition("TITLE", "new_game", "MAP");
        },
        [](TitleScene& scene) { // CONTINUE
            WeaponManager::GetInstance()->RestoreSnapshot(scene.weaponManagerSnapshot_);
            SaveDataManager::GetInstance()->LoadContinue(*RunData::GetInstance());
            SceneFlow::GetInstance()->Transition("TITLE", "continue", "MAP");
        },
        [](TitleScene& scene) { // TRAINING
            WeaponManager::GetInstance()->RestoreSnapshot(scene.weaponManagerSnapshot_);
            SceneFlow::GetInstance()->Transition("TITLE", "training", "TRAINING");
        },
        [](TitleScene& scene) { // OPTIONS
            // 画面は切り替えず、メニューの中身だけ設定項目に入れ替える
            scene.menuPage_ = &OptionsMenu();
            scene.optionsMenu_.SetItems(OptionsMenuItems()); // 開くたびにカーソルを先頭へ戻す
        },
    };

    menu_.Update(input_);
    if (menu_.ConsumeConfirm(input_)) {
        const int row = menu_.GetSelectedIndex();
        if (row >= 0 && row < static_cast<int>(std::size(kMainMenuActions))) {
            kMainMenuActions[row](*this);
        }
    }
}

void TitleScene::UpdateOptionsMenu()
{
    optionsMenu_.Update(input_);

    const int row = optionsMenu_.GetSelectedIndex();
    if (row == kOptionsRowBgm || row == kOptionsRowSe) {
        if (input_->TriggerMenuLeft()) {
            SceneShared::AdjustVolume(audio_, row == kOptionsRowBgm, -SceneShared::kVolumeStep);
        }
        if (input_->TriggerMenuRight()) {
            SceneShared::AdjustVolume(audio_, row == kOptionsRowBgm, SceneShared::kVolumeStep);
        }
    }

    const bool confirmed = optionsMenu_.ConsumeConfirm(input_);
    if (confirmed && row == kOptionsRowCommandList) {
        commandList_.Open();
        menuPage_ = &CommandListMenu();
        return;
    }
    const bool backSelected = confirmed && row == kOptionsRowBack;
    const bool cancelPressed = input_->TriggerMenuCancel();
    if (cancelPressed) {
        audio_->PlayMenuSelect();
    }
    if (backSelected || cancelPressed) {
        menuPage_ = &MainMenu();
    }
}

void TitleScene::RefreshVisualTransformsForEditor()
{
    // エディタの自由カメラで視点を動かしても、止まっているデモの見た目が画面に張り付かないようにする
    skydome_->Update(camera_.get());
    player_->RefreshVisualTransforms();
    for (auto& dummy : demoDummies_) {
        dummy.object->Update();
    }
}

void TitleScene::UpdateDemo()
{
    const TitleDemoTuning& tune = TitleDemoTune();
    const float arenaLeft = tune.arenaMinX + tune.arenaEdgeMargin;
    const float arenaRight = tune.arenaMaxX - tune.arenaEdgeMargin;
    // 倒された敵は戦闘範囲の外から個別に復帰する。ほかの状態は巻き戻さず、
    // タイトル表示中はひと続きの戦闘としてデモを動かし続ける。
    std::uniform_real_distribution<float> respawnDistanceDist(tune.respawnDistanceMin, tune.respawnDistanceMax);
    std::uniform_real_distribution<float> respawnDelayDist(tune.respawnDelayMin, tune.respawnDelayMax);
    std::uniform_real_distribution<float> moveSpeedDist(tune.respawnMoveSpeedMin, tune.respawnMoveSpeedMax);
    std::uniform_real_distribution<float> attackDelayDist(tune.respawnAttackDelayMin, tune.respawnAttackDelayMax);
    std::uniform_real_distribution<float> phaseDist(0.0f, GameConstants::kTwoPi);
    std::bernoulli_distribution sideDist(std::clamp(tune.respawnRightChance, 0.0f, 1.0f));
    for (auto& dummy : demoDummies_) {
        if (!dummy.defeated) {
            continue;
        }
        dummy.respawnTimer -= GameConstants::kFrameDeltaTime;
        if (dummy.respawnTimer > 0.0f) {
            continue;
        }
        const float side = sideDist(demoRng_) ? 1.0f : -1.0f;
        dummy.x = std::clamp(player_->GetPosition().x + side * respawnDistanceDist(demoRng_), arenaLeft, arenaRight);
        dummy.airborne = false;
        dummy.baseY = tune.groundY + tune.dummyFootOffsetY;
        dummy.currentY = dummy.baseY;
        dummy.bobPhase = phaseDist(demoRng_);
        dummy.moveSpeed = moveSpeedDist(demoRng_);
        dummy.attackCooldown = attackDelayDist(demoRng_);
        dummy.attackAnimTimer = 0.0f;
        dummy.lungeOffset = 0.0f;
        dummy.hitFlashTimer = 0.0f;
        dummy.defeated = false;
        dummy.object->SetColor(kDummyNormalColor);
        dummy.object->SetScale({ tune.dummyScale, tune.dummyScale, tune.dummyScale });
        dummy.object->SetPosition({ dummy.x, dummy.currentY, 0.0f });
        dummy.object->Update();
    }

    const float playerX = player_->GetPosition().x;
    const float attackAnimSeconds = (std::max)(tune.dummyAttackAnimSeconds, GameConstants::kFrameDeltaTime);
    const int distanceVariants = (std::max)(tune.dummyPreferredDistanceVariants, 1);
    // 各敵は独立して行動する。それぞれの速度でプレイヤーに近づき、
    // 少しずつ異なる距離で止まって、個別の待機時間で攻撃する。
    for (size_t i = 0; i < demoDummies_.size(); ++i) {
        auto& dummy = demoDummies_[i];
        if (dummy.defeated) {
            continue;
        }
        const float delta = playerX - dummy.x;
        const float distance = std::abs(delta);
        const float preferredDistance = tune.dummyPreferredDistanceBase
            + static_cast<float>(static_cast<int>(i) % distanceVariants) * tune.dummyPreferredDistanceStep;
        dummy.attackCooldown = (std::max)(0.0f,
            dummy.attackCooldown - GameConstants::kFrameDeltaTime);
        dummy.attackAnimTimer = (std::max)(0.0f,
            dummy.attackAnimTimer - GameConstants::kFrameDeltaTime);
        if (distance > preferredDistance && dummy.attackAnimTimer <= 0.0f) {
            const float direction = delta >= 0.0f ? 1.0f : -1.0f;
            dummy.x += direction * dummy.moveSpeed * GameConstants::kFrameDeltaTime;
            dummy.x = std::clamp(dummy.x, tune.stageMinX + tune.arenaEdgeMargin, tune.stageMaxX - tune.arenaEdgeMargin);
        } else if (dummy.attackCooldown <= 0.0f) {
            dummy.attackAnimTimer = attackAnimSeconds;
            std::uniform_real_distribution<float> nextAttackDist(tune.dummyNextAttackMin, tune.dummyNextAttackMax);
            dummy.attackCooldown = nextAttackDist(demoRng_);
        }
        const float attackPhase = 1.0f - dummy.attackAnimTimer / attackAnimSeconds;
        const float lungeDirection = delta >= 0.0f ? 1.0f : -1.0f;
        dummy.lungeOffset = dummy.attackAnimTimer > 0.0f
            ? std::sin(std::clamp(attackPhase, 0.0f, 1.0f) * GameConstants::kPi) * tune.dummyLungeDistance * lungeDirection
            : 0.0f;
    }

    // デモのプレイヤーは最も近い生存中の敵に反応する。
    demoTargetIndex_ = -1;
    float closestDistance = FLT_MAX;
    for (int i = 0; i < static_cast<int>(demoDummies_.size()); ++i) {
        if (demoDummies_[i].defeated) {
            continue;
        }
        const float distance = std::abs(demoDummies_[i].x - playerX);
        if (distance < closestDistance) {
            closestDistance = distance;
            demoTargetIndex_ = i;
        }
    }
    DemoDummy* target = demoTargetIndex_ >= 0 ? &demoDummies_[demoTargetIndex_] : nullptr;
    // 回避中は回避方向を向いたままにする。前転モーションのまま後ろへ動くと滑って見える
    if (target != nullptr && !player_->IsDodging()) {
        player_->FaceTarget({ target->x, target->currentY, 0.0f });
    }
    const bool inRange = target != nullptr && std::abs(target->x - playerX) <= tune.approachStopDistance;
    const float targetDeltaX = target != nullptr ? target->x - playerX : 0.0f;
    bool walkingRight = target != nullptr && !inRange && targetDeltaX > 0.0f;
    bool walkingLeft = target != nullptr && !inRange && targetDeltaX < 0.0f;

    // このフレームにデモのプレイヤーへ押させるボタン（Input::Action の並び順）
    std::array<bool, static_cast<size_t>(Input::Action::Count)> pressed {};
    auto press = [&pressed](Input::Action action, bool on = true) { pressed[static_cast<size_t>(action)] = on; };
    auto isPressed = [&pressed](Input::Action action) { return pressed[static_cast<size_t>(action)]; };
    bool dodgeAwayFromTarget = false;
    demoEnemyDodgeCooldown_ = (std::max)(0.0f,
        demoEnemyDodgeCooldown_ - GameConstants::kFrameDeltaTime);
    if (target != nullptr && demoEnemyDodgeCooldown_ <= 0.0f) {
        for (const auto& enemy : demoDummies_) {
            const bool incomingStrike = !enemy.defeated
                && enemy.attackAnimTimer > tune.incomingStrikeWindowStart && enemy.attackAnimTimer < tune.incomingStrikeWindowEnd
                && std::abs(enemy.x - playerX) < tune.incomingStrikeRange;
            if (incomingStrike) {
                press(Input::Action::Dodge);
                dodgeAwayFromTarget = true;
                demoEnemyDodgeCooldown_ = tune.enemyDodgeCooldown;
                break;
            }
        }
    }
    demoBackstepTimer_ = (std::max)(demoBackstepTimer_ - GameConstants::kFrameDeltaTime, 0.0f);
    if (demoBackstepTimer_ > 0.0f) {
        walkingRight = false;
        walkingLeft = false;
        // 対象から離れる方向へ短くステップする。
        if (targetDeltaX >= 0.0f) {
            walkingLeft = true;
        } else {
            walkingRight = true;
        }
    }
    if (walkingRight || walkingLeft) {
        demoTravelTimer_ += GameConstants::kFrameDeltaTime;
        // 空中敵へ高度を合わせる時だけジャンプする。地上敵には回避接近を混ぜる。
        struct MoveTrick {
            bool (*applies)(const DemoDummy& target, float distance);
            Input::Action action;
        };
        static constexpr MoveTrick kMoveTricks[] = {
            { [](const DemoDummy& t, float distance) { return t.airborne && distance <= TitleDemoTune().jumpToAirborneRange; }, Input::Action::Jump },
            { [](const DemoDummy& t, float distance) { return !t.airborne && distance > TitleDemoTune().dodgeApproachMinDistance; }, Input::Action::Dodge },
        };
        if (!demoMoveTrickUsed_ && demoTravelTimer_ >= tune.moveTrickDelay && target != nullptr) {
            for (const MoveTrick& trick : kMoveTricks) {
                if (trick.applies(*target, std::abs(targetDeltaX))) {
                    press(trick.action);
                    demoMoveTrickUsed_ = true;
                    break;
                }
            }
        }
    }
    // 空中の敵には遠距離攻撃を使い、ジャンプの高さが一致するのを待たない。
    const bool heightAligned = target != nullptr
        && (target->airborne
            || std::abs(player_->GetPosition().y - target->currentY) <= tune.hitHeightTolerance);
    if (target != nullptr && inRange && heightAligned && demoBackstepTimer_ <= 0.0f) {
        demoAttackTimer_ += GameConstants::kFrameDeltaTime;
        const DemoWeaponStyle& style = DemoWeaponStyle::For(WeaponManager::GetInstance()->GetCurrent().type);
        const int attackCount = style.ComboLength();
        const float pulseInterval = (std::max)(tune.attackPulseInterval, GameConstants::kFrameDeltaTime);
        const int pulseIndex = static_cast<int>(demoAttackTimer_ / pulseInterval);
        const bool onPulseFrame = std::fmod(demoAttackTimer_, pulseInterval) < GameConstants::kFrameDeltaTime;
        if (onPulseFrame && pulseIndex < attackCount) {
            // 敵と周回に応じて通常コンボと武器固有技を織り交ぜる（上の行から順に見て、最初に当てはまった入力を押す）
            struct StrikeRule {
                bool (*applies)(const DemoDummy& target, const DemoWeaponStyle& style, int pulseIndex, int attackCount);
                Input::Action action;
            };
            static constexpr StrikeRule kStrikeRules[] = {
                { [](const DemoDummy& t, const DemoWeaponStyle&, int, int) { return t.airborne; }, Input::Action::Shoot },
                { [](const DemoDummy&, const DemoWeaponStyle& s, int p, int c) { return s.IsSkillPulse(p, c); }, Input::Action::Skill },
                { [](const DemoDummy&, const DemoWeaponStyle&, int, int) { return true; }, Input::Action::Attack },
            };
            for (const StrikeRule& rule : kStrikeRules) {
                if (rule.applies(*target, style, pulseIndex, attackCount)) {
                    press(rule.action);
                    break;
                }
            }
            target->hitFlashTimer = tune.dummyHitFlashSeconds;
            if (particleManager_) {
                const auto& weapon = WeaponManager::GetInstance()->GetCurrent();
                const Vector4 color = { weapon.effectColor[0], weapon.effectColor[1], weapon.effectColor[2], tune.hitEffectAlpha };
                const float facing = targetDeltaX >= 0.0f ? 1.0f : -1.0f;
                Vector3 weaponPos = player_->GetActiveWeaponWorldPosition();
                weaponPos.z = tune.effectZ;
                const Vector3 targetPos = { target->x, target->currentY, tune.effectZ };
                const HitEffectPlayer fx(particleManager_, nullptr, &demoRng_);
                // 武器ごとの軌跡で、命中前から装備中の戦闘スタイルを見せる。
                fx.Trail(style.HitTrail(), weaponPos, color);
                fx.Star("title_hit", targetPos, { .color = color });
                style.EmitHit({ fx, weaponPos, targetPos, facing, color, isPressed(Input::Action::Skill) });
                if (isPressed(Input::Action::Shoot)) {
                    fx.Ring("title_shot", weaponPos);
                }
            }
            // 連打の合間に一歩引いて再接近させ、棒立ちコンボにならないようにする。
            if (pulseIndex + 1 < attackCount && ((pulseIndex + demoTargetIndex_) % 2) != 0) {
                demoBackstepTimer_ = tune.backstepSeconds;
            }
        }
        if (pulseIndex >= attackCount) {
            // 撃破: 地面へ沈めて退場させ、次の的へ向かう
            target->defeated = true;
            target->respawnTimer = respawnDelayDist(demoRng_);
            target->object->SetPosition({ target->x, target->currentY - tune.dummyDefeatSinkDepth, 0.0f });
            target->object->Update();
            demoTargetIndex_ = -1;
            demoAttackTimer_ = 0.0f;
            demoTravelTimer_ = 0.0f;
            demoMoveTrickUsed_ = false;
            // 敵を1体倒すたびに武器を切り替え、同じコンボを連続して見せない。
            auto* wm = WeaponManager::GetInstance();
            const int weaponCount = wm->GetCount();
            if (weaponCount > 0) {
                demoWeaponIndex_ = (demoWeaponIndex_ + 1) % weaponCount;
                wm->EquipForDemo(wm->GetList()[demoWeaponIndex_].type);
            }
        }
    }

    press(Input::Action::MoveRight, walkingRight);
    press(Input::Action::MoveLeft, walkingLeft);
    // 回避は移動入力が無いと向いている方向の逆へ下がるので、進みたい側の移動入力も押して向きを決める
    if (isPressed(Input::Action::Dodge)) {
        const bool moveRight = dodgeAwayFromTarget ? targetDeltaX < 0.0f : targetDeltaX >= 0.0f;
        press(moveRight ? Input::Action::MoveRight : Input::Action::MoveLeft);
    }
    // 実機の入力に一切左右されないよう、全アクションを毎フレーム明示的に指定する
    for (size_t i = 0; i < pressed.size(); ++i) {
        input_->SetActionOverride(static_cast<Input::Action>(i), pressed[i]);
    }

    player_->Update(input_);
    // 攻撃の踏み込みは通常移動のクランプ後に加算されるため、描画前にも舞台内へ収める。
    Vector3& playerPosRef = player_->GetPositionRef();
    playerPosRef.x = std::clamp(playerPosRef.x, arenaLeft, arenaRight);
    player_->RefreshVisualTransforms();

    for (auto& dummy : demoDummies_) {
        dummy.hitFlashTimer = (std::max)(dummy.hitFlashTimer - GameConstants::kFrameDeltaTime, 0.0f);
        if (dummy.defeated) {
            continue;
        }
        if (dummy.airborne) {
            dummy.bobPhase += GameConstants::kFrameDeltaTime * dummy.bobSpeed;
            dummy.currentY = dummy.baseY + std::sin(dummy.bobPhase) * tune.dummyBobAmplitude;
        } else {
            dummy.currentY = dummy.baseY;
        }
        const bool flashing = dummy.hitFlashTimer > 0.0f;
        dummy.object->SetColor(flashing ? tune.dummyFlashColor : kDummyNormalColor);
        const float shrink = flashing ? tune.dummyFlashShrink : 1.0f;
        dummy.object->SetScale({ tune.dummyScale * shrink, tune.dummyScale * shrink, tune.dummyScale * shrink });
        const float facingYaw = player_->GetPosition().x >= dummy.x
            ? 0.0f
            : GameConstants::kPi;
        dummy.object->SetRotation({ 0.0f, facingYaw, 0.0f });
        dummy.object->SetPosition({ dummy.x + dummy.lungeOffset, dummy.currentY, 0.0f });
        dummy.object->Update();
    }

    // 通常はゲームプレイ画面が継続的な武器の軌跡を出す。タイトルのデモでは
    // プレイヤーを直接描画するため、装備中の武器の軌跡もここで生成する。
    if (particleManager_) {
        const WeaponData& weapon = WeaponManager::GetInstance()->GetCurrent();
        const Vector4 trailColor = { weapon.effectColor[0], weapon.effectColor[1],
            weapon.effectColor[2], weapon.effectColor[3] };
        Vector3 trailPos = player_->GetActiveWeaponWorldPosition();
        trailPos.z = tune.effectZ;
        const bool attacking = player_->IsMeleeAttacking();
        const HitEffectPlayer fx(particleManager_, nullptr, &demoRng_);
        fx.Trail(DemoWeaponStyle::For(weapon.type).IdleTrail(), trailPos,
            { trailColor.x, trailColor.y, trailColor.z, attacking ? 1.0f : tune.idleTrailAlpha },
            { .scale = attacking ? 1.0f : tune.idleTrailScaleRatio });
    }

    // 闘技場を固定カメラで映し、独立した敵の動きを見やすくする。
    demoCameraX_ = tune.cameraX;
    camera_->SetTranslate({ demoCameraX_, tune.cameraY, GameConstants::kCameraDistanceZ });

    skydome_->Update(camera_.get());

    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    SkinnedObject3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
}

void TitleScene::Draw()
{
    // エディタ表示中はUpdate()が止まるため、文字コマンドの破棄は毎フレーム必ず通るここで行う
    fontRenderer_.Reset();
    DrawDemoShadowPass();
    DrawDemoWorld();
    GetStageEditor().DrawObjects();
    floatingTitle_.Draw();

    spriteCommon_->CommonDrawSettings();

    menuPage_->Draw(*this);
    GetStageEditor().DrawUIText(fontRenderer_);
    fontRenderer_.Draw();
}

void TitleScene::DrawVolumeValue(int row, float volume)
{
    UILayout& layout = UILayout::Get(kLayoutName);
    const float offsetX = layout.Float("options.volume_offset_x", kVolumeTextOffsetX);
    const float topRatio = layout.Float("options.volume_top_ratio", kVolumeTextTopRatio);
    const float scale = layout.Float("options.volume_scale", kVolumeTextScale);
    const Vector4 color = layout.Color("options.volume_color", kVolumeTextColor);

    const Vector2 menuPosition = optionsMenu_.GetPosition();
    const float itemHeight = optionsMenu_.GetItemSize().y;
    const float y = menuPosition.y + itemHeight * (static_cast<float>(row) + topRatio);
    fontRenderer_.DrawString(SceneShared::VolumeLabel(volume), menuPosition.x + offsetX, y, scale, color);
}

void TitleScene::DrawDemoShadowPass()
{
    // TrainingScene::Draw()と同じ最小構成（シャドウマップの状態遷移だけ行い、投影物は描かない）
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    shadowManager_->BeginShadowPass(commandList);
    modelCommon_->BeginShadowPass();
    shadowManager_->EndShadowPass(commandList);
}

void TitleScene::DrawDemoWorld()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dxCommon_->GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetBackBufferDsvHandle();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    D3D12_VIEWPORT viewport = dxCommon_->GetCenteredClientViewport();
    D3D12_RECT scissor = dxCommon_->GetCenteredClientScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    modelCommon_->CommonDrawSettings();
    objectCommon_->SetDefaultLight(commandList);
    shadowManager_->SetShadowMap(commandList, SrvManager::GetInstance());
    skydome_->Draw();
    for (auto& dummy : demoDummies_) {
        if (!dummy.defeated) {
            dummy.object->Draw();
        }
    }
    player_->Draw();
    if (particleManager_) {
        particleManager_->Update(camera_.get());
        particleManager_->Draw(camera_.get());
    }
}

void TitleScene::Finalize()
{
    // デモ用のグループを残すと、本編が同名のグループを生成する際に停止する。
    // シーン切り替え元でGPUの完了を待ってから呼ばれるので、安全に解放できる。
    if (particleManager_) {
        particleManager_->ClearAllGroups();
        particleManager_ = nullptr;
    }
    // デモ用に全解放していた武器を元に戻し、自動操作用のオーバーライドも解除する
    // （NEW GAME では退避先をランの開始状態に差し替え済み。CONTINUE/TRAINING は既に戻し済みで、二重に戻しても無害）
    WeaponManager::GetInstance()->RestoreSnapshot(weaponManagerSnapshot_);
    input_->ClearActionOverrides();
}
