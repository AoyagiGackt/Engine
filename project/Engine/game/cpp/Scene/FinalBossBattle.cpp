/**
 * @file FinalBossBattle.cpp
 * @brief ラスボス戦の共通部分（入力の置き換えと回り込み・闘技場の回転・カメラ・分身・武器の退避・描画）
 * @note 段階は FinalBossPhases.cpp、ボスの技は FinalBossMoves.cpp に置く
 */
#include "FinalBossBattle.h"
#include "Camera.h"
#include "CharacterVisuals.h"
#include "EnemyEntity.h"
#include "FinalBossTuning.h"
#include "GameConstants.h"
#include "Input.h"
#include "ModelManager.h"
#include "Player.h"
#include "StringUtility.h"
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr float kVanishedLift = 100.0f; ///< 瞬身で消えている間、当たり判定を遠くへ逃がす高さ
constexpr float kLineFadeRate = 3.0f; ///< 台詞が薄くなる速さ（毎秒）
constexpr float kMinFadeSeconds = 0.01f; ///< 出入りの長さが0の時に割り算で困らないための下限
constexpr int kPhantomPoolExtra = 3; ///< 後半戦で増える撃ち出す剣の数（剣の数の上限に足す）

/** @brief 本編の操作名と、ラスボス戦の間に読み替える闘技場の操作名 */
struct ArenaPromptAlias {
    std::wstring_view from;
    std::wstring_view to;
};
constexpr ArenaPromptAlias kArenaPromptAliases[] = {
    { L"{Move}", L"{ArenaMove}" }, // 左右の移動 → 前後左右の移動
    { L"{Jump}", L"{ArenaJump}" }, // ジャンプは前進のキーと分けてある
    { L"{Down}", L"{ArenaBack}" }, // 下は後ろへの入力（打ち上げは 後ろ+攻撃）
};

/** @brief 調整値の短い呼び名 */
const FinalBossTuning& Tune() {
    return FinalBossTuningStore::Get();
}

/** @brief 角度を -π〜π に収める */
float WrapAngle(float angle) {
    while (angle > gameConstants::kPi) {
        angle -= gameConstants::kTwoPi;
    }
    while (angle < -gameConstants::kPi) {
        angle += gameConstants::kTwoPi;
    }
    return angle;
}

/** @brief 角度 from を to へ近い回り方で rate の割合だけ寄せる */
float ApproachAngle(float from, float to, float rate) {
    return from + WrapAngle(to - from) * std::clamp(rate, 0.0f, 1.0f);
}

/** @brief XZ 平面で点を Y 軸回りに回す（MakeRotateYMatrix と同じ向き） */
Vector3 RotateY(const Vector3& v, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return { v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
}

/** @brief 手に持つ武器の見た目の表から種類の行を探す（無ければ先頭の行） */
const HeldWeaponVisual& FindHeldVisual(WeaponType type) {
    for (const HeldWeaponVisual& visual : kHeldWeaponVisuals) {
        if (visual.type == type) {
            return visual;
        }
    }
    return kHeldWeaponVisuals[0];
}
} // namespace

/** @brief 武器の種類のモデルを読み込んで、浮かべて描く実体を作る */
std::unique_ptr<Object3d> CreateWeaponObject(ModelCommon* modelCommon, WeaponType type, float scale) {
    const HeldWeaponVisual& visual = FindHeldVisual(type);
    auto object = std::make_unique<Object3d>();
    object->Initialize(modelCommon);
    object->SetModel(ModelManager::GetInstance()->GetOrLoad(modelCommon, visual.modelPath, visual.texturePath));
    object->SetScale({ scale, scale, scale });
    return object;
}

/** @brief モデル・分身・闘技場・HUD を用意する */
void FinalBossBattle::Initialize(const Links& links) {
    links_ = links;
    arena_.Initialize({ links_.modelCommon, links_.particles });
    hud_.Initialize(links_.spriteCommon, links_.font);
    telegraph_.Initialize(links_.modelCommon);

    // ボスの見た目はプレイヤーと同じモデルの分身（銃は持たせない）
    shadow_ = std::make_unique<Player>();
    shadow_->Initialize(links_.modelCommon);
    shadow_->SetGunHidden(true);

    // 撃ち出す剣と、生まれる刀は最初に全部作っておく（ゲーム中に作り直さない）
    const int phantomPool = Tune().phantomCount + kPhantomPoolExtra;
    phantoms_.resize(static_cast<size_t>((std::max)(phantomPool, 1)));
    for (PhantomBlade& blade : phantoms_) {
        blade.object = CreateWeaponObject(links_.modelCommon, WeaponType::kSword, Tune().phantomScale);
    }
    forgedKatana_ = CreateWeaponObject(links_.modelCommon, WeaponType::kSword, Tune().gatherScale);
    BuildMoves();
    initialized_ = true;
}

/** @brief シーンを抜ける時に、武器の状態と入力の差し替えを元へ戻す */
void FinalBossBattle::Finalize() {
    RestoreWeapons();
    ClearInputOverrides();
    phase_ = nullptr;
}

// ══════════════════════════════════════════════════════
// 闘技場の回転と立てる範囲
// ══════════════════════════════════════════════════════

/** @brief 闘技場の回転の中心を pivotX にして、角度 angle だけ回す */
void FinalBossBattle::RotateArenaAbout(float pivotX, float angle) {
    // 闘技場は剛体として回すので、中心の位置も向きも同じ角度だけ回る
    const Vector3 pivot = { pivotX, 0.0f, 0.0f };
    const Vector3 offset = { arenaCenter_.x - pivot.x, 0.0f, arenaCenter_.z - pivot.z };
    const Vector3 rotated = RotateY(offset, angle);
    arenaCenter_ = { pivot.x + rotated.x, arenaCenter_.y, pivot.z + rotated.z };
    arenaYaw_ += angle;
}

/** @brief 闘技場の中で X 軸の上に立てる範囲（左右の端） */
void FinalBossBattle::ArenaLimits(float& outMinX, float& outMaxX) const {
    // X 軸（z=0）と、縁から内側の円が交わる区間
    const float limit = (std::max)(arena_.GetRadius() - Tune().edgeMargin, 0.0f);
    const float halfChord = std::sqrt((std::max)(limit * limit - arenaCenter_.z * arenaCenter_.z, 0.0f));
    outMinX = arenaCenter_.x - halfChord;
    outMaxX = arenaCenter_.x + halfChord;
}

/** @brief 位置が闘技場の中に収まるか */
bool FinalBossBattle::InsideArena(const Vector3& scenePosition) const {
    const float limit = (std::max)(arena_.GetRadius() - Tune().edgeMargin, 0.0f);
    const float dx = scenePosition.x - arenaCenter_.x;
    const float dz = scenePosition.z - arenaCenter_.z;
    return dx * dx + dz * dz <= limit * limit;
}

/** @brief プレイヤーの立てる範囲を闘技場の中に合わせる */
void FinalBossBattle::ApplyPlayerLimits() {
    float minX = 0.0f;
    float maxX = 0.0f;
    ArenaLimits(minX, maxX);
    links_.player->SetHorizontalBounds(minX, maxX);
}

// ══════════════════════════════════════════════════════
// 入力とプレイヤーの向き
// ══════════════════════════════════════════════════════

/** @brief 闘技場の前後左右の入力を横スクロールの操作へ置き換え、回り込みを反映する */
void FinalBossBattle::PrepareInput() {
    Input* input = links_.input;
    Player* player = links_.player;
    // 差し替えた操作は闘技場の操作の読み取りに影響しないので、先に読む
    const float forward = (input->PushAction(Input::Action::kArenaForward) ? 1.0f : 0.0f)
        - (input->PushAction(Input::Action::kArenaBack) ? 1.0f : 0.0f);
    const float side = (input->PushAction(Input::Action::kArenaRight) ? 1.0f : 0.0f)
        - (input->PushAction(Input::Action::kArenaLeft) ? 1.0f : 0.0f);
    const float playerX = player->GetPosition().x;
    const float toBoss = bossX_ >= playerX ? 1.0f : -1.0f;

    // 前後はボスへ近づく/離れる左右の移動、ジャンプは専用のキー、下は後ろへの入力
    input->SetActionOverride(Input::Action::kMoveRight, forward * toBoss > 0.0f);
    input->SetActionOverride(Input::Action::kMoveLeft, forward * toBoss < 0.0f);
    input->SetActionOverride(Input::Action::kJump, input->PushAction(Input::Action::kArenaJump));
    input->SetActionOverride(Input::Action::kDown, forward < 0.0f);
    overridesActive_ = true;

    // 横の入力はボスの周りを回り込む（闘技場を逆へ回して見せる）。技の最中は回らない
    const bool busy = player->IsMeleeAttacking() || player->IsDodging() || player->IsRampaging()
        || player->IsSkillSequenceActive() || player->IsWarping();
    if (side != 0.0f && !busy) {
        const float distance = (std::max)(std::abs(bossX_ - playerX), Tune().minOrbitRadius);
        const float step = side * Tune().strafeSpeed * gameConstants::kFrameDeltaTime;
        const float angle = std::atan2(step, distance);
        // 回した後もプレイヤーが闘技場の中に残る時だけ回す
        const Vector3 savedCenter = arenaCenter_;
        const float savedYaw = arenaYaw_;
        RotateArenaAbout(bossX_, angle);
        if (!InsideArena(player->GetPosition())) {
            arenaCenter_ = savedCenter;
            arenaYaw_ = savedYaw;
        }
    }
    playerMoving_ = (forward != 0.0f || side != 0.0f) && !busy;
    playerHeading_ = std::atan2(side, forward);
    player->SetStrafeMoving(side != 0.0f && forward == 0.0f && !busy);
    // 前後に動いていなければボスを向く（攻撃もボスの方へ出る）
    if (forward == 0.0f && !busy) {
        player->FaceTarget({ bossX_, player->GetPosition().y, 0.0f });
    }
    ApplyPlayerLimits();
}

/** @brief 闘技場の入力を横スクロールの操作へ置き換える差し替えを解く */
void FinalBossBattle::ClearInputOverrides() {
    if (!overridesActive_ || links_.input == nullptr) {
        return;
    }
    links_.input->ClearActionOverrides();
    overridesActive_ = false;
}

/** @brief プレイヤーの見た目の向きを進む方へ寄せる（攻撃中と止まっている時はボスを向く） */
void FinalBossBattle::UpdatePlayerFacing(float deltaTime) {
    Player* player = links_.player;
    float target = 0.0f;
    if (playerMoving_ && !player->IsMeleeAttacking()) {
        // 体の向き（ボスの方か反対か）から見た、進みたい方の角度
        const float toBoss = bossX_ >= player->GetPosition().x ? 1.0f : -1.0f;
        const float facing = player->GetLastDirX() * toBoss > 0.0f ? 0.0f : gameConstants::kPi;
        target = WrapAngle(playerHeading_ - facing);
    }
    playerYawOffset_ = ApproachAngle(playerYawOffset_, target, Tune().turnRate * deltaTime);
    player->SetVisualYawOffset(playerYawOffset_);
}

// ══════════════════════════════════════════════════════
// カメラ
// ══════════════════════════════════════════════════════

/** @brief 戦闘中のプレイヤーの後ろのカメラ（cameraYaw_ の向き） */
FinalBossBattle::CameraShot FinalBossBattle::FightShot() const {
    const Vector3 player = links_.player->GetPosition();
    const Vector3 forward = { std::sin(cameraYaw_), 0.0f, std::cos(cameraYaw_) };
    const Vector3 right = { std::cos(cameraYaw_), 0.0f, -std::sin(cameraYaw_) };
    const float bossY = Tune().shadowBaseY + bossLift_;
    CameraShot shot;
    shot.eye = { player.x - forward.x * Tune().cameraBack + right.x * Tune().cameraSide,
        player.y + Tune().cameraHeight,
        player.z - forward.z * Tune().cameraBack + right.z * Tune().cameraSide };
    shot.target = { player.x + (bossX_ - player.x) * Tune().cameraLookAhead,
        player.y + (bossY - player.y) * Tune().cameraLookAhead + Tune().cameraLookHeight,
        0.0f };
    return shot;
}

/** @brief 闘技場のカメラの位置と向きをカメラへ書き込む */
void FinalBossBattle::ApplyCamera(Camera& camera) {
    const float dt = gameConstants::kFrameDeltaTime;
    CameraShot desired = desiredShot_;
    if (fightCamera_) {
        // ボスの方を向く向きへ回してから、その向きの後ろに置く（ボスを跳び越えても横を回って振り返る）
        const float toBoss = bossX_ >= links_.player->GetPosition().x ? 1.0f : -1.0f;
        cameraYaw_ = ApproachAngle(cameraYaw_, toBoss * gameConstants::kHalfPi, Tune().cameraTurnRate * dt);
        desired = FightShot();
    }
    if (!cameraPlaced_) {
        cameraShot_ = desired;
        cameraPlaced_ = true;
    }
    const float follow = std::clamp(Tune().cameraFollowRate * dt, 0.0f, 1.0f);
    cameraShot_.eye = Lerp(cameraShot_.eye, desired.eye, follow);
    cameraShot_.target = Lerp(cameraShot_.target, desired.target, follow);

    // 目から注視点への向きを、縦軸回り（yaw）と見下ろし（x 正で下向き）の回転にする
    const float dx = cameraShot_.target.x - cameraShot_.eye.x;
    const float dy = cameraShot_.target.y - cameraShot_.eye.y;
    const float dz = cameraShot_.target.z - cameraShot_.eye.z;
    const float flat = std::sqrt(dx * dx + dz * dz);
    camera.SetTranslate(cameraShot_.eye);
    camera.SetRotate({ std::atan2(-dy, flat), std::atan2(dx, dz), 0.0f });
}

// ══════════════════════════════════════════════════════
// 分身・ボスの実体・HUD
// ══════════════════════════════════════════════════════

/** @brief 1フレームぶんの共通の更新（分身・ボスの実体・闘技場・HUD） */
void FinalBossBattle::UpdateCommon(float deltaTime) {
    UpdateShadow();
    SyncBossEntity();
    arena_.SetFrame(arenaCenter_, arenaYaw_);
    arena_.SetRage(rageStarted_ ? 1.0f : 0.0f);
    arena_.Update(deltaTime);
    UpdateHud(deltaTime);
}

/** @brief 分身の位置・向き・姿勢・色を反映する */
void FinalBossBattle::UpdateShadow() {
    shadow_->SetPosition({ bossX_, Tune().shadowBaseY + bossLift_, 0.0f });
    shadow_->FaceTarget({ bossX_ + bossFacing_, Tune().shadowBaseY, 0.0f });
    // 殴られた瞬間だけ明るく光らせ、後半戦は輪郭の光を赤くする
    const float flash = boss_ != nullptr ? std::clamp(boss_->GetHitFlashRatio(), 0.0f, 1.0f) : 0.0f;
    const Vector4& base = Tune().shadowColor;
    const Vector4& lit = Tune().shadowFlashColor;
    const Vector4 color = { base.x + (lit.x - base.x) * flash, base.y + (lit.y - base.y) * flash,
        base.z + (lit.z - base.z) * flash, base.w };
    shadow_->SetAppearanceTint(color, rageStarted_ ? Tune().shadowRimColorRage : Tune().shadowRimColor);
    // 分身は正面から見られるので、刀の握りを直す回転を足す（調整値エディタで保存するとすぐ反映される）
    shadow_->SetHeldWeaponGripOffset(Tune().shadowKatanaGripRotate);
    // 斬る段は段の姿勢で、それ以外は構え・走り・宙の姿勢で見せる
    if (pose_.attack != nullptr) {
        shadow_->PreviewMeleePose(pose_.attack, pose_.attackTime, WeaponType::kSword);
        shadowPreviewing_ = true;
        return;
    }
    if (shadowPreviewing_) {
        shadow_->EndMeleePreview();
        shadowPreviewing_ = false;
    }
    shadow_->UpdateCinematicPose(pose_.airborne, pose_.lean, pose_.moving);
}

/** @brief ボスの実体の位置と向きを反映する（撃破後は実体が自分で倒れる） */
void FinalBossBattle::SyncBossEntity() {
    if (boss_ == nullptr || boss_->IsDefeated()) {
        return;
    }
    const float lift = bossVanished_ ? kVanishedLift : bossLift_;
    boss_->SetPosition({ bossX_, bossBaseY_ + lift, 0.0f });
    boss_->SetScriptedFacing(bossFacing_);
}

/** @brief 体力バー・名乗り・台詞・操作説明を HUD へ渡す */
void FinalBossBattle::UpdateHud(float deltaTime) {
    if (line_.empty()) {
        lineAlpha_ = 0.0f;
    } else if (lineAlpha_ <= 0.0f) {
        line_.clear();
    }
    FinalBossHud::State state;
    state.hpRatio = boss_ != nullptr && boss_->GetMaxHp() > 0
        ? static_cast<float>(boss_->GetHp()) / static_cast<float>(boss_->GetMaxHp()) : 0.0f;
    state.visibleBar = barVisible_;
    state.rage = rageStarted_;
    state.letterbox = letterbox_;
    state.whiteVeil = whiteVeil_;
    state.bossName = StringUtility::ConvertString(Tune().bossName);
    state.nameReveal = nameReveal_;
    state.line = line_;
    state.lineAlpha = std::clamp(lineAlpha_, 0.0f, 1.0f);
    // 操作の表記は、今使っている機器（キーボードかパッドか）に合わせる
    const auto expand = [this](const std::string& text) {
        return ExpandPrompts(StringUtility::ConvertString(text));
    };
    guideAlpha_ = std::clamp(guideAlpha_ + (guideVisible_ ? deltaTime : -deltaTime) / (std::max)(Tune().guideFadeSeconds, kMinFadeSeconds), 0.0f, 1.0f);
    if (guideAlpha_ > 0.0f) {
        state.guideTitle = expand(Tune().guideTitle);
        state.guide = expand(Tune().guide);
        state.guideConfirm = expand(Tune().guideConfirm);
        state.guideAlpha = guideAlpha_;
    }
    hud_.Update(state, deltaTime);
    // 台詞は出し続ける段階が毎フレーム 1 に戻し、戻されなくなったら薄くして消す
    lineAlpha_ -= kLineFadeRate * deltaTime;
}

/** @brief 案内文の {操作名} を今の機器の表記にする（ラスボス戦の間は本編の操作名を闘技場の操作へ読み替えてから） */
std::wstring FinalBossBattle::ExpandPrompts(const std::wstring& text) const {
    std::wstring result = text;
    if (IsActive()) {
        for (const ArenaPromptAlias& alias : kArenaPromptAliases) {
            for (size_t at = result.find(alias.from); at != std::wstring::npos; at = result.find(alias.from, at + alias.to.size())) {
                result.replace(at, alias.from.size(), alias.to);
            }
        }
    }
    const Input* input = links_.input != nullptr ? links_.input : Input::GetCurrent();
    return input != nullptr ? input->ExpandPrompts(result) : result;
}

/** @brief 闘技場の操作の一覧（戦闘の前の操作説明と同じ文を、今の機器の表記にして1行ずつ） */
std::vector<std::wstring> FinalBossBattle::ControlsRows() const {
    std::vector<std::wstring> rows;
    const std::wstring text = ExpandPrompts(StringUtility::ConvertString(Tune().guide));
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t end = text.find(L'\n', begin);
        const std::wstring row = text.substr(begin, end == std::wstring::npos ? std::wstring::npos : end - begin);
        if (!row.empty()) {
            rows.push_back(row);
        }
        if (end == std::wstring::npos) {
            break;
        }
        begin = end + 1;
    }
    return rows;
}

/** @brief 闘技場の操作の一覧の見出し */
std::wstring FinalBossBattle::ControlsTitle() const {
    return StringUtility::ConvertString(Tune().guideTitle);
}

// ══════════════════════════════════════════════════════
// 武器の退避
// ══════════════════════════════════════════════════════

/** @brief 武器を退避して刀だけにする（銃はそのまま） */
void FinalBossBattle::EquipKatanaOnly() {
    if (weaponsSwapped_) {
        return;
    }
    WeaponManager* weapons = WeaponManager::GetInstance();
    weaponBackup_ = weapons->SaveSnapshot();
    weapons->Reset();
    weapons->Acquire(WeaponType::kSword);
    weaponsSwapped_ = true;
}

/** @brief 退避した武器へ戻す */
void FinalBossBattle::RestoreWeapons() {
    if (!weaponsSwapped_) {
        return;
    }
    WeaponManager::GetInstance()->RestoreSnapshot(weaponBackup_);
    weaponsSwapped_ = false;
}

// ══════════════════════════════════════════════════════
// 描画
// ══════════════════════════════════════════════════════

/** @brief 闘技場・分身・撃ち出す剣・集まる武器を描く */
void FinalBossBattle::Draw3D() {
    if (!IsActive()) {
        return;
    }
    arena_.Draw();
    telegraph_.Draw();
    // 行列はカメラが動いた後に計算し直してから描く（止めた物が画面に張り付かないように）
    for (PhantomBlade& blade : phantoms_) {
        if (blade.active) {
            blade.object->Update();
            blade.object->Draw();
        }
    }
    if (gatherVisible_) {
        for (GatherWeapon& weapon : gatherWeapons_) {
            weapon.object->Update();
            weapon.object->Draw();
        }
    }
    if (katanaVisible_) {
        forgedKatana_->Update();
        forgedKatana_->Draw();
    }
    // 切断の破片を見せている間（ボスの実体が隠れている間）は分身も描かない
    if (!bossVanished_ && (boss_ == nullptr || boss_->IsVisible())) {
        shadow_->Draw();
    }
}

/** @brief 切断の破片に使う分身の見た目を返す */
bool FinalBossBattle::GetSliceSource(SliceSource& out) const {
    if (!IsActive() || shadow_ == nullptr) {
        return false;
    }
    float scale = 1.0f;
    shadow_->GetStaticModelPlacement(out.position, scale);
    out.model = shadow_->GetModel();
    out.scale = { scale, scale, scale };
    return out.model != nullptr;
}

/** @brief ボス戦の HUD を描く */
void FinalBossBattle::DrawHud() {
    if (IsActive()) {
        hud_.Draw();
    }
}

} // namespace engine::game
